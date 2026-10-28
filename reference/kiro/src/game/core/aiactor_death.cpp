// Address range: 0x8013ce14 - 0x8013d9e7 (death/respawn/find subset) | batch_369 | 2026-09-05
//
// AI actor destroy / respawn / char-def init and team-lookup helpers.
// Continuation of batches 364-368. Per-actor render object = *(actor + 0x7c);
// combat/nav state lives there. DAT_805015bd = game mode byte (0x0a coop).
// DAT_80611d90 player state pool (0xea0 stride), DAT_80612610 base player count,
// DAT_806120fc extra-bot count, DAT_80588610 visible-node ptr table,
// DAT_80588a1c respawned-bot entity table. FUN_803826xx/__save_gpr_xx = fp/gpr
// scratch-frame helpers (actor handle in the high word).
//
// Functions:
//   aiactor_push_zone_history          0x8013ce14  store zone in +0xe7c ring(3)
//   aiactor_destroy_instance           0x8013d170  full teardown / death cleanup
//   aiactor_init_from_char_def         0x8013d36c  weapon/attachment/team setup
//   aiactor_find_player_by_team        0x8013d618  first live player on a team
//   aiactor_find_entity_by_team        0x8013d6b0  team lookup with flag filters
//   aiactor_alloc_stub                 0x8013d794  bot-slot alloc stub (returns 0)
//   aiactor_respawn_bot                0x8013d79c  find free slot + spawn bot

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern int  soundpad_find_zone_by_id(int id);
extern void player_ai_projectile_clear_entity_refs(int entityId);
extern void player_ai_on_death_cleanup(int charData);
extern void player_ai_stop_ambient_sound(int entity);
extern void entity_destroy_with_event(int* entity);
extern int  player_ai_is_sniper_weapon(int weaponType);
extern int  match_build_weapon_list(void);
extern int* spawn_def_get_entry(int index);
extern void player_ai_list_insert_target(int listBase, int entity);
extern int  entity_create_attachment(u64 a, double p2, double p3, u64 p4, u64 p5, u64 p6,
                                      u64 p7, u64 p8, int entity);
}

// ---- raw data symbols ------------------------------------------------------
extern char DAT_805015bd;    // game mode byte (0x0a coop)
extern int  DAT_80611b8c;    // zone table base (0x20 stride)
extern int  DAT_80611c24;    // decal/handle pool base (0x2e8 stride)
extern int  DAT_80611d90;    // player state pool base (0xea0 stride)
extern int  DAT_80610ba0;    // weapon def table base (0x29c stride)
extern int  DAT_8043e23c;    // weapon-slot -> def index table (0x3c stride*int)
extern int  DAT_80612094;    // active special-entity count
extern int  DAT_80612610;    // base player/actor count
extern int  DAT_80612098;    // visible-node count
extern int  DAT_80588610;    // visible-node ptr table
extern int  DAT_806120fc;    // active extra-bot count
extern int  DAT_803ccaac;    // spawn offset x
extern int  DAT_803ccab0;    // spawn offset y
extern int  DAT_803ccab4;    // spawn offset z
extern int  DAT_80588a1c;    // respawned-bot entity table

extern float FLOAT_80615cac; // 0.0
extern float FLOAT_80615ca8; // 1.0

// ---- raw function symbols --------------------------------------------------
extern "C" {
extern void FUN_8037ee2c(void);                          // stop looping sfx
extern int  FUN_8013e9cc(int actor, int phase, int* ctx, int flag); // behaviour phase gate
extern void FUN_8013e7cc(void);                          // clear combat timers
extern void FUN_8013e774(int actor);                     // release nav resources
extern u64  FUN_8013db30(int actor);                     // pre-drop weapon prep (returns handle)
extern void FUN_8038b4cc(int base, int a, int n);        // zero a region / memset-like
extern void FUN_8013fcf8(int actor, u32 flags);          // despawn linked spawns
extern void FUN_80158844(int* rd);                       // free decal handle
extern void FUN_801c007c(int* soundSlot);                // stop 3d sound slot
extern u64  FUN_801a9a8c(int* actor, u32 defId, int budget); // load weapon anim/model
extern void FUN_8013fdac(int actor);                     // apply spawn-linked spawns
extern u64  FUN_8022274c(int slot);                      // build spawn frame from slot

extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
extern int                FUN_803826d4(void);            // fp frame enter (respawn)
extern void               FUN_80382720(void);            // fp frame leave

// player_ai_drop_all_weapons / player_ai_spawn_bot / player_ai_assign_team have
// register-frame ABIs; declare them at the arity of these call sites.
extern void player_ai_drop_all_weapons(u64 a, double p2, double p3, double p4, u64 p5,
                                       u64 p6, u64 p7, u64 p8, int* rd);
extern int  player_ai_spawn_bot(double p1, double p2, double p3, double p4, u64 p5, u64 p6,
                                u64 p7, u64 p8, u32 charIdx, u32 skin, int slot, int frameHi,
                                int one, int param14);
extern u64  player_ai_assign_team(int charData);
extern int  FUN_80022a08(int weap);                      // has team-anim variant?
extern u64  FUN_80023514(double a, double b);            // play team pose anim
}

// forward decls for functions defined later in this file (used before definition)
int  aiactor_alloc_stub(void);
void aiactor_destroy_instance(u64, double, double, double, u64, u64, u64, u64, int);

// spawn_create_instance / player_ai_drop_all_weapons have register-frame ABIs
// whose real arity differs from a plain decl; invoke through typed casts.
extern "C" void spawn_create_instance(void);
typedef int (*spawn_ci_fn)(u64, double, double, u64, u64, u64, u64, u64, int, int, int, int,
                           int*, int*, int, int, int, int);
static int spawn_create_instance_ex(u64 a, double p2, double p3, u64 p4, u64 p5, u64 p6, u64 p7,
                                    u64 p8, int a9, int a10, int a11, int a12, int* a13, int* a14,
                                    int a15, int a16, int a17, int a18)
{
    return ((spawn_ci_fn)(void*)&spawn_create_instance)(a, p2, p3, p4, p5, p6, p7, p8, a9, a10,
                                                        a11, a12, a13, a14, a15, a16, a17, a18);
}
typedef u64 (*drop_ret_fn)(u64, double, double, double, u64, u64, u64, u64, int*);
static u64 player_ai_drop_all_weapons_ret(u64 a, double p2, double p3, double p4, u64 p5, u64 p6,
                                          u64 p7, u64 p8, int* rd)
{
    return ((drop_ret_fn)(void*)&player_ai_drop_all_weapons)(a, p2, p3, p4, p5, p6, p7, p8, rd);
}

// aiactor_alloc_stub 0x8013d794  (bot slot allocator, currently a no-op stub)
int aiactor_alloc_stub(void)
{
    return 0;
}

// aiactor_push_zone_history 0x8013ce14
// Records a zone id into the actor's 3-slot zone-history ring (+0xe7c) at the
// first empty slot, and resets the path cursors.
void aiactor_push_zone_history(int actor, int srcObj)
{
    int rd = *(int*)(actor + 0x7c);
    int off = 0;
    int i = 3;
    do {
        if (*(int*)(rd + off + 0xe7c) == 0) {
            *(int*)(rd + off + 0xe7c) = *(int*)(srcObj + 0x2c);
            break;
        }
        off = off + 4;
        i = i + -1;
    } while (i != 0);
    *(int*)(rd + 0x9ac) = 0;
    *(int*)(rd + 0x9a8) = 0;
}

// aiactor_find_player_by_team 0x8013d618
// Returns the entity of the first live player whose char-def team == param_1,
// skipping actors flagged 8 (dead/inactive). 0 if none.
int aiactor_find_player_by_team(int team)
{
    int   poolOff = DAT_80612610 * 0xea0;
    int   remaining = (DAT_806120fc + DAT_80612610) - DAT_80612610;
    int   inA, inB;
    int*  p;

    if (DAT_80612610 < DAT_806120fc + DAT_80612610) {
        do {
            p = (int*)(DAT_80611d90 + poolOff);
            if (DAT_805015bd == '\n') { inA = p[4]; inB = p[5]; }
            else                      { inA = p[6]; inB = p[7]; }
            if (((inB != 0 || inA != 0) && (*(int*)(p[0x2fc] + 8) == team)) &&
                ((p[9] & 8) == 0)) {
                return *p;
            }
            poolOff = poolOff + 0xea0;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return 0;
}

// aiactor_find_entity_by_team 0x8013d6b0
// Team lookup with flag-selectable pool + filters:
//   flags&1 -> scan bot pool (else visible-node list)
//   flags&4 -> start at index 0 (else at DAT_80612610)
//   flags&2 -> allow flag-8 (dead) actors
// Returns the matching entity or 0.
int aiactor_find_entity_by_team(int team, u32 flags)
{
    u32  start = DAT_80612610 & ~-(int)((flags >> 2) & 1);
    int  end   = ((flags & 1) != 0) ? (DAT_806120fc + DAT_80612610) : DAT_80612098;
    int  remaining = end - (int)start;
    int  poolOff = (int)start * 0xea0;
    int  listOff = (int)start * 4;
    int  inA, inB;
    int* p;

    if ((int)start < end) {
        do {
            if ((flags & 1) == 0) p = *(int**)((int)&DAT_80588610 + listOff);
            else                  p = (int*)(DAT_80611d90 + poolOff);
            if ((p[0x2fc] != 0) && (*(int*)(p[0x2fc] + 8) == team)) {
                if (DAT_805015bd == '\n') { inA = p[4]; inB = p[5]; }
                else                      { inA = p[6]; inB = p[7]; }
                if ((inB != 0 || inA != 0) && (((p[9] & 8) == 0) || ((flags & 2) != 0))) {
                    return *p;
                }
            }
            listOff = listOff + 4;
            poolOff = poolOff + 0xea0;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return 0;
}

// aiactor_destroy_instance 0x8013d170
// Full per-actor teardown on death: stop sfx, run behaviour phase 5, drop
// weapons, release decal/spawn/sound resources, run death cleanup and destroy
// the underlying entity.
void aiactor_destroy_instance(u64 param_1, double param_2, double param_3, double param_4,
                              u64 param_5, u64 param_6, u64 param_7, u64 param_8, int actor)
{
    int  zoneRow;
    int  handle;
    u32  handleId;
    int* rd;
    u64  dropFrame;
    (void)param_1;

    rd = *(int**)(actor + 0x7c);
    FUN_8037ee2c();
    FUN_8013e9cc(actor, 5, (int*)0, 0);
    FUN_8013e7cc();
    FUN_8013e774(actor);
    dropFrame = FUN_8013db30(actor);
    rd[7] = 0; rd[6] = 0; rd[5] = 0; rd[4] = 0;
    *(u32*)(*rd + 0x28) = *(u32*)(*rd + 0x28) & 0xffffff9e;
    *(float*)(*rd + 0x1cc) = FLOAT_80615cac;
    player_ai_drop_all_weapons(dropFrame, param_2, param_3, param_4, param_5,
                               param_6, param_7, param_8, rd);
    rd[0x19e] = -1;
    FUN_8038b4cc((int)(rd + 0x14), -0x7f9ef670, 0x1e);
    if (rd[0x2fd] != 0) {
        DAT_80612094 = DAT_80612094 + -1;
        *(int*)(rd[0x2fd] + 4) = *(int*)(rd[0x2fd] + 4) + -1;
        *(int*)(rd[0x2fd] + 8) = *(int*)(rd[0x2fd] + 8) + 1;
        rd[0x2fd] = 0;
    }
    if ((rd[10] & 0x400000U) != 0) {
        DAT_80612094 = DAT_80612094 + -1;
    }
    if (DAT_805015bd == '\n') {
        player_ai_projectile_clear_entity_refs((int)rd);
        if (*(u32*)(rd[0x2fc] + 0x70) != 0) {
            FUN_8013fcf8(actor, *(u32*)(rd[0x2fc] + 0x70));
        }
    }
    if (rd[0x270] != 0) {
        zoneRow = soundpad_find_zone_by_id((int)*(short*)(rd[0x270] + 8));
        zoneRow = DAT_80611b8c + zoneRow * 0x20;
        *(u32*)(zoneRow + 4) = *(u32*)(zoneRow + 4) & 0xbfffffff;
        rd[0x270] = 0;
    }
    handleId = rd[0x302];
    if (handleId == 0) {
        handle = 0;
    }
    else {
        handle = DAT_80611c24 + (handleId >> 0x16) * 0x2e8;
        if (*(u32*)(handle + 0x2c) != handleId) {
            handle = 0;
            rd[0x302] = 0;
        }
    }
    if (handle != 0) {
        FUN_80158844(rd);
    }
    rd[0x2fc] = 0;
    player_ai_on_death_cleanup((int)rd);
    rd[1] = 0;
    FUN_801c007c(rd + 0x3a5);
    player_ai_stop_ambient_sound(*rd);
    entity_destroy_with_event((int*)*rd);
    *rd = 0;
}

// aiactor_init_from_char_def 0x8013d36c
// Initialises a freshly-spawned actor from its char-def block: propagates flag
// bits, resolves the primary weapon (or the sniper/dual-pistol special cases),
// spawns any attachment entity, copies input masks and resolves the home zone.
void aiactor_init_from_char_def(u64 param_1, double param_2, double param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int* rd;
    int  actor;
    int  weapList;
    int* cdef;
    int  weapZone;
    u64  uVar6;
    int  local_20[3];

    rd = (int*)__save_gpr_28();
    actor = *rd;
    weapList = match_build_weapon_list();
    cdef = (int*)rd[0x2fc];
    if (cdef != (int*)0) {
        rd[10] = rd[10] | (cdef[0x15] & 0xff000000U);
        rd[0xb] = rd[0xb] | (cdef[0x16] & 0xe0000000U);
        *(int*)(actor + 0x60) = cdef[2];
        if (*(short*)(cdef + 6) == -1) {
            if (player_ai_is_sniper_weapon((int)*(short*)((int)rd + 0x4a)) == 0) {
                if (*(short*)((int)rd + 0x4a) == 0x60) {
                    rd[0x25] = 0x42;
                    uVar6 = FUN_801a9a8c(rd, *(u32*)(DAT_80610ba0 +
                            (&DAT_8043e23c)[rd[0x25] * 0xf] * 0x29c + 0x14), 10000);
                }
                else if (*(short*)((int)rd + 0x4a) == 0x62) {
                    rd[0x25] = 0x43;
                    uVar6 = FUN_801a9a8c(rd, *(u32*)(DAT_80610ba0 +
                            (&DAT_8043e23c)[rd[0x25] * 0xf] * 0x29c + 0x14), 10000);
                }
                else {
                    rd[0x25] = 0;
                    uVar6 = 0;
                }
            }
            else {
                rd[0x25] = 0x40;
                uVar6 = FUN_801a9a8c(rd, *(u32*)(DAT_80610ba0 +
                        (&DAT_8043e23c)[rd[0x25] * 0xf] * 0x29c + 0x14), 10000);
            }
        }
        else {
            rd[0x25] = *(int*)(weapList + *(short*)(cdef + 6) * 4 + 8);
            weapZone = DAT_80610ba0 + (&DAT_8043e23c)[rd[0x25] * 0xf] * 0x29c;
            FUN_801a9a8c(rd, *(u32*)(weapZone + 0x14), 10000);
            uVar6 = FUN_801a9a8c(rd, *(u32*)(weapZone + 0x1c), 10);
        }
        if (*(short*)(cdef + 5) != -1) {
            int  spawned;
            int* spawnDef;
            local_20[0] = DAT_803ccaac;
            local_20[1] = DAT_803ccab0;
            local_20[2] = DAT_803ccab4;
            spawnDef = spawn_def_get_entry(0);
            spawned = spawn_create_instance_ex(uVar6, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, 0, 0x200,
                                               (int)*(short*)(cdef + 5), 0, local_20, spawnDef,
                                               0, 0, 0, 0);
            player_ai_list_insert_target((int)(rd + 0xc1), spawned);
        }
        if (DAT_805015bd == '\n') {
            rd[5] = cdef[1];
            rd[4] = *cdef;
        }
        weapZone = cdef[0x14];
        if ((weapZone == 0) || (weapZone == -1)) {
            rd[0x26e] = 0;
        }
        else {
            rd[0x26e] = soundpad_find_zone_by_id(weapZone);
        }
        rd[9] = 1;
        if (cdef[0x1c] != 0) {
            FUN_8013fdac(actor);
            rd[0x2fb] = *(int*)(rd[0x2fc] + 0x70);
        }
    }
    __restore_gpr_28();
}

// aiactor_respawn_bot 0x8013d79c
// Finds a free extra-bot slot (allocating a new one up to 30), destroys any
// stale entity there, spawns a fresh bot with team assignment, weapons, and any
// team-pose / attachment entities. Fills the DAT_80588a1c respawn table.
void aiactor_respawn_bot(u64 param_1, double param_2, double param_3, double param_4,
                         u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                         u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                         u32 param_13, int param_14)
{
    bool  spawnedNew;
    int   savedBots;
    int   attempts;
    int   slot;
    int   scanRemaining;
    int   poolOff;
    int*  p;
    int   foundSlot;
    u64   frame;
    double zero, one, tmp;
    (void)param_1; (void)param_9; (void)param_10; (void)param_13;

    frame     = FUN_803826d4();
    savedBots = DAT_806120fc;
    attempts  = 0;
    slot      = -1;
    do {
        spawnedNew    = false;
        scanRemaining = (DAT_806120fc + DAT_80612610) - DAT_80612610;
        poolOff       = DAT_80612610 * 0xea0;
        int idx       = DAT_80612610;
        foundSlot     = slot;
        if (DAT_80612610 < DAT_806120fc + DAT_80612610) {
            do {
                if (DAT_805015bd == '\n') p = (int*)(DAT_80611d90 + poolOff + 0x10);
                else                      p = (int*)(DAT_80611d90 + poolOff + 0x18);
                foundSlot = idx;
                if (p[1] == 0 && p[0] == 0) break;
                idx = idx + 1;
                poolOff = poolOff + 0xea0;
                scanRemaining = scanRemaining + -1;
                foundSlot = slot;
            } while (scanRemaining != 0);
        }
        if ((foundSlot == -1) && (DAT_806120fc < 0x1e)) {
            foundSlot = DAT_80612610 + DAT_806120fc;
            DAT_806120fc = DAT_806120fc + 1;
        }
        if (foundSlot == -1) {
            int allocated = (attempts == 0) ? aiactor_alloc_stub() : 0;
            savedBots = DAT_806120fc;
            if (allocated == 0) { DAT_806120fc = savedBots; FUN_80382720(); return; }
            u64 spawnFrame = FUN_8022274c(allocated);
            aiactor_destroy_instance(spawnFrame, param_2, param_3, param_4, param_5,
                                     param_6, param_7, param_8, allocated);
            spawnedNew = true;
            attempts = attempts + 1;
        }
        slot = foundSlot;
    } while (spawnedNew);

    p = (int*)(DAT_80611d90 + slot * 0xea0);
    if ((int*)*p != (int*)0) {
        entity_destroy_with_event((int*)*p);
        *p = 0;
    }
    zero = (double)FLOAT_80615cac;
    one  = zero;
    tmp  = zero;
    int botEnt = player_ai_spawn_bot(zero, zero, zero, zero, param_5, param_6, param_7, param_8,
                                     param_11, param_12, slot, (int)frame, 1, param_14);
    int rel = slot - DAT_80612610;
    (&DAT_80588a1c)[rel] = botEnt;
    savedBots = DAT_806120fc;
    if ((&DAT_80588a1c)[rel] != 0) {
        u64 pose = player_ai_assign_team(*(int*)((&DAT_80588a1c)[rel] + 0x7c));
        pose = player_ai_drop_all_weapons_ret(pose, zero, one, tmp, param_5, param_6, param_7,
                                              param_8, p);
        p[0x2fc] = (int)((unsigned long long)frame >> 0x20);
        if (*(short*)(p[0x2fc] + 0x48) != -1) {
            if (FUN_80022a08((int)*(short*)(p[0x2fc] + 0x48)) == 0) {
                pose = FUN_80023514((double)FLOAT_80615cac, (double)FLOAT_80615cac);
            }
            else {
                pose = FUN_80023514((double)FLOAT_80615cac, (double)FLOAT_80615cac);
            }
        }
        savedBots = DAT_806120fc;
        if (*(int*)(p[0x2fc] + 0x88) != -1) {
            entity_create_attachment(pose, zero, one, param_5, param_6, param_7, param_8,
                                     (u64)0, *p);
            savedBots = DAT_806120fc;
        }
    }
    DAT_806120fc = savedBots;
    FUN_80382720();
}
