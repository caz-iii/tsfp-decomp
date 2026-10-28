// worm_stat_level.cpp
// Control-station / conveyor-door set-piece level driver (sibling of the MTHS /
// RAIN level drivers). Distinct soundpad reverb-zone band (0x57e..0x75d) and its
// own 0x20-byte ambient state block DAT_80612868. Provides:
//   - an object event handler that latches a soundpad zone node into the entity,
//   - a preload (alloc block + 0x2d0/0x2e0 resources + clear sound banks),
//   - a level setup that flags ~50 soundpad reverb zones,
//   - a teardown that clears the block,
//   - a level-actor registration (two actor tables + resource pool),
//   - an ambient 3D loop tick that averages per-player zone volume into a 3D
//     source (banks 0x35 and 8) plus a master-tick wrapper,
//   - a powered sliding-door subsystem (class 0x2c3): open/close ramp with
//     start/stop sounds 0x378/0x379, a thud sound 0x370, and an open/close
//     travel state machine.
//
// Batch: batch_667  (addresses 0x8021be00 .. 0x8021ca7c)

#include "common.h"

// ---------------------------------------------------------------------------
// Named cross-module helpers. generated_decls.h is not pulled into this TU, so
// (matching the worm_mths_level.cpp / worm_rain_level.cpp convention) every
// helper is declared as a file-local extern with a signature matching its call
// site.
// ---------------------------------------------------------------------------
extern int *soundpad_resolve_zone_node(int zoneHandle);
extern int  soundpad_get_entity_zone_id(int zoneNode);
extern int  soundpad_find_zone_by_id(int zoneId);              // @0x8006cefc
extern void sound_clear_bank_ref(int bank);                    // @0x8000ed94
extern void memset_wrapper(int dst, int value, int len);       // @0x80003100
extern void entity_register_for_update(int entity, int list, int flags); // @0x800708c0

// Sound playback helpers.
extern void sound_init_play_params(int paramsBlock);           // @0x8000ea08
extern unsigned int sound_play_request(int *paramsBlock);      // @0x8000e924
extern int  sound_is_playing(unsigned int handle);
extern void sound_stop_handle(void);

// entity_preload_resources / level_actors_register / level_resource_pool_setup
// are used through the wide floating-point ABI here; the shared decls differ in
// arity, so bind by ROM address as file-local typed aliases (matching the
// worm_mths_level.cpp pattern).
typedef long long (*preload_res_fp)(long long, double, double, long long, long long,
                                    long long, long long, long long, int);
static const preload_res_fp __stat_preload_res = (preload_res_fp)0x80070ba8; // entity_preload_resources

typedef int (*mem_alloc_fp)(long long, double, double, long long, long long,
                            long long, long long, long long, int size, int count,
                            long long, long long, long long, long long, long long,
                            long long);
static const mem_alloc_fp __stat_mem_alloc = (mem_alloc_fp)0x80006c44;        // mem_alloc

typedef long long (*level_actors_fp)(long long, double, double, long long, long long,
                                     long long, long long, long long, int slot,
                                     void *table, int n);
static const level_actors_fp __stat_level_actors_register = (level_actors_fp)0x8002961c; // level_actors_register

typedef void (*level_pool_fp)(long long, double, double, long long, long long,
                              long long, long long, long long);
static const level_pool_fp __stat_level_resource_pool_setup = (level_pool_fp)0x800296a8; // level_resource_pool_setup

// sound_update_3d_source takes the position triple in the FP ABI; use a typed
// alias so the double-typed call args line up (defined @0x800102e4).
typedef void (*snd3d_fp)(double x, double y, double z, int handleSlot, int a, int bank, int b);
static const snd3d_fp __stat_snd3d = (snd3d_fp)0x800102e4; // sound_update_3d_source

// ---------------------------------------------------------------------------
// File-local externs for raw ROM symbols still un-named elsewhere in the tree.
// ---------------------------------------------------------------------------
extern int  FUN_80238f8c(void);   // replay/preview mode gate (returns -1 when live)
extern int  FUN_803826f0(void);   // scoped-entity push (returns scoped record ptr)
extern void FUN_8038273c(void);   // scoped-entity pop

// Raw data / constants ------------------------------------------------------
extern int   DAT_80612868;   // ambient state block ptr (0x20 bytes; alloced in preload)
extern int   DAT_80611b8c;   // soundpad zone array base
extern int   DAT_80611d78;   // per-player record base
extern int   DAT_80612610;   // active player count
extern int   DAT_80611824;   // node table base (indexed by node id)
extern int   DAT_806108e4;   // "doors enabled" global gate
extern unsigned int DAT_806109c0; // frame-time denominator (int, bias-converted)

extern float  FLOAT_80618374; // 0.0f baseline
extern float  FLOAT_80618378; // zone-8 volume lo bound
extern float  FLOAT_8061837c; // zone-8 volume hi clamp
extern float  FLOAT_80618380; // zone-8 volume range divisor
extern float  FLOAT_80618384; // 1.0f / full-volume reference
extern float  FLOAT_80618388; // ambient source secondary coord
extern double DOUBLE_80618390; // 4503599627370496.0 (int<->double bias mantissa)
extern float  FLOAT_80618398; // door ramp rate
extern float  FLOAT_8061839c; // door open limit
extern float  FLOAT_806183a0; // door closed target
extern float  FLOAT_806108ec; // frame delta scale

extern int   DAT_8045ce54;   // actor table 0
extern int   DAT_8045ce6c;   // actor table 1

// Forward decls for functions defined later in this file but called earlier.
static void statlvl_ambient_loop_tick(void);
static void statlvl_door_thud(int entity);

// ---------------------------------------------------------------------------
// 0x8021be00  statlvl_zone_node_event
// Object event handler. On event 3 (install), resolve the soundpad zone node
// bound to this entity's actor record (+0x678) and cache its node value into
// both ent+0xc4 and ent+0x70. Returns -2 to signal the event was consumed.
// ---------------------------------------------------------------------------
int statlvl_zone_node_event(int entity, int event)
{
    int nodeValue;
    int *zoneNode;
    int result;

    result = 0;
    if (event == 3) {
        zoneNode = soundpad_resolve_zone_node(*(int *)(*(int *)(entity + 0x7c) + 0x678));
        nodeValue = zoneNode[6];
        result = (int)0xfffffffe;                 // -2
        *(int *)(entity + 0xc4) = nodeValue;
        *(int *)(entity + 0x70) = nodeValue;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8021be54  statlvl_preload
// Allocate the 0x20-byte ambient state block, preload two resource groups
// (0x2d0, 0x2e0), and clear sound banks 0x51 / 0x35 / 8.
// ---------------------------------------------------------------------------
void statlvl_preload(long long a1, double a2, double a3, long long a4, long long a5,
                     long long a6, long long a7, long long a8)
{
    long long chained;

    DAT_80612868 = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                                    0x20, 1, 0, 0, 0, 0, 0, 0);
    chained = __stat_preload_res(a1, a2, a3, a4, a5, a6, a7, a8, 0x2d0);
    __stat_preload_res(chained, a2, a3, a4, a5, a6, a7, a8, 0x2e0);
    sound_clear_bank_ref(0x51);
    sound_clear_bank_ref(0x35);
    sound_clear_bank_ref(8);
}

// ---------------------------------------------------------------------------
// 0x8021bea8  statlvl_level_setup
// Zero the ambient block, seed its two handle slots to -1, then flag the level's
// soundpad reverb zones. Most zones get the 0x20000 reverb bit; a handful of
// transition zones get 0x10000 instead.
// ---------------------------------------------------------------------------
static void statlvl_zone_flag(int zoneId, unsigned int bit)
{
    int slot;
    slot = soundpad_find_zone_by_id(zoneId);
    slot = DAT_80611b8c + slot * 0x20;
    *(unsigned int *)(slot + 4) = *(unsigned int *)(slot + 4) | bit;
}

void statlvl_level_setup(void)
{
    static const int kReverbZones[] = {
        0x57e, 0x57f, 0x582, 0x583, 0x58c, 0x58d, 0x58e, 0x58f,
        0x59b, 0x59c, 0x59d, 0x59e, 0x5a9, 0x5b0, 0x5ae, 0x684,
        0x688, 0x6c0, 0x6c8, 0x480, 0x47f, 0x47c, 0x6e2, 0x6d6,
        0x6df, 0x5b8, 0x6ac, 0x70b, 0x5d7, 0x5d6, 0x709, 0x707,
        0x70e, 0x71f, 0x605, 0x606, 0x718, 0x716, 0x714, 0x5e9,
        0x5ce, 0x618, 0x5fb, 0x6a6, 0x75a, 0x75b, 0x75c, 0x75d,
    };
    static const int kTransitionZones[] = {
        0x6d1, 0x6d8, 0x68a, 0x466, 0x6ba,
    };
    int i;

    memset_wrapper(DAT_80612868, 0, 0x18);
    *(unsigned int *)(DAT_80612868 + 0)  = 0xffffffff;
    *(unsigned int *)(DAT_80612868 + 4)  = 0xffffffff;

    for (i = 0; i < (int)(sizeof(kReverbZones) / sizeof(kReverbZones[0])); i = i + 1) {
        statlvl_zone_flag(kReverbZones[i], 0x20000);
    }
    for (i = 0; i < (int)(sizeof(kTransitionZones) / sizeof(kTransitionZones[0])); i = i + 1) {
        statlvl_zone_flag(kTransitionZones[i], 0x10000);
    }
}

// ---------------------------------------------------------------------------
// 0x8021c588  statlvl_level_teardown
// Drop the ambient block reference and release sound bank 0x51.
// ---------------------------------------------------------------------------
void statlvl_level_teardown(void)
{
    DAT_80612868 = 0;
    sound_clear_bank_ref(0x51);
}

// ---------------------------------------------------------------------------
// 0x8021c5b4  statlvl_actors_register
// Register the level's two actor tables (slots 0 and 1) and set up the resource
// pool.
// ---------------------------------------------------------------------------
void statlvl_actors_register(long long a1, double a2, double a3, long long a4,
                             long long a5, long long a6, long long a7, long long a8)
{
    long long chained;

    chained = __stat_level_actors_register(a1, a2, a3, a4, a5, a6, a7, a8,
                                           0, &DAT_8045ce54, 1);
    chained = __stat_level_actors_register(chained, a2, a3, a4, a5, a6, a7, a8,
                                           1, &DAT_8045ce6c, 1);
    __stat_level_resource_pool_setup(chained, a2, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// 0x8021c610  statlvl_ambient_loop_tick
// Per-frame ambient loop. When not in replay/preview (FUN_80238f8c() == -1),
// walk the active players, resolve each one's current soundpad zone type, and
// accumulate a volume contribution: zone type 8 ramps from its per-zone volume
// (+0x6c) into the 0..1 range; zone types 0x23 / 0x2c and the low/high fringe
// ranges add a fixed contribution FLOAT_80618384. The accumulated total is
// averaged over the participating player count. Two 3D sound sources are then
// updated: the averaged loop on bank 0x35 and its complement on bank 8.
// ---------------------------------------------------------------------------
static void statlvl_ambient_loop_tick(void)
{
    float perZone;
    float clamped;
    float bounded;
    int   playerIndex;
    int   participants;
    int   recOffset;
    int  *actorRec;
    int  *zoneNode;
    int   zoneType;
    double loopVol;
    double compVol;

    loopVol = (double)FLOAT_80618374;
    compVol = loopVol;

    if (FUN_80238f8c() == -1) {
        participants = 0;
        recOffset = 0;
        for (playerIndex = 0; playerIndex < DAT_80612610; playerIndex = playerIndex + 1) {
            if (-1 < *(int *)(DAT_80611d78 + recOffset + 4)) {
                actorRec = *(int **)(*(int *)(DAT_80611d78 + recOffset + 0x14) + 0x7c);
                zoneNode = soundpad_resolve_zone_node(actorRec[0x19e]);
                zoneType = soundpad_get_entity_zone_id((int)zoneNode);

                if (zoneType == 0x23) {
                    compVol = (double)(float)(compVol + (double)FLOAT_80618384);
                } else if (zoneType < 0x23) {
                    if (zoneType == 8) {
                        perZone = *(float *)(*actorRec + 0x6c);
                        clamped = perZone;
                        if (FLOAT_8061837c < perZone) {
                            clamped = FLOAT_8061837c;
                        }
                        bounded = FLOAT_80618378;
                        if ((FLOAT_80618378 <= clamped) && (bounded = perZone, FLOAT_8061837c < perZone)) {
                            bounded = FLOAT_8061837c;
                        }
                        compVol = (double)(float)(compVol + (double)((bounded - FLOAT_8061837c) / FLOAT_80618380));
                    } else if ((zoneType < 8) && (-1 < zoneType)) {
                        compVol = (double)(float)(compVol + (double)FLOAT_80618384);
                    }
                } else if (zoneType < 0x34) {
                    if (zoneType == 0x2c) {
                        compVol = (double)(float)(compVol + (double)FLOAT_80618384);
                    }
                } else if (zoneType < 0x39) {
                    compVol = (double)(float)(compVol + (double)FLOAT_80618384);
                }
                participants = participants + 1;
            }
            recOffset = recOffset + 0x141c;
        }
        compVol = (double)(float)(compVol /
                  (double)(float)((double)CONCAT44(0x43300000, participants ^ 0x80000000) - DOUBLE_80618390));
        loopVol = (double)(float)((double)FLOAT_80618384 - compVol);
    }

    __stat_snd3d(compVol, (double)FLOAT_80618388, (double)FLOAT_80618384, DAT_80612868,      0, 0x35, 0);
    __stat_snd3d(loopVol, (double)FLOAT_80618388, (double)FLOAT_80618384, DAT_80612868 + 4,  0, 8,    0);
}

// ---------------------------------------------------------------------------
// 0x8021c7d4  statlvl_master_tick
// Master driver: advance the sliding-door slide state then update the ambient
// loop. (FUN_8021dc94 is the door-slide driver decompiled in the next batch.)
// ---------------------------------------------------------------------------
extern void FUN_8021dc94(void);  // door-slide driver (next batch)

void statlvl_master_tick(void)
{
    FUN_8021dc94();
    statlvl_ambient_loop_tick();
}

// ---------------------------------------------------------------------------
// 0x8021c7f8  statlvl_door_ramp_tick
// Powered sliding-door motion ramp. When doors are enabled, mark the entity as
// moving (bit 0x4000000), then for open direction (+0x1a0 == 1) ramp +0x70
// upward toward FLOAT_8061839c, and for the closing direction (+0x1a0 in [-...,0])
// ramp downward toward FLOAT_80618374 baseline. On reaching a limit, clear the
// moving bit and swap the running "moving" loop (0x378) for the "stop" one-shot
// (0x379). While still moving with no active handle, start the 0x378 loop and
// latch its handle at +0x22c. Finally flag the entity for its post-move update
// (bit 2 at +0x18).
// ---------------------------------------------------------------------------
void statlvl_door_ramp_tick(int entity)
{
    bool reachedLimit;
    float openLimit;
    float baseline;
    int   direction;
    unsigned int handle;
    // Sound play-params blocks. sound_init_play_params fills a fixed layout;
    // we only overwrite the id (word 0), the world position (words 7/8/9 at
    // byte offset 0x1c/0x20/0x24) and the trailing "positioned" flag byte at
    // byte offset 0x28. Sized generously to cover the block sound_play_request
    // reads (matches the decompiler's local_78 / local_48 frames).
    int   playParamsStop[16];
    int   startParams[16];

    if (DAT_806108e4 != 0) {
        reachedLimit = false;
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x4000000;
        openLimit = FLOAT_8061839c;
        baseline = FLOAT_80618374;
        direction = *(int *)(entity + 0x1a0);

        if (direction == 1) {
            *(float *)(entity + 0x70) =
                FLOAT_80618398 *
                (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                FLOAT_806108ec + *(float *)(entity + 0x70);
            if (openLimit < *(float *)(entity + 0x70)) {
                reachedLimit = true;
                *(float *)(entity + 0x70) = openLimit;
            }
        } else if ((direction < 1) && (-1 < direction)) {
            *(float *)(entity + 0x70) =
                -(FLOAT_80618398 *
                  (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                  FLOAT_806108ec - *(float *)(entity + 0x70));
            if (*(float *)(entity + 0x70) < baseline) {
                *(float *)(entity + 0x70) = baseline;
                reachedLimit = true;
            }
        }

        if (reachedLimit) {
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xfbffffff;
            if (-1 < *(int *)(entity + 0x22c)) {
                sound_is_playing(*(unsigned int *)(entity + 0x22c));
                sound_stop_handle();
                *(int *)(entity + 0x22c) = -1;
                sound_init_play_params((int)playParamsStop);
                playParamsStop[0] = 0x379;                                    /* sound id      */
                playParamsStop[7] = *(int *)(entity + 100);                   /* pos X (0x1c)  */
                playParamsStop[8] = *(int *)(entity + 0x68);                  /* pos Y (0x20)  */
                playParamsStop[9] = *(int *)(entity + 0x6c);                  /* pos Z (0x24)  */
                *((char *)playParamsStop + 0x28) = 1;                         /* positioned    */
                sound_play_request(playParamsStop);
            }
        } else if (*(int *)(entity + 0x22c) < 0) {
            sound_init_play_params((int)startParams);
            startParams[0] = 0x378;                                           /* sound id      */
            startParams[7] = *(int *)(entity + 100);                          /* pos X (0x1c)  */
            startParams[8] = *(int *)(entity + 0x68);                         /* pos Y (0x20)  */
            startParams[9] = *(int *)(entity + 0x6c);                         /* pos Z (0x24)  */
            *((char *)startParams + 0x28) = 1;                                /* positioned    */
            handle = sound_play_request(startParams);
            *(unsigned int *)(entity + 0x22c) = handle;
        }
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 2;
    }
}

// ---------------------------------------------------------------------------
// 0x8021c9d0  statlvl_door_thud
// Play the door-stop "thud" one-shot (sound 0x370) at the entity's world
// position (base +0x64/0x68/0x6c plus offset +0x1a8/0x1ac/0x1b0), but only for
// the 'CC*' door variant identified by class id 0x2c3.
// ---------------------------------------------------------------------------
static void statlvl_door_thud(int entity)
{
    int   playParams[16];
    float posX;
    float posY;
    float posZ;

    posX = *(float *)(entity + 100)   + *(float *)(entity + 0x1a8);
    posY = *(float *)(entity + 0x68)  + *(float *)(entity + 0x1ac);
    posZ = *(float *)(entity + 0x6c)  + *(float *)(entity + 0x1b0);

    if (*(short *)(entity + 0x94) == 0x2c3) {
        sound_init_play_params((int)playParams);
        playParams[0] = 0x370;                            /* sound id     */
        *(float *)(playParams + 7) = posX;                /* pos X (0x1c) */
        *(float *)(playParams + 8) = posY;                /* pos Y (0x20) */
        *(float *)(playParams + 9) = posZ;                /* pos Z (0x24) */
        *((char *)playParams + 0x28) = 1;                 /* positioned   */
        sound_play_request(playParams);
    }
}

// ---------------------------------------------------------------------------
// 0x8021ca7c  statlvl_door_slide_tick
// Sliding-door open/close travel state machine. Reads the currently scoped door
// entity (FUN_803826f0 -> +4 = entity, +0xd0 selects the door's node record).
// Integrates the door node's travel value (+0x1a4) by the ramp speed * per-frame
// delta, and mirrors it into the node table entry's +0xd4 position (with +0xd8 as
// previous). State +0x1a0: 2 = opening (until fully open, then mark done bit
// 0x40 or flip to state 1 and fire the thud), 3 = closing (mirror logic to state
// 0), else idle (park the node at baseline).
// ---------------------------------------------------------------------------
void statlvl_door_slide_tick(void)
{
    float travelFull;
    float frameDelta;
    float step;
    int   scoped;
    int   doorNode;
    int   tableEntry;

    scoped     = FUN_803826f0();
    travelFull = FLOAT_80618384;
    frameDelta = FLOAT_806108ec;
    doorNode   = *(int *)(scoped + 4);
    tableEntry = *(int *)(DAT_80611824 + *(int *)(doorNode + 0xd0) * 4);

    step = FLOAT_80618384 /
           (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390);
    *(unsigned int *)(scoped + 0x18) = *(unsigned int *)(scoped + 0x18) | 0x82;
    frameDelta = frameDelta * step;

    *(float *)(scoped + 0x1a4) =
        *(float *)(doorNode + 0x100) * frameDelta * *(float *)(scoped + 0xd0) + *(float *)(scoped + 0x1a4);
    *(int *)(doorNode + 0xd8) = *(int *)(doorNode + 0xd4);
    *(float *)(doorNode + 0xd4) = *(float *)(scoped + 0x1a4) * *(float *)(tableEntry + 0x18);

    step = FLOAT_806183a0;
    if (*(int *)(scoped + 0x1a0) == 2) {
        if (*(float *)(doorNode + 0xd4) <= *(float *)(tableEntry + 0x18) - frameDelta) {
            *(float *)(doorNode + 0x100) = travelFull;
            *(unsigned int *)(scoped + 0x18) = *(unsigned int *)(scoped + 0x18) | 0x40;
        } else {
            entity_register_for_update(scoped, 3, 0);
            *(int *)(scoped + 0x1a0) = 1;
            statlvl_door_thud(scoped);
            step = FLOAT_80618374;
            *(float *)(scoped + 0x1a4) = FLOAT_80618384;
            *(int *)(doorNode + 0xd4) = *(int *)(tableEntry + 0x18);
            *(float *)(doorNode + 0x100) = step;
        }
    } else if (*(int *)(scoped + 0x1a0) == 3) {
        if (frameDelta <= *(float *)(doorNode + 0xd4)) {
            *(unsigned int *)(scoped + 0x18) = *(unsigned int *)(scoped + 0x18) | 0x40;
            *(float *)(doorNode + 0x100) = step;
        } else {
            entity_register_for_update(scoped, 3, 0);
            *(int *)(scoped + 0x1a0) = 0;
            statlvl_door_thud(scoped);
            step = FLOAT_80618374;
            *(float *)(scoped + 0x1a4) = FLOAT_80618374;
            *(float *)(doorNode + 0xd4) = step;
            *(float *)(doorNode + 0x100) = step;
        }
    } else {
        *(float *)(doorNode + 0x100) = FLOAT_80618374;
    }
    FUN_8038273c();
}

// ===========================================================================
// Batch: batch_668  (addresses 0x8021cbfc .. 0x8021da40)
// Control-station door/switch event handlers + animated mechanism ticks. These
// continue the worm_stat_level driver: named 4CC props 'STRS' (sliding door),
// 'SRMP' (ramp), 'THDR'/'BAGB' (blast door + trigger), 'PLRD' (paired lever) and
// a 4-axis lever/gear mechanism. Reuses DAT_80612868 (ambient/state block),
// DAT_806108e4 (doors-enabled gate), DAT_806109c0 (frame denom) and the
// FLOAT_806183xx constant band.
// ===========================================================================

// --- Additional named cross-module helpers (file-local externs) ------------
extern int   entity_find_by_fourcc(unsigned int fourcc);        // FUN_8023c32c @0x8023c32c
extern void  player_apply_camera_shake(double amp, double freq, double dur, int playerRec); // @0x800a9f08
extern unsigned int rng_counter_tick(void);                      // @0x8013104c
extern void  sound_set_pitch(double pitch, unsigned int handle); // @0x8000ebf0
extern int  *entity_get_world_position(int entity);              // @0x8007c260
extern int   mover_get_active_state(int entity);                 // @0x80082d88
extern int   soundpad_get_entity_data(int kind, int *outList);   // @0x8006fa58
extern void  mtx4_set_identity(int mtx);                         // @0x8012ef38
extern void  mtx4_multiply(float *dst, float *a, float *b);      // @0x80130bd4
extern void  mtx4_set_rotation_y(double angle, float *mtx);      // @0x8012f05c
extern void  mtx4_set_transform_euler(double px, double py, double pz,
                                      double rx, double ry, double rz, float *mtx); // @0x8012f388
extern int   __save_gpr_25(void);
extern int   __save_gpr_28(void);
extern void  __restore_gpr_25(void);
extern void  __restore_gpr_28(void);

// switch_find_slot_by_entity returns a slot record pointer (undefined4* in the
// decompiler; [2] = the paired entity).
extern int *switch_find_slot_by_entity(unsigned int fourcc);     // @0x8008bbd0

// mtx4_set_translation is used in the matrix-relative accumulate form (3 doubles,
// matrix threaded implicitly) — bind by ROM address as a typed alias, matching
// the worm_rain_level.cpp convention.
typedef void (*mtx_set_translation3_fp)(double x, double y, double z);
static const mtx_set_translation3_fp __stat_mtx_set_translation = (mtx_set_translation3_fp)0x8012f534; // mtx4_set_translation

// sound_register_fade_out takes the target handle slot pointer + a done-flag ptr.
extern void sound_register_fade_out(double vol, double rate, int *handleSlot, unsigned int *doneFlag); // @0x8001063c

// Raw ROM helpers used by the blast-door trigger (0x8021d0ac).
extern int  FUN_80232760(int fourcc);   // '4CC actor present/active' predicate (returns bool)
extern int  FUN_802333ec(int hi, int lo); // scoped-entity notify helper

// sound_update_3d_source used with an int return + FP-ABI position triple in this
// section (matches the __stat_snd3d layout); declare the int-returning form once
// here so callers below resolve.
extern int sound_update_3d_source(double x, double y, double z, int handleSlot,
                                  int posSrc, int bank, int b);

// --- Additional FLOAT/DOUBLE constants for this section --------------------
extern float  FLOAT_806183a4; // motion speed scale
extern float  FLOAT_806183a8; // 1.0f / phase base
extern float  FLOAT_806183ac; // rng divisor
extern float  FLOAT_806183b0; // close lower clamp
extern float  FLOAT_806183b4; // shake amp / fade vol
extern float  FLOAT_806183b8; // shake freq
extern float  FLOAT_806183bc; // shake dur
extern float  FLOAT_806183c0; // player hazard flash a
extern float  FLOAT_806183c4; // player hazard flash b
extern float  FLOAT_806183c8; // hazard idle volume
extern double DOUBLE_806183d0; // int<->double bias mantissa (rng path)
extern float  FLOAT_806183d8; // spin denom scale
extern float  FLOAT_806183dc; // spin denom divisor
extern float  FLOAT_806183e0; // spin decay factor
extern float  FLOAT_806183e4; // 3d source coord
extern float  FLOAT_806183e8; // alt pitch base
extern float  FLOAT_806183ec; // deg->rad-ish rotation scale
extern float  FLOAT_806183f0; // rotation divisor
extern float  FLOAT_806183f4; // lever drive scale
extern float  FLOAT_806183f8; // lever spin cap
extern float  FLOAT_806183fc; // per-axis step scale
extern float  FLOAT_80618400; // per-axis wrap limit
extern float  FLOAT_80618404; // switch sound gain
extern float  FLOAT_80618408; // lever axis clamp
extern float  FLOAT_8061840c; // lever rest height
extern float  FLOAT_80618410; // lever rise accel
extern float  FLOAT_80618414; // lever velocity clamp
extern float  FLOAT_80618418; // zone-node height bias

// ---------------------------------------------------------------------------
// 0x8021cbfc  statlvl_strs_door_toggle
// 'STRS' sliding-door state toggle event. Reads the current state (+0x1a0): if
// closed/mid (0) -> open (state 2) and play sound 0x374 at the door's world
// position; if opening (1) -> close (state 3) with the same sound; otherwise no
// sound. Sets the "notify" bit 0x40 and re-registers for update list 3.
// ---------------------------------------------------------------------------
void statlvl_strs_door_toggle(void)
{
    int state;
    int door;
    float posX;
    float posY;
    float posZ;
    int  paramsOpen[16];
    int  paramsClose[16];

    door = entity_find_by_fourcc(0x53545253);   // 'STRS'
    if (door == 0) {
        return;
    }
    state = *(int *)(door + 0x1a0);
    posX = *(float *)(door + 100)  + *(float *)(door + 0x1a8);
    posY = *(float *)(door + 0x68) + *(float *)(door + 0x1ac);
    posZ = *(float *)(door + 0x6c) + *(float *)(door + 0x1b0);

    if ((state != 3) && (state <= 2)) {
        if (state != 0) {
            if (-1 < state) {
                *(int *)(door + 0x1a0) = 3;
                sound_init_play_params((int)paramsOpen);
                *((char *)paramsOpen + 0x28) = 1;
                *(unsigned int *)((char *)paramsOpen + 0x18) |= 0xa0; /* local_88 flags */
                paramsOpen[0] = 0x374;
                *(float *)(paramsOpen + 7) = posX;
                *(float *)(paramsOpen + 8) = posY;
                *(float *)(paramsOpen + 9) = posZ;
                sound_play_request(paramsOpen);
            }
            goto done;
        }
    } else {
        goto done;
    }
    *(int *)(door + 0x1a0) = 2;
    sound_init_play_params((int)paramsClose);
    *((char *)paramsClose + 0x28) = 1;
    *(unsigned int *)((char *)paramsClose + 0x18) |= 0xa0; /* local_58 flags */
    paramsClose[0] = 0x374;
    *(float *)(paramsClose + 7) = posX;
    *(float *)(paramsClose + 8) = posY;
    *(float *)(paramsClose + 9) = posZ;
    sound_play_request(paramsClose);
done:
    *(unsigned int *)(door + 0x18) = *(unsigned int *)(door + 0x18) | 0x40;
    entity_register_for_update(door, 3, (int)0x8021ca7c); /* statlvl_door_slide_tick cb */
}

// ---------------------------------------------------------------------------
// 0x8021cd4c  statlvl_srmp_ramp_toggle
// 'SRMP' ramp state toggle. Same shape as the STRS door but only the open
// transition (state 0 -> 2) plays a sound (0x36f); the close transition (1 -> 3)
// is silent. Sets bit 0x40 and re-registers for update list 3.
// ---------------------------------------------------------------------------
void statlvl_srmp_ramp_toggle(void)
{
    int state;
    int ramp;
    float posX;
    float posY;
    float posZ;
    int  params[16];

    ramp = entity_find_by_fourcc(0x53524d50);   // 'SRMP'
    if (ramp == 0) {
        return;
    }
    state = *(int *)(ramp + 0x1a0);
    posX = *(float *)(ramp + 100)  + *(float *)(ramp + 0x1a8);
    posY = *(float *)(ramp + 0x68) + *(float *)(ramp + 0x1ac);
    posZ = *(float *)(ramp + 0x6c) + *(float *)(ramp + 0x1b0);

    if ((state != 3) && (state <= 2)) {
        if (state != 0) {
            if (-1 < state) {
                *(int *)(ramp + 0x1a0) = 3;
            }
            goto done;
        }
    } else {
        goto done;
    }
    *(int *)(ramp + 0x1a0) = 2;
    sound_init_play_params((int)params);
    *((char *)params + 0x28) = 1;
    *(unsigned int *)((char *)params + 0x18) |= 0xa0;
    params[0] = 0x36f;
    *(float *)(params + 7) = posX;
    *(float *)(params + 8) = posY;
    *(float *)(params + 9) = posZ;
    sound_play_request(params);
done:
    *(unsigned int *)(ramp + 0x18) = *(unsigned int *)(ramp + 0x18) | 0x40;
    entity_register_for_update(ramp, 3, (int)0x8021ca7c); /* statlvl_door_slide_tick cb */
}

// ---------------------------------------------------------------------------
// 0x8021ce64  statlvl_crusher_hazard_tick
// Powered crusher/press hazard tick (scoped entity via __save_gpr_28 prologue).
// While doors are enabled, drives the press position +0x70 with a randomized
// speed toward its open/closed limit (state +0x1a0). When it reaches the limit,
// scans players within radius (+0x1c0): any player inside gets a camera shake
// and a hazard-flash pair written to their record (+0xe54/+0xe58). Updates a 3D
// loop source (sound 0x376) at either the moving or idle volume.
// ---------------------------------------------------------------------------
void statlvl_crusher_hazard_tick(void)
{
    float dx;
    float dy;
    float dz;
    bool  atLimit;
    int   entity;
    unsigned int rnd;
    int   playerRec;
    int   recOffset;
    int   i;
    double loopVol;

    entity = __save_gpr_28();
    if (DAT_806108e4 != 0) {
        loopVol = (double)FLOAT_806183a0;
        atLimit = false;
        if (*(int *)(entity + 0x1a0) == 0) {
            rnd = rng_counter_tick();
            *(float *)(entity + 0x70) =
                FLOAT_806183a4 *
                (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                FLOAT_806108ec *
                (FLOAT_806183a8 + (float)((double)CONCAT44(0x43300000, rnd) - DOUBLE_806183d0) / FLOAT_806183ac) +
                *(float *)(entity + 0x70);
            if (FLOAT_80618374 < *(float *)(entity + 0x70)) {
                atLimit = true;
                *(float *)(entity + 0x70) = FLOAT_80618374;
            }
        } else {
            rnd = rng_counter_tick();
            *(float *)(entity + 0x70) =
                -(FLOAT_806183a4 *
                  (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                  FLOAT_806108ec *
                  (FLOAT_806183a8 + (float)((double)CONCAT44(0x43300000, rnd) - DOUBLE_806183d0) / FLOAT_806183ac) -
                  *(float *)(entity + 0x70));
            if (*(float *)(entity + 0x70) < FLOAT_806183b0) {
                *(float *)(entity + 0x70) = FLOAT_806183b0;
                atLimit = true;
            }
        }
        if (!atLimit) {
            recOffset = 0;
            for (i = 0; i < DAT_80612610; i = i + 1) {
                playerRec = DAT_80611d78 + recOffset;
                if ((-1 < *(int *)(playerRec + 4)) &&
                    (dx = *(float *)(playerRec + 0xa8) - *(float *)(entity + 100),
                     dy = *(float *)(playerRec + 0xac) - *(float *)(entity + 0x68),
                     dz = *(float *)(playerRec + 0xb0) - *(float *)(entity + 0x6c),
                     dz * dz + dx * dx + dy * dy <
                     *(float *)(entity + 0x1c0) * *(float *)(entity + 0x1c0))) {
                    player_apply_camera_shake((double)FLOAT_806183b4, (double)FLOAT_806183b8,
                                              (double)FLOAT_806183bc, playerRec);
                    *(float *)(DAT_80611d78 + recOffset + 0xe54) = FLOAT_806183c0;
                    *(float *)(DAT_80611d78 + recOffset + 0xe58) = FLOAT_806183c4;
                }
                recOffset = recOffset + 0x141c;
            }
            loopVol = (double)FLOAT_806183c8;
        }
        __stat_snd3d(loopVol, (double)FLOAT_80618388, (double)FLOAT_80618384,
                     entity + 0x22c, entity + 100, 0x376, 0);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8021d0ac  statlvl_blast_door_open
// Blast-door open trigger. Reads the scoped entity; if the 'BAGB' trigger actor
// is present, resolves the 'THDR' blast-door entity, notifies the scoped entity
// (FUN_802333ec) and clears the door's collision-enabled bit (+4 record, +0x30
// bit0).
// ---------------------------------------------------------------------------
void statlvl_blast_door_open(void)
{
    int door;
    long long scoped;

    scoped = (long long)(unsigned int)FUN_803826f0();
    if (FUN_80232760(0x42414742)) {           // 'BAGB'
        door = entity_find_by_fourcc(0x54484452); // 'THDR'
        FUN_802333ec((int)((unsigned long long)scoped >> 0x20), (int)scoped);
        if (door != 0) {
            *(unsigned int *)(*(int *)(door + 4) + 0x30) =
                *(unsigned int *)(*(int *)(door + 4) + 0x30) & 0xfffffffe;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8021d128  statlvl_spinner_sound_tick
// Rotor/spinner audio tick. Derives a spin-rate proxy from +0xe0, decays it
// DAT_806108e4 times by FLOAT_806183e0, advances the visual angle +0xc8, then
// drives a 3D loop source (sound 0x373) whose volume scales with spin rate and
// whose pitch (sound_set_pitch) tracks it, with an alternate base when a state
// flag (**+0x2d8 bit0) is clear.
// ---------------------------------------------------------------------------
void statlvl_spinner_sound_tick(int entity)
{
    float decay;
    int   handleValid;
    double spinRate;
    double pitchBase;

    decay = FLOAT_806183e0;
    spinRate = (double)(*(float *)(entity + 0xe0) /
               ((FLOAT_806183d8 *
                 (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) /
                FLOAT_806183dc));
    for (int i = 0; i < DAT_806108e4; i = i + 1) {
        *(float *)(entity + 0xe0) = *(float *)(entity + 0xe0) * decay;
    }
    *(float *)(entity + 200) =
        *(float *)(entity + 0xe0) *
        (float)((double)FLOAT_80618384 / (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
        FLOAT_806108ec + *(float *)(entity + 200);

    handleValid = sound_update_3d_source(
        (double)(float)((double)FLOAT_806183c8 * spinRate - (double)FLOAT_806183a8),
        (double)FLOAT_806183e4, (double)FLOAT_80618384,
        entity + 0x22c, entity + 100, 0x373, 0);
    if (handleValid != 0) {
        pitchBase = (double)FLOAT_806183a8;
        if ((**(unsigned int **)(entity + 0x2d8) & 1) == 0) {
            pitchBase = (double)FLOAT_806183e8;
        }
        sound_set_pitch((double)(float)((double)FLOAT_806183a8 * spinRate + pitchBase),
                        *(unsigned int *)(entity + 0x22c));
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
}

// ---------------------------------------------------------------------------
// 0x8021d264  statlvl_rotor_bone_tick
// Single-bone rotor matrix tick. Builds a rotate-about-pivot transform on the
// entity's local matrix (+4 record, +0x10 -> +0x40 slot): identity, translate to
// the bone vertex pivot, Euler rotation about Z by (FLOAT_806183ec * +0xe0) /
// FLOAT_806183f0, translate back, then compose into the slot. Sets dirty bit
// 0x200000.
// ---------------------------------------------------------------------------
void statlvl_rotor_bone_tick(int entity)
{
    int   *record;
    float *pivot;
    float  posMatrix[17];
    double pvx;
    double pvy;
    double pvz;
    double travel;

    travel = (double)*(float *)(entity + 0x1a4);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    __stat_mtx_set_translation((double)FLOAT_80618374, travel, (double)FLOAT_80618374);

    record = *(int **)(*(int *)(entity + 4) + 4);
    pivot = (float *)record[*record * -0x30 + 0x1b];
    pvx = (double)pivot[0];
    pvy = (double)pivot[1];
    pvz = (double)pivot[2];
    mtx4_set_transform_euler(pvx, pvy, pvz, (double)FLOAT_80618374, (double)FLOAT_80618374,
                             (double)((FLOAT_806183ec * *(float *)(entity + 0xe0)) / FLOAT_806183f0),
                             (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40));
    __stat_mtx_set_translation(-pvx, -pvy, -pvz);

    pivot = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_multiply(posMatrix, pivot, pivot);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x8021d378  statlvl_plrd_lever_drive
// 'PLRD' paired-lever drive tick (scoped entity). Ensures both the scoped entity
// and its paired switch-slot entity are registered for update list 2, sets the
// paired entity's travel +0x1a4 from a normalized lever position, advances its
// spin +0xe0 up to a cap FLOAT_806183f8, then integrates four rotation axes
// (three at +0xe0/e4/e8, the fourth at +0xd0), each stepped by a per-frame delta
// and wrapped at FLOAT_80618400, with the sign chosen per the direction bitmask
// at +0x1a0.
// ---------------------------------------------------------------------------
void statlvl_plrd_lever_drive(void)
{
    float wrapLimit;
    float stepScale;
    double biasMant;
    float cap;
    float spin;
    int   scoped;
    int  *slot;
    int   axisOff;
    float *axis;
    int   axisIdx;
    int   remaining;
    long long threaded;

    threaded = (long long)(unsigned int)FUN_803826f0();
    scoped = (int)((unsigned long long)threaded >> 0x20);
    slot = switch_find_slot_by_entity(0x44524c50);   // 'PLRD'

    if (*(int *)(scoped + 0x15c) == 0) {
        entity_register_for_update(scoped, 2, (int)0x8021da40); /* statlvl_multi_rotor_tick cb */
    }
    if (*(int *)(slot[2] + 0x15c) == 0) {
        entity_register_for_update(slot[2], 2, (int)0x8021d264); /* statlvl_rotor_bone_tick cb */
    }

    cap = FLOAT_806183f8;
    *(float *)(slot[2] + 0x1a4) =
        FLOAT_806183f4 * (*(float *)((int)threaded + 0xc) / *(float *)((int)threaded + 0x18));
    spin = *(float *)(slot[2] + 0xe0);
    if (spin < cap) {
        *(float *)(slot[2] + 0xe0) = spin + FLOAT_806108ec;
    }

    wrapLimit = FLOAT_80618400;
    stepScale = FLOAT_806183fc;
    biasMant  = DOUBLE_80618390;
    axisIdx = 0;
    axisOff = 0;
    remaining = 4;
    do {
        float step = stepScale *
            (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - biasMant)) *
            FLOAT_806108ec;
        if ((*(unsigned int *)(scoped + 0x1a0) & (1u << axisIdx)) != 0) {
            step = -step;
        }
        if (axisIdx < 3) {
            axis = (float *)(scoped + axisOff + 0xe0);
        } else {
            axis = (float *)(scoped + 0xd0);
        }
        *axis = *axis + step;
        if (*axis <= wrapLimit) {
            if (*axis < FLOAT_80618374) {
                *axis = wrapLimit;
            }
        } else {
            *axis = *axis - wrapLimit;
        }
        axisIdx = axisIdx + 1;
        axisOff = axisOff + 4;
        remaining = remaining + -1;
    } while (remaining != 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8021d4fc  statlvl_switch_toggle_event
// Switch/button toggle event. XORs the target entity's state bitmask (+0x1a0) by
// the value carried in the event descriptor (*param+0xc); if the target is the
// class-0x2cb button and the toggle turned it on (delta == 1), plays activation
// sound 0x37f at the button's world position.
// ---------------------------------------------------------------------------
void statlvl_switch_toggle_event(int *event)
{
    int entity;
    int *worldPos;
    unsigned int delta;
    int params[16];

    delta = *(unsigned int *)(*event + 0xc);
    entity = entity_find_by_fourcc(*(unsigned int *)(*event + 0x14));
    if (((entity != 0) &&
         (*(unsigned int *)(entity + 0x1a0) = *(unsigned int *)(entity + 0x1a0) ^ delta,
          *(short *)(entity + 0x94) == 0x2cb)) && (delta == 1)) {
        worldPos = entity_get_world_position(entity);
        sound_init_play_params((int)params);
        *((char *)params + 0x28) = 1;
        params[0] = 0x37f;
        *(float *)(params + 7) = FLOAT_80618404;
        params[8] = worldPos[0];
        params[9] = worldPos[1];
        params[10] = worldPos[2];
        sound_play_request(params);
    }
}

// ---------------------------------------------------------------------------
// 0x8021d5a4  statlvl_lever_master_tick
// Master lever/floodgate mechanism tick (scoped entity via __save_gpr_25). While
// the entity's mover is idle and doors are enabled:
//   * integrate four rotation axes (like statlvl_plrd_lever_drive), tracking how
//     many hit their clamp and firing per-axis "click" sound 0x380 whenever an
//     axis moves more than a threshold; a per-axis "engaged" flag lives in
//     DAT_80612868+off+8.
//   * when all four are clamped, clear the collision bit on the mover.
//   * if the "flood" bit 0x10 is set, start loop 0x381 once, accelerate the water
//     level +0xb8 and raise the surface height +0x68 up to rest FLOAT_8061840c
//     (fading the loop out and playing splash 0x380 on arrival), then push the
//     new water height into all soundpad zone nodes of kinds 0x1d and 0x2d.
// ---------------------------------------------------------------------------
void statlvl_lever_master_tick(void)
{
    float velClamp;
    int   entity;
    int   clampedCount;
    float *axis;
    int  *worldPos;
    int  *zoneNode;
    int   axisIdx;
    int   axisOff;
    unsigned int stateBits;
    double stepScaled;
    double axisPrev;
    double clampHi;
    double clampLo;
    double biasMant;
    double fullRef;
    double phaseBase;
    int   zoneCount;
    int   zoneIdx;
    int   nodeOff;
    int   paramsBig[16];
    int   paramsFade[16];
    int   paramsClick[16];

    entity = __save_gpr_25();
    clampedCount = 0;
    int actorRec = *(int *)(entity + 0x7c);
    if ((mover_get_active_state(entity) == 0) && (DAT_806108e4 != 0)) {
        stateBits = 0;
        biasMant  = DOUBLE_80618390;
        axisOff   = 0;
        double negClamp = -(double)FLOAT_80618408;
        fullRef  = (double)FLOAT_80618384;
        phaseBase = (double)FLOAT_806183a8;
        do {
            clampHi = (double)FLOAT_80618408;
            clampLo = (double)FLOAT_80618374;
            stepScaled = (double)(float)((double)FLOAT_806183fc *
                (double)((float)(fullRef / (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - biasMant)) *
                         FLOAT_806108ec));
            double signedStep = stepScaled;
            if ((*(unsigned int *)(entity + 0x1a0) & (1u << stateBits)) != 0) {
                signedStep = -stepScaled;
            }
            if ((stateBits & 1) == 0) {
                clampHi = (double)FLOAT_80618374;
                clampLo = negClamp;
            } else if (stateBits == 1) {
                clampHi = (double)FLOAT_80618388;
            }
            if ((int)stateBits < 3) {
                axis = (float *)(entity + axisOff + 0xe0);
            } else {
                axis = (float *)(entity + 0xd0);
            }
            axisPrev = (double)*axis;
            *axis = (float)(axisPrev + signedStep);
            if ((double)*axis <= clampHi) {
                if ((double)*axis < clampLo) {
                    *axis = (float)clampLo;
                }
            } else {
                *axis = (float)clampHi;
            }
            if ((*(int *)(actorRec + 8) == 3) && (axisPrev == (double)*axis)) {
                clampedCount = clampedCount + 1;
            }
            if ((float)(phaseBase * stepScaled) <= ABS((float)(axisPrev - (double)*axis))) {
                *(int *)(DAT_80612868 + axisOff + 8) = 1;
            } else if (*(int *)(DAT_80612868 + axisOff + 8) != 0) {
                worldPos = entity_get_world_position(entity);
                sound_init_play_params((int)paramsClick);
                paramsClick[0] = 0x380;                             /* sound id       */
                *(float *)((char *)paramsClick + 0x18) = FLOAT_80618404; /* gain (0x18) */
                *(int *)((char *)paramsClick + 0x1c) = worldPos[0]; /* pos X (0x1c)   */
                *(int *)((char *)paramsClick + 0x20) = worldPos[1]; /* pos Y (0x20)   */
                *(int *)((char *)paramsClick + 0x24) = worldPos[2]; /* pos Z (0x24)   */
                *((char *)paramsClick + 0x28) = 1;                  /* positioned     */
                sound_play_request(paramsClick);
                *(int *)(DAT_80612868 + axisOff + 8) = 0;
            }
            stateBits = stateBits + 1;
            axisOff = axisOff + 4;
        } while ((int)stateBits < 4);

        if (clampedCount == 4) {
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) & 0xfffffffe;
        }

        stateBits = *(unsigned int *)(entity + 0x1a0);
        if ((stateBits & 0x10) != 0) {
            if ((stateBits & 0x20) == 0) {
                *(unsigned int *)(entity + 0x1a0) = stateBits | 0x20;
                sound_init_play_params((int)paramsFade);
                paramsFade[0] = 0x381;
                stateBits = sound_play_request(paramsFade);
                *(unsigned int *)(entity + 0x22c) = stateBits;
            }
            velClamp = *(float *)(entity + 0xb8);
            if (-velClamp <= *(float *)(entity + 0x68) - FLOAT_8061840c) {
                *(float *)(entity + 0xb8) =
                    FLOAT_80618410 *
                    (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                    FLOAT_806108ec + velClamp;
            } else {
                *(float *)(entity + 0xb8) =
                    velClamp * -((FLOAT_80618384 /
                        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                        FLOAT_806183a8 - FLOAT_80618384);
            }
            {
                float clamp = FLOAT_80618414 * (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390);
                if (clamp < *(float *)(entity + 0xb8)) {
                    clamp = *(float *)(entity + 0xb8);
                }
                *(float *)(entity + 0xb8) = clamp;
            }
            velClamp = FLOAT_8061840c;
            fullRef = (double)FLOAT_80618384;
            *(float *)(entity + 0x68) =
                *(float *)(entity + 0xb8) *
                (float)(fullRef / (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
                FLOAT_806108ec + *(float *)(entity + 0x68);
            if ((*(float *)(entity + 0x68) < velClamp) &&
                (*(float *)(entity + 0x68) = velClamp, -1 < *(int *)(entity + 0x22c))) {
                sound_register_fade_out(fullRef, (double)FLOAT_806183b4,
                                        (int *)(entity + 0x22c), (unsigned int *)0);
                sound_init_play_params((int)(paramsBig + 1));
                paramsBig[1] = 0x380;
                sound_play_request(paramsBig + 1);
            }
            zoneCount = soundpad_get_entity_data(0x1d, paramsBig);
            nodeOff = 0;
            for (zoneIdx = 0; zoneIdx < zoneCount; zoneIdx = zoneIdx + 1) {
                zoneNode = soundpad_resolve_zone_node(*(int *)(paramsBig[0] + nodeOff));
                nodeOff = nodeOff + 4;
                *(short *)((int)zoneNode + 10) = (short)*(int *)(entity + 0x34);
                zoneNode[4] = (int)(float)((double)FLOAT_80618418 + (double)*(float *)(entity + 0x68));
            }
            zoneCount = soundpad_get_entity_data(0x2d, paramsBig);
            nodeOff = 0;
            for (zoneIdx = 0; zoneIdx < zoneCount; zoneIdx = zoneIdx + 1) {
                zoneNode = soundpad_resolve_zone_node(*(int *)(paramsBig[0] + nodeOff));
                nodeOff = nodeOff + 4;
                *(short *)((int)zoneNode + 10) = (short)*(int *)(entity + 0x34);
                zoneNode[4] = (int)(float)((double)FLOAT_80618418 + (double)*(float *)(entity + 0x68));
            }
            *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 2;
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x8021da40  statlvl_multi_rotor_tick
// Four-rotor Y-rotation matrix tick. Resets the entity's local matrix (+4 record,
// +0x10) to identity, sets four Y-rotation sub-matrices (slots +0x40/+0x80/+0xc0/
// +0x100) from rotation angles derived from +0xe0/+0xe4/+0xe8/+0xd0, then leaves
// the +0x140 slot identity. Sets dirty bit 0x200000.
// ---------------------------------------------------------------------------
void statlvl_multi_rotor_tick(int entity)
{
    int base = *(int *)(*(int *)(entity + 4) + 0x10);
    mtx4_set_identity(base);
    mtx4_set_rotation_y((double)((FLOAT_806183ec * *(float *)(entity + 0xe0)) / FLOAT_806183f0),
                        (float *)(base + 0x40));
    mtx4_set_rotation_y((double)((FLOAT_806183ec * *(float *)(entity + 0xe4)) / FLOAT_806183f0),
                        (float *)(base + 0x80));
    mtx4_set_rotation_y((double)((FLOAT_806183ec * *(float *)(entity + 0xe8)) / FLOAT_806183f0),
                        (float *)(base + 0xc0));
    mtx4_set_rotation_y((double)((FLOAT_806183ec * *(float *)(entity + 0xd0)) / FLOAT_806183f0),
                        (float *)(base + 0x100));
    mtx4_set_identity(base + 0x140);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ===========================================================================
// Batch: batch_669  (addresses 0x8021db20 .. 0x8021e518)
// Control-station animated-prop sound/motion ticks, matrix ticks, a 'WORK'-team
// bot anim-event handler, and the core/reactor lever-driven state machine. These
// continue the worm_stat_level driver, reusing DAT_806108e4 (doors-enabled gate),
// DAT_806109c0 (frame denom), DAT_80611928 (frame counter) and the FLOAT_806184xx
// constant band.
// ===========================================================================

// --- Additional named cross-module helpers (file-local externs) ------------
extern double math_sqrt(double x);                               // @0x80131538
extern void   anim_set_blend_weight_alt(double weight, int record); // @0x80022fac
extern void   particle_entity_enable_all(void);                  // @0x800e442c
extern void   particle_entity_disable_all(void);                 // @0x800e4490
extern int    aiactor_find_player_by_team(int team);             // @0x8013d618
extern int    anim_event_scan_active(void);                      // @0x800293f0
extern int   *anim_active_clip_slot(int bot);                    // @0x80025868
extern void   object_sound_slot_accumulate(int object, int slot, int amount); // @0x801a9a8c

// FUN_803826e4 pushes/returns the scoped entity here (int); FUN_80382730 pops.
extern int  FUN_803826e4(void);   // scoped-entity push (returns entity)
extern void FUN_80382730(void);   // scoped-entity pop

// --- Additional constants for this section ---------------------------------
extern int    DAT_80611928;   // global frame/time counter (for elapsed-since +0x90)
extern float  DAT_80583228;   // sine LUT base (0x800-entry, &0x7ff)
extern float  FLOAT_80618370; // sine LUT phase scale
extern float  FLOAT_8061841c; // spin-up ramp factor
extern float  FLOAT_80618420; // spin-up loop volume scale
extern float  FLOAT_80618424; // 3d source coord (0x37d loop)
extern float  FLOAT_80618428; // pitch base (0x37d)
extern float  FLOAT_8061842c; // pitch scale (0x37d)
extern float  FLOAT_80618430; // conveyor drive scale
extern float  FLOAT_80618434; // 3d source coord (0x37e loop)
extern float  FLOAT_80618438; // shutdown ramp target
extern float  FLOAT_8061843c; // 3d source coord (0x38d loop)
extern float  FLOAT_80618440; // core idle target
extern float  FLOAT_80618444; // core fade-out rate
extern float  FLOAT_80618448; // core state-1 target base
extern float  FLOAT_8061844c; // core state-2 target base
extern float  FLOAT_80618450; // core state-2 rng scale
extern float  FLOAT_80618454; // core slew rate
extern float  FLOAT_80618458; // cam LUT bias
extern float  FLOAT_8061845c; // cam LUT phase scale

// ---------------------------------------------------------------------------
// 0x8021db20  statlvl_motor_spinup_tick
// Powered-motor spin-up prop. State 2 (running): ramp blend weight +0x1a4 by
// FLOAT_8061841c (clamped to 1.0), push it to the anim blend, and derive a 3D
// loop (sound 0x37d) volume from sqrt(weight). Transitioning from state 1 plays
// a start-up one-shot 0x37c and enters state 2. Always updates the 3D source and
// tracks pitch (sound_set_pitch) with the loop volume.
// ---------------------------------------------------------------------------
void statlvl_motor_spinup_tick(int entity)
{
    float ramp;
    int   handleValid;
    int  *worldPos;
    double loopVol;
    double blend;
    int   params[16];

    ramp = FLOAT_8061841c;
    loopVol = (double)FLOAT_806183a0;
    int state = *(int *)(entity + 0x1a0);
    if (state == 2) {
        for (int i = 0; i < DAT_806108e4; i = i + 1) {
            *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) * ramp;
        }
        if (FLOAT_80618384 < *(float *)(entity + 0x1a4)) {
            *(float *)(entity + 0x1a4) = FLOAT_80618384;
        }
        anim_set_blend_weight_alt((double)*(float *)(entity + 0x1a4), *(int *)(entity + 4));
        blend = math_sqrt((double)*(float *)(entity + 0x1a4));
        loopVol = (double)(float)((double)FLOAT_80618420 * blend);
        if ((double)FLOAT_80618404 < (double)(float)((double)FLOAT_80618420 * blend)) {
            loopVol = (double)FLOAT_80618404;
        }
    } else if ((state < 2) && (0 < state)) {
        worldPos = entity_get_world_position(entity);
        sound_init_play_params((int)params);
        *((char *)params + 0x28) = 1;
        params[0] = 0x37c;
        *(float *)(params + 7) = FLOAT_80618404;
        params[8] = worldPos[0];
        params[9] = worldPos[1];
        params[10] = worldPos[2];
        sound_play_request(params);
        *(int *)(entity + 0x1a0) = 2;
    }
    worldPos = entity_get_world_position(entity);
    handleValid = sound_update_3d_source(loopVol, (double)FLOAT_80618424, (double)FLOAT_80618384,
                                         entity + 0x22c, (int)worldPos, 0x37d, 0);
    if (handleValid != 0) {
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
    if (-1 < *(int *)(entity + 0x22c)) {
        sound_set_pitch((double)(float)((double)FLOAT_8061842c * loopVol + (double)FLOAT_80618428),
                        *(unsigned int *)(entity + 0x22c));
    }
}

// ---------------------------------------------------------------------------
// 0x8021dc94  statlvl_work_bot_anim_tick
// 'WORK'-team bot animation-event handler (statlvl_master_tick's first sub-tick).
// When the WORK-team bot's active anim clip is 0x6e1, sets its object state to 5,
// clears the object's +0x54c byte flag, and accumulates 1000 into sound slot 2.
// ---------------------------------------------------------------------------
void statlvl_work_bot_anim_tick(void)
{
    int bot;
    int *clip;
    int object;

    bot = aiactor_find_player_by_team(0x574f524b);   // 'WORK'
    if (((bot != 0) && (anim_event_scan_active() != 0)) &&
        (clip = anim_active_clip_slot(bot), *clip == 0x6e1)) {
        object = *(int *)(bot + 0x7c);
        *(int *)(object + 0x94) = 5;
        *(char *)(object + 0x54c) = 0;
        object_sound_slot_accumulate(object, 2, 1000);
    }
}

// ---------------------------------------------------------------------------
// 0x8021dd10  statlvl_conveyor_sound_tick
// Conveyor/belt sound tick. When running (state +0x1a0 != 0): ramp +0x1a4 by
// FLOAT_8061841c (clamped to 1.0), integrate belt scroll +0x70 by
// FLOAT_80618430 * weight * frame-delta. Drives a 3D loop (sound 0x37e) whose
// volume tracks sqrt(weight) and whose pitch tracks the same.
// ---------------------------------------------------------------------------
void statlvl_conveyor_sound_tick(int entity)
{
    float ramp;
    int   handleValid;
    int  *worldPos;
    double loopVol;

    ramp = FLOAT_8061841c;
    loopVol = (double)FLOAT_806183a0;
    if (*(int *)(entity + 0x1a0) != 0) {
        for (int i = 0; i < DAT_806108e4; i = i + 1) {
            *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) * ramp;
        }
        if (FLOAT_80618384 < *(float *)(entity + 0x1a4)) {
            *(float *)(entity + 0x1a4) = FLOAT_80618384;
        }
        *(float *)(entity + 0x70) =
            FLOAT_806108ec * FLOAT_80618430 * *(float *)(entity + 0x1a4) *
            (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) +
            *(float *)(entity + 0x70);
        loopVol = math_sqrt((double)*(float *)(entity + 0x1a4));
    }
    worldPos = entity_get_world_position(entity);
    handleValid = sound_update_3d_source(loopVol, (double)FLOAT_80618434, (double)FLOAT_80618384,
                                         entity + 0x22c, (int)worldPos, 0x37e, 0);
    if (handleValid != 0) {
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
    if (-1 < *(int *)(entity + 0x22c)) {
        sound_set_pitch((double)(float)((double)FLOAT_806183a8 * loopVol + (double)FLOAT_806183a8),
                        *(unsigned int *)(entity + 0x22c));
    }
}

// ---------------------------------------------------------------------------
// 0x8021de3c  statlvl_dual_bone_counter_rotate
// Dual-bone counter-rotating matrix tick. Builds two rotate-about-pivot Euler
// transforms on the entity's local matrix: the +0x80 slot rotates the tail bone
// by -(+0x1a4), the +0x40 slot rotates the head bone by +(+0x1a4). Leaves the
// +0x10 base identity. Sets dirty bit 0x200000.
// ---------------------------------------------------------------------------
void statlvl_dual_bone_counter_rotate(int entity)
{
    int   *record;
    int    bone;
    float *slot;
    float  posMatrix[17];
    double pvx;
    double pvy;
    double pvz;

    record = *(int **)(*(int *)(entity + 4) + 4);
    bone = record[*record * -0x30 + 0x1b];
    pvx = (double)*(float *)(bone + 0x18);
    pvy = (double)*(float *)(bone + 0x1c);
    pvz = (double)*(float *)(bone + 0x20);
    mtx4_set_transform_euler(pvx, pvy, pvz, (double)FLOAT_80618374, (double)FLOAT_80618374,
                             -(double)*(float *)(entity + 0x1a4),
                             (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80));
    __stat_mtx_set_translation(-pvx, -pvy, -pvz);
    slot = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80);
    mtx4_multiply(posMatrix, slot, slot);

    record = *(int **)(*(int *)(entity + 4) + 4);
    slot = (float *)record[*record * -0x30 + 0x1b];
    pvx = (double)slot[0];
    pvy = (double)slot[1];
    pvz = (double)slot[2];
    mtx4_set_transform_euler(pvx, pvy, pvz, (double)FLOAT_80618374, (double)FLOAT_80618374,
                             (double)*(float *)(entity + 0x1a4),
                             (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40));
    __stat_mtx_set_translation(-pvx, -pvy, -pvz);
    slot = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_multiply(posMatrix, slot, slot);

    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x8021df98  statlvl_shutdown_ramp_tick
// Machine shutdown ramp. Advances +0x1a4 by a per-frame delta; on reaching
// FLOAT_80618438 it clears the collision-enabled bit (+4 rec, +0x30 bit0), sets
// state 1, re-registers for update list 3, disables all particles, and plays the
// shutdown-complete one-shot 0x38b.
// ---------------------------------------------------------------------------
void statlvl_shutdown_ramp_tick(int entity)
{
    int params[16];

    if (FLOAT_80618438 <= *(float *)(entity + 0x1a4)) {
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) & 0xfffffffe;
        *(int *)(entity + 0x1a0) = 1;
        entity_register_for_update(entity, 3, 0);
        particle_entity_disable_all();
        sound_init_play_params((int)params);
        params[0] = 0x38b;
        sound_play_request(params);
    } else {
        *(float *)(entity + 0x1a4) =
            *(float *)(entity + 0x1a4) +
            FLOAT_806108ec *
            (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390));
    }
}

// ---------------------------------------------------------------------------
// 0x8021e050  statlvl_startup_event
// Machine start-up event. Registers the entity for update lists 3 and 2, enables
// all particles, and plays two start-up one-shots (0x389, 0x38a) at the entity's
// world position.
// ---------------------------------------------------------------------------
void statlvl_startup_event(int entity)
{
    int  *worldPos;
    int   paramsA[16];
    int   paramsB[16];

    entity_register_for_update(entity, 3, (int)0x8021df98); /* statlvl_shutdown_ramp_tick cb */
    entity_register_for_update(entity, 2, (int)0x8021de3c); /* statlvl_dual_bone_counter_rotate cb */
    particle_entity_enable_all();

    worldPos = entity_get_world_position(entity);
    sound_init_play_params((int)paramsA);
    *((char *)paramsA + 0x28) = 1;
    paramsA[0] = 0x389;
    paramsA[7] = worldPos[0];
    paramsA[8] = worldPos[1];
    paramsA[9] = worldPos[2];
    sound_play_request(paramsA);

    worldPos = entity_get_world_position(entity);
    sound_init_play_params((int)paramsB);
    *((char *)paramsB + 0x28) = 1;
    paramsB[0] = 0x38a;
    paramsB[7] = worldPos[0];
    paramsB[8] = worldPos[1];
    paramsB[9] = worldPos[2];
    sound_play_request(paramsB);
}

// ---------------------------------------------------------------------------
// 0x8021e134  statlvl_simple_loop_tick
// Simple ambient prop 3D loop tick (sound 0x38d) at the entity's world position.
// ---------------------------------------------------------------------------
void statlvl_simple_loop_tick(int entity)
{
    int *worldPos;
    int  handleValid;

    worldPos = entity_get_world_position(entity);
    handleValid = sound_update_3d_source((double)FLOAT_80618404, (double)FLOAT_8061843c,
                                         (double)FLOAT_80618384, entity + 0x22c, (int)worldPos, 0x38d, 0);
    if (handleValid != 0) {
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
}

// ---------------------------------------------------------------------------
// 0x8021e194  statlvl_core_rotor_state_tick
// Reactor-core rotor state machine (scoped entity). Resolves four related props
// by a shared per-instance suffix byte (+0x60): 'OBL?' (gate), 'OLT?' (light),
// 'RFB?' (rotor body), and the fixed 'CSLL' collision master. Chooses a target
// spin rate by state (+0x1a0): open+valve-on -> state 2 (fast, rng jitter),
// open+valve-off -> state 1 (mid, rng jitter + enable particles), closed ->
// state 0 (idle, disable particles + fade loop). Slews the current rate +0xc0
// toward the target at FLOAT_80618454 * elapsed-since +0x90; when it steps up
// past the target it flags the 'OLT?' light on and sets a per-instance bit in
// the 'CSLL' master (bit index from the suffix, base '1LDN').
// ---------------------------------------------------------------------------
void statlvl_core_rotor_state_tick(void)
{
    int   scoped;
    int   gate;
    int   light;
    int   rotorBody;
    int   collMaster;
    int   suffix;
    int  *slot;
    unsigned int rnd;
    double target;
    double current;
    double slew;

    scoped = FUN_803826e4();
    target = (double)FLOAT_80618374;
    gate       = entity_find_by_fourcc(*(unsigned int *)(scoped + 0x60) & 0xff | 0x4c424f00); // 'OBL' + suffix
    light      = entity_find_by_fourcc(*(unsigned int *)(scoped + 0x60) & 0xff | 0x544c4f00); // 'OLT' + suffix
    rotorBody  = entity_find_by_fourcc(*(unsigned int *)(scoped + 0x60) & 0xff | 0x42465200); // 'RFB' + suffix
    collMaster = entity_find_by_fourcc(0x43534c4c); // 'CSLL'
    suffix = *(int *)(scoped + 0x60);

    if ((gate == 0) || (*(int *)(gate + 0x1a0) == 0)) {
        *(int *)(scoped + 0x1a0) = 0;
    } else {
        slot = switch_find_slot_by_entity(0x54444c56); // 'VLDT'
        if ((slot == (int *)0) || (slot[6] == 0)) {
            *(int *)(scoped + 0x1a0) = 1;
        } else {
            *(int *)(scoped + 0x1a0) = 2;
        }
    }

    int state = *(int *)(scoped + 0x1a0);
    if (state == 1) {
        rnd = rng_counter_tick();
        target = (double)(FLOAT_806183a4 *
                 ((float)((double)CONCAT44(0x43300000, rnd) - DOUBLE_806183d0) / FLOAT_806183ac) +
                 FLOAT_80618448);
        particle_entity_enable_all();
        entity_register_for_update(rotorBody, 3, (int)0x8021e134); /* statlvl_simple_loop_tick cb */
    } else if (state < 1) {
        if (-1 < state) {
            target = (double)FLOAT_80618440;
            particle_entity_disable_all();
            sound_register_fade_out((double)FLOAT_80618404, (double)FLOAT_80618444,
                                    (int *)(rotorBody + 0x22c), (unsigned int *)0);
            entity_register_for_update(rotorBody, 3, 0);
        }
    } else if (state < 3) {
        rnd = rng_counter_tick();
        target = (double)(FLOAT_80618450 *
                 ((float)((double)CONCAT44(0x43300000, rnd) - DOUBLE_806183d0) / FLOAT_806183ac) +
                 FLOAT_8061844c);
    }

    current = (double)*(float *)(scoped + 0xc0);
    slew = (double)(FLOAT_80618454 *
           (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) *
           (float)((double)CONCAT44(0x43300000, DAT_80611928 - *(int *)(scoped + 0x90) ^ 0x80000000) - DOUBLE_80618390));
    if (ABS((double)(float)(target - current)) <= slew) {
        *(float *)(scoped + 0xc0) = (float)target;
    } else if (target <= current) {
        *(float *)(scoped + 0xc0) = (float)(current - slew);
    } else {
        *(float *)(scoped + 0xc0) = (float)(current + slew);
        if (light != 0) {
            *(int *)(*(int *)(light + 4) + 0x30) = 2;
            *(unsigned int *)(*(int *)(collMaster + 4) + 0x30) =
                *(unsigned int *)(*(int *)(collMaster + 4) + 0x30) | (1u << (suffix - 0x4e444c31));
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8021e424  statlvl_cam_piston_tick
// Cam/piston prop tick. Winds down a phase +0x1a4, samples the shared sine LUT
// DAT_80583228 by (phase-derived index & 0x7ff) to produce a velocity, integrates
// the visual angle +0xcc and a translation on the sub-object matrix (+0x40). When
// the translation reaches its limit (+0x28), resets phase and travel and
// re-registers for update list 3.
// ---------------------------------------------------------------------------
void statlvl_cam_piston_tick(int entity)
{
    float phaseScale;
    float lutBias;
    float rotScale;
    float phaseBase;
    float fullRef;
    float lutPhase;
    float step;
    int   subObj;

    lutPhase  = FLOAT_8061845c;
    lutBias   = FLOAT_80618458;
    rotScale  = FLOAT_806183ec;
    phaseBase = FLOAT_806183a8;
    fullRef   = FLOAT_80618384;
    phaseScale = FLOAT_80618370;
    subObj = *(int *)(entity + 0x7c);

    step = FLOAT_806108ec *
           (FLOAT_80618384 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390));
    *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) - step;
    step = step * *(float *)(entity + 0xd0) *
           (lutBias + (&DAT_80583228)[(int)(lutPhase * rotScale * phaseScale * *(float *)(entity + 0x1a4) + phaseBase) & 0x7ff]);
    *(float *)(entity + 0xcc) = *(float *)(entity + 0xcc) + step;
    *(float *)(subObj + 0x40) = step * *(float *)(entity + 0x1c4) + *(float *)(subObj + 0x40);
    if (*(float *)(subObj + 0x28) <= *(float *)(subObj + 0x40)) {
        *(float *)(entity + 0x1a4) = FLOAT_80618374;
        *(float *)(subObj + 0x40) = fullRef;
        entity_register_for_update(entity, 3, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x8021e518  statlvl_single_bone_rotate
// Single-bone rotate-about-pivot matrix tick. Resets the entity's local matrix
// to identity, then builds a rotate-about-bone-pivot Euler transform (rotation
// angle = +0xcc) into the +0x40 slot. Sets dirty bit 0x200000.
// ---------------------------------------------------------------------------
void statlvl_single_bone_rotate(int entity)
{
    int   *record;
    float *pivot;
    float  posMatrix[17];
    double pvx;
    double pvy;
    double pvz;

    record = *(int **)(*(int *)(entity + 4) + 4);
    pivot = (float *)record[*record * -0x30 + 0x1b];
    pvx = (double)pivot[0];
    pvy = (double)pivot[1];
    pvz = (double)pivot[2];
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    mtx4_set_transform_euler(pvx, pvy, pvz, (double)FLOAT_80618374, (double)FLOAT_80618374,
                             (double)*(float *)(entity + 0xcc),
                             (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40));
    __stat_mtx_set_translation(-pvx, -pvy, -pvz);
    pivot = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_multiply(posMatrix, pivot, pivot);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ===========================================================================
// Batch: batch_670  (addresses 0x8021e5fc .. 0x8021edcc)
// Control-station object-init class dispatcher (classes 0x2c2..0x2e1 — wires up
// the door/switch/prop behaviours from batches 667-669), a story-gated portal
// opener, and a SECOND ambient-soundzone subsystem (block DAT_80612870, sound
// banks 0x1b/0x19) with its own per-player zone-volume LUT.
// ===========================================================================

// --- Additional named cross-module helpers (file-local externs) ------------
extern void render_instance_set_material(double v, int *instance, int mode); // @0x80061da0
extern void anim_play_scaled(double a, double b);                // @0x80023280
extern void entity_set_render_type(int entity, int renderCb);    // @0x80070b80
extern int *soundpad_find_active_portal(int *zoneA, int *zoneB); // @0x8006c74c
extern int  FUN_8025b8e4(int a);   // player/team-count query (returns count)

// Render-type callbacks referenced by the object-init dispatcher; declared as
// externs so their addresses can be passed by value (matching the decompiler's
// entity_set_render_type(entity, <cb>) usage).
extern void weapon_impact_smoke_dispatch(void);   // @0x800822ac
extern void damage_character_receive(void);       // @0x80091628

// --- Additional constants / data -------------------------------------------
extern int    DAT_80612870;   // ambient soundzone block ptr (0x10 bytes; alloced in preload)
extern char   DAT_805015bd;   // level/story stage byte
extern int    DAT_8045d958;   // portal-pair table (id-A at +0, id-B at +4; 8-byte stride)
extern int    DAT_8045d95c;
extern double DOUBLE_80618490; // int<->double bias mantissa (this LUT's average path)
extern float  FLOAT_80618460; // switch-prop init +0xd0 constant
extern float  FLOAT_80618464; // class 0x2c3 init +0xd0
extern float  FLOAT_80618468; // class 0x2cb init axes
extern float  FLOAT_8061846c; // class 0x2c8/0x2d1 init travel base
extern float  FLOAT_80618470; // zone LUT: default/idle volume
extern float  FLOAT_80618474; // zone LUT: value for 0xc/0x13
extern float  FLOAT_80618478; // zone LUT: value for 6/0x12
extern float  FLOAT_8061847c; // zone LUT: full/1.0 reference
extern float  FLOAT_80618480; // ambient slew rate scale
extern float  FLOAT_80618484; // ambient min slew
extern float  FLOAT_80618488; // 3d source secondary coord
extern char   DAT_805015bd;   // (declared above)

// ---------------------------------------------------------------------------
// 0x8021e5fc  statlvl_switch_prop_reset
// Switch-linked prop reset. Resolves the switch slot bound to this prop by its
// linked-entity id (+0x40), grabs the paired entity (slot[2]), resets its
// material (mode 1) and travel (+0x1a4 = 0), registers it for update lists 3 and
// 2, sets +0xd0 to FLOAT_80618460 and clears the slot's +0x40 word.
// ---------------------------------------------------------------------------
void statlvl_switch_prop_reset(int prop)
{
    int *slot;
    int  paired;

    slot = switch_find_slot_by_entity(*(int *)(prop + 0x40));
    paired = slot[2];
    render_instance_set_material((double)FLOAT_80618374, *(int **)(paired + 4), 1);
    *(float *)(paired + 0x1a4) = FLOAT_80618374;
    entity_register_for_update(paired, 3, (int)0x8021e424); /* statlvl_cam_piston_tick cb */
    entity_register_for_update(paired, 2, (int)0x8021e518); /* statlvl_single_bone_rotate cb */
    *(float *)(paired + 0xd0) = FLOAT_80618460;
    *(float *)&slot[0x10] = FLOAT_80618374;
}

// ---------------------------------------------------------------------------
// 0x8021e688  statlvl_object_init_dispatch
// Master object-init dispatcher for the control-station prop classes (scoped
// entity). Switches on class id (+0x94) and wires each prop's initial state +
// update-list callbacks:
//   0x2c2/0x2c3 - door: anim reset, update list 2 (slide), +0xd0 speed const
//   0x2c5       - crusher: flagged, gated by team/player count -> armed vs idle
//   0x2c7       - simple update list 3
//   0x2c8/0x2c9 - lever: travel base, 'ENM1' variant -> state 2, update list 4
//   0x2ca       - byte flag 2, update list 3
//   0x2cb       - button/floodgate lever: 4 axes seeded, update lists 4+2, push
//                 water height into soundpad zone nodes (kinds 0x1d/0x2d)
//   0x2cd/0x2d6 - collision-master clear / OBL1 shutdown variant
//   0x2cf       - spinner: seed spin from frame rate, update list 3
//   0x2d1       - 'GOV1' variant -> state 1, update list 3
//   0x2d4       - 'CHAN' variant -> state 1, weapon-impact-smoke render type
//   0x2e1       - particles off
// ---------------------------------------------------------------------------
void statlvl_object_init_dispatch(void)
{
    float posY;
    int   entity;
    int   count;
    int  *idWords;
    int   nodeCount;
    int   nodeOff;
    int  *zoneNode;
    double heightBias;
    long long threaded;

    threaded = (long long)(unsigned int)__save_gpr_28();
    entity  = (int)((unsigned long long)threaded >> 0x20);
    idWords = (int *)(int)threaded;

    switch (*(unsigned short *)(entity + 0x94)) {
    case 0x2c2:
        anim_play_scaled((double)FLOAT_80618374, (double)FLOAT_80618374);
        entity_register_for_update(entity, 2, (int)0x8001d9ec); /* model_pose_eval */
        *(int *)(entity + 0x1a0) = 0;
        *(float *)(entity + 0x1a4) = FLOAT_80618374;
        *(float *)(entity + 0xd0) = FLOAT_80618404;
        break;
    case 0x2c3:
        anim_play_scaled((double)FLOAT_80618374, (double)FLOAT_80618374);
        entity_register_for_update(entity, 2, (int)0x8001d9ec); /* model_pose_eval */
        *(int *)(entity + 0x1a0) = 0;
        *(float *)(entity + 0x1a4) = FLOAT_80618374;
        *(float *)(entity + 0xd0) = FLOAT_80618464;
        break;
    case 0x2c5:
        *(char *)(entity + 0x32) = 1;
        count = FUN_8025b8e4(0);
        if ((count < 2) && ((DAT_805015bd != '\n') || (DAT_80612610 < 2))) {
            *(int *)(entity + 0x1a0) = 0;
        } else {
            *(int *)(entity + 0x1a0) = 1;
            *(float *)(entity + 0x70) = FLOAT_8061839c;
        }
        entity_register_for_update(entity, 3, (int)0x8021c7f8); /* statlvl_door_ramp_tick */
        break;
    case 0x2c7:
        *(int *)(entity + 0x1a0) = 0;
        entity_register_for_update(entity, 3, (int)0x8021ce64); /* statlvl_crusher_hazard_tick */
        break;
    case 0x2c8:
    case 0x2c9:
        *(float *)(entity + 0x1a4) = FLOAT_8061846c;
        if (*idWords == 0x454e4d31) {   // 'ENM1'
            *(float *)(entity + 0x1a4) = FLOAT_80618384;
            *(int *)(entity + 0x1a0) = 2;
        }
        entity_register_for_update(entity, 4, (int)0x8021db20); /* statlvl_motor_spinup_tick */
        break;
    case 0x2ca:
        *(char *)(entity + 0x32) = 2;
        *(int *)(entity + 0x1a0) = 0;
        entity_register_for_update(entity, 3, (int)0x8021e194); /* statlvl_core_rotor_state_tick */
        break;
    case 0x2cb:
        *(int *)(entity + 0x1a0) = 5;
        *(float *)(entity + 0xe0) = FLOAT_80618468;
        *(float *)(entity + 0xe4) = FLOAT_80618388;
        *(float *)(entity + 0xe8) = FLOAT_80618468;
        *(float *)(entity + 0xd0) = FLOAT_80618408;
        entity_register_for_update(entity, 4, (int)0x8021d5a4); /* statlvl_lever_master_tick */
        entity_register_for_update(entity, 2, (int)0x8021da40); /* statlvl_multi_rotor_tick */
        nodeCount = soundpad_get_entity_data(0x1d, idWords);
        heightBias = (double)FLOAT_80618418;
        nodeOff = 0;
        for (int i = 0; i < nodeCount; i = i + 1) {
            zoneNode = soundpad_resolve_zone_node(*(int *)(idWords[0] + nodeOff));
            nodeOff = nodeOff + 4;
            *(short *)((int)zoneNode + 10) = (short)*(int *)(entity + 0x34);
            zoneNode[4] = (int)(float)(heightBias + (double)*(float *)(entity + 0x68));
        }
        nodeCount = soundpad_get_entity_data(0x2d, idWords);
        heightBias = (double)FLOAT_80618418;
        nodeOff = 0;
        for (int i = 0; i < nodeCount; i = i + 1) {
            zoneNode = soundpad_resolve_zone_node(*(int *)(idWords[0] + nodeOff));
            nodeOff = nodeOff + 4;
            *(short *)((int)zoneNode + 10) = (short)*(int *)(entity + 0x34);
            zoneNode[4] = (int)(float)(heightBias + (double)*(float *)(entity + 0x68));
        }
        break;
    case 0x2cd:
        *(int *)(*(int *)(entity + 4) + 0x30) = 0;
        break;
    case 0x2cf:
        *(char *)(entity + 0x32) = 2;
        *(float *)(entity + 0xe0) =
            (FLOAT_806183d8 * (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618390)) /
            FLOAT_806183dc;
        entity_register_for_update(entity, 3, (int)0x8021d128); /* statlvl_spinner_sound_tick */
        break;
    case 0x2d1:
        *(float *)(entity + 0x1a4) = FLOAT_8061846c;
        if (*idWords == 0x474f5631) {   // 'GOV1'
            *(float *)(entity + 0x1a4) = FLOAT_80618384;
            *(int *)(entity + 0x1a0) = 1;
        }
        entity_register_for_update(entity, 3, (int)0x8021dd10); /* statlvl_conveyor_sound_tick */
        break;
    case 0x2d4:
        if (idWords[1] == 0x4348414e) { // 'CHAN'
            *(int *)(entity + 0x1a0) = 1;
        }
        entity_set_render_type(entity, (int)(long long)&weapon_impact_smoke_dispatch);
        break;
    case 0x2d6:
        entity_register_for_update(entity, 0, (int)0x8021e050); /* statlvl_startup_event */
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 4;
        *(int *)(*(int *)(entity + 4) + 0x30) = 0;
        *(int *)(entity + 0x1a0) = 0;
        if ((idWords != (int *)0) && (*idWords == 0x4c424f31)) { // 'LBO1'
            entity_register_for_update(entity, 0, 0);
            *(float *)(entity + 0x1a4) = FLOAT_80618438;
            *(int *)(entity + 0x1a0) = 1;
            entity_register_for_update(entity, 2, (int)0x8021de3c); /* statlvl_dual_bone_counter_rotate */
        }
        particle_entity_disable_all();
        break;
    case 0x2e1:
        particle_entity_disable_all();
        break;
    }
    (void)posY;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8021eaa0  statlvl_open_portals
// Opens 5 portal pairs (scoped): for each (id-A, id-B) pair from the table
// DAT_8045d958/95c, finds the active portal between the two soundpad zones and
// clears its 0x10000 reverb/closed bit.
// ---------------------------------------------------------------------------
void statlvl_open_portals(void)
{
    int  zoneA;
    int  zoneB;
    int  base;
    unsigned int *portal;
    int  i;
    int  off;

    FUN_803826e4();
    off = 0;
    for (i = 0; i < 5; i = i + 1) {
        zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045d95c + off));
        base  = DAT_80611b8c;
        zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045d958 + off));
        portal = (unsigned int *)soundpad_find_active_portal(
                     (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal & 0xfffeffff;
        }
        off = off + 8;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8021eb30  statlvl_open_portals_story
// Story-gated portal open: opens the 5 portal pairs only when the level stage
// byte DAT_805015bd == '\r' (0x0d).
// ---------------------------------------------------------------------------
void statlvl_open_portals_story(void)
{
    if (DAT_805015bd == '\r') {
        statlvl_open_portals();
    }
}

// ---------------------------------------------------------------------------
// 0x8021eb64  statlvl_zone_ambient_volume
// Per-player soundpad-zone -> ambient-volume LUT for the second ambient block.
//   zone 6, 0x12          -> FLOAT_80618478
//   zone 0xc, 0x13        -> FLOAT_80618474
//   zone 0xd..0x10        -> FLOAT_80618470 (idle)
//   default               -> FLOAT_8061847c (full)
// ---------------------------------------------------------------------------
double statlvl_zone_ambient_volume(int zoneType)
{
    switch (zoneType) {
    case 6:
    case 0x12:
        return (double)FLOAT_80618478;
    case 0xc:
    case 0x13:
        return (double)FLOAT_80618474;
    case 0xd:
    case 0xe:
    case 0xf:
    case 0x10:
        return (double)FLOAT_80618470;
    default:
        return (double)FLOAT_8061847c;
    }
}

// ---------------------------------------------------------------------------
// 0x8021eba8  statlvl_ambient2_loop_tick
// Second ambient-soundzone loop tick (scoped). Averages every active player's
// zone-ambient volume (via statlvl_zone_ambient_volume), slews the shared level
// *DAT_80612870 toward that average (rate FLOAT_80618480 * |target-cur|, min
// FLOAT_80618484 per frame), then drives two 3D loops: bank 0x1b at the level
// and bank 0x19 at its complement.
// ---------------------------------------------------------------------------
void statlvl_ambient2_loop_tick(void)
{
    float target;
    float slew;
    float minSlew;
    float cur;
    unsigned int participants;
    int   i;
    int   recOffset;
    double acc;
    double contrib;

    FUN_803826f0();
    acc = (double)FLOAT_80618470;
    participants = 0;
    recOffset = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        if (-1 < *(int *)(DAT_80611d78 + recOffset + 4)) {
            contrib = statlvl_zone_ambient_volume(
                *(int *)(**(int **)(*(int *)(DAT_80611d78 + recOffset + 0x14) + 0x7c) + 0x34));
            acc = (double)(float)(acc + contrib);
            participants = participants + 1;
        }
        recOffset = recOffset + 0x141c;
    }
    target = (float)(acc / (double)(float)((double)CONCAT44(0x43300000, participants ^ 0x80000000) - DOUBLE_80618490));
    minSlew = (FLOAT_8061847c / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618490)) *
              FLOAT_80618484;
    slew = FLOAT_80618480 * ABS(*(float *)DAT_80612870 - target);
    if (slew < minSlew) {
        slew = minSlew;
    }
    cur = *(float *)DAT_80612870;
    if (slew <= ABS(target - cur)) {
        if (cur <= target) {
            *(float *)DAT_80612870 = cur + slew;
        } else {
            *(float *)DAT_80612870 = cur - slew;
        }
    } else {
        *(float *)DAT_80612870 = target;
    }
    sound_update_3d_source((double)*(float *)DAT_80612870, (double)FLOAT_80618488, (double)FLOAT_8061847c,
                           DAT_80612870 + 8, 0, 0x1b, 0);
    sound_update_3d_source((double)(float)((double)FLOAT_8061847c - (double)*(float *)DAT_80612870),
                           (double)FLOAT_80618488, (double)FLOAT_8061847c, DAT_80612870 + 4, 0, 0x19, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8021ed30  statlvl_ambient2_preload
// Preload for the second ambient block: allocate a 0x10-byte block
// (DAT_80612870) and clear sound banks 0x1b / 0x19.
// ---------------------------------------------------------------------------
void statlvl_ambient2_preload(long long a1, double a2, double a3, long long a4, long long a5,
                              long long a6, long long a7, long long a8)
{
    DAT_80612870 = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                                    0x10, 1, 0, 0, 0, 0, 0, 0);
    sound_clear_bank_ref(0x1b);
    sound_clear_bank_ref(0x19);
}

// ---------------------------------------------------------------------------
// 0x8021ed6c  statlvl_ambient2_init
// Zero the second ambient block and seed its two handle slots (+4, +8) to -1.
// ---------------------------------------------------------------------------
void statlvl_ambient2_init(void)
{
    memset_wrapper(DAT_80612870, 0, 0xc);
    *(int *)(DAT_80612870 + 4) = -1;
    *(int *)(DAT_80612870 + 8) = -1;
}

// ---------------------------------------------------------------------------
// 0x8021edac  statlvl_ambient2_render
// Render-pass wrapper for the second ambient loop tick.
// ---------------------------------------------------------------------------
void statlvl_ambient2_render(void)
{
    statlvl_ambient2_loop_tick();
}

// ---------------------------------------------------------------------------
// 0x8021edcc  statlvl_char_damage_rendertype
// For the class-0xa0 character prop, installs the damage-receive render type so
// it participates in the damage/hit visual pass.
// ---------------------------------------------------------------------------
void statlvl_char_damage_rendertype(int entity)
{
    if (*(short *)(entity + 0x94) == 0xa0) {
        entity_set_render_type(entity, (int)(long long)&damage_character_receive);
    }
}

// ===========================================================================
// Batch 671 : 0x8021ee04 - 0x8021f660
// Control-station damage dispatch to zone actors, class-driven damage render
// type install, third ambient source, a specific zone volume setter, the
// per-player "in hazard zone" state tick, the per-player ambient level query
// and its averaging mix tick (drives two 3D loops), the hazard particle-emitter
// setup, and a frame-scaled scalar step helper.
// APPENDED to worm_stat_level.cpp.
// ---------------------------------------------------------------------------
// --- Additional named cross-module helpers (file-local externs) ------------
extern int   statlvl_class_get_member(int classIdx, int key, int offset); // @0x802216f0 (member payload by key)
extern void  switch_slot_reset(int slotBlock);                 // @0x8008dc3c
extern void  damage_dispatch_to_entity(double a1, double a2, double a3, long long a4,
                                        long long a5, long long a6, long long a7,
                                        long long a8, int *dmgBlock);   // @0x80090180
extern void  damage_entity_receive(void);                      // @0x80090f7c (render-type cb, by addr)
extern void  ambient_source_update(int bank, float *state, int *coords);  // @0x80011008
extern int   soundpad_resolve_entity_to_zone(int fourcc);      // @0x8006cfdc
extern void  scalar_step_toward_snap(double target, double maxStep, float *value); // @0x8012edfc
extern int   anim_is_clip_active(int bot, int clipId);         // @0x8002588c
extern void  player_set_state(int playerRec, int state);       // @0x800ab2a4
extern int   scene_node_get_render_data(int nodeId);           // @0x80059988
extern void  particle_emitter_set_flags(int *handle, int flags, int mode);   // @0x800e3538
extern int   particle_handle_is_valid(int *handle);            // @0x800e3528
extern void  particle_emitter_set_color_keys(int handle, int count, int color); // @0x800e4028

// Data references for this batch
extern int   DAT_80612880;   // hazard/ambient-mix state block ptr (0xc bytes + 2 handles)
extern int   DAT_806128ac;   // actor-class table base (0x4c stride, +0xc = actor list)
extern int   DAT_80611e2c;   // fallback null particle handle
extern int   FLOAT_80612878; // ambient3 source state (bank 0x37)
extern int   DAT_8045e530;   // ambient3 source coord block
extern int   DAT_8045e8f0;   // hazard-zone rect table (stride 0x14: x,minZ,z,rangeZ,r^2)
extern int   DAT_8045e8f4;
extern int   DAT_8045e8f8;
extern int   DAT_8045e8fc;
extern int   DAT_8045e900;
extern int   DAT_8045e964;   // particle-slot index table (5 entries, stride 4)

extern float  FLOAT_80618498; // damage base amount / accumulator seed
extern float  FLOAT_8061849c; // damage init +0x1cc / +0x1d8
extern float  FLOAT_806184a0; // damage init +0x1dc
extern float  FLOAT_806184a8; // zone 0x545 volume .x
extern float  FLOAT_806184ac; // zone 0x545 volume .y
extern float  FLOAT_806184b0; // 0.0f baseline / min clamp
extern float  FLOAT_806184b4; // 1.0f full reference / max clamp
extern float  FLOAT_806184b8; // non-swimming zone extent scale
extern float  FLOAT_806184bc; // zone 7-9 ref y
extern float  FLOAT_806184c0; // zone 7-9 ref x
extern float  FLOAT_806184c4; // zone 7-9 ref z
extern float  FLOAT_806184c8; // zone 7-9 ref y2
extern float  FLOAT_806184cc; // zone 7-9 ref z2
extern float  FLOAT_806184d0; // zone 7-9 distance bias
extern float  FLOAT_806184d4; // zone 7-9 distance divisor
extern float  FLOAT_806184d8; // zone 2/0x1a height bias
extern float  FLOAT_806184dc; // zone 2/0x1a height scale
extern float  FLOAT_806184e0; // ambient-mix slew rate scale
extern float  FLOAT_806184e4; // 3d loop 0x25 coord base
extern float  FLOAT_806184e8; // 3d loop coord y
extern float  FLOAT_806184ec; // 3d loop 0x24 coord base
extern double DOUBLE_806184f0; // int<->double bias mantissa (avg path)
extern float  FLOAT_806108ec; // frame delta scale (declared elsewhere too)

// ---------------------------------------------------------------------------
// 0x8021ee04  statlvl_damage_apply_to_zone_actors
// Applies a damage event to a control-station actor selected by class 0xcf.
// State 1 selects the primary actor (class 0xcf, slot=param_10 in [0,6)); other
// states index the per-team actor list (DAT_806128ac[state] +0x54 +slot*4). The
// hit magnitude is the base amount plus the actor's stored +0x1cc value, and the
// damage block also carries the current global damage tag (DAT_80611d78+0x14).
// ---------------------------------------------------------------------------
void statlvl_damage_apply_to_zone_actors(unsigned long long a1, double a2, double a3,
                                         long long a4, long long a5, long long a6,
                                         long long a7, long long a8,
                                         int *stateSlot, int slot)
{
    int         actor;
    int         dmgBlockB[3];   // local_c8 / local_c4 / local_c0 (+ pad)
    float       dmgAmountB;     // local_a8
    int         dmgBlockA[3];   // local_6c / local_68 / local_64
    float       dmgAmountA;     // local_4c

    int state = *stateSlot;
    if (state == 1) {
        if (slot >= 0 && slot < 6) {
            actor = statlvl_class_get_member(1, 0xcf, slot);
            switch_slot_reset((int)dmgBlockA);
            dmgAmountA   = (float)((double)FLOAT_80618498 + (double)*(float *)(actor + 0x1cc));
            dmgBlockA[1] = *(int *)(DAT_80611d78 + 0x14);
            dmgBlockA[0] = actor;
            dmgBlockA[2] = dmgBlockA[1];
            (void)dmgAmountA;
            damage_dispatch_to_entity((double)FLOAT_80618498, a2, a3, a4, a5, a6, a7, a8,
                                      dmgBlockA);
        }
    } else if (state > 0 && state < 3 && slot >= 0 && slot < 4) {
        actor = *(int *)(*(int *)(DAT_806128ac + state * 0x4c + 0xc) + slot * 4 + 0x54);
        if (actor != 0) {
            switch_slot_reset((int)dmgBlockB);
            dmgAmountB   = (float)((double)FLOAT_80618498 + (double)*(float *)(actor + 0x1cc));
            dmgBlockB[1] = *(int *)(DAT_80611d78 + 0x14);
            dmgBlockB[0] = actor;
            dmgBlockB[2] = dmgBlockB[1];
            (void)dmgAmountB;
            damage_dispatch_to_entity((double)FLOAT_80618498, a2, a3, a4, a5, a6, a7, a8,
                                      dmgBlockB);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8021ef08  statlvl_prop_install_damage_rendertype
// For the control-station destructible classes, installs the damage-receive
// render type and seeds the prop's damage floats (+0x1cc/+0x1d8/+0x1dc). The
// 0xc9..0xcf group additionally forces its object state (+4 record +0x30) to 3.
// ---------------------------------------------------------------------------
void statlvl_prop_install_damage_rendertype(int entity)
{
    float f1;
    float f2;

    switch (*(unsigned short *)(entity + 0x94)) {
    case 200:   // 0xc8
        entity_set_render_type(entity, (int)(long long)&damage_entity_receive);
        f2 = FLOAT_806184a0;
        f1 = FLOAT_8061849c;
        *(float *)(entity + 0x1d8) = FLOAT_8061849c;
        *(float *)(entity + 0x1cc) = f1;
        *(float *)(entity + 0x1dc) = f2;
        break;
    case 0xc9:
    case 0xcb:
    case 0xcc:
    case 0xcd:
    case 0xd0:
        *(int *)(*(int *)(entity + 4) + 0x30) = 3;
        entity_set_render_type(entity, (int)(long long)&damage_entity_receive);
        f2 = FLOAT_806184a0;
        f1 = FLOAT_8061849c;
        *(float *)(entity + 0x1d8) = FLOAT_8061849c;
        *(float *)(entity + 0x1cc) = f1;
        *(float *)(entity + 0x1dc) = f2;
        break;
    case 0xcf:
        *(int *)(*(int *)(entity + 4) + 0x30) = 3;
        entity_set_render_type(entity, (int)(long long)&damage_entity_receive);
        f2 = FLOAT_806184a0;
        f1 = FLOAT_8061849c;
        *(float *)(entity + 0x1d8) = FLOAT_8061849c;
        *(float *)(entity + 0x1cc) = f1;
        *(float *)(entity + 0x1dc) = f2;
        break;
    }
}

// ---------------------------------------------------------------------------
// 0x8021effc  statlvl_ambient3_render
// Third ambient source: pushes state FLOAT_80612878 + coord block into bank 0x37.
// ---------------------------------------------------------------------------
void statlvl_ambient3_render(void)
{
    ambient_source_update(0x37, (float *)&FLOAT_80612878, (int *)&DAT_8045e530);
}

// ---------------------------------------------------------------------------
// 0x8021f030  statlvl_zone_0x545_set_volume
// Resolves the soundpad zone tied to actor 0x545 and writes its volume triple
// (+0xc/+0x10/+0x14) from a fixed constant set.
// ---------------------------------------------------------------------------
void statlvl_zone_0x545_set_volume(void)
{
    float fy = FLOAT_806184ac;
    int zone = soundpad_resolve_entity_to_zone(0x545);
    if (zone != 0) {
        *(float *)(zone + 0xc)  = FLOAT_806184a8;
        *(float *)(zone + 0x10) = fy;
        *(float *)(zone + 0x14) = FLOAT_806184b0;
    }
}

// ---------------------------------------------------------------------------
// 0x8021f074  statlvl_player_in_hazard_zone_tick
// For each active player, tests their world position against up to 4 rectangular
// hazard zones (table at DAT_8045e8f0, stride 0x14: cx, minZ, cz, rangeZ, r^2).
// Swimming players (state 0x800 or 4) use FLOAT_806184b4 as the z extent scale,
// others FLOAT_806184b8. When inside a zone, records the normalized depth into
// the player render block (+0xb20) and, if not already, sets state 0x800 (and
// stamps +0xb1c when transitioning from a non-swim, non-4 state). When outside,
// clears +0xb20 and drops state 0x800 back to 4.
// ---------------------------------------------------------------------------
void statlvl_player_in_hazard_zone_tick(void)
{
    float zExtentScale;
    float dx;
    float dz;
    bool  inside;
    int   zoneIdx;
    int   zoneOff;
    int   remaining;
    int   playerRec;
    int   playerIdx;
    int   playerStride;

    FUN_803826f0();
    playerIdx    = 0;
    playerStride = 0;
    while (true) {
        if (DAT_80612610 <= playerIdx) {
            FUN_8038273c();
            return;
        }
        inside    = false;
        playerRec = DAT_80611d78 + playerStride;
        zExtentScale = FLOAT_806184b4;
        if (*(int *)(playerRec + 0x114) != 0x800 && *(int *)(playerRec + 0x114) != 4) {
            zExtentScale = FLOAT_806184b8;
        }
        zoneIdx   = 0;
        zoneOff   = 0;
        remaining = 4;
        do {
            if (*(float *)((int)&DAT_8045e8f4 + zoneOff) <= *(float *)(playerRec + 0xac) &&
                *(float *)(playerRec + 0xac) <=
                    zExtentScale * *(float *)((int)&DAT_8045e900 + zoneOff) +
                    *(float *)((int)&DAT_8045e8f4 + zoneOff)) {
                dx = *(float *)((int)&DAT_8045e8f0 + zoneOff) - *(float *)(playerRec + 0xa8);
                dz = *(float *)((int)&DAT_8045e8f8 + zoneOff) - *(float *)(playerRec + 0xb0);
                if (dx * dx + dz * dz <= *(float *)((int)&DAT_8045e8fc + zoneOff)) {
                    inside = true;
                    break;
                }
            }
            zoneIdx   = zoneIdx + 1;
            zoneOff   = zoneOff + 0x14;
            remaining = remaining + -1;
        } while (remaining != 0);

        if (inside) {
            *(float *)(*(int *)(*(int *)(playerRec + 0x14) + 0x7c) + 0xb20) =
                (((float)(&DAT_8045e8f4)[zoneIdx * 5] + (float)(&DAT_8045e900)[zoneIdx * 5]) -
                 *(float *)(playerRec + 0xac)) / (float)(&DAT_8045e900)[zoneIdx * 5];
            int st = *(int *)(DAT_80611d78 + playerStride + 0x114);
            if (st != 0x800) {
                if (st != 4) {
                    *(float *)(*(int *)(*(int *)(DAT_80611d78 + playerStride + 0x14) + 0x7c) + 0xb1c) =
                        FLOAT_806184b0;
                }
                player_set_state(DAT_80611d78 + playerStride, 0x800);
            }
        } else {
            *(float *)(*(int *)(*(int *)(playerRec + 0x14) + 0x7c) + 0xb20) = FLOAT_806184b0;
            if (*(int *)(DAT_80611d78 + playerStride + 0x114) == 0x800) {
                player_set_state(DAT_80611d78 + playerStride, 4);
            }
        }
        playerIdx    = playerIdx + 1;
        playerStride = playerStride + 0x141c;
    }
}

// ---------------------------------------------------------------------------
// 0x8021f21c  statlvl_player_zone_ambient_level
// Returns the ambient hazard level [0,1] for one player, keyed on the zone type
// of their current soundpad node. Zone 2/0x1a: linear ramp of height above bias.
// Zones 7-9: inverse of the summed distance to two reference points. Zones
// 0x1f..0x27: full (1.0). Everything else: 0.0. Suppressed to 1.0 while the
// player's "special" clip (0xff) is active.
// ---------------------------------------------------------------------------
double statlvl_player_zone_ambient_level(int *playerRenderBlock)
{
    float  f1;
    float  f2;
    float  f3;
    int    zoneType;
    int   *zoneNode;
    int    playerRec;
    double dHeight;
    double dSum;
    double dxA;
    double dxB;

    playerRec = *playerRenderBlock;
    if (anim_is_clip_active(playerRec, 0xff) != 0) {
        return (double)FLOAT_806184b4;
    }
    zoneNode = soundpad_resolve_zone_node(playerRenderBlock[0x19e]);
    zoneType = soundpad_get_entity_zone_id((int)zoneNode);
    if (zoneType == 0x1a) {
LAB_height_ramp:
        dHeight = (double)((*(float *)(playerRec + 0x68) - FLOAT_806184d8) * FLOAT_806184dc);
        {
            double clamped = dHeight;
            if ((double)FLOAT_806184b4 < dHeight) clamped = (double)FLOAT_806184b4;
            if (clamped < (double)FLOAT_806184b0) return (double)FLOAT_806184b0;
        }
        if (dHeight <= (double)FLOAT_806184b4) return dHeight;
        return (double)FLOAT_806184b4;
    }
    if (zoneType < 0x1a) {
        if (zoneType < 7) {
            if (zoneType == 2) goto LAB_height_ramp;
        } else if (zoneType < 10) {
            dxA  = (double)(FLOAT_806184c0 - *(float *)(playerRec + 0x68));
            f1   = FLOAT_806184c0 - *(float *)(playerRec + 0x68);
            f2   = FLOAT_806184c8 - *(float *)(playerRec + 100);
            dSum = (double)(FLOAT_806184bc - *(float *)(playerRec + 100));
            f3   = FLOAT_806184cc - *(float *)(playerRec + 0x6c);
            dxB  = (double)(FLOAT_806184c4 - *(float *)(playerRec + 0x6c));
            double dA = math_sqrt((double)(f3 * f3 + f2 * f2 + f1 * f1));
            double dB = math_sqrt((double)(float)(dxB * dxB +
                                   (double)(float)(dSum * dSum +
                                   (double)(float)(dxA * dxA))));
            f1 = ((float)(dB + dA) - FLOAT_806184d0) / FLOAT_806184d4;
            f2 = f1;
            if (FLOAT_806184b4 < f1) f2 = FLOAT_806184b4;
            f3 = FLOAT_806184b0;
            if (FLOAT_806184b0 <= f2 && (f3 = f1, FLOAT_806184b4 < f1)) f3 = FLOAT_806184b4;
            return (double)(FLOAT_806184b4 - f3);
        }
    } else if (zoneType < 0x22) {
        if (zoneType == 0x1f) return (double)FLOAT_806184b4;
    } else if (zoneType < 0x28) {
        return (double)FLOAT_806184b4;
    }
    return (double)FLOAT_806184b0;
}

// ---------------------------------------------------------------------------
// 0x8021f410  statlvl_ambient_zone_mix_tick
// Averages statlvl_player_zone_ambient_level over all live players, slews the
// averaged value toward its target in DAT_80612880[0] with a frame-scaled step,
// then drives two 3D ambient loops (banks 0x25 and 0x24) symmetrically around
// that value.
// ---------------------------------------------------------------------------
void statlvl_ambient_zone_mix_tick(void)
{
    unsigned int count;
    int          playerIdx;
    int          playerStride;
    double       level;
    double       accum;

    FUN_803826f0();
    accum        = (double)FLOAT_806184b0;
    count        = 0;
    playerStride = 0;
    for (playerIdx = 0; playerIdx < DAT_80612610; playerIdx = playerIdx + 1) {
        if (-1 < *(int *)(DAT_80611d78 + playerStride + 4)) {
            level = statlvl_player_zone_ambient_level(
                        *(int **)(*(int *)(DAT_80611d78 + playerStride + 0x14) + 0x7c));
            accum = (double)(float)(accum + level);
            count = count + 1;
        }
        playerStride = playerStride + 0x141c;
    }
    scalar_step_toward_snap(
        (double)(float)(accum / (double)(float)((double)CONCAT44(0x43300000, count ^ 0x80000000)
                                               - DOUBLE_806184f0)),
        (double)(FLOAT_806184e0 *
                 (FLOAT_806184b4 /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)
                          - DOUBLE_806184f0)) * FLOAT_806108ec),
        (float *)DAT_80612880);
    sound_update_3d_source((double)(FLOAT_806184e4 - *(float *)DAT_80612880),
                           (double)FLOAT_806184e8, (double)FLOAT_806184b4,
                           DAT_80612880 + 1, 0, 0x25, 0);
    sound_update_3d_source((double)(*(float *)DAT_80612880 - FLOAT_806184ec),
                           (double)FLOAT_806184e8, (double)FLOAT_806184b4,
                           DAT_80612880 + 2, 0, 0x24, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8021f550  statlvl_ambient_zone_mix_render
// Render-pass wrapper for the ambient zone mix tick.
// ---------------------------------------------------------------------------
void statlvl_ambient_zone_mix_render(void)
{
    statlvl_ambient_zone_mix_tick();
}

// ---------------------------------------------------------------------------
// 0x8021f570  statlvl_hazard_particles_init
// Initializes the 5 hazard particle emitters for scene node 0x13: clears the
// mix state block, sets the "no auto-emit" flag (0x10000000) on each emitter and
// installs its color keys, then seeds the two loop-handle slots (+4/+8) to -1.
// Emitter indices come from DAT_8045e964; missing ones fall back to DAT_80611e2c.
// ---------------------------------------------------------------------------
void statlvl_hazard_particles_init(void)
{
    int   renderData;
    int   emitterHandle;
    int  *emitterPtr;
    bool  valid;
    int   i;
    int   idxOff;
    int   flagsSlot;   // local_20 / local_1c
    int   validSlot;   // local_24
    int   handleWork[2];

    __save_gpr_28();
    renderData = scene_node_get_render_data(0x13);
    statlvl_zone_0x545_set_volume();
    memset_wrapper(DAT_80612880, 0, 0xc);
    i      = 0;
    idxOff = 0;
    do {
        if (*(int *)((int)&DAT_8045e964 + idxOff) < *(int *)(renderData + 0x1e8)) {
            emitterPtr = (int *)(*(int *)(renderData + 0x1fc) +
                                 *(int *)((int)&DAT_8045e964 + idxOff) * 4);
        } else {
            emitterPtr = (int *)&DAT_80611e2c;
        }
        emitterHandle = *emitterPtr;
        handleWork[0] = emitterHandle;   // local_20
        flagsSlot     = emitterHandle;   // local_1c
        particle_emitter_set_flags(handleWork, 0x10000000, 0);
        validSlot = emitterHandle;
        valid = particle_handle_is_valid(&validSlot);
        if (valid) {
            particle_emitter_set_color_keys(flagsSlot, 9, -0x7fba16c0);
        }
        i      = i + 1;
        idxOff = idxOff + 4;
    } while (i < 5);
    *(int *)(DAT_80612880 + 4) = -1;
    *(int *)(DAT_80612880 + 8) = -1;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8021f660  statlvl_scalar_step_toward
// Steps *value toward target by at most (rate * frameDelta). Snaps exactly to
// target (returns 1) when within one step; otherwise moves one step and returns 0.
// ---------------------------------------------------------------------------
unsigned int statlvl_scalar_step_toward(double target, double rate, float *value)
{
    double current;
    double maxStep;

    current = (double)*value;
    maxStep = (double)(float)(rate *
                (double)((FLOAT_806184b4 /
                          (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)
                                  - DOUBLE_806184f0)) * FLOAT_806108ec));
    if ((double)(float)(current - target) <= maxStep) {
        if (-maxStep <= (double)(float)(current - target)) {
            *value = (float)target;
            return 1;
        }
        *value = (float)(current + maxStep);
    } else {
        *value = (float)(current - maxStep);
    }
    return 0;
}

// ===========================================================================
// Batch 672 : 0x8021f6e4 - 0x802201c4
// Control-station gauge scale matrix, zone-pair portal toggle, the large "BMB"
// blast-door state machine, the garage-door open trigger, debris launch/despawn,
// a shutter slide tick, a dual counter-rotor matrix, a conveyor scroll advance,
// the subsystem preload, and the "explode players inside this volume" routine.
// APPENDED to worm_stat_level.cpp.
// ---------------------------------------------------------------------------
// --- Additional named cross-module helpers (file-local externs) ------------
extern void  mtx4_set_scale(double x, double y, double z);          // @0x8012f62c (writes active mtx)
extern void  mtx4_rotate_about_point(int mtx, int *point, float *angles); // @0x80130374
extern int  *soundpad_find_any_portal(int *zoneA, int *zoneB);      // @0x8006c818
extern int   entity_spawn_by_type(double x, double a2, double a3, long long a4, long long a5,
                                   long long a6, long long a7, long long a8,
                                   int typeId, int posSrc, void *rot, int a12, int a13); // @0x80072d70
extern void  entity_assign_room(int entity);                        // @0x8007384c
extern int   player_ai_explosion_spawn(int a, int b, float *pos, float *dir,
                                        int src, int owner, int flag); // @0x800b91e4
extern void  character_on_death_init(long long unit, double a2, double a3, double a4,
                                      double a5, double a6, long long a7, long long a8,
                                      int who);                     // @0x8008ea74
extern void  entity_destroy_with_event(void *entity);               // @0x800735e4
extern int   player_is_scoped_or_mounted(int playerSlot);           // @0x800a9ce0

// Data references for this batch
extern int    DAT_80611c24;   // entity handle table base (0x2e8 stride, +0x2c = handle)
extern char   DAT_805015be;   // secondary story/stage byte
extern int    DAT_805015c8;   // conveyor-advance enable gate
extern int    DAT_804a0098;   // conveyor-advance denominator (int, bias-converted)
extern int    DAT_80611d54;   // conveyor target record ptr
extern int    DAT_80612098;   // active AI-character count (explode-in-volume loop)
extern int    DAT_80588610;   // AI-character record pointer array base
extern int    DAT_8045e998;   // zone-pair portal id table B (stride 8)
extern int    DAT_8045e99c;   // zone-pair portal id table A (stride 8)
extern int    DAT_803dffa0;   // BMB spawn rotation triple (a)
extern int    DAT_803dffac;   // dual-rotor pivot point (0x2c/0x30/0x34)
extern int    DAT_803dffc4;   // explosion spawn dir triple

extern float  FLOAT_806184f8; // gauge height->scale divisor
extern float  FLOAT_806184fc; // BMB scalar-step rate
extern float  FLOAT_80618500; // BMB door thickness / offset
extern float  FLOAT_80618504; // BMB height trigger threshold
extern float  FLOAT_80618508; // BMB reset height
extern float  FLOAT_8061850c; // BMB open target height
extern float  FLOAT_80618510; // BMB close target height
extern float  FLOAT_80618514; // BMB linked-panel offset
extern float  FLOAT_80618518; // BMB slot spacing / room offset
extern float  FLOAT_8061851c; // debris x-velocity scale
extern float  FLOAT_80618520; // debris z-velocity scale
extern float  FLOAT_80618524; // debris despawn floor
extern float  FLOAT_80618528; // shutter open rate
extern float  FLOAT_8061852c; // shutter open limit / rest
extern float  FLOAT_80618530; // shutter close-rate factor / rotor mirror
extern float  FLOAT_8061853c; // conveyor scroll rate
extern float  FLOAT_80618540; // explode-volume vertical padding

// ---------------------------------------------------------------------------
// 0x8021f6e4  statlvl_scale_bar_by_height
// Drives a gauge/liquid-bar bone: identity + Y-scale = 1 + |height/divisor| from
// the render block (+0x7c, +4), zeroed X/Z translation, and marks +0x28 dirty.
// ---------------------------------------------------------------------------
void statlvl_scale_bar_by_height(int entity)
{
    double height;
    double one;

    height = (double)*(float *)(*(int *)(entity + 0x7c) + 4);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    one = (double)FLOAT_806184b4;
    mtx4_set_scale(one,
                   (double)(float)(one + ABS((double)(float)(height / (double)FLOAT_806184f8))),
                   one);
    __stat_mtx_set_translation((double)FLOAT_806184b0, height, (double)FLOAT_806184b0);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x8021f780  statlvl_toggle_zone_pair_portals
// Opens or closes the 2 portals joining paired soundpad zones (id tables at
// DAT_8045e99c / DAT_8045e998, stride 8). The __save_gpr_25 prologue gate selects
// close (clear bit 0x10000) when zero, open (set 0x10000) otherwise.
// ---------------------------------------------------------------------------
void statlvl_toggle_zone_pair_portals(void)
{
    int          zoneA;
    int          zoneBase;
    int          zoneB;
    unsigned int *portal;
    int          i;
    int          off;
    int          openGate;

    openGate = __save_gpr_25();
    i   = 0;
    off = 0;
    do {
        zoneA    = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045e99c + off));
        zoneBase = DAT_80611b8c;
        zoneB    = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045e998 + off));
        portal   = (unsigned int *)soundpad_find_any_portal(
                        (int *)(DAT_80611b8c + zoneB * 0x20),
                        (int *)(zoneBase + zoneA * 0x20));
        if (portal != nullptr) {
            if (openGate == 0) {
                *portal = *portal & 0xfffeffff;
            } else {
                *portal = *portal | 0x10000;
            }
        }
        i   = i + 1;
        off = off + 8;
    } while (i < 2);
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x8021f82c  statlvl_bmb_blast_door_tick
// Multi-panel blast-door / crushing-press state machine (fourcc 'BMB1'..'BMB5').
// State 0 scans for a free 'BMB' panel via FUN_8023c32c and latches its handle.
// States 1-9 slew the two panel offsets (+puVar8[2], +puVar8[4]) toward open/
// close targets with statlvl_scalar_step_toward, spawn a linked debris entity
// (type 0x119) once past the height threshold, then push the resulting position
// into the linked panel record (+0x64/+0x68/+0x6c), all child bones (+500 list),
// and the collision AABB (+0x1ac/+0x1c4). Finishes by re-roomting the entity.
// ---------------------------------------------------------------------------
void statlvl_bmb_blast_door_tick(unsigned long long a1, double a2, double a3, long long a4,
                                 long long a5, long long a6, long long a7, long long a8)
{
    float           childBase;
    float           thickness;
    float           despawn;
    int             entity;
    unsigned int    handle;
    int             stepDone;
    int             i;
    int             childOff;
    short          *st;               // puVar8: state block at (+0x7c)
    int             linked;           // iVar7: linked panel record
    float           spawnRot[3];      // local_28 / local_24 / local_20

    entity = __save_gpr_28();
    st     = *(short **)(entity + 0x7c);
    handle = *(unsigned int *)(st + 6);
    if (handle == 0) {
        linked = 0;
    } else {
        linked = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int *)(linked + 0x2c) != handle) {
            linked = 0;
            *(int *)(st + 6) = 0;
        }
    }

    switch (*st) {
    case 0: {
        unsigned int fourcc = 0x424d4231;   // 'BMB1'
        int panel = entity_find_by_fourcc(0x424d4231);
        while ((panel == 0 || *(int *)(panel + 0x1a0) != 0) && fourcc != 0x424d4235) {
            fourcc = fourcc + 1;
            panel  = entity_find_by_fourcc(fourcc);
        }
        if (panel == 0 || *(int *)(panel + 0x1a0) != 0) {
            *st = 6;
            *(int *)(st + 6) = 0;
        } else {
            *st = 1;
            *(int *)(st + 6) = *(int *)(panel + 0x2c);
        }
        *(char *)((int)st + 3) = 0;
        break;
    }
    case 1:
        stepDone = statlvl_scalar_step_toward(
                       (double)(*(float *)(linked + 0x6c) - *(float *)(entity + 0xb0)),
                       (double)FLOAT_806184fc, (float *)(st + 4));
        if (stepDone != 0) *st = 2;
        break;
    case 2:
        stepDone = statlvl_scalar_step_toward(
                       (double)(FLOAT_80618500 + *(float *)(linked + 0x1c4) +
                                (*(float *)(linked + 0x68) - *(float *)(entity + 0xac))),
                       (double)FLOAT_806184fc, (float *)(st + 2));
        if (stepDone != 0) *st = 3;
        break;
    case 3:
        *(char *)(st + 1) = 1;
        *st = 4;
        break;
    case 4:
        stepDone = statlvl_scalar_step_toward((double)FLOAT_806184b0, (double)FLOAT_806184fc,
                                              (float *)(st + 2));
        if (stepDone != 0) *st = 5;
        break;
    case 5:
        stepDone = statlvl_scalar_step_toward((double)FLOAT_806184b0, (double)FLOAT_806184fc,
                                              (float *)(st + 4));
        if (stepDone != 0) *st = 6;
        break;
    case 6:
        if ((double)FLOAT_80618504 < (double)*(float *)(entity + 100) &&
            (*(float *)(entity + 0x68) = FLOAT_80618508, linked == 0)) {
            spawnRot[0] = *(float *)&DAT_803dffa0;
            spawnRot[1] = *(float *)((int)&DAT_803dffa0 + 4);
            spawnRot[2] = *(float *)((int)&DAT_803dffa0 + 8);
            linked = entity_spawn_by_type((double)*(float *)(entity + 100), a2, a3, a4, a5, a6,
                                          a7, a8, 0x119, entity + 100, spawnRot, 0, 0);
            *(int *)(st + 6) = *(int *)(linked + 0x2c);
            *(char *)(st + 1) = 1;
        }
        stepDone = statlvl_scalar_step_toward((double)FLOAT_8061850c, (double)FLOAT_806184fc,
                                              (float *)(entity + 100));
        if (stepDone != 0) *st = 7;
        break;
    case 7:
        stepDone = statlvl_scalar_step_toward((double)FLOAT_80618510, (double)FLOAT_806184fc,
                                              (float *)(st + 2));
        if (stepDone != 0) {
            *(char *)(st + 1) = 0;
            if (linked != 0 && *(int *)(linked + 0x1a0) == 0) {
                *(int *)(linked + 0x1a0) = 1;
                statlvl_toggle_zone_pair_portals();
            }
            if (*(char *)((int)st + 3) != '\0') *st = 8;
        }
        break;
    case 8:
        stepDone = statlvl_scalar_step_toward((double)FLOAT_806184b0, (double)FLOAT_806184fc,
                                              (float *)(st + 2));
        if (stepDone != 0) *st = 9;
        break;
    case 9:
        if (*(float *)(entity + 100) < FLOAT_80618504) {
            *(int *)(entity + 0x68) = *(int *)(entity + 0xac);
        }
        stepDone = statlvl_scalar_step_toward((double)*(float *)(entity + 0xa8),
                                              (double)FLOAT_806184fc, (float *)(entity + 100));
        if (stepDone != 0) *st = 0;
        break;
    }

    *(float *)(entity + 0x6c) = *(float *)(entity + 0xb0) + *(float *)(st + 4);
    thickness = FLOAT_80618500;
    if (linked != 0 && *(char *)(st + 1) != '\0') {
        *(float *)(linked + 100) = *(float *)(entity + 100) - FLOAT_80618514;
        *(float *)(linked + 0x68) =
            ((*(float *)(entity + 0x68) + *(float *)(st + 2)) - *(float *)(linked + 0x1c4)) - thickness;
        *(int *)(linked + 0x6c) = *(int *)(entity + 0x6c);
    }
    statlvl_zone_0x545_set_volume();
    thickness = FLOAT_80618500;
    childOff  = 0;
    for (i = 0; i < *(int *)(entity + 0x1e4); i = i + 1) {
        childBase = *(int *)(*(int *)(entity + 500) + childOff);
        childOff  = childOff + 4;
        *(float *)((int)childBase + 0xc) =
            (*(float *)((int)childBase + 0x18) + *(float *)(st + 2)) - thickness;
    }
    *(float *)(entity + 0x1ac) =
        (*(float *)(st + 2) * FLOAT_80618518 - FLOAT_80618500) - FLOAT_80618518;
    *(float *)(entity + 0x1c4) =
        ABS((*(float *)(st + 2) * FLOAT_80618518 - FLOAT_80618500) - FLOAT_80618518);
    entity_assign_room(entity);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8021fc20  statlvl_garage_trigger_open
// When not in stage 5/6 and the caller's object flag (+0x1a0) is 1, activates the
// 'GRAB' switch panel (+0x7c +3 = 1) and 'BBDR' garage door (+0x1a0 = 1), advances
// own +0x1a0 to 2, and opens the linked zone-pair portals.
// ---------------------------------------------------------------------------
void statlvl_garage_trigger_open(int *self)
{
    int obj  = *self;
    if (DAT_805015be != '\x05' && DAT_805015be != '\x06' && *(int *)(obj + 0x1a0) == 1) {
        int grab = entity_find_by_fourcc(0x47524142);   // 'GRAB'
        int bbdr = entity_find_by_fourcc(0x42424452);   // 'BBDR'
        if (bbdr != 0) *(int *)(bbdr + 0x1a0) = 1;
        if (grab != 0) *(char *)(*(int *)(grab + 0x7c) + 3) = 1;
        *(int *)(obj + 0x1a0) = 2;
        statlvl_toggle_zone_pair_portals();
    }
}

// ---------------------------------------------------------------------------
// 0x8021fccc  statlvl_debris_launch_tick
// Ballistic debris spawned by the blast door: when in launch state (+0x68 == 2)
// integrates x/z velocity (words +0x2e/+0x3a) and vertical fall (+0x1a) by frame
// delta, and destroys the debris once it falls below FLOAT_80618524. Always sets
// the "processed" flag bit 2 in +6.
// ---------------------------------------------------------------------------
void statlvl_debris_launch_tick(unsigned int *debris)
{
    float  one;
    double bias;
    float  velX;
    float  velZ;
    float  floorY;

    if (debris[0x68] == 2) {
        debris[0xd] = 0xd5;
        bias = DOUBLE_806184f0;
        debris[0xe] = 0xd5;
        one = FLOAT_806184b4;
        *(char *)(debris + 0xc) = 1;
        velZ = FLOAT_80618520;
        velX = FLOAT_8061851c;
        debris[10] = debris[10] | 0x40020000;
        floorY = FLOAT_80618524;
        *(char *)((int)debris + 0x32) = 2;
        float frameDelta = one / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - bias);
        debris[0x2e] = (unsigned int)(int)-(velX * frameDelta * FLOAT_806108ec - (float)(int)debris[0x2e]);
        debris[0x1a] = (unsigned int)(int)(FLOAT_806108ec * (float)(int)debris[0x2e] * frameDelta + (float)(int)debris[0x1a]);
        debris[0x3a] = (unsigned int)(int)(velZ * frameDelta * FLOAT_806108ec + (float)(int)debris[0x3a]);
        debris[0x32] = (unsigned int)(int)((float)(int)debris[0x3a] * frameDelta * FLOAT_806108ec + (float)(int)debris[0x32]);
        if ((float)(int)debris[0x1a] < floorY) {
            entity_destroy_with_event(debris);
        }
    }
    debris[6] = debris[6] | 2;
}

// ---------------------------------------------------------------------------
// 0x8021fe24  statlvl_shutter_slide_tick
// Slides a multi-panel shutter open (state 1: rise to FLOAT_8061852c, snap to
// state 2) or closed (state 2: fall to 0, snap to state 0), integrating +0x1a4 by
// a frame-scaled rate. State -? handles the reverse ramp. Also OR-marks each child
// panel (+0x1f0 list) flag 0x2000 and sets the entity dirty bit 2 in +0x18.
// ---------------------------------------------------------------------------
void statlvl_shutter_slide_tick(int entity)
{
    float openLimit;
    float zero;
    int   state;
    int   childOff;
    int   panel;
    int   i;

    childOff = 0;
    for (i = 0; i < *(int *)(entity + 0x1e0); i = i + 1) {
        openLimit = FLOAT_8061852c;   // hoisted (matches decompiler)
        zero      = FLOAT_806184b0;
        panel     = *(int *)(*(int *)(entity + 0x1f0) + childOff);
        childOff  = childOff + 4;
        *(unsigned short *)(panel + 8) = *(unsigned short *)(panel + 8) | 0x2000;
    }
    float frameScale = FLOAT_806184b4 /
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_806184f0);
    zero      = FLOAT_806184b0;
    openLimit = FLOAT_8061852c;
    state = *(int *)(entity + 0x1a0);
    if (state == 1) {
        *(float *)(entity + 0x1a4) =
            FLOAT_80618528 * FLOAT_806108ec * frameScale + *(float *)(entity + 0x1a4);
        if (openLimit <= *(float *)(entity + 0x1a4)) {
            *(float *)(entity + 0x1a4) = FLOAT_80618530;
            *(int *)(entity + 0x1a0) = 2;
        }
    } else if (state < 1) {
        if (-1 < state) {
            *(float *)(entity + 0x1a4) =
                -(FLOAT_806184d0 * FLOAT_806108ec * frameScale - *(float *)(entity + 0x1a4));
            if (*(float *)(entity + 0x1a4) < zero) *(float *)(entity + 0x1a4) = zero;
        }
    } else if (state < 3) {
        *(float *)(entity + 0x1a4) = -(frameScale * FLOAT_806108ec - *(float *)(entity + 0x1a4));
        if (*(float *)(entity + 0x1a4) < zero) {
            *(float *)(entity + 0x1a4) = FLOAT_8061852c;
            *(int *)(entity + 0x1a0) = 0;
        }
    }
    *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 2;
}

// ---------------------------------------------------------------------------
// 0x8021ffb0  statlvl_dual_rotor_rotate
// Two counter-rotating rotor bones about a shared pivot: identity, rotate the
// first bone (+0x40) by base angle + spin (+0x1c8), then the second (+0x80) by the
// mirrored angle (-(k*spin - angle)). Marks +0x28 dirty.
// ---------------------------------------------------------------------------
void statlvl_dual_rotor_rotate(int entity)
{
    float pivot[3];    // local_2c / local_28 / local_24
    float angles[3];   // local_38 / local_34 / local_30

    pivot[0]  = *(float *)&DAT_803dffac;
    pivot[1]  = *(float *)((int)&DAT_803dffac + 4);
    pivot[2]  = *(float *)((int)&DAT_803dffac + 8);
    angles[0] = *(float *)((int)&DAT_803dffac + 0xc);
    angles[1] = *(float *)((int)&DAT_803dffac + 0x10);
    angles[2] = *(float *)((int)&DAT_803dffac + 0x14);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    angles[2] = angles[2] + *(float *)(entity + 0x1c8);
    mtx4_rotate_about_point(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40, (int *)pivot, angles);
    angles[2] = -(FLOAT_80618530 * *(float *)(entity + 0x1c8) - angles[2]);
    mtx4_rotate_about_point(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80, (int *)pivot, angles);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x802200d8  statlvl_conveyor_scroll_advance
// If the conveyor is enabled (DAT_805015c8) and both its record and count global
// are valid, advances the target's scroll position (+0x68) by a frame-fraction of
// the elapsed-vs-total ratio (DAT_806108e4 / DAT_804a0098) scaled by FLOAT_8061853c.
// ---------------------------------------------------------------------------
void statlvl_conveyor_scroll_advance(void)
{
    int target;
    if (0 < (int)DAT_804a0098 && DAT_80611d54 != 0 &&
        (target = *(int *)(DAT_80611d54 + 0xe04)) != 0 && DAT_805015c8 != 0) {
        *(float *)(target + 0x68) =
            *(float *)(target + 0x68) +
            (FLOAT_8061853c *
             (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_806184f0)) /
            (float)((double)CONCAT44(0x43300000, DAT_804a0098 ^ 0x80000000) - DOUBLE_806184f0);
    }
}

// ---------------------------------------------------------------------------
// 0x80220170  statlvl_preload
// Preload pass for this subsystem: clears sound banks 0x51 and 0x631, preloads the
// three entity resource groups (0x14/0x15/0x16), then allocates the 0x10-byte mix
// state block into DAT_80612880.
// ---------------------------------------------------------------------------
void statlvl_preload(long long a1, double a2, double a3, long long a4, long long a5, long long a6,
                     long long a7, long long a8, int a9, int a10, int a11, int a12, int a13,
                     int a14, int a15, int a16)
{
    long long r = a1;
    sound_clear_bank_ref(0x51);
    sound_clear_bank_ref(0x631);
    r = __stat_preload_res(r, a2, a3, a4, a5, a6, a7, a8, 0x14);
    r = __stat_preload_res(r, a2, a3, a4, a5, a6, a7, a8, 0x15);
    r = __stat_preload_res(r, a2, a3, a4, a5, a6, a7, a8, 0x16);
    DAT_80612880 = __stat_mem_alloc(r, a2, a3, a4, a5, a6, a7, a8, 0x10, 1,
                                    a11, a12, a13, a14, a15, a16);
}

// ---------------------------------------------------------------------------
// 0x802201c4  statlvl_explode_players_in_volume
// For every live AI character not scoped/mounted and without the immunity flag
// (record[9] & 8), tests the character's swept AABB against this object's AABB
// (built from +0x68/+0x6c/+100 center and +0x1c0/+0x1c4/+0x1c8 half-extents). Any
// character overlapping the volume gets an explosion spawned on it (type 0x2c/
// 0x17), its render bit cleared, and character_on_death_init invoked.
// ---------------------------------------------------------------------------
void statlvl_explode_players_in_volume(void)
{
    bool         outside;
    int          hit;
    int          rec;
    int          recOff;
    int         *charPP;
    long long    explosion;
    double       minX, maxX, minY, maxY, minZ, maxZ;
    double       halfX, halfY;
    double       cx0, cx1, cz0;
    float        centerPos[3];   // local_7c / local_78 / local_74
    float        spawnDir[3];    // local_88 / local_84 / local_80
    int          entity;

    entity = __save_gpr_28();
    minX = (double)(*(float *)(entity + 0x68) - *(float *)(entity + 0x1c4));
    maxX = (double)(*(float *)(entity + 0x68) + *(float *)(entity + 0x1c4));
    recOff = 0;
    minZ = (double)(*(float *)(entity + 0x6c) - *(float *)(entity + 0x1c8));
    maxZ = (double)(*(float *)(entity + 0x6c) + *(float *)(entity + 0x1c8));
    minY = (double)((*(float *)(entity + 100) - *(float *)(entity + 0x1c0)) - FLOAT_80618540);
    maxY = (double)(*(float *)(entity + 100) + *(float *)(entity + 0x1c0) + FLOAT_80618540);

    for (int i = 0; i < DAT_80612098; i = i + 1) {
        charPP = *(int **)((int)&DAT_80588610 + recOff);
        if (((*(int *)(*charPP + 0x150) == 0) ||
             (player_is_scoped_or_mounted(*(int *)(*charPP + 0x150)) == 0)) &&
            ((charPP[9] & 8U) == 0)) {
            rec = *charPP;
            outside = false;
            centerPos[0] = *(float *)(rec + 100) + *(float *)(rec + 0x1a8);
            centerPos[1] = *(float *)(rec + 0x68) + *(float *)(rec + 0x1ac);
            centerPos[2] = *(float *)(rec + 0x6c) + *(float *)(rec + 0x1b0);
            halfX = (double)*(float *)(rec + 0x1c4);
            halfY = (double)*(float *)(rec + 0x1c8);
            cx0 = (double)(centerPos[0] - *(float *)(rec + 0x1c0));
            cx1 = (double)(float)((double)centerPos[1] - halfX);
            cz0 = (double)(float)((double)centerPos[1] + halfX);
            if (((double)(centerPos[0] + *(float *)(rec + 0x1c0)) < minY || maxY < cx0) ||
                (cz0 < minX ||
                 (maxX < cx1 || (double)(float)((double)centerPos[2] + halfY) < minZ)) ||
                maxZ < (double)(float)((double)centerPos[2] - halfY)) {
                outside = true;
            }
            if (!outside) {
                spawnDir[0] = *(float *)&DAT_803dffc4;
                spawnDir[1] = *(float *)((int)&DAT_803dffc4 + 4);
                spawnDir[2] = *(float *)((int)&DAT_803dffc4 + 8);
                explosion = player_ai_explosion_spawn(0x2c, 0x17, centerPos, spawnDir,
                                                      *charPP, *charPP, 1);
                *(unsigned int *)(*charPP + 0x28) = *(unsigned int *)(*charPP + 0x28) & 0xfffffffe;
                character_on_death_init(explosion, cz0, cx0, cx1, halfX, halfY, 0, 0, *charPP);
            }
        }
        recOff = recOff + 4;
    }
    __restore_gpr_28();
}

// ===========================================================================
// Batch 673 : 0x802203d4 - 0x802209cc
// Destructible-object init dispatcher, two timed fracture/destroy render ticks,
// the physics-island break tick + its init dispatcher, a footstep preload, the
// level-object spawn iterator, and a small fixed 10-slot list registry
// (init / insert-unique / scored-add). APPENDED to worm_stat_level.cpp.
// ---------------------------------------------------------------------------
// --- Additional named cross-module helpers (file-local externs) ------------
extern int   entity_model_cache_alloc(long long a1, long long a2, long long a3, long long a4,
                                       long long a5, long long a6, long long a7, long long a8,
                                       int entity, int a10, int size, int a12, int a13,
                                       int a14, int a15, int a16);  // @0x8007c190
extern void  subobj_fracture_spawn_default(double a1, double a2, double a3, double a4, double a5,
                                           double a6, double a7, long long a8, int entity,
                                           int typeId, int count, int a12,
                                           float *a13, float *a14);  // @0x8014b028
extern int   subobj_island_create_axis(int body, int a2, float *submesh, float *axis);  // @0x8014eaf8
extern int   subobj_island_create_point(int body, int a2, float *point);                // @0x8014e918
extern void  subobj_island_set_twist_limits(double lo, double hi, int island);          // @0x8014f0a8
extern void  subobj_island_set_restitution(double v, int island);                       // @0x8014f050
extern void  subobj_island_set_drift_gain(double v, int island);                        // @0x8014f080
extern void  subobj_island_set_joint_callback(int island, int cb);                      // @0x8014f094
extern void  subobj_break_tick_impl(long long a1, long long a2, double a3, double a4, double a5,
                                     double a6, double a7, long long a8);                // @0x8014a0f0
extern int   scene_node_get_submesh(int nodeId, int idx);                               // @0x80059998
extern void  player_ai_clear_footstep_sounds(void);                                     // @0x800b000c
extern int   level_get_info_cached(int levelId);                                        // @0x80035bb4
extern int   spawn_flags_match_mode(int flags);                                         // @0x8003710c
extern long long entity_preload_resources(long long a1, long long a2, long long a3, long long a4,
                                          long long a5, long long a6, long long a7, long long a8,
                                          int group);                                    // @0x80070ba8

// Raw ROM helpers still un-named elsewhere (upcoming batches)
extern long long FUN_80253ae4(void);        // active-break record accessor (returns record ptr)
extern int       FUN_8022a8a0(void);         // objective/round-state accessor (returns state rec)
extern void      FUN_802215a8(int a, int b, int c, int d, int e); // level object spawn (5 words)
extern void      statlvl_class_set_complete_cb(int cb); // @0x802217d4 (default class +0x3c callback)
extern void      FUN_8022126c(int a);        // default-spawn fallback
extern void      FUN_8022161c(int a);        // spawn sub-item by id

// Data references for this batch
extern int    DAT_80612614;   // current level/map id
extern int    DAT_805015c0;   // game-mode flags
extern int    DAT_806128a0;   // default spawn-group record ptr
extern int    DAT_803dffd0;   // island axis vector block (0x2c/0x30/0x34)

extern float  FLOAT_8061854c; // fracture-type 0xde init +0x1cc/+0x1d8
extern float  FLOAT_80618550; // fracture-type 0xde init +0x1dc
extern float  FLOAT_80618548; // fracture timer expiry threshold
extern float  FLOAT_80618554; // island twist limit lo
extern float  FLOAT_80618558; // island twist limit hi
extern float  FLOAT_8061855c; // island restitution
extern float  FLOAT_80618560; // island drift gain

// ---------------------------------------------------------------------------
// 0x802203d4  statlvl_destructible_init_dispatch
// Init dispatcher for the destructible/breakable object classes (0x117-0x11b).
// 0x11a: allocates a 0x10-byte model-cache block (+0x7c), seeds its position from
// the entity (+0x68/+0x6c), and registers two update callbacks. 0x119: the garage
// switch panel (flag 0x10, clears +0x1a0/+0xb8, installs statlvl_garage_trigger_open
// render type). 0x117/0x118: single update callback. 0x11b: two callbacks + +0x1a4
// reset.
// ---------------------------------------------------------------------------
void statlvl_destructible_init_dispatch(long long a1, long long a2, long long a3, long long a4,
                                        long long a5, long long a6, long long a7, long long a8,
                                        int entity, int a10, int a11, int a12, int a13,
                                        int a14, int a15, int a16)
{
    float          zero;
    unsigned short cls;
    int            cache;
    short         *st;

    zero = FLOAT_806184b0;
    cls  = *(unsigned short *)(entity + 0x94);
    if (cls == 0x11a) {
        cache = entity_model_cache_alloc(a1, a2, a3, a4, a5, a6, a7, a8, entity, a10,
                                         0x10, a12, a13, a14, a15, a16);
        *(int *)(entity + 0x7c) = cache;
        memset_wrapper(*(int *)(entity + 0x7c), 0, 0x10);
        st = *(short **)(entity + 0x7c);
        *(int *)(st + 4) = *(int *)(entity + 0x6c);
        *(int *)(st + 2) = *(int *)(entity + 0x68);
        *st = 0;
        entity_register_for_update(entity, 3, (int)(long long)&statlvl_bmb_blast_door_tick);
        entity_register_for_update(entity, 2, (int)(long long)&statlvl_scale_bar_by_height);
    } else if (cls < 0x11a) {
        if (cls < 0x119) {
            if (0x116 < cls) {
                entity_register_for_update(entity, 3, (int)(long long)&statlvl_shutter_slide_tick);
            }
        } else {
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x10;
            *(int *)(entity + 0x1a0) = 0;
            *(float *)(entity + 0xb8) = zero;
            entity_register_for_update(entity, 3, (int)(long long)&statlvl_dual_rotor_rotate);
            entity_set_render_type(entity, (int)(long long)&statlvl_garage_trigger_open);
        }
    } else if (cls < 0x11c) {
        *(int *)(entity + 0x1a0) = 0;
        *(float *)(entity + 0x1a4) = FLOAT_806184b0;
        entity_register_for_update(entity, 3, (int)(long long)&statlvl_conveyor_scroll_advance);
        entity_register_for_update(entity, 2, (int)(long long)&statlvl_explode_players_in_volume);
    }
}

// ---------------------------------------------------------------------------
// 0x80220518  statlvl_fracture_tick_5c
// Fracture countdown: decrements the object's timer (+0x1cc) by its per-tick rate
// (param[8]); once at/below FLOAT_80618548 spawns the default fracture set (type
// 0x5c, 6 pieces) with optional color overrides (param[10]/[0xb]), clears render
// type, and destroys the object.
// ---------------------------------------------------------------------------
void statlvl_fracture_tick_5c(long long a1, long long a2, double a3, double a4, double a5,
                              double a6, double a7, long long a8, int *self)
{
    float  expiry = FLOAT_80618548;
    double timer  = (double)*(float *)(*self + 0x1cc);
    *(float *)(*self + 0x1cc) = (float)(timer - (double)(float)self[8]);
    if (*(float *)(*self + 0x1cc) <= expiry) {
        float *colA = (self[10] != 0) ? (float *)self[10] : nullptr;
        float *colB = (self[0xb] != 0) ? (float *)self[0xb] : nullptr;
        subobj_fracture_spawn_default((double)(float)self[0xd], timer, a3, a4, a5, a6, a7, a8,
                                      *self, 0x5c, 6, 0, colA, colB);
        entity_set_render_type(*self, 0);
        entity_destroy_with_event((void *)*self);
    }
}

// ---------------------------------------------------------------------------
// 0x802205bc  statlvl_fracture_tick_0c
// Identical to statlvl_fracture_tick_5c but spawns fracture type 0xc.
// ---------------------------------------------------------------------------
void statlvl_fracture_tick_0c(long long a1, long long a2, double a3, double a4, double a5,
                              double a6, double a7, long long a8, int *self)
{
    float  expiry = FLOAT_80618548;
    double timer  = (double)*(float *)(*self + 0x1cc);
    *(float *)(*self + 0x1cc) = (float)(timer - (double)(float)self[8]);
    if (*(float *)(*self + 0x1cc) <= expiry) {
        float *colA = (self[10] != 0) ? (float *)self[10] : nullptr;
        float *colB = (self[0xb] != 0) ? (float *)self[0xb] : nullptr;
        subobj_fracture_spawn_default((double)(float)self[0xd], timer, a3, a4, a5, a6, a7, a8,
                                      *self, 0xc, 6, 0, colA, colB);
        entity_set_render_type(*self, 0);
        entity_destroy_with_event((void *)*self);
    }
}

// ---------------------------------------------------------------------------
// 0x80220660  statlvl_break_island_tick
// Per-frame tick for a breakable physics-island object: fetches the active break
// record (FUN_80253ae4) and forwards to the shared island break solver.
// ---------------------------------------------------------------------------
void statlvl_break_island_tick(long long a1, long long a2, double a3, double a4, double a5,
                               double a6, double a7, long long a8)
{
    long long rec = FUN_80253ae4();
    subobj_break_tick_impl(rec, a2, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// 0x80220698  statlvl_break_object_init_dispatch
// Init dispatcher for breakable objects (scoped entity via FUN_803826f0). Class
// 0xd4 builds an axis-hinge physics island (axis submesh 0xe or 0x14 by map id
// DAT_80612614), sets twist/restitution/drift, wires the joint callback, and
// installs statlvl_break_island_tick. Classes 0xd7/0xd8/0xdd build a point island.
// 0xde installs statlvl_fracture_tick_0c (+ init floats). 0xdf/0xe0 install
// statlvl_fracture_tick_5c.
// ---------------------------------------------------------------------------
void statlvl_break_object_init_dispatch(void)
{
    float        f1;
    float        f2;
    float       *submesh;
    int          island;
    int          entity;
    long long    scoped;
    float        axis[3];   // local_28 / local_24 / local_20

    scoped = (long long)FUN_803826f0();
    entity = (int)((unsigned long long)scoped >> 0x20);
    switch (*(unsigned short *)(entity + 0x94)) {
    case 0xd4:
        if (*(int *)(entity + 0x80) != 0) {
            axis[0] = *(float *)&DAT_803dffd0;
            axis[1] = *(float *)((int)&DAT_803dffd0 + 4);
            axis[2] = *(float *)((int)&DAT_803dffd0 + 8);
            if (DAT_80612614 == 0x46) {
                submesh = (float *)scene_node_get_submesh(0xe, 0);
            } else {
                submesh = (float *)scene_node_get_submesh(0x14, 0);
            }
            island = subobj_island_create_axis(*(int *)(entity + 0x80), 0, submesh, axis);
            subobj_island_set_twist_limits((double)FLOAT_80618554, (double)FLOAT_80618558, island);
            subobj_island_set_restitution((double)FLOAT_8061855c, island);
            subobj_island_set_drift_gain((double)FLOAT_80618560, island);
            subobj_island_set_joint_callback(island, *(int *)scoped);
            entity_set_render_type(entity, (int)(long long)&statlvl_break_island_tick);
        }
        break;
    case 0xd7:
    case 0xd8:
    case 0xdd:
        if (*(int *)(entity + 0x80) != 0) {
            island = subobj_island_create_point(*(int *)(entity + 0x80), 0, (float *)(entity + 100));
            subobj_island_set_restitution((double)FLOAT_8061855c, island);
            subobj_island_set_joint_callback(island, *(int *)scoped);
        }
        break;
    case 0xde:
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x10;
        entity_set_render_type(entity, (int)(long long)&statlvl_fracture_tick_0c);
        f2 = FLOAT_80618550;
        f1 = FLOAT_8061854c;
        *(float *)(entity + 0x1d8) = FLOAT_8061854c;
        *(float *)(entity + 0x1cc) = f1;
        *(float *)(entity + 0x1dc) = f2;
        break;
    case 0xdf:
    case 0xe0:
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x10;
        entity_set_render_type(entity, (int)(long long)&statlvl_fracture_tick_5c);
        break;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80220828  statlvl_preload_footsteps
// Preload pass: loads entity resource group 0x13 and resets the AI footstep sound
// cache.
// ---------------------------------------------------------------------------
void statlvl_preload_footsteps(long long a1, long long a2, long long a3, long long a4,
                               long long a5, long long a6, long long a7, long long a8)
{
    entity_preload_resources(a1, a2, a3, a4, a5, a6, a7, a8, 0x13);
    player_ai_clear_footstep_sounds();
}

// ---------------------------------------------------------------------------
// 0x80220850  statlvl_spawn_level_objects
// Walks the cached level info spawn table (info+0xd4, count +0xd8, stride 0x38).
// For each entry whose spawn flags match the active game mode, spawns the base
// object (FUN_802215a8 with 5 words), optionally links a follow-up (word[7]), uses
// the default spawn group when word[0xd]==0, then spawns up to 5 sub-items from the
// id array at +0x20 (terminated by -1). Gated by DAT_805015c0 bit 0x40 / round state.
// ---------------------------------------------------------------------------
void statlvl_spawn_level_objects(void)
{
    int  info;
    int  roundRec;
    int  i;
    int  off;
    int  matched;
    int  subCount;
    int  subOff;
    int  subId;
    int *entry;

    FUN_803826e4();
    info = level_get_info_cached(DAT_80612614);
    if (info != 0 &&
        ((DAT_805015c0 & 0x40) == 0 ||
         (roundRec = FUN_8022a8a0(), (*(unsigned int *)(roundRec + 0x18) & 2) != 0))) {
        i   = 0;
        off = 0;
        while (i < *(int *)(info + 0xd8)) {
            entry = (int *)(*(int *)(info + 0xd4) + off);
            if (entry != nullptr && (matched = spawn_flags_match_mode(entry[2])) != 0) {
                FUN_802215a8(entry[0], entry[3], entry[4], entry[5], entry[6]);
                if (entry[7] != 0) {
                    statlvl_class_set_complete_cb(entry[7]);
                }
                if (entry[0xd] == 0) {
                    FUN_8022126c(DAT_806128a0);
                }
                subCount = 0;
                subOff   = 0;
                do {
                    subId = *(int *)((int)entry + subOff + 0x20);
                    if (subId == -1) break;
                    FUN_8022161c(subId);
                    subCount = subCount + 1;
                    subOff   = subOff + 4;
                } while (subCount < 5);
            }
            i   = i + 1;
            off = off + 0x38;
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80220954  slotlist_init
// Initializes a fixed 10-slot registry: sets the sentinel head (*list) and all 10
// slot ids (+4..+0x2c) to the sentinel value, and zeroes the 10 per-slot count
// fields (+0x2c..) and 10 payload fields (+0x54..).
// ---------------------------------------------------------------------------
void slotlist_init(int *list, int sentinel)
{
    int n;
    int off;

    *list = sentinel;
    n   = 10;
    off = 0;
    do {
        *(int *)((int)list + off + 4)    = sentinel;
        *(int *)((int)list + off + 0x2c) = 0;
        *(int *)((int)list + off + 0x54) = 0;
        n   = n + -1;
        off = off + 4;
    } while (n != 0);
}

// ---------------------------------------------------------------------------
// 0x80220988  slotlist_insert_unique
// Inserts value into the first free slot (id == sentinel) of the 10-slot list;
// returns early without inserting if value already present. No-op if the list is
// full.
// ---------------------------------------------------------------------------
void slotlist_insert_unique(int *list, int value)
{
    int idx;
    int off;
    int id;

    idx = 0;
    off = 0;
    while (true) {
        id = *(int *)((int)list + off + 4);
        if (id == *list) {                          // free slot (== sentinel)
            *(int *)((int)list + off + 4) = value;
            return;
        }
        if (id == value) break;                     // already present
        idx = idx + 1;
        off = off + 4;
        if (9 < idx) return;                        // full
    }
}

// ---------------------------------------------------------------------------
// 0x802209cc  slotlist_add_scored
// Finds the slot matching key (param_4), summing the per-slot counts up to it. If
// the owner (+0x1e) is idle, shifts the trailing payload window down, stores the
// new payload (param_3) at the accumulated index, bumps that slot's count, and (if
// the owner's +0x48 flag != -1) increments the owner's global tally (+0x30).
// ---------------------------------------------------------------------------
void slotlist_add_scored(int owner, int *list, int payload, int key)
{
    int accum;
    int off;
    int id;
    int shift;
    int shiftOff;

    accum = 0;
    if (list == nullptr) return;
    off = 0;
    while (true) {
        id = *(int *)((int)list + off + 4);
        if (id == *list) return;                    // reached sentinel, key not found
        accum = accum + *(int *)((int)list + off + 0x2c);
        if (key == id) break;
        off = off + 4;
    }
    if (list[0x1e] != 0) return;
    shift    = 9 - accum;
    shiftOff = 0x20;
    if (accum < 9) {
        do {
            *(int *)((int)list + shiftOff + 0x58) = *(int *)((int)list + shiftOff + 0x54);
            shift    = shift + -1;
            shiftOff = shiftOff + -4;
        } while (shift != 0);
    }
    list[accum + 0x15] = payload;
    *(int *)((int)list + off + 0x2c) = *(int *)((int)list + off + 0x2c) + 1;
    if (*(char *)(owner + 0x48) == -1) return;
    *(int *)(owner + 0x30) = *(int *)(owner + 0x30) + 1;
}

// ===========================================================================
// Batch 674 : 0x80220a7c - 0x80220ec8
// The remaining 10-slot registry ops (payload query + value removal), the
// award/HUD name & result string lookups, an object id-pair reader, and a
// spatial region-test family (sphere/box containment, per-player, any-player).
// APPENDED to worm_stat_level.cpp.
// ---------------------------------------------------------------------------
// --- Additional named cross-module helpers (file-local externs) ------------
extern int   get_string_by_id(int stringId);                    // @0x800061bc
extern short *award_get_entry(int mapId);                       // @0x8002f174

// Raw ROM helpers still un-named elsewhere (upcoming batches)
extern int   FUN_80349608(void);        // arcade/custom map name string accessor
extern int   FUN_80349580(long long a1, long long a2, long long a3, long long a4, long long a5,
                          long long a6, long long a7, long long a8, int a9); // arcade name resolve
extern int   statlvl_get_team1_remaining(void);  // @0x80221864 (DAT_805c6da4 tally, team 1)
extern int   statlvl_get_team0_remaining(void);  // @0x80221854 (DAT_805c6da0 tally, team 0)

// Data references for this batch
extern int    DAT_80611d74;   // primary player record ptr (string-variant gate)
extern int    DAT_80611d90;   // per-player entity record base (0xea0 stride)
extern char   DAT_80610ea8;   // empty-string sentinel byte
extern float  FLOAT_80618568; // min-health threshold for region test

// ---------------------------------------------------------------------------
// 0x80220a7c  slotlist_get_payload
// Returns the payload stored for the slot whose id matches key, at the payload
// index (accumulated slot counts + offset). Zero if the key is not present.
// ---------------------------------------------------------------------------
unsigned int slotlist_get_payload(int list, int key, int offset)
{
    int accum;
    int off;
    int slot;
    int n;

    accum = 0;
    off   = 0;
    n     = 10;
    do {
        slot = list + off;
        if (key == *(int *)(slot + 4)) {
            return *(unsigned int *)(list + (accum + offset) * 4 + 0x54);
        }
        off   = off + 4;
        accum = accum + *(int *)(slot + 0x2c);
        n     = n + -1;
    } while (n != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80220ac8  slotlist_remove_value
// Removes every occurrence of value from the payload window of the 10-slot list,
// compacting the trailing payloads down and decrementing the owning slot's count
// plus the actor-class tally (DAT_806128ac[param_1] +0x30) and clearing its handle
// (+0x2c = -1). Repeats until value no longer found.
// ---------------------------------------------------------------------------
void slotlist_remove_value(int classIdx, int *list, int value)
{
    int classOff;
    int payloadIdx;
    int payloadOff;
    int shift;
    int shiftOff;
    int scan;
    int cnt;
    int payload;

    classOff   = classIdx * 0x4c;
    payloadIdx = 0;
    payloadOff = 0;
    do {
        while (true) {
            payload = *(int *)((int)list + payloadOff + 0x54);
            if (payload == 0) return;
            if (9 < payloadIdx) return;
            if (payload == value) break;
            payloadIdx = payloadIdx + 1;
            payloadOff = payloadOff + 4;
        }
        shift    = 9 - payloadIdx;
        shiftOff = payloadIdx << 2;
        if (payloadIdx < 9) {
            do {
                *(int *)((int)list + shiftOff + 0x54) = *(int *)((int)list + shiftOff + 0x58);
                shift    = shift + -1;
                shiftOff = shiftOff + 4;
            } while (shift != 0);
        }
        cnt        = 0;
        list[0x1e] = 0;
        for (scan = 0; *(int *)((int)list + scan + 4) != *list; scan = scan + 4) {
            int slotCount = *(int *)((int)list + scan + 0x2c);
            if (payloadIdx < cnt + slotCount) {
                *(int *)((int)list + scan + 0x2c) = slotCount + -1;
                *(int *)(classOff + DAT_806128ac + 0x30) =
                    *(int *)(classOff + DAT_806128ac + 0x30) + -1;
                *(int *)(classOff + DAT_806128ac + 0x2c) = -1;
                break;
            }
            cnt = cnt + slotCount;
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// 0x80220ba8  award_get_map_name_string
// Returns the localized map/award name string for the current map. Map 100 (arcade
// custom) uses FUN_80349608; otherwise reads the award-entry table (+8 = string id).
// ---------------------------------------------------------------------------
int award_get_map_name_string(void)
{
    short *entry = award_get_entry(DAT_80612614);
    int    result;
    if (DAT_80612614 == 100) {
        result = FUN_80349608();
    } else if (entry == nullptr || *(int *)(entry + 8) == 0) {
        result = 0;
    } else {
        result = get_string_by_id(*(int *)(entry + 8));
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x80220c00  award_pick_string_variant
// Chooses which of an award entry's two string-id slots to use: variant +4 when in
// stage 3 with the primary player in state 2, else the default variant +2.
// ---------------------------------------------------------------------------
unsigned short award_pick_string_variant(int entry)
{
    if (DAT_805015bd == '\x03' &&
        *(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0x84) == 2) {
        return *(unsigned short *)(entry + 4);
    }
    return *(unsigned short *)(entry + 2);
}

// ---------------------------------------------------------------------------
// 0x80220c3c  award_resolve_string
// Resolves the display string for an award entry: map 100 uses the arcade resolver
// FUN_80349580; otherwise picks the variant and looks it up via get_string_by_id.
// ---------------------------------------------------------------------------
void award_resolve_string(long long a1, long long a2, long long a3, long long a4, long long a5,
                          long long a6, long long a7, long long a8, int entry)
{
    unsigned short strId;
    if (DAT_80612614 == 100) {
        FUN_80349580(a1, a2, a3, a4, a5, a6, a7, a8, entry);
    } else {
        strId = award_pick_string_variant(entry);
        get_string_by_id((unsigned int)strId);
    }
}

// ---------------------------------------------------------------------------
// 0x80220c74  award_result_string
// Returns a win/lose/draw result string. mode 1: win/lose by FUN_80221864 (0x159a
// / 0x159b). mode 0: win/lose by FUN_80221854 (0x1598 / 0x1599). Otherwise the
// empty-string sentinel DAT_80610ea8.
// ---------------------------------------------------------------------------
int award_result_string(int mode)
{
    int   pred;
    char *str;

    if (mode == 1) {
        pred = statlvl_get_team1_remaining();
        str  = (pred == 1) ? (char *)get_string_by_id(0x159a) : (char *)get_string_by_id(0x159b);
    } else if (mode < 1 && -1 < mode) {
        pred = statlvl_get_team0_remaining();
        str  = (pred == 1) ? (char *)get_string_by_id(0x1598) : (char *)get_string_by_id(0x1599);
    } else {
        str = &DAT_80610ea8;
    }
    return (int)str;
}

// ---------------------------------------------------------------------------
// 0x80220cf4  statlvl_object_get_ids
// Reads an object's two ids into *out0 / *out2 (scoped entity via FUN_803826f0).
// If the object is inactive (+9 bit 0 clear) both are 0; if no accessor callbacks
// (+0x40/+0x44) the raw fields (+0x2c/+0x30) are used; otherwise the callbacks are
// invoked.
// ---------------------------------------------------------------------------
void statlvl_object_get_ids(unsigned int a1, unsigned int a2, unsigned int *out2)
{
    int             entity;
    unsigned int    v;
    unsigned int   *out0;
    long long       scoped;

    scoped = (long long)FUN_803826f0();
    entity = (int)((unsigned long long)scoped >> 0x20);
    out0   = (unsigned int *)scoped;
    if ((*(unsigned char *)(entity + 9) & 1) == 0) {
        *out0 = 0;
        *out2 = 0;
    } else if (*(void **)(entity + 0x40) == nullptr || *(int *)(entity + 0x44) == 0) {
        *out0 = *(unsigned int *)(entity + 0x2c);
        *out2 = *(unsigned int *)(entity + 0x30);
    } else {
        v     = (*(unsigned int (**)(void))(entity + 0x40))();
        *out0 = v;
        v     = (*(unsigned int (**)(void))(entity + 0x44))();
        *out2 = v;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80220d90  zone_point_in_region
// Point-in-region test. region[0]==0 (sphere/cylinder): horizontal squared
// distance < r^2 (r from region[4]+region[6]) AND |dy| < region[5]. region[0]==1
// (AABB): point within [region[1..3], region[4..6]]. Returns 1 if inside.
// ---------------------------------------------------------------------------
unsigned int zone_point_in_region(float *point, int *region)
{
    if (*region == 0) {
        float dx = *point - (float)region[1];
        float dz = point[2] - (float)region[3];
        float r2 = (float)region[4] * (float)region[4] + (float)region[6] * (float)region[6];
        if (dx * dx + dz * dz < r2 && ABS(point[1] - (float)region[2]) < (float)region[5]) {
            return 1;
        }
    } else if (*region == 1) {
        if ((float)region[1] <= *point && *point <= (float)region[4] &&
            (float)region[2] <= point[1] && point[1] <= (float)region[5] &&
            (float)region[3] <= point[2] && point[2] <= (float)region[6]) {
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80220e80  zone_player_in_region
// Tests player[playerIdx]'s position against the region, skipping players whose
// health (+0x1cc) is at or below FLOAT_80618568.
// ---------------------------------------------------------------------------
unsigned int zone_player_in_region(int playerIdx, int *region)
{
    int rec = *(int *)(DAT_80611d90 + playerIdx * 0xea0);
    if (*(float *)(rec + 0x1cc) <= FLOAT_80618568) {
        return 0;
    }
    return zone_point_in_region((float *)(rec + 100), region);
}

// ---------------------------------------------------------------------------
// 0x80220ec8  zone_any_player_in_region
// Returns 1 if any active player is inside the region, else 0.
// ---------------------------------------------------------------------------
unsigned int zone_any_player_in_region(int *region)
{
    int i = 0;
    while (true) {
        if (DAT_80612610 <= i) return 0;
        if (zone_player_in_region(i, region) != 0) break;
        i = i + 1;
    }
    return 1;
}

// ===========================================================================
// Batch 675 : 0x80220f30 - 0x80221310
// The per-actor-class objective state machine that sits on top of the
// DAT_806128ac 0x4c-stride actor-class table. Field +0xb is the class "objective
// state" byte (0 = inactive, 1 = armed/pending, 2/3 = active, 5 = completed),
// +0x8 = team/category index (into the per-team active tally DAT_805c6da0), +0x6
// = objective string id (ushort), +0x34 = completion deadline stamp, +0x38/+0x3c
// = optional completion callbacks. Provides three state predicates, a countdown
// HUD broadcast, an objective-string resolver, the "objective completed" handler
// (fires HUD banners + bark + callbacks), plus the force-complete / disarm / arm
// / advance transitions. APPENDED to worm_stat_level.cpp.
// ---------------------------------------------------------------------------
// --- Additional named cross-module helpers (file-local externs) ------------
extern unsigned int session_get_state_flag(void);              // @0x801c1714 (active-player count)
extern void         hud_text_submit_countdown(void);           // @0x801060c4
extern void         hud_text_submit(int player, int channel, unsigned int *msg); // @0x80105d5c
extern unsigned int hud_portrait_label_fadeout(int player, int classRec);        // @0x80107fa0
extern int          sound_resolve_sample_id(int stringId);     // @0x80010074
extern void         aiactor_bark_queue_insert(int sampleId, int priority);       // @0x80143bd4

// Raw ROM helpers still un-named elsewhere.
extern int *FUN_8022da98(void);   // objective "leader"/owner record accessor (returns int*)
extern unsigned int *FUN_803494f4(unsigned long long a1, double a2, double a3, long long a4,
                                  long long a5, long long a6, long long a7, long long a8,
                                  int stringId); // arcade objective banner (returns string ptr)

// Data references for this batch
extern char DAT_805c6da0;   // per-team active-objective tally array (byte-indexed by class +0x8)

// ---------------------------------------------------------------------------
// 0x80220f30  statlvl_class_is_state5
// Returns 1 iff the actor-class's objective state (+0xb) is exactly 5
// (completed). Implemented in the ROM as countLeadingZeros(5 - state) >> 5, which
// is 1 only when 5 - state == 0.
// ---------------------------------------------------------------------------
unsigned int statlvl_class_is_state5(int classIdx)
{
    return countLeadingZeros(5 - (unsigned int)*(unsigned char *)(DAT_806128ac + classIdx * 0x4c + 0xb)) >> 5;
}

// ---------------------------------------------------------------------------
// 0x80220f50  statlvl_class_is_state4
// Returns 1 iff the actor-class's objective state (+0xb) is exactly 4.
// ---------------------------------------------------------------------------
unsigned int statlvl_class_is_state4(int classIdx)
{
    return countLeadingZeros(4 - (unsigned int)*(unsigned char *)(DAT_806128ac + classIdx * 0x4c + 0xb)) >> 5;
}

// ---------------------------------------------------------------------------
// 0x80220f70  statlvl_class_is_active
// Returns 1 iff the actor-class's objective state (+0xb) is neither 0 nor 1,
// i.e. the objective is active or beyond (2/3/5).
// ---------------------------------------------------------------------------
int statlvl_class_is_active(int classIdx)
{
    char state = *(char *)(DAT_806128ac + classIdx * 0x4c + 0xb);
    if (state != '\0' && state != '\x01') {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80220fa0  statlvl_class_broadcast_countdown
// Broadcasts the objective countdown HUD line to every live player whose stored
// deadline (+4 of the per-player record, stride 0x141c from DAT_80611d78) is
// still ahead of the active-player-count gate and who is not scoped/mounted. The
// incoming FP arg (f1) is preserved across the loop but unused by the countdown
// submit itself. Skipped entirely when the scoped record's +8 category is 2.
// ---------------------------------------------------------------------------
void statlvl_class_broadcast_countdown(double bannerArg)
{
    int scoped;
    int playerRec;
    int playerIdx;
    int recOffset;

    (void)bannerArg;
    scoped = __save_gpr_28();          // scoped per-player record base returned in r3
    if (*(char *)(scoped + 8) != '\x02') {
        recOffset = 0;
        for (playerIdx = 0; playerIdx < DAT_80612610; playerIdx = playerIdx + 1) {
            playerRec = DAT_80611d78 + recOffset;
            if (((int)*(int *)(playerRec + 4) < (int)session_get_state_flag()) &&
                (player_is_scoped_or_mounted(playerRec) == 0)) {
                hud_text_submit_countdown();
            }
            recOffset = recOffset + 0x141c;
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80221040  statlvl_resolve_objective_string
// Resolves the objective banner string. Arcade custom map (DAT_80612614 == 100)
// routes through the arcade banner builder FUN_803494f4 (which consumes the wide
// FP ABI); every other map just looks the id up via get_string_by_id.
// ---------------------------------------------------------------------------
unsigned int *statlvl_resolve_objective_string(unsigned long long a1, double a2, double a3,
                                               long long a4, long long a5, long long a6,
                                               long long a7, long long a8, int stringId)
{
    if (DAT_80612614 == 100) {
        return FUN_803494f4(a1, a2, a3, a4, a5, a6, a7, a8, stringId);
    }
    return (unsigned int *)get_string_by_id(stringId);
}

// ---------------------------------------------------------------------------
// 0x80221074  statlvl_class_complete
// "Objective completed" handler for the scoped actor-class record. Two paths:
//   * Story stage 3 (DAT_805015bd == 3) with category (+8) != 2: latches the
//     class to state 5, stamps a short grace deadline (+0x34 = frame + 3*denom),
//     resolves the objective's "owner" record (FUN_8022da98) and — for every live
//     player other than the owner — pushes the objective-name banner (HUD channel
//     3) and fades the owner's portrait label. On non-arcade maps it also queues
//     an audio bark of the objective's sample. Fires the two optional completion
//     callbacks (+0x38 taking no args, +0x3c taking (record, 4)).
//   * Otherwise the classic transition: arm (state 1 -> 2, bump team tally) then,
//     for active states 2/3, latch to 5, broadcast the countdown, and fire the
//     same callbacks.
// ---------------------------------------------------------------------------
void statlvl_class_complete(unsigned long long a1, double a2, double a3, long long a4,
                            long long a5, long long a6, long long a7, long long a8)
{
    int  classIdx;
    int  classRec;
    int  ownerIdx;
    int *ownerRec;
    int  playerIdx;
    unsigned int *banner;
    int  sampleId;
    double bannerArg;

    bannerArg = a2;
    classIdx  = FUN_803826f0();
    classRec  = DAT_806128ac + classIdx * 0x4c;

    if ((DAT_805015bd == '\x03') && (*(char *)(classRec + 8) != '\x02')) {
        if ((*(char *)(classRec + 0xb) == '\x02') || (*(char *)(classRec + 0xb) == '\x03')) {
            *(unsigned char *)(classRec + 0xb) = 5;
            *(int *)(classRec + 0x34) = DAT_80611928 + DAT_806109c0 * -3;
            ownerRec = FUN_8022da98();
            ownerIdx = *ownerRec;
            for (playerIdx = 0; playerIdx < (int)session_get_state_flag(); playerIdx = playerIdx + 1) {
                if (classIdx != ownerIdx) {
                    banner = statlvl_resolve_objective_string(
                                 a1, bannerArg, a3, a4, a5, a6, a7, a8,
                                 (unsigned int)*(unsigned short *)(classRec + 6));
                    hud_text_submit(playerIdx, 3, banner);
                }
                bannerArg = (double)hud_portrait_label_fadeout(playerIdx, classRec);
            }
            if ((DAT_80612614 != 100) && (classIdx != ownerIdx)) {
                sampleId = sound_resolve_sample_id((unsigned int)*(unsigned short *)(classRec + 6));
                aiactor_bark_queue_insert(sampleId, 10);
            }
            if (*(void **)(classRec + 0x38) != nullptr) {
                (*(void (**)(void))(classRec + 0x38))();
            }
            if (*(void **)(classRec + 0x3c) != nullptr) {
                (*(void (**)(int, int))(classRec + 0x3c))(classRec, 4);
            }
        }
    } else {
        if (*(char *)(classRec + 0xb) == '\x01') {
            *(unsigned char *)(classRec + 0xb) = 2;
            (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] =
                (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] + 1;
        }
        if ((*(char *)(classRec + 0xb) == '\x02') || (*(char *)(classRec + 0xb) == '\x03')) {
            *(unsigned char *)(classRec + 0xb) = 5;
            statlvl_class_broadcast_countdown(bannerArg);
            if (*(void **)(classRec + 0x38) != nullptr) {
                (*(void (**)(void))(classRec + 0x38))();
            }
            if (*(void **)(classRec + 0x3c) != nullptr) {
                (*(void (**)(int, int))(classRec + 0x3c))(classRec, 4);
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80221240  statlvl_class_force_complete
// Force-latches an active objective (state 2/3) to completed (5) without firing
// any HUD/bark/callback side effects. No-op for other states.
// ---------------------------------------------------------------------------
void statlvl_class_force_complete(int classIdx)
{
    int  classRec = DAT_806128ac + classIdx * 0x4c;
    char state    = *(char *)(classRec + 0xb);
    if ((state != '\x02') && (state != '\x03')) {
        return;
    }
    *(unsigned char *)(classRec + 0xb) = 5;
}

// ---------------------------------------------------------------------------
// 0x8022126c  statlvl_class_disarm
// Reverts an active objective (state 2/3) back to armed/pending (state 1) and
// decrements its team's active-objective tally. No-op for other states.
// ---------------------------------------------------------------------------
void statlvl_class_disarm(int classIdx)
{
    int classRec = DAT_806128ac + classIdx * 0x4c;
    if ((*(char *)(classRec + 0xb) != '\x02') && (*(char *)(classRec + 0xb) != '\x03')) {
        return;
    }
    *(unsigned char *)(classRec + 0xb) = 1;
    (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] =
        (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] + -1;
}

// ---------------------------------------------------------------------------
// 0x802212b4  statlvl_class_arm
// Promotes an armed/pending objective (state 1) to active (state 2), bumps its
// team's active-objective tally and broadcasts the countdown HUD line.
// ---------------------------------------------------------------------------
void statlvl_class_arm(int classIdx)
{
    int classRec = DAT_806128ac + classIdx * 0x4c;
    if (*(char *)(classRec + 0xb) == '\x01') {
        *(unsigned char *)(classRec + 0xb) = 2;
        (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] =
            (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] + 1;
        statlvl_class_broadcast_countdown(0.0);
    }
}

// ---------------------------------------------------------------------------
// 0x80221310  statlvl_class_advance
// Advances the objective state machine one step: an armed objective (state 1) is
// armed via statlvl_class_arm; an active objective (state 2/3) is bumped to the
// "held" state 3 and the countdown is re-broadcast.
// ---------------------------------------------------------------------------
void statlvl_class_advance(int classIdx)
{
    int  classRec = DAT_806128ac + classIdx * 0x4c;
    char state    = *(char *)(classRec + 0xb);
    if (state == '\x01') {
        statlvl_class_arm(classIdx);
    } else if ((state == '\x02') || (state == '\x03')) {
        *(unsigned char *)(classRec + 0xb) = 3;
        statlvl_class_broadcast_countdown(0.0);
    }
}

// ===========================================================================
// Batch 676 : 0x8022136c - 0x8022165c
// The actor-class registry allocator + membership API that populates the
// DAT_806128ac 0x4c-stride table the batch_675 state machine runs on. The
// active-class index list lives at DAT_80612898 (count DAT_8061289c), the
// per-class slotlist pool at DAT_806128a8 (0x7c stride), and DAT_806128a0 holds
// the "current default" class index used by the member/sub-item helpers. Also
// two more state-set transitions (-> 4, -> 2) and the destroy-time cleanup that
// removes an entity from every class's slotlist. APPENDED to worm_stat_level.cpp.
// ---------------------------------------------------------------------------
// --- Data references for this batch ----------------------------------------
extern char DAT_805015bf;   // spawn mode/difficulty byte (0/1/2+ selects id arg)
extern int  DAT_80612898;   // active-class index list base (int[], stride 4)
extern int  DAT_8061289c;   // active-class count
extern int  DAT_806128a8;   // per-class slotlist pool base (0x7c stride, first 0x2d classes)

// Forward decl: statlvl_class_alloc_slot is defined below but called earlier.
static void statlvl_class_alloc_slot(unsigned int a1, unsigned int a2, int idEasy, int idNormal,
                                     int idHard, unsigned int modeFlags);

// ---------------------------------------------------------------------------
// 0x8022136c  statlvl_class_set_state4
// Sets an actor-class objective to state 4 (unless it is already 4 or fully idle
// at 0) and re-broadcasts the countdown HUD line.
// ---------------------------------------------------------------------------
void statlvl_class_set_state4(int classIdx)
{
    int  classRec = DAT_806128ac + classIdx * 0x4c;
    char state    = *(char *)(classRec + 0xb);
    if ((state != '\x04') && (state != '\0')) {
        *(unsigned char *)(classRec + 0xb) = 4;
        statlvl_class_broadcast_countdown(0.0);
    }
}

// ---------------------------------------------------------------------------
// 0x802213b4  statlvl_class_set_state2
// Sets an actor-class objective back to active state 2 (unless already 2 or idle
// at 0) and re-broadcasts the countdown HUD line.
// ---------------------------------------------------------------------------
void statlvl_class_set_state2(int classIdx)
{
    int  classRec = DAT_806128ac + classIdx * 0x4c;
    char state    = *(char *)(classRec + 0xb);
    if ((state != '\x02') && (state != '\0')) {
        *(unsigned char *)(classRec + 0xb) = 2;
        statlvl_class_broadcast_countdown(0.0);
    }
}

// ---------------------------------------------------------------------------
// 0x802213fc  statlvl_set_default_class
// Sets the "current default" actor-class index that the member / sub-item / field
// helpers below operate on.
// ---------------------------------------------------------------------------
void statlvl_set_default_class(int classIdx)
{
    DAT_806128a0 = classIdx;
}

// ---------------------------------------------------------------------------
// 0x80221404  statlvl_class_register_with_ids
// Registers the scoped actor-class (via statlvl_class_alloc_slot, mode 1) and
// then stamps its two identifier half-words (+4 = groupId, +6 = objective string
// id) into the freshly allocated class record.
// ---------------------------------------------------------------------------
void statlvl_class_register_with_ids(unsigned int a1, unsigned int a2, unsigned short groupId,
                                     unsigned short stringId, int idEasy, int idNormal, int idHard)
{
    int       classIdx;
    int       classRec;
    long long scoped;

    scoped   = (long long)(unsigned int)FUN_803826f0();
    classIdx = (int)((unsigned long long)scoped >> 0x20);
    statlvl_class_alloc_slot((unsigned int)((unsigned long long)scoped >> 0x20),
                             (unsigned int)scoped, idEasy, idNormal, idHard, 1);
    classRec = DAT_806128ac + classIdx * 0x4c;
    *(unsigned short *)(classRec + 4) = groupId;
    *(unsigned short *)(classRec + 6) = stringId;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80221464  statlvl_class_alloc_slot
// Allocates and initializes one actor-class slot in DAT_806128ac for class index
// classIdx. Selects the difficulty-appropriate team/category id (DAT_805015bf: 1
// -> idEasy, 0 -> idNormal, >=3 -> disabled) and, when the spawn flags match the
// active game mode, commits the slot: writes team (+8), records the index in the
// active-class list (DAT_80612898/DAT_8061289c), bumps the per-team tally
// (DAT_805c6da0[team]), seeds the class handle (+0 = index), state (+0xb = 2) and
// the two counters (+2/+3 words = 0), and wires its 10-slot member registry from
// the pool (DAT_806128a8, first 0x2d classes only). Sets DAT_806128a0 to the new
// index (or -1 when the slot is skipped).
// ---------------------------------------------------------------------------
static void statlvl_class_alloc_slot(unsigned int a1, unsigned int a2, int idEasy, int idNormal,
                                     int idHard, unsigned int modeFlags)
{
    int             classIdx;
    int             matched;
    unsigned short *classRec;
    int             team;

    (void)a2;
    classIdx = __save_gpr_28();      // scoped class index returned in r3
    DAT_806128a0 = -1;

    team = idNormal;
    if ((DAT_805015bf != 1) && (team = idEasy, DAT_805015bf != 0) && (team = idHard, 2 < DAT_805015bf)) {
        team = -1;
    }
    if (team != -1) {
        classRec = (unsigned short *)(DAT_806128ac + classIdx * 0x4c);
        matched  = spawn_flags_match_mode(modeFlags);
        if (matched != 0) {
            *(char *)(classRec + 4) = (char)team;                 // +8 team/category byte
            DAT_806128a0 = classIdx;
            *(int *)(DAT_80612898 + DAT_8061289c * 4) = classIdx; // append to active-class list
            (&DAT_805c6da0)[*(unsigned char *)(classRec + 4)] =
                (&DAT_805c6da0)[*(unsigned char *)(classRec + 4)] + 1;
            *classRec = (short)classIdx;                          // +0 handle
            *(unsigned char *)((int)classRec + 0xb) = 2;          // +0xb state = active
            classRec[1] = (unsigned short)team;                   // +2 (r4 groupId scratch)
            classRec[2] = 0;                                      // +4 counters
            classRec[3] = 0;                                      // +6
            if (classIdx < 0x2d) {
                *(int *)(classRec + 6) = DAT_806128a8 + DAT_8061289c * 0x7c;  // +0xc slotlist ptr
                slotlist_init(*(int **)(classRec + 6), 0xffffffff);
            } else {
                *(int *)(classRec + 6) = 0;
            }
            DAT_8061289c = DAT_8061289c + 1;
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802215a8  statlvl_class_register
// Convenience registrar: allocates a class slot with the standard "all game
// modes" flag set (0x700). Used by the level-object spawn iterator
// (statlvl_spawn_level_objects, batch_673).
// ---------------------------------------------------------------------------
void statlvl_class_register(unsigned int a1, unsigned int a2, int idEasy, int idNormal, int idHard)
{
    statlvl_class_alloc_slot(a1, a2, idEasy, idNormal, idHard, 0x700);
}

// ---------------------------------------------------------------------------
// 0x802215cc  statlvl_class_add_member
// Adds an entity/member id to the current default class's 10-slot member registry
// (record +0xc).
// ---------------------------------------------------------------------------
void statlvl_class_add_member(int value)
{
    slotlist_insert_unique(*(int **)(DAT_806128ac + DAT_806128a0 * 0x4c + 0xc), value);
}

// ---------------------------------------------------------------------------
// 0x80221604  statlvl_class_set_param
// Stores a 16-bit parameter into the current default class record (+0x2a).
// ---------------------------------------------------------------------------
void statlvl_class_set_param(unsigned short value)
{
    *(unsigned short *)(DAT_806128ac + DAT_806128a0 * 0x4c + 0x2a) = value;
}

// ---------------------------------------------------------------------------
// 0x8022161c  statlvl_class_add_subitem
// Appends a sub-item id to the current default class's fixed sub-item array
// (record +0x10, -1-terminated), skipping duplicates.
// ---------------------------------------------------------------------------
void statlvl_class_add_subitem(int subId)
{
    int classRec = DAT_806128ac + DAT_806128a0 * 0x4c;
    int off      = 0;
    int cur;
    while (true) {
        cur = *(int *)(classRec + off + 0x10);
        if (cur == -1) {
            *(int *)(classRec + off + 0x10) = subId;
            return;
        }
        if (cur == subId) {
            return;
        }
        off = off + 4;
    }
}

// ---------------------------------------------------------------------------
// 0x8022165c  statlvl_class_remove_entity_all
// Destroy-time cleanup for the scoped entity: during story stages '\n'/'\x03',
// walks every active class (DAT_80612898/DAT_8061289c) and removes the entity's
// id from each class's member registry (classes < 0x2d only).
// ---------------------------------------------------------------------------
void statlvl_class_remove_entity_all(void)
{
    int entityId;
    int i;
    int off;
    int classIdx;

    entityId = FUN_803826f0();
    if ((DAT_805015bd == '\n') || (DAT_805015bd == '\x03')) {
        off = 0;
        for (i = 0; i < DAT_8061289c; i = i + 1) {
            classIdx = *(int *)(DAT_80612898 + off);
            if (classIdx < 0x2d) {
                slotlist_remove_value(classIdx,
                                      *(int **)(DAT_806128ac + classIdx * 0x4c + 0xc),
                                      entityId);
            }
            off = off + 4;
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// Batch 677 : 0x802216f0 - 0x802218f8
// The rest of the actor-class registry API: the member-payload query, the
// scored "add scoped entity to every class" pass (companion to batch_676's
// remove-all), two per-class field setters (+0x48 tally flag, +0x3c completion
// callback), the per-team class counters and the team-0/team-1 "objectives
// remaining" tally accessors, the Nth-class-by-team lookup, the registry reset,
// and the registry allocator (sizes all the DAT_806128xx buffers from the
// dynamic class count FUN_802322cc + 0x2d fixed). APPENDED to worm_stat_level.cpp.
//
// NOTE: this batch resolves several raw-extern references used by earlier
// batches in this file: FUN_802216f0 -> statlvl_class_get_member (batch_671),
// FUN_802217d4 -> statlvl_class_set_complete_cb (batch_673), FUN_80221854 ->
// statlvl_get_team0_remaining and FUN_80221864 -> statlvl_get_team1_remaining
// (both batch_674). Those extern decls + call sites above were updated to the
// real names.
// ---------------------------------------------------------------------------
// --- Data references for this batch ----------------------------------------
extern int  DAT_805c6da4;   // per-team active-objective tally, team 1 (DAT_805c6da0[1])
extern int  DAT_806128a4;   // per-class aux buffer base (8-stride, alloc'd in registry_alloc)
extern int  DAT_8061288c;   // 0x200-byte registry scratch block

// --- Named cross-module helpers (file-local externs) -----------------------
extern unsigned int FUN_802322cc(void);   // dynamic extra-class count (added to the 0x2d fixed slots)

// Registry init sub-passes invoked by statlvl_registry_alloc (decompiled next batches).
extern void statlvl_registry_seed(unsigned long long a1, double a2, double a3, long long a4,
                                  long long a5, long long a6, long long a7, long long a8); // @0x8022199c
extern void statlvl_registry_aux_init(void);                       // @0x80222230 registry post-alloc init

// ---------------------------------------------------------------------------
// 0x802216f0  statlvl_class_get_member
// Returns the member payload stored in class classIdx's 10-slot registry (+0xc)
// for the given key at the given payload offset (thin wrapper over
// slotlist_get_payload).
// ---------------------------------------------------------------------------
int statlvl_class_get_member(int classIdx, int key, int offset)
{
    return (int)slotlist_get_payload(*(int *)(DAT_806128ac + classIdx * 0x4c + 0xc), key, offset);
}

// ---------------------------------------------------------------------------
// 0x80221720  statlvl_class_add_entity_all
// Spawn-time companion to statlvl_class_remove_entity_all: during story stages
// '\n'/'\x03', adds the scoped entity to every active class's member registry
// (classes < 0x2d), scored by the entity's class id (+0x94), via
// slotlist_add_scored.
// ---------------------------------------------------------------------------
void statlvl_class_add_entity_all(void)
{
    int entityId;
    int i;
    int off;
    int classRec;

    entityId = FUN_803826f0();
    if ((DAT_805015bd == '\n') || (DAT_805015bd == '\x03')) {
        off = 0;
        for (i = 0; i < DAT_8061289c; i = i + 1) {
            if (*(int *)(DAT_80612898 + off) < 0x2d) {
                classRec = DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c;
                slotlist_add_scored(classRec, *(int **)(classRec + 0xc), entityId,
                                    (unsigned int)*(unsigned short *)(entityId + 0x94));
            }
            off = off + 4;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802217b8  statlvl_class_clear_tally_flag
// Clears the current default class's "tally-suppressed" byte (+0x48 = 0), so
// subsequent slotlist_add_scored calls increment the owner's global tally.
// ---------------------------------------------------------------------------
void statlvl_class_clear_tally_flag(void)
{
    *(unsigned char *)(DAT_806128ac + DAT_806128a0 * 0x4c + 0x48) = 0;
}

// ---------------------------------------------------------------------------
// 0x802217d4  statlvl_class_set_complete_cb
// Stores the completion callback pointer into the current default class record
// (+0x3c); this is the callback fired by statlvl_class_complete.
// ---------------------------------------------------------------------------
void statlvl_class_set_complete_cb(int cb)
{
    *(int *)(DAT_806128ac + DAT_806128a0 * 0x4c + 0x3c) = cb;
}

// ---------------------------------------------------------------------------
// 0x802217ec  statlvl_count_classes_by_team
// Counts objectives for a team. When useLiveScan != 0, scans the active-class
// list and counts classes whose team byte (+8) matches; otherwise returns the
// precomputed tally DAT_805c6da0[team].
// ---------------------------------------------------------------------------
int statlvl_count_classes_by_team(unsigned int team, int useLiveScan)
{
    int count;
    int i;
    int off;

    if (useLiveScan != 0) {
        count = 0;
        off   = 0;
        for (i = DAT_8061289c; i > 0; i = i - 1) {
            if (team == *(unsigned char *)(DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c + 8)) {
                count = count + 1;
            }
            off = off + 4;
        }
        return count;
    }
    return (&DAT_805c6da0)[team];
}

// ---------------------------------------------------------------------------
// 0x80221854  statlvl_get_team0_remaining
// Returns team 0's active-objective tally (DAT_805c6da0[0]).
// ---------------------------------------------------------------------------
int statlvl_get_team0_remaining(void)
{
    return (int)DAT_805c6da0;
}

// ---------------------------------------------------------------------------
// 0x80221864  statlvl_get_team1_remaining
// Returns team 1's active-objective tally (DAT_805c6da0[1] == DAT_805c6da4).
// ---------------------------------------------------------------------------
int statlvl_get_team1_remaining(void)
{
    return DAT_805c6da4;
}

// ---------------------------------------------------------------------------
// 0x80221874  statlvl_find_class_by_team_index
// Returns the record pointer of the index-th active class whose team byte (+8)
// matches team. Armed-but-inactive classes (state +0xb == 1) are skipped unless
// includeArmed != 0. Returns 0 when fewer than index+1 matches exist.
// ---------------------------------------------------------------------------
int statlvl_find_class_by_team_index(int index, unsigned int team, int includeArmed)
{
    int found;
    int i;
    int off;
    int classRec;

    found = 0;
    off   = 0;
    for (i = DAT_8061289c; i > 0; i = i - 1) {
        classRec = DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c;
        if ((*(unsigned char *)(classRec + 8) == team) &&
            ((*(char *)(classRec + 0xb) != '\x01') || (includeArmed != 0))) {
            if (found == index) {
                return classRec;
            }
            found = found + 1;
        }
        off = off + 4;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802218e4  statlvl_registry_reset
// Resets the actor-class registry: zero the active-class count and clear the
// current-default index.
// ---------------------------------------------------------------------------
void statlvl_registry_reset(void)
{
    DAT_8061289c = 0;
    DAT_806128a0 = -1;
}

// ---------------------------------------------------------------------------
// 0x802218f8  statlvl_registry_alloc
// Allocates all actor-class registry buffers, sized for (FUN_802322cc() + 0x2d)
// classes (16-byte aligned): the 0x4c-stride class table (DAT_806128ac), the
// int index list (DAT_80612898), the fixed 0x15d0 slotlist pool (DAT_806128a8 =
// 0x2d * 0x7c), the 8-stride aux buffer (DAT_806128a4) and a 0x200 scratch block
// (DAT_8061288c). Then runs the default-state seed pass and post-alloc init.
// ---------------------------------------------------------------------------
void statlvl_registry_alloc(unsigned long long a1, double a2, double a3, long long a4, long long a5,
                            long long a6, long long a7, long long a8, int a9, int a10, int a11,
                            int a12, int a13, int a14, int a15, int a16)
{
    unsigned int extra;

    (void)a9; (void)a10;
    extra = FUN_802322cc();
    DAT_806128ac = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                                    ((extra + 0x2d) * 0x4c + 0xf) & 0xfffffff0, 1,
                                    a11, a12, a13, a14, a15, a16);
    extra = FUN_802322cc();
    DAT_80612898 = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                                    ((extra + 0x2d) * 4 + 0xf) & 0xfffffff0, 1,
                                    a11, a12, a13, a14, a15, a16);
    DAT_806128a8 = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x15d0, 1,
                                    a11, a12, a13, a14, a15, a16);
    extra = FUN_802322cc();
    DAT_806128a4 = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                                    ((extra + 0x2d) * 8 + 0xf) & 0xfffffff0, 1,
                                    a11, a12, a13, a14, a15, a16);
    DAT_8061288c = __stat_mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x200, 1,
                                    a11, a12, a13, a14, a15, a16);
    statlvl_registry_seed(a1, a2, a3, a4, a5, a6, a7, a8);
    statlvl_registry_aux_init();
}

// ===========================================================================
// Batch 678 : 0x8022199c - 0x802223b8
// The objective-registry lifecycle driver: the per-level seed/init pass that
// zeroes the whole class table and spawns the level's objectives, a post-init
// callback pass, the registered-scratch-buffer set/clear helpers, the per-class
// objective evaluation tick (counts alive members, fires completion or posts a
// "N remaining" HUD line), an arcade objective-string resolver variant, the
// MASTER mission tick (runs every class tick + story-stage handlers + team-0
// win/lose evaluation), the registry aux-buffer init, and the objective-state
// save/restore snapshot pair (used for netplay / rewind). APPENDED to
// worm_stat_level.cpp.
//
// NOTE: resolves batch_677's raw extern FUN_80222230 -> statlvl_registry_aux_init.
// ---------------------------------------------------------------------------
// --- Data references for this batch ----------------------------------------
extern int  DAT_80612894;   // registered scratch-buffer ptr (for save/restore snapshot)
extern int  DAT_80612890;   // registered scratch-buffer length
extern int  DAT_8061208c;   // active level-info record ptr (global)
extern unsigned int DAT_805015c4; // match/objective outcome flag word (0x8000 win, 0x10000 lose)

// --- Named cross-module helpers (file-local externs) -----------------------
extern int           FUN_803826e8(void);   // scoped-entity push (variant, returns record ptr)
extern void          FUN_80382734(void);   // scoped-entity pop (variant)
extern unsigned int *debug_log_formatted(double a1, double a2, double a3, long long a4, long long a5,
                                         long long a6, long long a7, long long a8, const char *fmt,
                                         int a10, int a11, int a12, int a13, int a14, int a15,
                                         int a16); // sprintf-style -> returns formatted string ptr
extern void          leaf_31e8(unsigned int dst, unsigned int src, int len); // memcpy leaf
extern void          match_process_end(unsigned long long a1, double a2, double a3, long long a4,
                                       long long a5, long long a6, long long a7, long long a8,
                                       int reason); // @0x80030160 end-of-match handler

// Raw ROM helpers still un-named elsewhere (decompiled in later batches).
extern int  FUN_8022a8a0(void);   // round-state record accessor (returns state rec ptr)
extern int  FUN_8022e558(unsigned long long a1, double a2, double a3, long long a4, long long a5,
                         long long a6, long long a7, long long a8); // story stage 3 objective handler
extern int  FUN_80222560(void);   // story stage 4/5 objective handler
extern void FUN_802224f0(void);    // force-complete flagged classes (-> statlvl_class_force_complete)
extern int  FUN_8022b310(unsigned long long a1, double a2, double a3, long long a4, long long a5,
                         long long a6, long long a7, long long a8); // round-mode init hook
extern int  FUN_8022b424(unsigned long long a1, double a2, double a3, long long a4, long long a5,
                         long long a6, long long a7, long long a8); // round-mode post hook
extern unsigned int *FUN_80349538(unsigned long long a1, double a2, double a3, long long a4,
                                  long long a5, long long a6, long long a7, long long a8,
                                  int stringId); // arcade objective banner (returns str ptr)

// The "%d %s" progress format string.
extern char s__d__s_80610eac[];   // "%d %s"

// ---------------------------------------------------------------------------
// 0x8022199c  statlvl_registry_seed
// Per-level objective-registry seed pass (scoped). Resets the active-class count
// / default index / snapshot counters, then walks all (FUN_802322cc()+0x2d) class
// slots: zeroes each 0x4c record, sets its "unassigned" markers (+0xb state 0,
// +0x48 = 0xff, +0x28 = 0xffff), re-inits its member slotlist (+0xc) and its
// 5-entry sub-item array (+0x10). Zeroes the 3-entry team tally (DAT_805c6da0[0..2]).
// Then (non-arcade) spawns the level's scripted objects, calls the level info's
// +0x108 init callback, and runs the round-mode init hook when the mode flags
// (DAT_805015c0 bit 0x40 / bit 0x8000000) request it.
// ---------------------------------------------------------------------------
void statlvl_registry_seed(unsigned long long a1, double a2, double a3, long long a4, long long a5,
                           long long a6, long long a7, long long a8)
{
    int          levelInfo;
    unsigned int total;
    int          i;
    int          off;
    int          classRec;
    int          subOff;
    int          subCount;

    FUN_803826e8();
    levelInfo = level_get_info_cached(DAT_80612614);
    DAT_8061289c = 0;
    DAT_806128a0 = -1;
    DAT_80612894 = 0;
    DAT_80612890 = 0;

    off = 0;
    for (i = 0; total = FUN_802322cc(), i < (int)(total + 0x2d); i = i + 1) {
        classRec = DAT_806128ac + off;
        memset_wrapper(classRec, 0, 0x4c);
        *(unsigned char *)(classRec + 0xb)  = 0;
        *(unsigned char *)(classRec + 0x48) = 0xff;
        *(unsigned short *)(classRec + 0x24) = 0;
        *(unsigned short *)(classRec + 0x26) = 0;
        *(unsigned short *)(classRec + 0x28) = 0xffff;
        *(unsigned short *)(classRec + 2) = 0;
        *(unsigned short *)(classRec + 4) = 0;
        *(unsigned short *)(classRec + 6) = 0;
        if (*(int **)(classRec + 0xc) != nullptr) {
            slotlist_init(*(int **)(classRec + 0xc), 0xffffffff);
        }
        subOff   = 0;
        subCount = 5;
        do {
            *(int *)(classRec + subOff + 0x10) = 0xffffffff;
            subOff   = subOff + 4;
            subCount = subCount - 1;
        } while (subCount != 0);
        off = off + 0x4c;
    }

    for (i = 0; i < 3; i = i + 1) {
        *(int *)((char *)&DAT_805c6da0 + i * 4) = 0;
    }

    if (DAT_80612614 != 100) {
        statlvl_spawn_level_objects();
    }
    if ((levelInfo != 0) && (*(void **)(levelInfo + 0x108) != nullptr)) {
        (*(void (**)(void))(levelInfo + 0x108))();
    }
    if (((DAT_805015c0 & 0x40) != 0) || ((DAT_805015c0 & 0x8000000) != 0)) {
        FUN_8022b310(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80221afc  statlvl_registry_post_init
// Post-init pass: for level 0x66 forces the active-class count back to 0, then
// for every active class with a +0x3c callback invokes it with arg 0 (initial
// activation). Runs the level info's +0x10c hook (via DAT_8061208c) and the
// round-mode post hook when the mode flags request it.
// ---------------------------------------------------------------------------
void statlvl_registry_post_init(unsigned long long a1, double a2, double a3, long long a4,
                                long long a5, long long a6, long long a7, long long a8)
{
    int classRec;
    int i;
    int off;

    if (DAT_80612614 == 0x66) {
        DAT_8061289c = 0;
    }
    off = 0;
    for (i = 0; i < DAT_8061289c; i = i + 1) {
        classRec = DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c;
        if (*(int *)(classRec + 0x3c) != 0) {
            DAT_806128a0 = (unsigned int)*(unsigned short *)classRec;
            (*(void (**)(int, int))(classRec + 0x3c))(classRec, 0);
        }
        off = off + 4;
    }
    DAT_806128a0 = -1;
    if ((DAT_8061208c != 0) && (*(void **)(DAT_8061208c + 0x10c) != nullptr)) {
        (*(void (**)(void))(DAT_8061208c + 0x10c))();
    }
    if (((DAT_805015c0 & 0x40) != 0) || ((DAT_805015c0 & 0x8000000) != 0)) {
        FUN_8022b424(a1, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// 0x80221bdc  statlvl_set_snapshot_buffer
// Registers the scratch buffer (ptr + length) used by the objective-state
// save/restore snapshot passes.
// ---------------------------------------------------------------------------
void statlvl_set_snapshot_buffer(int ptr, int len)
{
    DAT_80612894 = ptr;
    DAT_80612890 = len;
}

// ---------------------------------------------------------------------------
// 0x80221be8  statlvl_clear_snapshot_buffer
// Zeroes the registered snapshot scratch buffer (if one is set).
// ---------------------------------------------------------------------------
void statlvl_clear_snapshot_buffer(void)
{
    if ((DAT_80612894 != 0) && (DAT_80612890 != 0)) {
        memset_wrapper(DAT_80612894, 0, DAT_80612890);
    }
}

// ---------------------------------------------------------------------------
// 0x80221c24  statlvl_class_evaluate_tick
// Per-class objective evaluation tick (scoped class). For a normal (non-stage-3)
// class < 0x2d whose gate (+0x24 != -1) is armed and state is not 4/5: counts how
// many of its registered members are "down" (health +0x1cc <= FLOAT_80618568)
// when the gate mode (+0x24) is 0, and compares that count (+0x16) against the
// target (+0x18). When they match (delta 0) the objective completes; otherwise a
// "N <objective-name>" progress banner (format "%d %s") is posted to every live
// player (HUD channel 2) and a progress sound (0xd6c) plays. Also runs a
// soundpad-zone completion check (+8 != -1): any live player standing in one of
// the class's up-to-5 registered zones completes it. Finally invokes the class's
// +0x3c callback with arg 3 (per-frame).
// ---------------------------------------------------------------------------
void statlvl_class_evaluate_tick(unsigned long long a1, double a2, double a3, long long a4,
                                 long long a5, long long a6, long long a7, long long a8)
{
    int           classIdx;
    int           classRec;
    int           memberList;
    int           member;
    int           downCount;
    int           i;
    int           off;
    int           remaining;
    int           playerIdx;
    int           playerOff;
    int           playerRec;
    int           zoneId;
    int           zoneSlot;
    unsigned int *banner;
    int           strId;
    int           params[20];

    classIdx = FUN_803826e4();
    classRec = DAT_806128ac + classIdx * 0x4c;

    if ((DAT_805015bd != '\x03') && (classIdx < 0x2d)) {
        if ((*(char *)(classRec + 0x48) != -1) &&
            (*(char *)(classRec + 0xb) != '\x05') && (*(char *)(classRec + 0xb) != '\x04')) {
            memberList = *(int *)(classRec + 0xc);
            downCount  = 0;
            off        = 0;
            remaining  = 10;
            do {
                member = *(int *)(memberList + off + 0x54);
                if (member == 0) break;
                if ((*(char *)(classRec + 0x48) == '\0') &&
                    ((double)*(float *)(member + 0x1cc) <= (double)FLOAT_80618568)) {
                    downCount = downCount + 1;
                }
                off       = off + 4;
                remaining = remaining - 1;
            } while (remaining != 0);

            if (downCount != *(int *)(classRec + 0x2c)) {
                *(int *)(classRec + 0x2c) = downCount;
                int delta = *(int *)(classRec + 0x30) - *(int *)(classRec + 0x2c);
                if (delta == 0) {
                    statlvl_class_complete((double)FLOAT_80618568, a2, a3, a4, a5, a6, a7, a8);
                } else if ((*(unsigned char *)(classRec + 9) & 1) != 0) {
                    playerOff = 0;
                    for (playerIdx = 0; playerIdx < DAT_80612610; playerIdx = playerIdx + 1) {
                        if ((delta >= 2) && (*(unsigned short *)(classRec + 0x26) != 0)) {
                            strId  = get_string_by_id((unsigned int)*(unsigned short *)(classRec + 0x26));
                            banner = debug_log_formatted((double)FLOAT_80618568, a2, a3, a4, a5, a6,
                                                         a7, a8, s__d__s_80610eac, delta, strId,
                                                         downCount, memberList, member, 0, 0);
                            hud_text_submit(*(int *)(DAT_80611d78 + playerOff + 4), 2, banner);
                        } else if ((delta == 1) && (*(unsigned short *)(classRec + 0x24) != 0)) {
                            strId  = get_string_by_id((unsigned int)*(unsigned short *)(classRec + 0x24));
                            banner = debug_log_formatted((double)FLOAT_80618568, a2, a3, a4, a5, a6,
                                                         a7, a8, s__d__s_80610eac, 1, strId,
                                                         downCount, memberList, member, 0, 0);
                            hud_text_submit(*(int *)(DAT_80611d78 + playerOff + 4), 2, banner);
                        }
                        playerOff = playerOff + 0x141c;
                    }
                }
                if ((*(unsigned char *)(classRec + 9) & 1) != 0) {
                    sound_init_play_params((int)params);
                    params[0] = 0xd6c;
                    sound_play_request(params);
                }
            }
        }
        if (*(int *)(classRec + 0x10) != -1) {
            playerOff = 0;
            for (playerIdx = 0; playerIdx < DAT_80612610; playerIdx = playerIdx + 1) {
                playerRec = *(int *)(DAT_80611d78 + playerOff + 0x14);
                if ((double)FLOAT_80618568 < (double)*(float *)(playerRec + 0x1cc)) {
                    int playerZone = *(int *)(*(int *)(playerRec + 0x7c) + 0x678);
                    i   = 0;
                    off = 0;
                    while ((i < 5) && (zoneId = *(int *)(classRec + off + 0x10), zoneId != -1)) {
                        zoneSlot = soundpad_find_zone_by_id(zoneId);
                        if (playerZone == zoneSlot) {
                            statlvl_class_complete((double)FLOAT_80618568, a2, a3, a4, a5, a6, a7, a8);
                        }
                        i   = i + 1;
                        off = off + 4;
                    }
                }
                playerOff = playerOff + 0x141c;
            }
        }
    }

    if (*(int *)(classRec + 0x3c) != 0) {
        DAT_806128a0 = (unsigned int)*(unsigned short *)classRec;
        (*(void (**)(int, int))(classRec + 0x3c))(classRec, 3);
        DAT_806128a0 = -1;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80221ed0  statlvl_resolve_objective_string2
// Arcade-aware objective string resolver (variant of statlvl_resolve_objective_string
// that routes arcade map 100 through FUN_80349538 instead of FUN_803494f4).
// ---------------------------------------------------------------------------
unsigned int *statlvl_resolve_objective_string2(unsigned long long a1, double a2, double a3,
                                                long long a4, long long a5, long long a6,
                                                long long a7, long long a8, int stringId)
{
    if (DAT_80612614 == 100) {
        return FUN_80349538(a1, a2, a3, a4, a5, a6, a7, a8, stringId);
    }
    return (unsigned int *)get_string_by_id(stringId);
}

// ---------------------------------------------------------------------------
// 0x80221f04  statlvl_mission_tick
// Master mission/objective tick (scoped). Gated by DAT_806108e4 (doors/logic
// enabled). In round modes (DAT_805015c0 bit 0x40) that have not yet started
// (round state +0x18 bit 2 clear) it only runs the level +0x110 hook. Otherwise:
// runs statlvl_class_evaluate_tick for every active class, dispatches the story-
// stage handler (stage 3 -> FUN_8022e558, stage 4/5 -> FUN_80222560), runs the
// level +0x110 hook, and then the win/lose evaluation:
//   * stage 10 all-players-out-of-play (state != 0x10) -> lose (bit 0x10000).
//   * while team-0 objectives remain: tally per-class completed(state4)/failed
//     (state5 or +9 bit1) vs total; all completed -> victory string 0x159e +
//     match end (bit 0x8000); all resolved with failures -> defeat 0x159c +
//     match end (bit 0x10000); otherwise a periodic (every 10 frames) status
//     banner 0x159d.
// ---------------------------------------------------------------------------
void statlvl_mission_tick(unsigned long long a1, double a2, double a3, long long a4, long long a5,
                          long long a6, long long a7, long long a8)
{
    int           levelInfo;
    int           roundRec;
    int           i;
    int           off;
    int           playerOff;
    int           classRec;
    int           total;
    int           completed;
    int           failed;
    bool          anyInPlay;
    unsigned int *banner;
    int           playerIdx;

    FUN_803826f0();
    levelInfo = level_get_info_cached(DAT_80612614);
    if (DAT_806108e4 == 0) {
        goto done;
    }

    if ((DAT_805015c0 & 0x40) != 0) {
        roundRec = FUN_8022a8a0();
        if ((*(unsigned int *)(roundRec + 0x18) & 2) == 0) {
            if (((*(unsigned int *)(roundRec + 0x18) & 0x40) != 0) && (levelInfo != 0) &&
                (*(void **)(levelInfo + 0x110) != nullptr)) {
                (*(void (**)(void))(levelInfo + 0x110))();
            }
            goto done;
        }
    }

    for (i = 0; i < DAT_8061289c; i = i + 1) {
        statlvl_class_evaluate_tick(a1, a2, a3, a4, a5, a6, a7, a8);
    }

    if (DAT_805015bd != 4) {
        if (DAT_805015bd < 4) {
            if (DAT_805015bd > 2) {
                FUN_8022e558(a1, a2, a3, a4, a5, a6, a7, a8);
            }
        } else if (DAT_805015bd < 6) {
            FUN_80222560();
        }
    }
    if ((levelInfo != 0) && (*(void **)(levelInfo + 0x110) != nullptr)) {
        (*(void (**)(void))(levelInfo + 0x110))();
    }

    // Stage 10: end when every player has left play (state != 0x10 means in play).
    if (((DAT_805015c4 & 0x8000) == 0) && (DAT_805015bd == 10)) {
        anyInPlay = false;
        playerOff = 0;
        for (i = DAT_80612610; i > 0; i = i - 1) {
            if (*(int *)(DAT_80611d78 + playerOff + 0x114) != 0x10) {
                anyInPlay = true;
            }
            playerOff = playerOff + 0x141c;
        }
        if (!anyInPlay) {
            DAT_805015c4 = DAT_805015c4 | 0x10000;
            match_process_end(a1, a2, a3, a4, a5, a6, a7, a8, 0);
            goto done;
        }
    }

    // Team-0 objective win/lose evaluation.
    if (((DAT_805015c4 & 0x8000) == 0) && (statlvl_get_team0_remaining() > 0)) {
        completed = 0;
        failed    = 0;
        total     = 0;
        off       = 0;
        for (i = DAT_8061289c; i > 0; i = i - 1) {
            classRec = DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c;
            if (*(char *)(classRec + 8) == '\0') {
                total = total + 1;
                if (*(char *)(classRec + 0xb) == '\x04') {
                    completed = completed + 1;
                } else if ((*(char *)(classRec + 0xb) == '\x05') ||
                           ((*(unsigned char *)(classRec + 9) & 2) != 0)) {
                    failed = failed + 1;
                }
            }
            off = off + 4;
        }
        if (completed < 1) {
            if (failed == total) {
                FUN_802224f0();
                for (playerIdx = 0; playerIdx < session_get_state_flag(); playerIdx = playerIdx + 1) {
                    banner = statlvl_resolve_objective_string2(a1, a2, a3, a4, a5, a6, a7, a8, 0x159e);
                    hud_text_submit(playerIdx, 4, banner);
                }
                DAT_805015c4 = DAT_805015c4 | 0x8000;
                match_process_end(a1, a2, a3, a4, a5, a6, a7, a8, 0);
            }
        } else if (total == completed + failed) {
            for (playerIdx = 0; playerIdx < session_get_state_flag(); playerIdx = playerIdx + 1) {
                banner = (unsigned int *)get_string_by_id(0x159c);
                hud_text_submit(playerIdx, 4, banner);
            }
            DAT_805015c4 = DAT_805015c4 | 0x10000;
            match_process_end(a1, a2, a3, a4, a5, a6, a7, a8, 0);
        } else if (DAT_80611928 == (DAT_80611928 / 10) * 10) {
            for (playerIdx = 0; playerIdx < session_get_state_flag(); playerIdx = playerIdx + 1) {
                banner = (unsigned int *)get_string_by_id(0x159d);
                hud_text_submit(playerIdx, 2, banner);
            }
            DAT_805015c4 = DAT_805015c4 | 0x10000;
        }
    }
done:
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80222230  statlvl_registry_aux_init
// Zeroes the per-class aux buffer (DAT_806128a4, 8-byte pairs for all
// FUN_802322cc()+0x2d classes) and the 0x200-byte scratch block (DAT_8061288c).
// Resolves batch_677's FUN_80222230 raw extern.
// ---------------------------------------------------------------------------
void statlvl_registry_aux_init(void)
{
    unsigned int total;
    int          i;
    int          off;

    __save_gpr_28();
    off = 0;
    for (i = 0; total = FUN_802322cc(), i < (int)(total + 0x2d); i = i + 1) {
        *(int *)(DAT_806128a4 + off)     = 0;
        *(int *)(DAT_806128a4 + off + 4) = 0;
        off = off + 8;
    }
    memset_wrapper(DAT_8061288c, 0, 0x200);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802222ac  statlvl_snapshot_save
// Objective-state snapshot (save side, scoped). Invokes each active class's
// +0x3c callback with arg 2 (pre-save; clears the +0xa byte first), then copies
// every class's live state (+0xb) and +0xa byte into the aux buffer DAT_806128a4
// (8-byte pairs). Finally copies the registered scratch buffer (DAT_80612894) into
// the snapshot block (DAT_8061288c). Used to capture a rollback point for netplay.
// ---------------------------------------------------------------------------
void statlvl_snapshot_save(void)
{
    int          classRec;
    unsigned int total;
    int          i;
    int          off;
    int          auxOff;
    int          auxBase;

    FUN_803826f0();
    off = 0;
    for (i = 0; i < DAT_8061289c; i = i + 1) {
        classRec = DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c;
        if (*(int *)(classRec + 0x3c) != 0) {
            DAT_806128a0 = (unsigned int)*(unsigned short *)classRec;
            *(unsigned char *)(classRec + 0xa) = 0;
            (*(void (**)(int, int))(classRec + 0x3c))(classRec, 2);
        }
        off = off + 4;
    }

    off    = 0;
    auxOff = 0;
    for (i = 0; total = FUN_802322cc(), i < (int)(total + 0x2d); i = i + 1) {
        auxBase = DAT_806128a4;
        *(unsigned int *)(DAT_806128a4 + auxOff)     = (unsigned int)*(unsigned char *)(DAT_806128ac + off + 0xb);
        *(unsigned int *)(auxBase + auxOff + 4)      = (unsigned int)*(unsigned char *)(DAT_806128ac + off + 0xa);
        off    = off + 0x4c;
        auxOff = auxOff + 8;
    }
    DAT_806128a0 = -1;
    if ((DAT_80612894 != 0) && (DAT_80612890 != 0)) {
        leaf_31e8((unsigned int)DAT_8061288c, (unsigned int)DAT_80612894, DAT_80612890);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802223b8  statlvl_snapshot_restore
// Objective-state snapshot (restore side, scoped). Writes each class's saved
// state (+0xb) and +0xa byte back from the aux buffer DAT_806128a4 (only for
// entries that were captured); when a class leaves the armed state (was 1, now
// != 1) its team tally (DAT_805c6da0[team]) is bumped. Restores the scratch block
// from the snapshot (DAT_8061288c -> DAT_80612894), then invokes each active
// class's +0x3c callback with arg 1 (post-restore).
// ---------------------------------------------------------------------------
void statlvl_snapshot_restore(void)
{
    int          classRec;
    unsigned int total;
    int          i;
    int          off;
    int          auxOff;
    int          auxBase;

    FUN_803826f0();
    off    = 0;
    auxOff = 0;
    for (i = 0; total = FUN_802322cc(), auxBase = DAT_806128a4, i < (int)(total + 0x2d); i = i + 1) {
        if (*(int *)(DAT_806128a4 + auxOff) != 0) {
            classRec = DAT_806128ac + off;
            if ((*(char *)(classRec + 0xb) == '\x01') && (*(int *)(DAT_806128a4 + auxOff) != 1)) {
                (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] =
                    (&DAT_805c6da0)[*(unsigned char *)(classRec + 8)] + 1;
            }
            *(char *)(classRec + 0xb) = (char)*(unsigned int *)(auxBase + auxOff);
            *(char *)(classRec + 0xa) = (char)*(unsigned int *)(DAT_806128a4 + auxOff + 4);
        }
        off    = off + 0x4c;
        auxOff = auxOff + 8;
    }
    if ((DAT_80612894 != 0) && (DAT_80612890 != 0)) {
        leaf_31e8((unsigned int)DAT_80612894, (unsigned int)DAT_8061288c, DAT_80612890);
    }

    off = 0;
    for (i = 0; i < DAT_8061289c; i = i + 1) {
        classRec = DAT_806128ac + *(int *)(DAT_80612898 + off) * 0x4c;
        if (*(int *)(classRec + 0x3c) != 0) {
            DAT_806128a0 = (unsigned int)*(unsigned short *)classRec;
            (*(void (**)(int, int))(classRec + 0x3c))(classRec, 1);
        }
        off = off + 4;
    }
    DAT_806128a0 = -1;
    FUN_8038273c();
}
