// ============================================================================
// Address range: 0x8036f678 - 0x8036fa5c | batch_1036 | 2026-09-12
//
// Level "prop" / ambient-object manager. Right after the flock/swarm module the
// binary has a small generic manager for the decoration/prop objects a level
// attaches through its level-info block. The level-info sub-object lives at
// level_info + 0x118 and is cached in DAT_80612ed8; it holds:
//   [0]      -> prop-descriptor struct ptr (texture/sound id fields at +0x04,
//               +0x0c, +0x38, +0x48, +0x60)
//   [0xb]    -> optional pre-load hook (code*)
//   [0xc]    -> prop-instance table base (0x4c-stride records)
//   [0x10]   -> next free instance index / running instance count
// A separate prop-TYPE table (0x158-stride) is walked from DAT_80612ed4 /
// DAT_80612f14. Each prop-instance record (0x4c) holds pos[0..3], a scaled copy
// at +0x3c.., anim params at +0x10.., a flags word at +0x14, a priority at +0x18,
// and an owner back-reference. Owners keep a small id list at +0x154 (count) /
// +0x12c (ids). This slice covers the level resource pre-load, instance spawn,
// the small field setters, the priority queries, and the type-table iterator.
//
//   prop_preload_sounds            0x8036f678  register the level's two swarm sound ids
//   prop_preload_resources         0x8036f6bc  preload prop textures + sounds (caller level_preload_all_resources)
//   prop_instance_spawn            0x8036f854  fill a new 0x4c instance record + register it in the owner list
//   prop_set_scale_vec             0x8036f8fc  write the instance scale vec (+0x24/+0x28/+0x2c)
//   prop_set_field14               0x8036f90c  write the instance flags/param word (+0x14)
//   prop_set_pose                  0x8036f914  write pos+rot (+0x00..+0x10) and copy +0x14 from the type
//   prop_activate_topmost_flagged  0x8036f93c  activate the highest-priority instance whose flag bit 1 is set
//   prop_find_topmost              0x8036f9c0  return the highest-priority (+0x18) instance record
//   prop_owner_has_instance        0x8036fa20  linear search the owner id list for an instance id
//   prop_type_foreach              0x8036fa5c  iterate the 0x158-stride prop-type table, callback until non-zero
//
// NOTE: non-matching decomp. The GameCube callee-save prologue spill is dropped.
// Struct offsets, table strides (0x4c instance, 0x158 type), the level-info
// +0x118 / +0x10 cursor fields and every texture/sound side effect match the
// original. Engine helpers whose generated prototype is narrower than the true
// ABI are reached through typed function-pointer casts (batch_1030+ convention).
#include "common.h"
#include "generated_decls.h"

// ---- cached level-prop state ----------------------------------------------
extern int    DAT_80612614;   // current level id (into level_get_info_cached)
extern int*   DAT_80612ed8;   // level prop sub-object (level_info + 0x118)
extern float* DAT_80612eec;   // scratch: last-spawned instance record
extern int    DAT_80612ed4;   // prop-TYPE table base (0x158-stride)
extern int    DAT_80612f14;   // prop-TYPE table count

// callees not yet defined anywhere (added locally with the observed arity)
extern int  FUN_80374d50(int a, int b, int c, int d, int e, int f, int g, int h, int soundBankId);
void prop_reindex_after_activate(int owner, int instanceIndex);  // 0x8037338c (defined batch_1041 below)

// raw-ABI helpers whose generated_decls.h prototype is void(void)
typedef int (*SpriteLoadFn)(int, int, int, int, int, int, int, int);
#define sprite_load_texture_set(...)  (((SpriteLoadFn)&sprite_load_texture_set)(__VA_ARGS__))

// ---------------------------------------------------------------------------
// prop_preload_sounds 0x8036f678
// Register (bank-reference) both swarm ambient sound ids that this level uses,
// so they are resident before the level starts.
// ---------------------------------------------------------------------------
void prop_preload_sounds(int levelId)
{
    int sid = (int)flock_sound_id_for_level(levelId, 0);
    sound_clear_bank_ref(sid);
    sid = (int)flock_sound_id_for_level(levelId, 1);
    sound_clear_bank_ref(sid);
}

// ---------------------------------------------------------------------------
// prop_preload_resources 0x8036f6bc   (caller: level_preload_all_resources)
// Resolve the level's prop sub-object, run its optional pre-load hook, then
// bank-load every texture the descriptor references (a 13-strong texture set
// when the +0x60 group is present, plus the +0x04 / +0x0c singles and an
// optional descriptor-supplied loader), a trailing 4-texture set, and the three
// swarm sound banks. Finally hand off to the follow-on loader with the prop
// count from descriptor[1].
// ---------------------------------------------------------------------------
void prop_preload_resources(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    int info = (int)level_get_info_cached(DAT_80612614);
    DAT_80612ed8 = (info == 0) ? (int*)0 : *(int**)(info + 0x118);

    int ctx = a1;
    if (DAT_80612ed8 != (int*)0) {
        if ((void*)DAT_80612ed8[0xb] != (void*)0) {
            ctx = ((int(*)(void))DAT_80612ed8[0xb])();
        }
        int desc = *DAT_80612ed8;
        if (*(int*)(desc + 0x60) != 0) {
            int h = ctx;
            for (int i = 0; i < 13; ++i)   // 13 chained texture-set loads
                h = sprite_load_texture_set(h, a2, a3, a4, a5, a6, a7, a8);
            ctx = h;
        }
        if (*(int*)(desc + 4) != -1)
            ctx = sprite_load_texture_set(ctx, a2, a3, a4, a5, a6, a7, a8);
        if (*(int*)(desc + 0xc) != -1)
            ctx = sprite_load_texture_set(ctx, a2, a3, a4, a5, a6, a7, a8);
        int* loader = *(int**)(desc + 0x48);
        if (loader != (int*)0 && *loader != 0)
            ctx = ((int(*)(void))*loader)();
        int h = ctx;
        for (int i = 0; i < 4; ++i)        // trailing 4-texture set
            h = sprite_load_texture_set(h, a2, a3, a4, a5, a6, a7, a8);
        ctx = h;
        for (int i = 0; i < 3; ++i)        // three swarm sound banks (0x156b..0x156d)
            sound_clear_bank_ref(i + 0x156b);
    }

    int count = (DAT_80612ed8 == (int*)0) ? 0 : DAT_80612ed8[1];
    FUN_80374d50(ctx, a2, a3, a4, a5, a6, a7, a8, count);
}

// ---------------------------------------------------------------------------
// prop_instance_spawn 0x8036f854
// Allocate the next prop-instance record (0x4c stride at level-info +0x0c base,
// index at +0x10), fill its pose (pos xyzw at +0x00 and a copy at +0x3c),
// anim/scale params (+0x10.., +0x1c..), then append its index to the owner's id
// list (owner +0x154 count / +0x12c ids) and advance the running instance count.
// ---------------------------------------------------------------------------
void prop_instance_spawn(double px, double py, double pz, double pw,
                         int recBase, int owner, int ownerSlot,
                         float animT, float sy, float sz)
{
    int idx = *(int*)(DAT_80612ed8 + 0x10);   // next free instance index
    float* rec = (float*)(recBase + idx * 0x4c);
    DAT_80612eec = rec;

    rec[0] = (float)px;  rec[1] = (float)py;  rec[2] = (float)pz;  rec[3] = (float)pw;
    rec[0xf] = (float)px; rec[0x10] = (float)py; rec[0x11] = (float)pz; rec[0x12] = (float)pw;
    rec[4] = animT;
    rec[5] = 0.0f;
    rec[6] = *(float*)(DAT_80612ed8 + 0x10);  // record its own index as a float tag
    rec[7] = sy;
    rec[8] = sz;
    rec[9] = 0.0f; rec[10] = 0.0f; rec[0xb] = 0.0f;

    // register in the owner slot table + owner id list
    *(int*)(owner + ownerSlot * 4 + 0xb4) = *(int*)(DAT_80612ed8 + 0x10);
    int c = *(int*)(owner + 0x154);
    int myIdx = *(int*)(DAT_80612ed8 + 0x10);
    *(int*)(owner + 0x154) = c + 1;
    *(int*)(owner + c * 4 + 0x12c) = myIdx;
    *(int*)(DAT_80612ed8 + 0x10) = *(int*)(DAT_80612ed8 + 0x10) + 1;
}

// ---------------------------------------------------------------------------
// prop_set_scale_vec 0x8036f8fc   write the instance scale vector (+0x24..+0x2c)
// ---------------------------------------------------------------------------
void prop_set_scale_vec(int rec, int sx, int sy, int sz)
{
    *(int*)(rec + 0x24) = sx;
    *(int*)(rec + 0x28) = sy;
    *(int*)(rec + 0x2c) = sz;
}

// ---------------------------------------------------------------------------
// prop_set_field14 0x8036f90c   write the instance flags/param word (+0x14)
// ---------------------------------------------------------------------------
void prop_set_field14(int rec, int value)
{
    *(int*)(rec + 0x14) = value;
}

// ---------------------------------------------------------------------------
// prop_set_pose 0x8036f914
// Write the instance position (x,y) + orientation (z,w,extra) and copy the
// per-type constant from the descriptor (+0x38) into the record's +0x14 slot.
// ---------------------------------------------------------------------------
void prop_set_pose(double x, double y, float* rec, float z, float w, float extra)
{
    rec[0] = (float)x;
    rec[1] = (float)y;
    rec[2] = z;
    rec[3] = w;
    rec[4] = extra;
    rec[5] = *(float*)(*DAT_80612ed8 + 0x38);
}

// ---------------------------------------------------------------------------
// prop_activate_topmost_flagged 0x8036f93c
// Scan the owner's instance id list for the highest-priority (+0x18) instance
// whose flag bit 1 (mask 2) is set, and activate it via FUN_8037338c.
// ---------------------------------------------------------------------------
void prop_activate_topmost_flagged(int owner)
{
    int bestId = -1;
    int bestPri = -1;
    int count = *(int*)(owner + 0x154);
    for (int o = 0; count > 0; --count, o += 4) {
        int id = *(int*)(owner + o + 0x12c);
        int rec = *(int*)(DAT_80612ed8 + 0xc) + id * 0x4c;
        int pri = *(int*)(rec + 0x18);
        if (bestPri < pri && (*(unsigned*)(rec + 0x14) & 2) != 0) {
            bestId = id;
            bestPri = pri;
        }
    }
    if (bestPri != -1)
        prop_reindex_after_activate(owner, bestId);
}

// ---------------------------------------------------------------------------
// prop_find_topmost 0x8036f9c0
// Return the owner's highest-priority (+0x18) instance record pointer.
// ---------------------------------------------------------------------------
int prop_find_topmost(int owner)
{
    int count = *(int*)(owner + 0x154);
    int bestPri = -1;
    int bestId = -1;
    for (int o = 0; count > 0; --count, o += 4) {
        int id = *(int*)(owner + o + 0x12c);
        int pri = *(int*)(*(int*)(DAT_80612ed8 + 0xc) + id * 0x4c + 0x18);
        if (bestPri < pri) {
            bestPri = pri;
            bestId = id;
        }
    }
    return *(int*)(DAT_80612ed8 + 0xc) + bestId * 0x4c;
}

// ---------------------------------------------------------------------------
// prop_owner_has_instance 0x8036fa20
// Return 1 if the owner's id list already contains the given instance id.
// ---------------------------------------------------------------------------
int prop_owner_has_instance(int owner, int instanceId)
{
    int count = *(int*)(owner + 0x154);
    for (int o = 0; count > 0; --count, o += 4) {
        if (*(int*)(owner + o + 0x12c) == instanceId)
            return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// prop_type_foreach 0x8036fa5c
// Iterate the 0x158-stride prop-type table, invoking the supplied callback on
// each entry until it returns non-zero. In the original the callback and its
// context arrive in registers (behind the callee-save prologue); they are
// modelled here as explicit parameters per the compile-clean standard.
// ---------------------------------------------------------------------------
void prop_type_foreach(int (*cb)(int, int), int ctx)
{
    int i = 0, off = 0;
    while (i < DAT_80612f14 && cb(DAT_80612ed4 + off, ctx) == 0) {
        i += 1;
        off += 0x158;
    }
}

// ============================================================================
// Address range: 0x8036fae0 - 0x8036fea4 | batch_1037 | 2026-09-12
//
// Prop mount/possession mechanic. On a handful of levels the props are mountable
// (a player climbs on / into one, e.g. a turret or ridden object). This slice
// adds the manager lifecycle (level-uses-mounts gate, reset, allocate, level
// start) and the mount/dismount interaction driven by a switch/trigger event.
//
// Extra state:
//   DAT_80611324  per-player mount-slot array (2 entries): entity currently
//                 mounted by player N, indexed by player-slot id
//   DAT_80612ee0  count of composite render targets to allocate for the props
//   DAT_805ecc10  the 3 allocated 256x256 render-target slot handles
//   DAT_80612ee4  "a mount happened this frame" latch
//   DAT_80612ed0/edc/ee8/ef0/ef4  misc per-manager scratch cleared on start
//
//   prop_level_uses_mounts    0x8036fae0  1 if the current level id (6/0x11/0x16) has mountable props
//   prop_manager_reset        0x8036fb18  re-resolve the level prop object, clear the type table, run its type-init hook
//   prop_manager_alloc        0x8036fbb4  allocate the 0xac0 prop-type table when mounts are enabled (caller session_alloc_all_systems)
//   prop_manager_level_start  0x8036fbf4  per-level reset: clear slots, allocate the composite render targets, run the level hook (caller level_spawn_all_entities)
//   prop_set_active_slot      0x8036fcf4  set the active render-target slot index (DAT_80612ee0)
//   prop_player_mount         0x8036fcfc  a player mounts a prop on a switch/trigger event (caller worm_event_switch_broadcast)
//   prop_entity_is_mounted    0x8036fe28  1 if the given entity currently occupies a player mount slot
//   prop_owner_slot_record    0x8036fe70  return the prop-instance record referenced by an owner slot
//   prop_player_mount_entity  0x8036fe90  return the entity a given player has mounted
//   prop_player_dismount      0x8036fea4  release a mount: clear the slot, restore player state / camera / model
//
// NOTE: non-matching decomp; struct offsets, the level-id gate values, the
// 0x4c instance stride and the mount-slot side effects match the original.

// ---- additional mount state ----------------------------------------------
extern int    DAT_80611320;    // pending frame counter (init 0xfffffc18)
extern int    DAT_80611324;    // per-player mount-slot array base (2 * u32)
extern int    DAT_80612ed0;    // scratch cleared on level start
extern int    DAT_80612edc;    // scratch cleared on level start
extern int    DAT_80612ee0;    // active/target render-target count (also slot index)
extern int    DAT_80612ee4;    // "mount happened this frame" latch
extern int    DAT_80612ee8;    // scratch cleared on level start
extern int    DAT_80612ef0;    // scratch cleared on level start
extern int    DAT_80612ef4;    // scratch cleared on level start
extern int    DAT_805ecc10;    // 3 composite render-target slot handles
extern float  FLOAT_8061b448, FLOAT_8061b44c, FLOAT_8061b450;
extern void   memset_wrapper(int dst, int val, int len);   // (harmless re-decl; same sig)

// prop-manager helpers not yet defined anywhere
extern void FUN_80375998(int a, int b, int c, int d, int e, int f, int g, int h, int table, int one, int a11, int a12, int a13, int a14, int a15, int a16);  // type-table init
extern void FUN_80374b84(void);      // sub-reset
void prop_mount_camera_fixup(int st, int player);  // 0x803741fc (defined batch_1042 below)
extern int  FUN_803826f0(void);      // scratch enter (returns packed regs)
extern void FUN_8038273c(void);      // scratch leave

// mem_alloc here uses the full 16-arg allocator ABI, not the (s32,s32) decl
typedef int (*MemAlloc16Fn)(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int);
#define mem_alloc(...)  (((MemAlloc16Fn)&mem_alloc)(__VA_ARGS__))

// ---------------------------------------------------------------------------
// prop_level_uses_mounts 0x8036fae0
// Return 1 only on the levels whose props are mountable (ids 6, 0x11, 0x16).
// ---------------------------------------------------------------------------
int prop_level_uses_mounts(void)
{
    if (DAT_80612614 != 0x11) {
        if (DAT_80612614 < 0x11) {
            if (DAT_80612614 != 6) return 0;
        } else if (DAT_80612614 != 0x16) {
            return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// prop_manager_reset 0x8036fb18
// Re-resolve the level prop sub-object, clear the active-slot / type-count
// state, zero the prop-type table, and (when mounts are enabled and a descriptor
// is present) run the descriptor's type-init hook (desc+0x48 -> [1]).
// ---------------------------------------------------------------------------
void prop_manager_reset(void)
{
    int info = (int)level_get_info_cached(DAT_80612614);
    DAT_80612ed8 = (int*)0;
    DAT_80612ee0 = -1;
    DAT_80612f14 = 0;
    if (info != 0) DAT_80612ed8 = *(int**)(info + 0x118);

    if (prop_level_uses_mounts() != 0) {
        memset_wrapper(DAT_80612ed4, 0, 0xac0);
        if (DAT_80612ed8 != (int*)0) {
            DAT_80612ed8[4] = 0;
            int desc = *DAT_80612ed8;
            int hookTbl = *(int*)(desc + 0x48);
            if (hookTbl != 0) {
                int (*hook)(void) = *(int(**)(void))(hookTbl + 4);
                if (hook != (int(*)(void))0) hook();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// prop_manager_alloc 0x8036fbb4   (caller: session_alloc_all_systems)
// When the level has mountable props, allocate the 0xAC0 prop-type table and run
// its one-time initialiser, then reset the manager.
// ---------------------------------------------------------------------------
void prop_manager_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                        int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    if (prop_level_uses_mounts() != 0) {
        DAT_80612ed4 = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0xac0, 1, a11, a12, a13, a14, a15, a16);
        FUN_80375998(a1, a2, a3, a4, a5, a6, a7, a8, DAT_80612ed4, 1, a11, a12, a13, a14, a15, a16);
    }
    prop_manager_reset();
    (void)a9; (void)a10;
}

// ---------------------------------------------------------------------------
// prop_manager_level_start 0x8036fbf4   (caller: level_spawn_all_entities)
// Per-level start: reset the mount slots and manager scratch, clear the 3
// render-target slot handles, allocate DAT_80612ee0 composite 256x256 render
// targets, then run the level prop object's start hook (obj+0x30).
// ---------------------------------------------------------------------------
void prop_manager_level_start(void)
{
    FUN_803826f0();
    DAT_80611320 = -1000;          // 0xfffffc18
    FUN_80374b84();
    DAT_80612edc = 0;
    for (int i = 0; i < 2; ++i) (&DAT_80611324)[i] = 0;   // clear both mount slots
    DAT_80612ef0 = 0;
    DAT_80612ee8 = 0;
    DAT_80612ef4 = 0;
    DAT_80612ed0 = 0;
    for (int i = 0; i < 3; ++i) (&DAT_805ecc10)[i] = -1;  // clear rt handles

    if (DAT_80612ee0 < 0) DAT_80612ee0 = 0;
    for (int i = 0; i < DAT_80612ee0; ++i)
        render_target_alloc_scratch(0x100, 0x100, (int*)((int)&DAT_805ecc10 + i * 4));

    if (DAT_80612ed8 != (int*)0) {
        int (*startHook)(void) = *(int(**)(void))((int)DAT_80612ed8 + 0x30);
        if (startHook != (int(*)(void))0) startHook();
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_set_active_slot 0x8036fcf4   set the active render-target slot index.
// ---------------------------------------------------------------------------
void prop_set_active_slot(int slot)
{
    DAT_80612ee0 = slot;
}

// ---------------------------------------------------------------------------
// prop_player_mount 0x8036fcfc   (caller: worm_event_switch_broadcast)
// A player mounts a prop when a switch/trigger fires. The interacting player
// (iVar3) and the prop entity (iVar1) come in the scratch-enter register pair.
// If the player is not already mounted, the prop accepts it (optional accept
// hook at obj+0x20) and the prop is in state 2, put the player into the mounted
// state (0x2000), record the mount in the slot table, run the mount fixup and
// the prop's on-mount hook, swap out the player model, and activate the prop's
// top flagged instance.
// ---------------------------------------------------------------------------
void prop_player_mount(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826f0();
    int propEnt = (int)(unsigned)(packed >> 0x20);
    int player  = (int)(unsigned)packed;

    int propState = *(int*)(propEnt + 0x7c);
    double t = (double)*(float*)(propEnt + 0x70);

    if (prop_entity_is_mounted(propState) == 0) {
        int accept = 1;
        int (*acceptHook)(int, int) = *(int(**)(int, int))((int)DAT_80612ed8 + 0x20);
        if (acceptHook != (int(*)(int, int))0) accept = acceptHook(propEnt, player);
        if (accept != 0 && *(int*)(*(int*)(player + 0x150) + 0x114) == 2) {
            DAT_80612ee4 = 1;
            t = (double)((float)((double)FLOAT_8061b448 * t) / FLOAT_8061b44c);
            math_poly_trig((double)(float)(t - (double)FLOAT_8061b450));
            math_poly_trig(t);
            player_set_state(*(int*)(player + 0x150), 0x2000);
            *(int*)(propState + 0x14) = *(int*)(player + 0x150);
            prop_mount_camera_fixup(propEnt, player);
            *(int*)(&DAT_80611324 + *(int*)(*(int*)(player + 0x150) + 4) * 4) = propEnt;
            int (*onMount)(int) = *(int(**)(int))(*(int*)(propState + 0x30) + 0x10);
            if (onMount != (int(*)(int))0) onMount(propEnt);
            object_model_swap_out_and_clear(*(int*)(*(int*)(*(int*)(player + 0x150) + 0x14) + 0x7c));
            prop_activate_topmost_flagged(*(int*)(propEnt + 0x7c));
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_entity_is_mounted 0x8036fe28
// Return 1 if any player mount slot currently holds the given prop entity.
// ---------------------------------------------------------------------------
int prop_entity_is_mounted(int propState)
{
    if (propState != 0) {
        for (int i = 0, o = 0; i < 2; ++i, o += 4) {
            int slot = *(int*)(&DAT_80611324 + o);
            if (slot != 0 && *(int*)(slot + 0x7c) == propState) return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// prop_owner_slot_record 0x8036fe70
// Return the prop-instance record (0x4c stride, base level-info+0xc) referenced
// by the owner's slot table (owner + slot*4 + 0xb4).
// ---------------------------------------------------------------------------
int prop_owner_slot_record(int owner, int slot)
{
    return *(int*)(DAT_80612ed8 + 0xc) + *(int*)(owner + slot * 4 + 0xb4) * 0x4c;
}

// ---------------------------------------------------------------------------
// prop_player_mount_entity 0x8036fe90
// Return the prop entity mounted by the given player (indexed by player-slot id).
// ---------------------------------------------------------------------------
int prop_player_mount_entity(int player)
{
    return *(int*)(&DAT_80611324 + *(int*)(player + 4) * 4);
}

// ---------------------------------------------------------------------------
// prop_player_dismount 0x8036fea4
// Release a player's mount: clear the mount slot, put the player back into the
// normal state, restore the camera to its default mode and swap the player model
// back in.
// ---------------------------------------------------------------------------
void prop_player_dismount(int propEnt)
{
    if (propEnt != 0) {
        int propState = *(int*)(propEnt + 0x7c);
        int rider = *(int*)(propState + 0x14);
        *(int*)(&DAT_80611324 + *(int*)(rider + 4) * 4) = 0;
        player_set_state(rider, 2);
        camera_set_mode_by_index(*(int*)(rider + 8), 0, *(int*)(rider + 0x14));
        object_model_swap_in(*(int*)(*(int*)(rider + 0x14) + 0x7c));
    }
}

// ============================================================================
// Address range: 0x8036ff1c - 0x80371348 | batch_1038 | 2026-09-12
//
// Prop on-screen info / speech-panel rendering plus the mounted-player input.
// Some props display a bordered (or fully textured) panel that shows a title and
// a block of wrapped, progressively-revealed dialogue text; this slice draws the
// panel and its 9-slice texture frame, lays out and reveals the text lines,
// advances the reveal timer, plays the random prop voice blips, and handles the
// controls while a player is mounted (fire to advance, other buttons to
// dismount). A few small accessors round it out.
//
//   prop_clear_flag2_run_hook   0x8036ff1c  clear instance flag bit 1 (+0x14) and run the +0x2c callback
//   prop_get_text_color         0x80370008  return the descriptor text colour (obj[0]+0x10)
//   prop_get_level_object       0x80370018  return the level prop object ptr (DAT_80612ed8)
//   prop_scissor_full           0x80370020  set the full 256x256 scissor rect
//   prop_set_scissor_clamped    0x80370050  set a scissor rect, empty if the box is inverted
//   prop_panel_draw             0x803700a0  draw a prop info/speech panel: frame (lines / gradient / 9-slice texture), title, corners
//   prop_panel_advance_text     0x80370e60  advance the scrolling text reveal, or fade the voice out when done
//   prop_panel_draw_lines       0x80370f38  lay out + draw the wrapped dialogue lines and the reveal cursor
//   prop_play_random_voice      0x80371348  play one of 3 random prop voice clips (0x156b..0x156d)
//   prop_mounted_input_update   0x8036ff58  mounted-player input: advance / dismount / cycle (caller player_state_dispatch)
//
// NOTE: non-matching decomp. The panel drawers take their record/owner pointers
// as explicit parameters (register-passed in the original behind the callee-save
// prologue). The 43000000-biased int<->float conversions are written as direct
// casts; scissor/quad/line/text side effects, the 9-slice sprite-id table walk
// (DAT_80495cf0.. stride 0x38) and every draw order match the original.

// ---- extra panel/text state -----------------------------------------------
extern int    DAT_80611d74;      // local player id (for the mounted-input query)
// DAT_80612edc declared as int earlier (batch_1037); used here as a layout cache key
extern int    DAT_80612efc, DAT_80612f00, DAT_80612f04, DAT_80612f08;  // panel text rect
extern int    DAT_80612f0c, DAT_80612f10;   // panel text origin x/y
extern int    DAT_80611928;      // ui flags (bit 3 = draw reveal cursor)
extern int    DAT_80610a20;      // material table base (*ptr; stride 0x58)
extern int    DAT_80495cf0;      // 9-slice sprite-id descriptor table (stride 0x38)
extern int    DAT_80495cf4, DAT_80495cf8, DAT_80495cfc, DAT_80495d00, DAT_80495d04;
extern int    DAT_80495d08, DAT_80495d0c, DAT_80495d10, DAT_80495d14, DAT_80495d18;
extern int    DAT_80495d1c, DAT_80495d20, DAT_80495d24;
extern float  FLOAT_806108ec;
extern float  FLOAT_8061b454, FLOAT_8061b458, FLOAT_8061b45c, FLOAT_8061b460, FLOAT_8061b464;
extern float  FLOAT_8061b468, FLOAT_8061b46c, FLOAT_8061b488, FLOAT_8061b48c;
extern float  FLOAT_80611674;
extern double DOUBLE_8061b480, DOUBLE_8061b490;
extern int    DAT_806109c0;

// panel helpers not in generated_decls.h (text_* are covered by generated_externs)
void prop_panel_draw_bevel(int x0, int y0, unsigned x1, unsigned y1);   // 0x80372dfc (defined batch_1040 below)
void prop_sprite_draw(int cx, int cy, int nameId);                 // 0x803717d0 (defined in batch_1039 below)
void prop_sprite_draw_rect(int a, int b, int nameId, int color, int r);  // 0x803718cc (defined below)
extern void FUN_80374cc4(void);                                    // mount action hook
extern int  FUN_8038b5c8(int str);                                 // measure line count
extern void FUN_8038b4cc(int dst, int str, int len);               // copy N chars

// rng_counter_tick returns in EAX; gx_draw_gradient_rect takes a packed reg pair
typedef unsigned int (*RngTickFnP)(void);
typedef void (*GradRectFn)(double, double, double, double);
#define rng_counter_tick()            (((RngTickFnP)&rng_counter_tick)())
#define gx_draw_gradient_rect(a,b,c,d) (((GradRectFn)&gx_draw_gradient_rect)(a,b,c,d))

// ---------------------------------------------------------------------------
// prop_clear_flag2_run_hook 0x8036ff1c
// Clear the instance flag bit 1 (mask 2) at +0x14 and, if present, run the
// instance callback at +0x2c.
// ---------------------------------------------------------------------------
void prop_clear_flag2_run_hook(int rec)
{
    *(unsigned*)(rec + 0x14) = *(unsigned*)(rec + 0x14) & 0xfffffffd;
    int (*hook)(void) = *(int(**)(void))(rec + 0x2c);
    if (hook != (int(*)(void))0) hook();
}

// ---------------------------------------------------------------------------
// prop_get_text_color 0x80370008   return descriptor text colour (obj[0]+0x10)
// ---------------------------------------------------------------------------
int prop_get_text_color(void)
{
    return *(int*)(*DAT_80612ed8 + 0x10);
}

// ---------------------------------------------------------------------------
// prop_get_level_object 0x80370018   return the level prop object ptr
// ---------------------------------------------------------------------------
int prop_get_level_object(void)
{
    return (int)DAT_80612ed8;
}

// ---------------------------------------------------------------------------
// prop_scissor_full 0x80370020   set the full 256x256 scissor rect
// ---------------------------------------------------------------------------
void prop_scissor_full(void)
{
    gx_set_scissor_rect(0, 0, 0xff, 0xff);
}

// ---------------------------------------------------------------------------
// prop_set_scissor_clamped 0x80370050
// Set a scissor rect; if the box is inverted (x1<x0 or y1<y0) set an empty rect.
// ---------------------------------------------------------------------------
int prop_set_scissor_clamped(int x0, int y0, int x1, int y1)
{
    if (x1 < x0 || y1 < y0) gx_set_scissor_rect(0, 0, 0, 0);
    else                    gx_set_scissor_rect(x0, y0, x1, y1);
    return 1;
}

// ---------------------------------------------------------------------------
// prop_panel_draw 0x803700a0
// Draw a prop info/speech panel. rec is the prop-instance record (rec[0..3] =
// panel rect x0,y0,x1,y1; rec[4] = title string id; rec[5] flags), owner is the
// interacting entity. An open/close animation factor (anim) grows the panel from
// its centre; the descriptor selects a border style: beveled fill, plain lines +
// gradient, or a full 9-slice textured frame (sprite ids from the DAT_80495cf0
// table, stride 0x38). The title string is drawn clipped, and a filled interior
// (colour, composite render target, or a nested fill) completes the panel.
// ---------------------------------------------------------------------------
void prop_panel_draw(float* rec, int owner, double anim)
{
    int obj = *DAT_80612ed8;
    double x0 = (double)rec[0], y0 = (double)rec[1];
    double x1 = (double)rec[2], y1 = (double)rec[3];
    float halfW = (float)(x1 - x0) * FLOAT_8061b454;
    float halfH = (float)(y1 - y0) * FLOAT_8061b454;

    // open/close grow factor
    float grow;
    if ((double)FLOAT_8061b45c <= anim) {
        grow = (float)((double)FLOAT_8061b458 - anim);
    } else {
        grow = (float)((double)*(unsigned*)(owner + 0x50) - DOUBLE_8061b480) /
               (*(float*)(obj + 0x54) * (float)((double)DAT_806109c0 - DOUBLE_8061b480));
    }

    int mode = *(int*)(obj + 0x50);
    if (mode == 2) {
        float fx, fy;
        if (grow <= FLOAT_8061b454) { fy = FLOAT_8061b460 * grow; fx = FLOAT_8061b464; }
        else { fy = FLOAT_8061b458; fx = FLOAT_8061b460 * (grow - FLOAT_8061b454); }
        double dx = (double)(halfW * fx);
        x0 = (double)(float)(x0 + dx); x1 = (double)(float)(x1 - dx);
        y0 = (double)(float)(y0 + (double)(halfH * fy));
        y1 = (double)(float)(y1 - (double)(halfH * fy));
    } else if (mode < 2) {
        if (mode != 0 && mode >= 0) {
            double dx = (double)(halfW * grow);
            x0 = (double)(float)(x0 + dx); x1 = (double)(float)(x1 - dx);
            y0 = (double)(float)(y0 + (double)(halfH * grow));
            y1 = (double)(float)(y1 - (double)(halfH * grow));
        }
    } else if (mode < 4 && grow < FLOAT_8061b468) {
        y1 = -(double)(float)((double)FLOAT_8061b46c * (double)(FLOAT_8061b460 * halfH * grow) - y1);
    }

    int fillMode = *(int*)(obj + 0x40);
    if (fillMode == 1) {
        prop_panel_draw_bevel((int)x0, (int)y0, (unsigned)x1, (unsigned)y1);
    } else if (fillMode < 1 && fillMode >= 0) {
        double lineH = (double)(float)((double)*(unsigned*)(obj + 0x30) - DOUBLE_8061b480);
        if (*(int*)(obj + 0x60) == 0) {
            // plain outline + gradient title bar
            if (*(int*)(obj + 0x1c) != 0)
                gx_draw_gradient_rect(x0, (double)(float)(y0 - lineH), x1, y0);
            int color = *(int*)(*DAT_80612ed8 + 0x14);
            int iy0 = (int)y0, ix0 = (int)x0, ix1 = (int)x1;
            gx_draw_line_counted(ix0, iy0, ix1, iy0, color);
            int iyTop = (int)(y0 - lineH);
            gx_draw_line_counted(ix0, iyTop, ix1, iyTop, color);
            int iy1 = (int)y1;
            gx_draw_line_counted(ix0, iy1, iy1, iy1, color);       // (matches original operand set)
            gx_draw_line_counted(ix0, iyTop, ix0, iy1, color);
            gx_draw_line_counted(ix1, iyTop, ix1, (int)((double)FLOAT_8061b458 + y1), color);
            if (((unsigned)rec[5] & 1) == 0 && (*(unsigned*)(*(int*)(owner + 0x30) + 0x14) & 1) == 0) {
                int ax = (int)((float)(x1 - (double)FLOAT_8061b460) - (float)(lineH * (double)FLOAT_8061b454));
                int ay = (int)((float)(y0 - (double)FLOAT_8061b460) - (float)(lineH * (double)FLOAT_8061b454));
                prop_sprite_draw(ax, ay, 0x388);
            }
            if (rec[4] != 0.0f) {
                int tx = (int)((double)FLOAT_8061b458 + x0);
                int ty = (int)(x1 - (double)FLOAT_8061b458);
                if (prop_set_scissor_clamped(tx, iyTop, ty, iy0) != 0) {
                    unsigned char* s = (unsigned char*)get_string_by_id((int)rec[4]);
                    int savedFont = text_set_font_table(0x1a);
                    text_truncate_ellipsis();
                    text_draw_char((double)(float)((double)FLOAT_8061b460 + x0),
                                   (double)(float)((double)FLOAT_8061b460 + (double)(float)(y0 - lineH)),
                                   s, *(unsigned*)(*DAT_80612ed8 + 0x24));
                    text_set_font_table(savedFont);
                }
            }
        } else {
            // textured 3-piece title bar
            int t = *(int*)(obj + 0x60) * 0x38;
            int mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495cf4 + t));
            int md  = DAT_80610a20 + mat * 0x58;
            gx_set_active_material(mat);
            double topY = (double)(float)(y0 - lineH);
            double midX = (double)(float)(x0 + lineH);
            gx_draw_sprite_quad(x0, topY, midX, y0, 0, (unsigned)*(unsigned short*)(md + 0x52) << 4, (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);
            mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495cfc + t));
            gx_set_active_material(mat);
            double midX2 = (double)(float)(x1 - lineH);
            md = DAT_80610a20 + mat * 0x58;
            gx_draw_sprite_quad(midX, topY, midX2, y0, 0, (unsigned)*(unsigned short*)(md + 0x52) << 4, (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);
            mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495cf8 + t));
            gx_set_active_material(mat);
            md = DAT_80610a20 + mat * 0x58;
            gx_draw_sprite_quad(midX2, topY, x1, y0, 0, (unsigned)*(unsigned short*)(md + 0x52) << 4, (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);
            mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d00 + t));
            md = DAT_80610a20 + mat * 0x58;
            unsigned short cu = *(unsigned short*)(md + 0x52), cv = *(unsigned short*)(md + 0x4e);
            if (((unsigned)rec[5] & 1) == 0 && (*(unsigned*)(*(int*)(owner + 0x30) + 0x14) & 1) == 0) {
                gx_set_active_material(mat);
                gx_draw_sprite_quad(midX2, topY, x1, y0, 0, (unsigned)cu << 4, (unsigned)cv << 4, 0);
            }
            if (rec[4] != 0.0f) {
                if (prop_set_scissor_clamped((int)midX, (int)topY, (int)(x1 - (double)FLOAT_8061b458), (int)y0) != 0) {
                    int savedFont = text_set_font_table(0x1a);
                    unsigned char* s = (unsigned char*)get_string_by_id((int)rec[4]);
                    double th = (double)text_get_char_height(s);
                    text_truncate_ellipsis();
                    text_draw_char((double)(float)((double)FLOAT_8061b460 + midX),
                                   (double)(float)((double)((float)(lineH - th) * FLOAT_8061b454) + topY),
                                   s, *(unsigned*)(*DAT_80612ed8 + 0x24));
                    text_set_font_table(savedFont);
                }
            }
        }
    }

    // interior fill within the (inset) panel rect
    double in = (double)FLOAT_8061b458;
    if (prop_set_scissor_clamped((int)(in + x0), (int)(in + y0), (int)(x1 - in), (int)(y1 - in)) != 0) {
        int o = *DAT_80612ed8;
        int fillColor = *(int*)(o + 0x18);
        if (fillColor != 0) {
            if (*(int*)(o + 0xc) == -1) {
                if (*(int*)(o + 0x60) != 0) {
                    int t = *(int*)(o + 0x60) * 0x38;
                    if (*(int*)(&DAT_80495d04 + t) != 0) {
                        // full 9-slice textured frame around the interior
                        if (*(int*)(&DAT_80495cf0 + t) != 0) {
                            int mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d24 + t));
                            int md = DAT_80610a20 + mat * 0x58;
                            gx_set_active_material(mat);
                            gx_draw_sprite_quad(x0, y0, x1, y1, 0, (unsigned)*(unsigned short*)(md + 0x52) << 4, (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);
                        }
                        // corners + edges (top-left, top-right, bottom-left, bottom-right, etc.)
                        int mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d04 + t));
                        int md = DAT_80610a20 + mat * 0x58;
                        double cw = (double)(float)((double)*(unsigned short*)(md + 0x52) - DOUBLE_8061b480);
                        double ch = (double)(float)((double)*(unsigned short*)(md + 0x4e) - DOUBLE_8061b480);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad(x0, y0, (double)(float)(x0 + ch), (double)(float)(y0 + cw), 0, (unsigned)*(unsigned short*)(md + 0x52) << 4, (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d14 + t));
                        md = DAT_80610a20 + mat * 0x58;
                        unsigned short eu = *(unsigned short*)(md + 0x52), ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad((double)(float)(x0 + ch), y0, (double)(float)(x1 - ch), (double)(float)(y0 + cw), 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d08 + t));
                        md = DAT_80610a20 + mat * 0x58;
                        eu = *(unsigned short*)(md + 0x52); ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad((double)(float)(x1 - ch), y0, x1, (double)(float)(y0 + cw), 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d1c + t));
                        md = DAT_80610a20 + mat * 0x58;
                        eu = *(unsigned short*)(md + 0x52); ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad(x0, (double)(float)(y0 + cw), (double)(float)(x0 + ch), (double)(float)(y1 - cw), 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d0c + t));
                        md = DAT_80610a20 + mat * 0x58;
                        eu = *(unsigned short*)(md + 0x52); ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad(x0, (double)(float)(y1 - cw), (double)(float)(x0 + ch), y1, 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d18 + t));
                        md = DAT_80610a20 + mat * 0x58;
                        eu = *(unsigned short*)(md + 0x52); ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad((double)(float)(x0 + ch), (double)(float)(y1 - cw), (double)(float)(x1 - ch), y1, 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d10 + t));
                        md = DAT_80610a20 + mat * 0x58;
                        double bcw = (double)(float)((double)*(unsigned short*)(md + 0x52) - DOUBLE_8061b480);
                        double bch = (double)(float)((double)*(unsigned short*)(md + 0x4e) - DOUBLE_8061b480);
                        eu = *(unsigned short*)(md + 0x52); ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad((double)(float)(x1 - bcw), (double)(float)(y1 - bch), x1, y1, 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d20 + t));
                        md = DAT_80610a20 + mat * 0x58;
                        eu = *(unsigned short*)(md + 0x52); ev = *(unsigned short*)(md + 0x4e);
                        gx_set_active_material(mat);
                        gx_draw_sprite_quad((double)(float)(x1 - (double)(float)((double)ev - DOUBLE_8061b480)), (double)(float)(y0 + cw), x1, (double)(float)(y1 - cw), 0, (unsigned)eu << 4, (unsigned)ev << 4, 0);

                        if (*(int*)(&DAT_80495cf0 + t) == 0) {
                            mat = (int)sprite_get_texture_id(*(int*)(&DAT_80495d24 + t));
                            md = DAT_80610a20 + mat * 0x58;
                            unsigned short fu = *(unsigned short*)(md + 0x52), fv = *(unsigned short*)(md + 0x4e);
                            gx_set_active_material(mat);
                            gx_draw_sprite_quad((double)(float)(x0 + ch), (double)(float)(y0 + cw), (double)(float)(x1 - bcw), (double)(float)(y1 - bch), 0, (unsigned)fu << 4, (unsigned)fv << 4, 0);
                        }
                        return;
                    }
                }
                gx_fill_rect_clipped((int)x0, (int)y0, (int)x1, (int)y1, fillColor);
            } else {
                prop_sprite_draw_rect(0x80, 0x80, *(int*)(o + 0xc), fillColor, 0x80);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// prop_panel_advance_text 0x80370e60
// Advance the progressive text-reveal timer for the current dialogue block. Once
// the current line's width is exceeded, step to the next line; when the last
// line is reached, register the voice-loop fade-out.
// ---------------------------------------------------------------------------
void prop_panel_advance_text(int rec, unsigned* fadePos)
{
    int* block = *(int**)(rec + 0x24);
    if (block != (int*)0 && *block != 0) {
        if (*(int*)(rec + 0x34) < *block) {
            int w = get_string_by_id(block[*(int*)(rec + 0x34) + 1]);
            w = FUN_8038b5c8(w);
            float lim = FLOAT_8061b458;
            *(float*)(rec + 8) = *(float*)(rec + 8) +
                (FLOAT_8061b48c * FLOAT_806108ec) / (float)((double)(w + 1) - DOUBLE_8061b490);
            if (lim <= *(float*)(rec + 8)) {
                *(int*)(rec + 0x34) += 1;
                *(float*)(rec + 8) = FLOAT_8061b464;
            }
        } else {
            sound_register_fade_out((double)FLOAT_8061b458, (double)FLOAT_8061b488, (int*)(rec + 4), fadePos);
        }
    }
}

// ---------------------------------------------------------------------------
// prop_panel_draw_lines 0x80370f38
// Lay out (once, cached against DAT_80612edc) and draw the wrapped dialogue text
// lines of the current block. The already-revealed lines are drawn in full; the
// currently-revealing line is drawn up to its reveal length, and (when the UI
// cursor flag is set) a small block cursor is drawn after it.
// ---------------------------------------------------------------------------
void prop_panel_draw_lines(int rec)
{
    int* block = *(int**)(rec + 0x24);
    if (block == (int*)0) return;

    // relayout the panel text rect when the block changes
    if (DAT_80612edc != (int)block) {
        if ((*(unsigned*)(*DAT_80612ed8 + 0x34) & 1) == 0) {
            DAT_80612f10 = 4;
            DAT_80612f0c = 4;
        } else {
            int maxW = 0;
            int startY = (int)((float)((double)block[0] - DOUBLE_8061b480) * (FLOAT_8061b460 + FLOAT_80611674) - FLOAT_8061b460);
            int n = 0, o = 0;
            int lineCount = *block;
            for (n = 0; n < lineCount; ++n, o += 4) {
                unsigned char* s = (unsigned char*)get_string_by_id(*(int*)((int)block + o + 4));
                int w = (int)text_get_char_width(s);
                if (maxW < w) maxW = w;
            }
            int h = startY;
            if (h < block[0xc]) h = block[0xc];
            if (maxW < block[0xb]) maxW = block[0xb];
            unsigned a = 0x100 - maxW, b = maxW + 0x100, c = h + 0x100;
            DAT_80612f10 = ((int)a >> 1) + (unsigned)((int)a < 0 && (a & 1) != 0);
            a = 0x100 - h;
            DAT_80612f08 = DAT_80612f10 - 6;
            DAT_80612f0c = ((int)a >> 1) + (unsigned)((int)a < 0 && (a & 1) != 0);
            DAT_80612f04 = DAT_80612f0c - 6;
            DAT_80612f00 = ((int)b >> 1) + (unsigned)((int)b < 0 && (b & 1) != 0) + 6;
            DAT_80612efc = ((int)c >> 1) + (unsigned)((int)c < 0 && (c & 1) != 0) + 6;
        }
        DAT_80612edc = *(int*)(rec + 0x24);
    }

    // draw the fully-revealed lines
    int x = DAT_80612f10;
    int y = DAT_80612f0c;
    int color = *(int*)(*DAT_80612ed8 + 0x10);
    int revealed = *(int*)(rec + 0x34);
    int o = 0;
    for (int i = 0; i < revealed; ++i, o += 4) {
        unsigned char* s = (unsigned char*)get_string_by_id(*(int*)(*(int*)(rec + 0x24) + o + 4));
        text_draw_char((double)x, (double)y, s, color);
        y = (int)((float)y + (FLOAT_8061b460 + FLOAT_80611674));
    }

    // draw the currently-revealing line up to its reveal length + optional cursor
    if (revealed < **(int**)(rec + 0x24)) {
        int full = get_string_by_id((*(int**)(rec + 0x24))[revealed + 1]);
        full = FUN_8038b5c8(full);
        int shown = (int)(*(float*)(rec + 8) * (float)((double)full - DOUBLE_8061b490));
        int strId = get_string_by_id(*(int*)(*(int*)(rec + 0x24) + *(int*)(rec + 0x34) * 4 + 4));
        unsigned char buf[256];
        FUN_8038b4cc((int)buf, strId, shown);
        buf[shown] = 0;
        // cursor x: end of the drawn text (or start if all blank)
        int curX = x;
        for (int k = shown - 1, ci = 0; k > 0; --k, ++ci) {
            if (buf[ci] != 0x20 && buf[ci] != 0) {
                float tw = text_get_char_width(buf);
                curX = (int)((double)x + (double)tw);
                break;
            }
        }
        text_draw_char((double)x, (double)y, buf, color);
        int cursorY = (int)((float)y + FLOAT_8061b460 + FLOAT_80611674);
        if ((DAT_80611928 >> 3 & 1) != 0) {
            int cy0 = (int)((float)cursorY - FLOAT_80611674);
            int cy1 = (int)((float)cy0 + FLOAT_80611674);
            gx_fill_rect_clipped(curX, cy0, curX + 6, cy1, color);
        }
    }
}

// ---------------------------------------------------------------------------
// prop_play_random_voice 0x80371348
// Play one of the three random prop "voice" blips (sound ids 0x156b..0x156d).
// ---------------------------------------------------------------------------
void prop_play_random_voice(void)
{
    unsigned r = rng_counter_tick();
    int params[13];
    sound_init_play_params((int)params);
    params[0] = (int)(r % 3) + 0x156b;
    sound_play_request(params);
}

// ---------------------------------------------------------------------------
// prop_mounted_input_update 0x8036ff58   (caller: player_state_dispatch)
// While a player is mounted on a prop, read the held buttons: on the configured
// action button either run the mount action hook (when the prop's +0x70 state is
// live and flag bit 1 is clear), advance the panel (clear flag2 + activate the
// next flagged instance), or dismount when the flag is already set.
// ---------------------------------------------------------------------------
void prop_mounted_input_update(void)
{
    FUN_803826f0();
    int propEnt = prop_player_mount_entity(DAT_80611d74);
    int propState = *(int*)(propEnt + 0x7c);
    int* rider = *(int**)(propState + 0x14);
    int held = input_get_buttons_held(*rider);
    if (rider[9] == held) {
        unsigned f = *(unsigned*)(*(int*)(propState + 0x30) + 0x14);
        if ((f & 1) == 0) {
            if ((f & 2) == 0 && *(int*)(*(int*)(propEnt + 0x7c) + 0x70) != 0) {
                FUN_80374cc4();
            } else if ((*(unsigned*)((int)DAT_80612eec + 0x14) & 1) == 0) {
                prop_clear_flag2_run_hook((int)DAT_80612eec);
                prop_activate_topmost_flagged(*(int*)(propEnt + 0x7c));
            } else {
                prop_player_dismount(propEnt);
            }
        }
    }
    FUN_8038273c();
}
// ============================================================================
// Address range: 0x80371398 - 0x80371d68 | batch_1039 | 2026-09-12
//
// Continuation of the level prop / speech-panel module. This slice covers the
// per-instance text-reveal cursor animation, the reveal-cursor glyph draw, the
// prop "icon" open/close scale animation and its threshold hook, the low-level
// prop sprite draws (single quad + composite render-target blit), the per-frame
// scale decay, the style-driven sprite renderer (descriptor +0x58 selects a
// 1/2/4-way draw), and the on-screen cull / visibility tests against the panel
// scratch rect (DAT_80612eec).
//
//   prop_panel_reveal_tick     0x80371398  advance the fractional reveal cursor (+0x48); on a
//                                          line change play a voice blip and reset the timer
//   prop_panel_draw_cursor     0x80371568  draw the current reveal line with a trailing '_' caret
//   prop_icon_anim_tick        0x80371618  grow/shrink the icon open/close factor (+0x60 target, +0x64 value)
//   prop_icon_run_hook         0x80371760  run the descriptor +0x4c hook once the icon is open past a threshold
//   prop_sprite_draw           0x803717d0  draw one prop sprite as a centred textured quad
//   prop_sprite_draw_rect      0x803718cc  draw one prop sprite as an axis-aligned rect of half-size +/-r
//   prop_scale_decay           0x80371958  reset (+0x54=const) or decay it by descriptor +0x5c over N frames
//   prop_draw_instance_sprites 0x803719a0  style-driven sprite draw (descriptor +0x58: 0/1/2/3 layouts)
//   prop_cull_test             0x80371cd8  return the panel scratch rect iff the instance is inside it, else 0
//   prop_instance_visible      0x80371d68  visible iff owner link ok, >2 refs, and passes the cull test
//
// NOTE: non-matching decomp. The GameCube callee-save prologue spill helpers
// (FUN_803826e8 save / FUN_80382734 restore) are kept as opaque scratch markers,
// matching the batch_1037/1038 convention; FUN_803826e8 hands back the packed
// (instance-record, self-id) pair the iterator passes in. The +0x48/+0x60/+0x64
// animation fields, the descriptor +0x58 style / +0x5c decay / +0x4c hook, the
// 0x156b voice ids and the DAT_80612eec panel rect all match the original.
// ---------------------------------------------------------------------------

// packed (instance-record ptr : high, self id : low) scratch enter/leave
extern unsigned long long FUN_803826e8(void);   // save r27..r31, return packed ctx
extern void   FUN_80382734(void);                // restore r27..r31
extern float  FLOAT_8061b498, FLOAT_8061b4a0, FLOAT_8061b49c, FLOAT_8061b4a4;
extern int    DAT_806108e4;                      // scale-decay frame count
extern void   FUN_8038b510(int* dst, int* str);  // expand string into a byte buffer

// ---------------------------------------------------------------------------
// prop_panel_reveal_tick 0x80371398
// Advance the progressive text-reveal cursor for the mounted prop's panel. When
// nothing is being shown (line count 0) or no line is selected yet, latch the
// self id into +0x40 and clear the cursor. Otherwise clamp the target character
// index to the measured line length, and while the prop is mounted (and either
// no dialogue is queued or its timer < 0x7d1) step the +0x48 fractional cursor
// up (this line still selected) or back down (line changed) by a random rate,
// playing a voice blip on each line change.
// ---------------------------------------------------------------------------
void prop_panel_reveal_tick(void)
{
    unsigned long long ctx = FUN_803826e8();
    int rec = (int)(ctx >> 0x20);       // prop-instance record
    int selfId = (int)ctx;              // this instance's id / address

    int lineCount = (*(int**)(rec + 0x24) == (int*)0) ? 0 : **(int**)(rec + 0x24);

    if (*(int*)(rec + 0x34) < lineCount || *(int*)(rec + 0x40) == -1) {
        *(int*)(rec + 0x40) = selfId;
        *(int*)(rec + 0x44) = 0;
    } else {
        int str = get_string_by_id(*(int*)(rec + 0x40));
        int len = FUN_8038b5c8(str);
        int target = (int)*(float*)(rec + 0x48) - 5;
        if (target < 0)            target = 0;
        else if (len + 1 < target) target = len + 1;

        if (prop_entity_is_mounted(rec) != 0 &&
            (*(int*)(*DAT_80612ed8 + 0x48) == 0 || *(int*)(rec + 0x20) < 0x7d1)) {
            if (target == *(int*)(rec + 0x44)) {
                if (target == 0)
                    *(int*)(rec + 0x40) = selfId;
            } else {
                prop_play_random_voice();
                *(int*)(rec + 0x44) = target;
                *(int*)(rec + 0x20) = 0;
            }
            float step = FLOAT_806108ec * FLOAT_8061b48c *
                         ((float)((double)(unsigned)rng_counter_tick() - DOUBLE_8061b490) / FLOAT_8061b498);
            if (*(int*)(rec + 0x40) == selfId)
                *(float*)(rec + 0x48) = *(float*)(rec + 0x48) + step;
            else
                *(float*)(rec + 0x48) = *(float*)(rec + 0x48) - step;
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// prop_panel_draw_cursor 0x80371568
// Draw the currently-revealing panel line with a trailing underscore caret at
// the reveal position (+0x44). Uses font table 0x1b for the caret pass and
// restores the previous font afterwards.
// ---------------------------------------------------------------------------
void prop_panel_draw_cursor(double x, double y, int rec)
{
    int prevFont = text_set_font_table(0x1b);
    if (-1 <= *(int*)(rec + 0x40)) {
        int caretPos = *(int*)(rec + 0x44);
        int* str = (int*)get_string_by_id(*(int*)(rec + 0x40));
        unsigned char buf[64];
        FUN_8038b510((int*)buf, str);
        buf[caretPos]     = 0x5f;   // '_'
        buf[caretPos + 1] = 0;
        text_draw_char(x, y, buf, *(int*)(*DAT_80612ed8 + 0x10));
    }
    text_set_font_table(prevFont);
}

// ---------------------------------------------------------------------------
// prop_icon_anim_tick 0x80371618
// Animate a prop "icon" open/close factor. When the instance references a live
// child (+0x2c -> +8 >= 0), latch its handle (+0x0c) into +0x60 as the open
// target. While a target is set, ease the +0x64 value up toward it (snapping to
// 0x7f when fully open) or, once the target is cleared, ease it back down and
// reset once it drops below the low threshold.
// ---------------------------------------------------------------------------
void prop_icon_anim_tick(int rec)
{
    int child = *(int*)(rec + 0x2c);
    bool live = (child != 0 && -1 < *(int*)(child + 8));
    int handle = live ? *(int*)(child + 0xc) : 0;

    if (handle != 0 && *(int*)(rec + 0x60) == 0)
        *(int*)(rec + 0x60) = handle;

    if (*(int*)(rec + 0x60) != 0) {
        if (*(int*)(rec + 0x60) == handle) {
            float v = FLOAT_8061b49c * FLOAT_806108ec +
                      (float)((double)(unsigned)*(unsigned*)(rec + 0x64) - DOUBLE_8061b480);
            *(int*)(rec + 0x64) = (int)v;
            if (FLOAT_8061b4a0 <= (float)((double)(unsigned)*(unsigned*)(rec + 0x64) - DOUBLE_8061b480))
                *(int*)(rec + 0x64) = 0x7f;
        } else {
            float v = -(FLOAT_8061b49c * FLOAT_806108ec -
                        (float)((double)(unsigned)*(unsigned*)(rec + 0x64) - DOUBLE_8061b480));
            *(int*)(rec + 0x64) = (int)v;
            if ((float)((double)(unsigned)*(unsigned*)(rec + 0x64) - DOUBLE_8061b480) < FLOAT_8061b464) {
                *(int*)(rec + 0x60) = 0;
                *(int*)(rec + 0x64) = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// prop_icon_run_hook 0x80371760
// Once the icon has opened past FLOAT_8061b464 (and the descriptor supplies a
// +0x4c hook), fire the hook.
// ---------------------------------------------------------------------------
void prop_icon_run_hook(int rec)
{
    void (*hook)(void) = *(void(**)(void))(*DAT_80612ed8 + 0x4c);
    if (hook != (void(*)(void))0 && *(int*)(rec + 0x60) != 0) {
        if (FLOAT_8061b464 < (float)((double)(unsigned)*(unsigned*)(rec + 0x64) - DOUBLE_8061b480))
            hook();
    }
}

// ---------------------------------------------------------------------------
// prop_sprite_draw 0x803717d0
// Draw one prop sprite as a screen-space textured quad centred on the current
// scratch position (+/- the half-size carried in the callee-save spill), using
// the sprite's texture id and its material-table UV extents (+0x4e/+0x52).
// ---------------------------------------------------------------------------
void prop_sprite_draw(int cx, int cy, int nameId)
{
    // half-size the original reads from the incoming callee-save reg
    float half = FLOAT_8061b458;
    int tex = sprite_get_texture_id(nameId);
    gx_set_active_material(tex);
    int mat = *(int*)DAT_80610a20 + tex * 0x58;
    gx_draw_sprite_quad((double)((float)cx - half), (double)((float)cy - half),
                        (double)((float)cx + half), (double)((float)cy + half), 0,
                        (unsigned)*(unsigned short*)(mat + 0x52) << 4,
                        (unsigned)*(unsigned short*)(mat + 0x4e) << 4, 0);
}

// ---------------------------------------------------------------------------
// prop_sprite_draw_rect 0x803718cc
// Draw one prop sprite as an axis-aligned rect of half-extent r about the
// current scratch position (composite render-target blit).
// ---------------------------------------------------------------------------
void prop_sprite_draw_rect(int a, int b, int nameId, int color, int r)
{
    (void)a; (void)b; (void)color;
    unsigned long long ctx = FUN_803826e8();
    int cx = (int)(ctx >> 0x20);
    int cy = (int)ctx;
    int tex = sprite_get_texture_id(nameId);
    gx_set_active_material(tex);
    int mat = *(int*)DAT_80610a20 + tex * 0x58;
    gx_draw_sprite_rect(cx - r, cy - r, cx + r, cy + r, 0,
                        (unsigned)*(unsigned short*)(mat + 0x52) << 4,
                        (unsigned)*(unsigned short*)(mat + 0x4e) << 4, 0);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// prop_scale_decay 0x80371958
// Reset the instance's per-frame scale (+0x54) to the constant when forced,
// otherwise multiply it by the descriptor decay factor (+0x5c) once per elapsed
// frame (DAT_806108e4 frames this tick).
// ---------------------------------------------------------------------------
void prop_scale_decay(int rec, int reset)
{
    if (reset != 0) {
        *(float*)(rec + 0x54) = FLOAT_8061b458;
        return;
    }
    for (int i = 0; i < DAT_806108e4; ++i)
        *(float*)(rec + 0x54) = *(float*)(rec + 0x54) * *(float*)(*DAT_80612ed8 + 0x5c);
}

// ---------------------------------------------------------------------------
// prop_draw_instance_sprites 0x803719a0
// Draw the sprites for one prop instance, offset by the scratch position, once
// the instance's +0x54 scale has grown past the visibility threshold. The
// descriptor style word (+0x58) selects the layout:
//   0/1  : one or two stacked draws
//   2    : single centred draw
//   3    : a plus/cross of five draws using sub-material id 2
// ---------------------------------------------------------------------------
void prop_draw_instance_sprites(int ox, int oy, int index)
{
    unsigned long long ctx = FUN_803826e8();
    float* off = (float*)ctx;                         // scratch xyz offset (+0x00..)
    float* rec = (float*)(DAT_80612ed8[3] + index * 0x4c);  // instance record
    (void)ox; (void)oy;

    if (FLOAT_8061b464 <= FLOAT_8061b4a4 * *(float*)((int)(ctx >> 0x20) + 0x54) - FLOAT_8061b48c) {
        int style = *(int*)(*DAT_80612ed8 + 0x58);
        int x = (int)(rec[0] + off[0]);
        int y = (int)(rec[1] + off[1]);
        int name = (int)off[2];
        if (style == 2) {
            prop_sprite_draw(x, y, name);
        } else if (style < 2) {
            if (style != 0 && -1 < style) {
                prop_sprite_draw(x, y, name);
                prop_sprite_draw(x, y, name);
            }
        } else if (style < 4) {
            float s = FLOAT_8061b458;
            prop_sprite_draw(x, y, name);
            int prev = gx_set_sub_material_id(2);
            prop_sprite_draw((int)((float)x + s), y, name);
            prop_sprite_draw((int)((float)x - s), y, name);
            prop_sprite_draw(x, (int)((float)y + s), name);
            prop_sprite_draw(x, (int)((float)y - s), name);
            gx_set_sub_material_id(prev);
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// prop_cull_test 0x80371cd8
// Return the panel scratch rect (DAT_80612eec) iff the instance's screen point
// (+0x38 x, +0x3c y) lies inside it (the y compare is relative to the panel's
// scrolled origin), else 0.
// ---------------------------------------------------------------------------
float* prop_cull_test(int rec)
{
    float* rect = DAT_80612eec;
    float yTop = DAT_80612eec[1] -
                 (float)((double)(unsigned)*(unsigned*)(*DAT_80612ed8 + 0x30) - DOUBLE_8061b480);
    if (*(float*)(rec + 0x38) < DAT_80612eec[0]  ||
        DAT_80612eec[2] < *(float*)(rec + 0x38)  ||
        *(float*)(rec + 0x3c) < yTop             ||
        DAT_80612eec[3] < *(float*)(rec + 0x3c))
        rect = (float*)0;
    return rect;
}

// ---------------------------------------------------------------------------
// prop_instance_visible 0x80371d68
// Visible iff the owner referenced enough live entries (>2 references at
// owner+0x10), the instance passes the on-screen cull test, and the owner link
// (+0x28) is consistent with the active link (+0x2c).
// ---------------------------------------------------------------------------
int prop_instance_visible(int rec)
{
    int vis = 0;
    if (1 < *(int*)(*(int*)(rec + 0x28) + 0x10) &&
        prop_cull_test(rec) != (float*)0 &&
        2 < *(int*)(*(int*)(rec + 0x28) + 0x10))
        vis = 1;
    if (vis != 0 && *(int*)(rec + 0x2c) != 0 &&
        *(int*)(rec + 0x28) != *(int*)(rec + 0x2c))
        vis = 0;
    return vis;
}
// ============================================================================
// Address range: 0x80371df4 - 0x80372eb0 | batch_1040 | 2026-09-12
//
// The per-frame update core of the mounted prop / speech-panel system. The
// driver (prop_update_driver) runs each frame while a player is mounted and
// fans out to: the neighbour drag/attach scan, the master input tick (analog
// aim + button handling + panel sound + descriptor hooks), the topmost-owner
// target scan, and the wired-entity HUD charge pass. Two small helpers draw the
// beveled panel frame.
//
//   prop_neighbour_drag_scan   0x80371df4  scan sibling instances by priority; drag/attach/select by style
//   prop_check_activate_region 0x80372168  mode-3 pointer-in-region test -> run hook / activate next
//   prop_link_update           0x80372268  per-style link update: charge, clamp target box, or relink
//   prop_panel_input_tick      0x8037241c  master input: analog aim, button, panel sound, per-instance hooks
//   prop_panel_should_autoclose 0x80372828 gate: timer > 0x7d0 and a queued dialogue present
//   prop_target_neighbour_scan 0x80372854  pick the in-range topmost-owner entry as the active target
//   prop_wire_charge_targets   0x80372a98  charge the HUD prompt bar for in-range, line-of-sight wired entities
//   prop_update_driver         0x80372d28  per-frame driver: decay timer, input tick, target scan, anim, wires
//   prop_panel_draw_bevel      0x80372dfc  draw a fixed-colour beveled panel frame
//   prop_panel_draw_bevel_col  0x80372eb0  draw a beveled panel frame in a supplied colour
//
// NOTE: non-matching decomp. Callee-save int spill (FUN_803826dc/e4/e8 save,
// FUN_80382728/30/34 restore) kept as opaque scratch markers; the packed return
// gives the (query-entity, screen-pos) pair the iterator hands in. Float
// callee-save spill (in_f29..in_f31 / in_psNN_1) is expressed as the concrete
// locals the original reloads. scene_raycast / anim_play_scaled are reached
// through typed casts where the generated prototype differs from the ABI. All
// struct offsets, the 0x4c instance / 0x18 owner-entry strides, the +0x14 flags,
// +0x28/+0x2c link fields and every sound/hook side effect match the original.
// ---------------------------------------------------------------------------

void prop_set_drag_cooldown(int st);                     // 0x80373c5c (defined batch_1042 below)
extern int    FUN_80375f18(int ent, unsigned* outFlag);   // panel-suppressed predicate
extern int    FUN_80374bf0(int propState);                // custom per-frame update hook
extern int    DAT_80611d90;                               // session player-record table base
extern int    DAT_80611d78;                               // HUD per-player context table base
extern int    hud_prompt_bar_charge_slot(int player, int slotCtx, float amount);
extern int    anim_is_clip_active(int animCtx, int clipId);
extern float  FLOAT_8061b4b0, FLOAT_8061b4b4, FLOAT_8061b4b8, FLOAT_8061b4bc;
extern float  FLOAT_8061b4c0, FLOAT_8061b4c4, FLOAT_8061b4c8, FLOAT_8061b4cc;
// int-spill scratch enter/leave variants (distinct save-mask entry points)
extern unsigned long long FUN_803826dc(void);
extern unsigned long long FUN_803826e4(void);
extern void   FUN_80382728(void);
extern void   FUN_80382730(void);
// GameCube gpr save/restore thunks; here they carry the packed (x:hi, y:lo) pos
extern unsigned long long __save_gpr_28(void);
extern void   __restore_gpr_28(void);
// scene_raycast really returns a hit flag (generated proto is void)
typedef int (*RaycastFn)(int, float*, float*, int, float*, float*, int*, unsigned, unsigned, int*);
#define raycast_hit(...)  (((RaycastFn)&scene_raycast)(__VA_ARGS__))
// anim_play_scaled ABI here takes two doubles
typedef void (*AnimPlayFn)(double, double);
#define prop_anim_play(a,b)  (((AnimPlayFn)&anim_play_scaled)((a),(b)))

// ---------------------------------------------------------------------------
// prop_neighbour_drag_scan 0x80371df4
// Walk the instance table in descending priority. For every flagged (+0x14 bit1)
// instance owned by the query entity, the style word (self id, low half of the
// scratch pair) drives the behaviour: style 2 drags the instance box to follow
// the pointer while inside it; style 0/1 clears the drag latch (+0x30); style 3
// hit-tests the pointer against the box edges to activate the next panel entry
// or begin a drag. Stops on the first instance that consumed the pointer.
// ---------------------------------------------------------------------------
void prop_neighbour_drag_scan(void)
{
    unsigned long long ctx = FUN_803826dc();
    int qent  = (int)(ctx >> 0x20);   // query entity (screen pos at +0x38/+0x3c)
    int style = (int)ctx;
    bool done = false;
    int prio = DAT_80612ed8[4];

    while (--prio >= 0) {
        int off = 0;
        for (int i = 0; i < DAT_80612ed8[4]; ++i) {
            if (prio == *(int*)(DAT_80612ed8[3] + off + 0x18) &&
                (*(unsigned*)(DAT_80612ed8[3] + off + 0x14) & 2) != 0 &&
                prop_owner_has_instance(qent, i) != 0) {
                float* box = (float*)(DAT_80612ed8[3] + off);
                if (style == 2) {
                    if (box[0xc] != 0.0f) {
                        float dx = *(float*)(qent + 0x38) - box[0xd];
                        float dy = *(float*)(qent + 0x3c) - box[0xe];
                        float scrollY = (float)((double)(unsigned)*(unsigned*)(*DAT_80612ed8 + 0x30) - DOUBLE_8061b480);
                        if (FLOAT_8061b464 <= box[0] + dx && box[2] + dx < FLOAT_8061b4b4 &&
                            scrollY < box[1] + dy && box[3] + dy < FLOAT_8061b4b4) {
                            box[0] += dx; box[2] += dx;
                            box[1] += dy; box[3] += dy;
                        }
                        box[0xd] = *(float*)(qent + 0x38);
                        box[0xe] = *(float*)(qent + 0x3c);
                    }
                    done = true;
                } else if (style < 2) {
                    if (style > 0 && box[0xc] != 0.0f)
                        box[0xc] = 0.0f;
                } else if (style < 4) {
                    float scrollY = (float)((double)(unsigned)*(unsigned*)(*DAT_80612ed8 + 0x30) - DOUBLE_8061b480);
                    float px = *(float*)(qent + 0x38);
                    float py = *(float*)(qent + 0x3c);
                    if (box[0] < px && px < box[2] &&
                        box[1] - scrollY < py && py < box[3]) {
                        prop_reindex_after_activate(qent, i);
                        done = true;
                    }
                    float edge = box[2] - FLOAT_8061b460;
                    if (edge - scrollY < px && px < edge) {
                        if (FLOAT_8061b460 + (box[1] - scrollY) < py &&
                            py < box[1] - FLOAT_8061b460 &&
                            ((unsigned)box[5] & 1) == 0 &&
                            (*(unsigned*)(*(int*)(qent + 0x30) + 0x14) & 1) == 0) {
                            prop_clear_flag2_run_hook((int)box);
                            prop_set_drag_cooldown(qent);
                            prop_activate_topmost_flagged(qent);
                            done = true;
                            goto after;
                        }
                    }
                    if (box[0] < px && px < box[2]) {
                        if (box[1] - scrollY < py && py < box[1]) {
                            box[0xd] = px;
                            box[0xe] = py;
                            box[0xc] = 1.4013e-45f;   // 1 as float bits
                            done = true;
                        }
                    }
                }
            }
        after:
            if (done) break;
            off += 0x4c;
        }
        if (done) break;
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// prop_check_activate_region 0x80372168
// For a style-3 instance, test the query entity's pointer against the box's
// bottom edge strip; when inside (and neither the box nor the entity is locked)
// either run the mount action hook or clear+activate the next panel entry.
// ---------------------------------------------------------------------------
void prop_check_activate_region(int qent, int style, int box)
{
    if (style != 3) return;
    float scrollY = (float)((double)(unsigned)*(unsigned*)(*DAT_80612ed8 + 0x30) - DOUBLE_8061b480);
    float edge = *(float*)(box + 8) - FLOAT_8061b460;
    if (edge - scrollY < *(float*)(qent + 0x38) && *(float*)(qent + 0x38) < edge) {
        if (FLOAT_8061b460 + (*(float*)(box + 4) - scrollY) < *(float*)(qent + 0x3c) &&
            *(float*)(qent + 0x3c) < *(float*)(box + 4) - FLOAT_8061b460 &&
            (*(unsigned*)(box + 0x14) & 1) == 0 &&
            (*(unsigned*)(*(int*)(qent + 0x30) + 0x14) & 1) == 0) {
            if (*(int*)(qent + 0x70) == 0) {
                prop_clear_flag2_run_hook(box);
                prop_set_drag_cooldown(qent);
            } else {
                FUN_80374cc4();
            }
            prop_activate_topmost_flagged(qent);
        }
    }
}

// ---------------------------------------------------------------------------
// prop_link_update 0x80372268
// Per-style bookkeeping for the mounted prop's link (+0x28 candidate / +0x2c
// active). Style 2 charges the +0x1c dwell timer (clamped to 0x14). Style 0/1
// either fires the descriptor +0x14 callback (charged and stable link), or when
// the candidate has >1 ref writes the entity screen pos into the linked box and
// clamps it inside the on-screen cull rect; then it clears the candidate + dwell.
// Style 3 promotes the active link to the candidate slot.
// ---------------------------------------------------------------------------
void prop_link_update(int qent, int style)
{
    int st = *(int*)(qent + 0x7c);
    if (style == 2) {
        if (*(int*)(st + 0x28) != 0 && 0 < *(int*)(*(int*)(st + 0x28) + 0x10)) {
            *(int*)(st + 0x1c) += DAT_806108e4;
            if (0x14 < *(int*)(st + 0x1c))
                *(int*)(st + 0x1c) = 0x14;
        }
    } else if (style < 2) {
        if (style != 0) {
            if (style < 0) return;
            int cand = *(int*)(st + 0x28);
            if (cand != 0) {
                if (*(int*)(st + 0x1c) < 0x14 && cand == *(int*)(st + 0x2c)) {
                    void (*cb)(int, int) = *(void(**)(int, int))((int)DAT_80612ed8 + 0x14);
                    if (cb != (void(*)(int,int))0)
                        cb(qent, *(int*)(cand + 0xc));
                } else if (1 < *(int*)(cand + 0x10)) {
                    float* rect = prop_cull_test(st);
                    int vis = prop_instance_visible(st);
                    float pad = FLOAT_8061b4b8;
                    if (vis != 0) {
                        int link = *(int*)(st + 0x28);
                        *(int*)(link + 0) = *(int*)(st + 0x38);
                        *(int*)(link + 4) = *(int*)(st + 0x3c);
                        if (*(float*)link < pad + rect[0])            *(float*)link = pad + rect[0];
                        if (rect[2] - pad < *(float*)link)            *(float*)link = rect[2] - pad;
                        if (*(float*)(link + 4) < pad + rect[1])      *(float*)(link + 4) = pad + rect[1];
                        if (rect[3] - pad < *(float*)(link + 4))      *(float*)(link + 4) = rect[3] - pad;
                    }
                }
                *(int*)(st + 0x28) = 0;
            }
        }
        *(int*)(st + 0x1c) = 0;
    } else if (style < 4) {
        *(int*)(st + 0x28) = *(int*)(st + 0x2c);
    }
}

// ---------------------------------------------------------------------------
// prop_panel_input_tick 0x8037241c
// The master per-frame input handler for the mounted panel. Reads the action
// button, moves the on-screen pointer by the analog stick (clamped to the panel
// rect), plays the open/close panel sound on a state change, fires the
// descriptor +0x30 dialogue hook and the +0x48 timeout hook, then dispatches to
// every flagged instance's per-frame callback plus the drag scan, icon anim,
// text advance and link update. Finally advances the panel dwell timer.
// ---------------------------------------------------------------------------
void prop_panel_input_tick(void)
{
    int qent = (int)FUN_803826e8();
    unsigned pressed = 0;
    int st = *(int*)(qent + 0x7c);
    int* rider = *(int**)(st + 0x14);
    if (rider != (int*)0 && rider[8] == (int)input_get_buttons_pressed(*rider))
        pressed = 1;

    if (DAT_806108e4 == 0) goto advance;
    {
        unsigned action = 0;
        if (rider != (int*)0 && rider[0x45] != 0x80 && rider[0x45] != 0x8000 &&
            (*(unsigned*)(*(int*)(st + 0x30) + 0x14) & 4) == 0 &&
            FUN_80375f18(qent, &action) == 0 &&
            qent == *(int*)(&DAT_80611324 + rider[1] * 4) && rider != (int*)0) {
            double ax = input_get_analog_value(*rider, 1);
            double ay = input_get_analog_value(*rider, 2);
            if ((double)FLOAT_8061b4bc <= ABS(ax) || (double)FLOAT_8061b4bc <= ABS(ay)) {
                *(float*)(st + 0x38) = FLOAT_8061b4c0 * (float)((double)FLOAT_806108ec * ax) + *(float*)(st + 0x38);
                *(float*)(st + 0x3c) = -(FLOAT_8061b4c0 * (float)((double)FLOAT_806108ec * ay) - *(float*)(st + 0x3c));
                *(int*)(st + 0x20) = 0;
            }
            if (*(float*)(st + 0x38) < FLOAT_8061b4b0) *(float*)(st + 0x38) = FLOAT_8061b4b0;
            if (FLOAT_8061b4c4 < *(float*)(st + 0x38)) *(float*)(st + 0x38) = FLOAT_8061b4c4;
            if (*(float*)(st + 0x3c) < FLOAT_8061b4b0) *(float*)(st + 0x3c) = FLOAT_8061b4b0;
            if (FLOAT_8061b4c4 < *(float*)(st + 0x3c)) *(float*)(st + 0x3c) = FLOAT_8061b4c4;
            action = pressed;
            if (DAT_80612ee4 != 0) {
                if (pressed == 0) DAT_80612ee4 = 0;
                action = 0;
            }
        }

        int soundState;
        if (*(unsigned short*)(st + 0x18) == action) {
            soundState = (action != 0) ? 2 : 0;
        } else {
            int sid = (int)flock_sound_id_for_level((unsigned)*(unsigned short*)(qent + 0x94), action);
            int params[13];
            sound_init_play_params((int)params);
            *(char*)((char*)params + 0x24) = 1;
            params[3] = *(int*)(qent + 0x64);
            params[4] = *(int*)(qent + 0x68);
            params[5] = *(int*)(qent + 0x6c);
            params[0] = sid;
            sound_play_request(params);
            soundState = (action != 0) ? 3 : 1;
            *(short*)(st + 0x18) = (short)action;
        }

        if (2000 < *(int*)(st + 0x20) && *(int*)(*DAT_80612ed8 + 0x48) != 0 &&
            *(int*)(*(int*)(*DAT_80612ed8 + 0x48) + 8) != 0 && DAT_80611320 != DAT_80611928) {
            DAT_80611320 = DAT_80611928;
            ((void(*)(void))*(void(**)(void))(*(int*)(*DAT_80612ed8 + 0x48) + 8))();
        }
        if (soundState != 0) {
            if (2000 < *(int*)(st + 0x20) && *(int*)(*DAT_80612ed8 + 0x48) != 0 &&
                qent == *(int*)(&DAT_80611324 + rider[1] * 4)) {
                soundState = 0;   // note: original sets iVar4=0 then latches ee4
                DAT_80612ee4 = 1;
            }
            *(int*)(st + 0x20) = 0;
        }

        void (*perFrame)(int, int, int) = *(void(**)(int, int, int))(*(int*)(st + 0x30) + 4);
        if (perFrame != (void(*)(int,int,int))0)
            perFrame(st, soundState, qent + 0x64);

        if (*(int*)(st + 0x70) == 0 || FUN_80374bf0(st) == 0) {
            int off = 0;
            for (int i = 0; i < DAT_80612ed8[4]; ++i) {
                if ((*(unsigned*)(DAT_80612ed8[3] + off + 0x14) & 2) != 0 &&
                    prop_owner_has_instance(st, i) != 0) {
                    void (*cb)(int, int, int, int) = *(void(**)(int,int,int,int))(DAT_80612ed8[3] + off + 0x28);
                    if (cb != (void(*)(int,int,int,int))0)
                        cb(st, DAT_80612ed8[3] + off, soundState, qent + 0x64);
                }
                off += 0x4c;
            }
            prop_neighbour_drag_scan();
            prop_icon_anim_tick(st);
            prop_panel_advance_text(st, (unsigned*)(qent + 0x64));
            prop_link_update(qent, soundState);
        }
        DAT_80612ee4 = DAT_80612ee4;   // preserved
    }
advance:
    if (*(int*)(st + 0x20) < 0x7fffff9b && (*(unsigned short*)(st + 0x1a) & 1) == 0)
        *(int*)(st + 0x20) += DAT_806108e4;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// prop_panel_should_autoclose 0x80372828
// True once the panel dwell timer has run past 0x7d0 and a queued dialogue
// (descriptor +0x48) is present.
// ---------------------------------------------------------------------------
int prop_panel_should_autoclose(int st)
{
    if (*(int*)(st + 0x20) < 0x7d1) return 0;
    if (*(int*)(*DAT_80612ed8 + 0x48) == 0) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// prop_target_neighbour_scan 0x80372854
// Walk the topmost owner's neighbour entries (0x18 stride) and pick the first
// whose box contains the pointer as the new active link (+0x2c), decaying the
// non-selected entries' two counters (+0x14, +0x5) by the descriptor rates. If
// the current active entry was re-hit, keep it; then run the scale decay.
// ---------------------------------------------------------------------------
void prop_target_neighbour_scan(void)
{
    int qent = (int)FUN_803826e8();
    int st = *(int*)(qent + 0x7c);
    bool keptActive = false;
    float px = *(float*)(st + 0x38);
    float py = *(float*)(st + 0x3c);
    int prevActive = *(int*)(st + 0x2c);
    if (*(int*)(st + 0x154) == 0) { FUN_80382734(); return; }

    float* owner = (float*)prop_find_topmost(st);
    int off = 0;
    *(int*)(st + 0x2c) = 0;
    for (int i = 0; i < (int)owner[8]; ++i) {
        float* e = (float*)((int)owner[7] + off);
        int half = 0x10;
        if ((int)e[2] < 0) half = 2 - (int)e[2];
        bool outside = ((float)((double)(unsigned)((int)px ^ 0x80000000) - DOUBLE_8061b480)
                        < (owner[0] + e[0]) - (float)((double)(unsigned)((unsigned)half ^ 0x80000000) - DOUBLE_8061b480));
        if (!outside &&
            !((float)((double)(unsigned)((unsigned)half ^ 0x80000000) - DOUBLE_8061b480) + owner[0] + e[0]
                  <= (float)((double)(unsigned)((int)px ^ 0x80000000) - DOUBLE_8061b480)) &&
            !((float)((double)(unsigned)((int)py ^ 0x80000000) - DOUBLE_8061b480) < (owner[1] + e[1]) - FLOAT_8061b49c) &&
            !(FLOAT_8061b49c + owner[1] + e[1] <= (float)((double)(unsigned)((int)py ^ 0x80000000) - DOUBLE_8061b480))) {
            if (e == *(float**)(st + 0x28))
                keptActive = true;
            else
                *(float**)(st + 0x2c) = e;
            if (DAT_806108e4 != 0) {
                int* cnt = (int*)((int)owner[7] + off + 0x14);
                *cnt += *(int*)(*DAT_80612ed8 + 0x3c);
                *cnt = (*cnt >> 1);
            }
        } else if (DAT_806108e4 != 0) {
            e[5] = (float)((int)e[5] + *(int*)(*DAT_80612ed8 + 0x38));
            int* cnt = (int*)((int)owner[7] + off + 0x14);
            *cnt = (*cnt >> 1);
        }
        off += 0x18;
    }
    if (keptActive && *(int*)(st + 0x2c) == 0)
        *(int*)(st + 0x2c) = *(int*)(st + 0x28);
    prop_scale_decay(st, (unsigned)(*(int*)(st + 0x2c) != prevActive));
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// prop_wire_charge_targets 0x80372a98
// The "wired to prop" HUD feedback pass. When the query entity is not being
// driven by a live mounted rider, charge the local prompt bar directly.
// Otherwise, for every session player whose record is in state 2 (and passes the
// descriptor +0x20 filter), aim a ray from the entity toward the player; if the
// player is within range, roughly facing, and has clear line of sight, charge
// that player's prompt bar.
// ---------------------------------------------------------------------------
void prop_wire_charge_targets(void)
{
    int qent = (int)FUN_803826e4();
    int st = *(int*)(qent + 0x7c);
    int rider = *(int*)(st + 0x14);
    if (DAT_806108e4 <= 0) { FUN_80382730(); return; }

    if (rider == 0 || prop_entity_is_mounted(st) == 0 ||
        (*(unsigned*)(*(int*)(st + 0x30) + 0x14) & 1) != 0) {
        hud_prompt_bar_charge_slot(*(int*)(rider + 4), *(int*)(rider + 0x24), 7.83326e-42f);
    } else if ((*(unsigned*)(qent + 0x28) & 4) != 0 && (*(unsigned*)(qent + 0x28) & 0x80000000) == 0) {
        int p = session_get_state_flag();
        while (--p >= 0) {
            int rec = *(int*)(*(int*)(DAT_80611d90 + p * 0xea0) + 0x150);
            void* filter = *(void**)((int)DAT_80612ed8 + 0x20);
            int pass = 1;
            if (filter != (void*)0)
                pass = ((int(*)(int, int))filter)(qent, *(int*)(rec + 0x14));
            if (pass != 0 && *(int*)(rec + 0x114) == 2) {
                int node = *(int*)(rec + 0x14);
                float rot[8];
                mtx4_set_rotation_y((double)((FLOAT_8061b448 * *(float*)(rec + 0x100)) / FLOAT_8061b44c), rot);
                float cx = rot[6], cy = rot[7], cz = 0.0f;   // matrix forward columns reloaded by caller
                float ex = *(float*)(rec + 0xb4);
                float ey = *(float*)(rec + 0xb8);
                float ez = *(float*)(rec + 0xbc);
                float dx = (*(float*)(qent + 0x64) + *(float*)(qent + 0x1a8)) - ex;
                float dy = (*(float*)(qent + 0x68) + *(float*)(qent + 0x1ac)) - ey;
                float dz = (*(float*)(qent + 0x6c) + *(float*)(qent + 0x1b0)) - ez;
                double d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 <= (double)*(float*)(qent + 0x25c)) {
                    double dist = math_sqrt(d2);
                    if (dist < (double)FLOAT_8061b4c8) dist = (double)FLOAT_8061b4c8;
                    float inv = (float)((double)FLOAT_8061b458 / dist);
                    float dot = (float)((double)(dz * inv) * cz +
                                (double)(float)((double)(dx * inv) * cx +
                                (double)(float)((double)(dy * inv) * cy)));
                    if (*(float*)(qent + 0x260) <= dot) {
                        unsigned saveN = *(unsigned*)(node + 0x28);
                        unsigned saveQ = *(unsigned*)(qent + 0x28);
                        *(unsigned*)(node + 0x28) = saveN & 0xffffffdf;
                        *(unsigned*)(qent + 0x28) = saveQ & 0xffffffdf;
                        int hit = raycast_hit(*(int*)(node + 0x34), &ex, &dx, 0, (float*)0, (float*)0, (int*)0, 0x2111070, 0, (int*)0);
                        *(unsigned*)(node + 0x28) = saveN;
                        *(unsigned*)(qent + 0x28) = saveQ;
                        if (hit == 0)
                            hud_prompt_bar_charge_slot(*(int*)(rec + 4),
                                *(int*)(DAT_80611d78 + *(int*)(rec + 4) * 0x141c + 0x20), 8.4148e-42f);
                    }
                }
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// prop_update_driver 0x80372d28
// The per-frame driver for a mounted prop entity: decay the +0x50 cooldown, run
// the input tick and the target scan, keep the idle animation clip 0x25 playing,
// count down the DAT_80612ef0 "recently used" timer, and run the wire charge.
// ---------------------------------------------------------------------------
void prop_update_driver(int qent)
{
    int st = *(int*)(qent + 0x7c);
    *(int*)(st + 0x50) -= DAT_806108e4;
    if (*(int*)(st + 0x50) < 1)
        *(int*)(st + 0x50) = 0;
    prop_panel_input_tick();
    prop_target_neighbour_scan();
    if (*(int*)(st + 0x14) != 0 &&
        anim_is_clip_active(*(int*)(*(int*)(st + 0x14) + 0x14), 0x25) == 0)
        prop_anim_play((double)FLOAT_8061b4cc, (double)FLOAT_8061b458);
    if (0 < DAT_80612ef0) {
        DAT_80612ef0 -= DAT_806108e4;
        if (DAT_80612ef0 < 1) {
            DAT_80612ef0 = 0;
            DAT_80612ee8 = 0;
            DAT_80612ef4 = 0;
        }
    }
    prop_wire_charge_targets();
}

// ---------------------------------------------------------------------------
// prop_panel_draw_bevel 0x80372dfc
// Draw a fixed-colour beveled panel frame: a dark fill inset by 1px, a light top
// / dark bottom edge and light left / dark right edge.
// ---------------------------------------------------------------------------
void prop_panel_draw_bevel(int x0, int y0, unsigned x1, unsigned y1)
{
    unsigned long long p = __save_gpr_28();
    int ax = (int)(p >> 0x20);
    int ay = (int)p;
    (void)x0; (void)y0;
    gx_fill_rect_clipped(ax + 1, ay + 1, x1 - 1, y1 - 1, 0x14);
    gx_draw_line_i(ax,     ay, x1,     ay, 0x50);
    gx_draw_line_i(ax + 1, y1, x1 + 1, y1, 0xffffff3c);
    gx_draw_line_i(ax,     ay + 1, ax, y1, 0x50);
    gx_draw_line_i(x1,     ay + 1, x1, y1, 0xffffff3c);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// prop_panel_draw_bevel_col 0x80372eb0
// As prop_panel_draw_bevel but with the fill/edge alpha driven by a supplied
// colour byte (edges use colour+0x3c).
// ---------------------------------------------------------------------------
void prop_panel_draw_bevel_col(int x0, int y0, unsigned x1, unsigned y1, unsigned color)
{
    unsigned long long p = FUN_803826e4();
    int ax = (int)(p >> 0x20);
    int ay = (int)p;
    (void)x0; (void)y0;
    gx_fill_rect_clipped(ax + 1, ay + 1, x1 - 1, y1 - 1, (color & 0xff) | 0xffffff00);
    unsigned lo = (color + 0x3c) & 0xff | 0xffffff00;
    unsigned hi = (color + 0x3c) & 0xff;
    gx_draw_line_i(ax,     ay, x1,     ay, lo);
    gx_draw_line_i(ax + 1, y1, x1 + 1, y1, hi);
    gx_draw_line_i(ax,     ay + 1, ax, y1, lo);
    gx_draw_line_i(x1,     ay + 1, x1, y1, hi);
    FUN_80382730();
}
// ============================================================================
// Address range: 0x80372f7c - 0x80373848 | batch_1041 | 2026-09-12
//
// The prop render / composite path plus the priority-list bookkeeping. The
// per-owner render walks every flagged instance and draws its entries (beveled
// text tags, sprites or the info panel), the composite pass blits the pointer
// sprite + scanline overlay into the prop render target, and a small set of
// helpers manage the "activation" of an entry (raise it to the top of the
// priority list, kick its open animation) and hand out round-robin render-target
// slot handles.
//
//   prop_composite_render      0x80372f7c  draw pointer + scanline overlay, bind the RT, set blend mode
//   prop_open_anim_factor       0x803732ac scale factor for the currently-opening entry (eases in)
//   prop_begin_open_anim        0x80373338 latch the active entry + seed its open-anim timer
//   prop_reindex_after_activate 0x8037338c decrement priorities above the activated entry, raise it to top
//   prop_entry_flag_bit1        0x803733f0 read bit1 of an owner's indexed instance flags word
//   prop_activate_entry         0x80373418 activate an owner entry: begin drag, set flag2, reindex, start anim
//   prop_render_owner_entries   0x803734a4 render one owner's 0x18-stride entries (frames / text / sprites / panel)
//   prop_render_all             0x8037378c iterate all owners by priority and render each flagged, owned one
//   prop_rt_slot_reset          0x8037383c reset the render-target slot cursor to 0
//   prop_rt_slot_next           0x80373848 return the next composite render-target handle (round-robin)
//
// NOTE: non-matching decomp. Callee-save spill (FUN_803826e4 / __save_gpr_25 /
// __save_gpr_28 save, FUN_80382730 / __restore_* restore) kept as opaque scratch
// markers carrying the packed (owner, index) or (x, y) pair. prop_panel_draw is
// void here but the original returns an "is-open" flag; it is reached through a
// typed cast so the visibility gate is preserved without disturbing batch_1038.
// FLOAT_80612ef8 (scanline scroll), the 0x4c instance / 0x18 entry strides, the
// +0x18 priority field, the +0x14 flags (bit1) and the round-robin RT slot table
// DAT_805ecc10 all match the original.
// ---------------------------------------------------------------------------

extern unsigned long long __save_gpr_25(void);      // save r25..r31 (packed hi/lo)
extern void   __restore_gpr_25(void);
extern float  FLOAT_80612ef8;                       // scanline overlay scroll offset
extern float  FLOAT_8061b4d0, FLOAT_8061b4d4;
extern double text_get_char_width(unsigned char* str);
// prop_panel_draw returns an "is-open" flag in the original (defined void above)
typedef int (*PanelDrawFn)(float*, int, double);
#define prop_panel_draw_flag(rec,owner,anim)  (((PanelDrawFn)&prop_panel_draw)((rec),(owner),(anim)))
// prop_draw_instance_sprites returns a "draw base sprite too" flag in the original (void here)
typedef int (*DrawInstFn)(int, int, int);
#define prop_draw_instance_flag(ox,oy,idx)  (((DrawInstFn)&prop_draw_instance_sprites)((ox),(oy),(idx)))

// ---------------------------------------------------------------------------
// prop_composite_render 0x80372f7c
// Composite pass for the prop render target. Draws the active drag target's icon
// (and a fallback 0x387 icon when the target is culled), the live entity's
// pointer sprite, an optional full-screen descriptor tint, a scrolling scanline
// overlay built from sprite 0x123, then binds the prop's render-target slot
// (cleared on level 6 when unmounted, default bind otherwise) and sets the
// colour-update / blend mode for the caller's blend index.
// ---------------------------------------------------------------------------
void prop_composite_render(int a, int b, int blendIndex)
{
    unsigned long long ctx = __save_gpr_25();
    int ptrTex = (int)(ctx >> 0x20);   // pointer sprite texture id (or -1)
    int st     = (int)ctx;             // prop panel state
    (void)a; (void)b;

    if (*(int*)(st + 0x28) != 0 && 0x13 < *(int*)(st + 0x1c)) {
        prop_sprite_draw((int)*(float*)(st + 0x38), (int)*(float*)(st + 0x3c),
                         *(int*)(*(int*)(st + 0x28) + 8));
        if (prop_instance_visible(st) == 0)
            prop_sprite_draw((int)*(float*)(st + 0x38), (int)*(float*)(st + 0x3c), 0x387);
    }

    if (*(int*)(st + 0x14) != 0) {
        int state = *(int*)(*(int*)(st + 0x14) + 0x114);
        if (state != 0x80 && state != 0x8000 &&
            (*(unsigned*)(*(int*)(st + 0x30) + 0x14) & 4) == 0 && ptrTex != -1) {
            int tex = sprite_get_texture_id(ptrTex);
            gx_set_active_material(tex);
            int mat = *(int*)DAT_80610a20 + tex * 0x58;
            gx_draw_sprite_rect((int)(*(float*)(st + 0x38) - FLOAT_8061b460),
                                (int)(*(float*)(st + 0x3c) - FLOAT_8061b460),
                                (int)(FLOAT_8061b4d0 + *(float*)(st + 0x38)),
                                (int)(FLOAT_8061b4d0 + *(float*)(st + 0x3c)),
                                (unsigned)*(unsigned short*)(mat + 0x52) << 4,
                                (unsigned)*(unsigned short*)(mat + 0x4e) << 4, 0, 0);
        }
    }

    if (DAT_80612ed8 != (int*)0 && *(int*)*DAT_80612ed8 != 0) {
        gx_set_sub_material_id(1);
        gx_fill_rect_clipped(0, 0, 0x100, 0x100, *(unsigned*)*DAT_80612ed8);
    }

    int scTex = sprite_get_texture_id(0x123);
    int scMat = *(int*)DAT_80610a20 + scTex * 0x58;
    unsigned short v = *(unsigned short*)(scMat + 0x4e);
    unsigned u = (unsigned)*(unsigned short*)(scMat + 0x52);
    gx_set_sub_material_id(0xb);
    gx_set_active_material(scTex);
    int band = (int)u * 2;
    for (int y = (int)FLOAT_80612ef8; y < 0xff; y += band)
        gx_draw_sprite_rect(0, y, 0x100, y + band, 0, 0, (unsigned)v << 4, u << 4);
    FLOAT_80612ef8 = FLOAT_80612ef8 - (float)DAT_806108e4;
    if (FLOAT_80612ef8 <= (float)(int)(u * -2))
        FLOAT_80612ef8 = FLOAT_80612ef8 + (float)band;

    if (DAT_80612614 == 6 && prop_entity_is_mounted(st) == 0)
        render_target_clear_bind(*(int*)(st + 0xc), 0x8000);
    else
        render_target_bind_default(*(int*)(st + 0xc));
    gx_set_color_update_mode(0);
    gx_set_blend_mode(blendIndex);
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// prop_open_anim_factor 0x803732ac
// Return the open/close scale for the entry currently animating. If this entry
// (owner, index) is the one whose open timer is running and the descriptor rate
// (+0x54) is set, ease the factor from the base down by the elapsed fraction;
// otherwise return the base scale.
// ---------------------------------------------------------------------------
double prop_open_anim_factor(int owner, int index)
{
    float f = FLOAT_8061b458;
    if (index == DAT_80612ee8 && owner == DAT_80612ef4 &&
        FLOAT_8061b464 < *(float*)(*DAT_80612ed8 + 0x54)) {
        f = FLOAT_8061b458 -
            (float)((double)(unsigned)(DAT_80612ef0) - DOUBLE_8061b480) /
            (*(float*)(*DAT_80612ed8 + 0x54) *
             (float)((double)(unsigned)(DAT_806109c0) - DOUBLE_8061b480));
    }
    return (double)f;
}

// ---------------------------------------------------------------------------
// prop_begin_open_anim 0x80373338
// Latch the (owner, index) being opened and seed its open-anim countdown from
// the descriptor rate (+0x54) times the global frame span (DAT_806109c0).
// ---------------------------------------------------------------------------
void prop_begin_open_anim(int index, int owner)
{
    DAT_80612ee8 = index;
    DAT_80612ef4 = owner;
    DAT_80612ef0 = (int)(*(float*)(*DAT_80612ed8 + 0x54) *
                         (float)((double)(unsigned)(DAT_806109c0) - DOUBLE_8061b480));
}

// ---------------------------------------------------------------------------
// prop_reindex_after_activate 0x8037338c
// Raise the just-activated instance to the top of the priority order: every
// other instance whose priority (+0x18) is above the activated one is
// decremented, and the activated instance takes the highest priority slot.
// Also caches the activated record in DAT_80612eec.
// ---------------------------------------------------------------------------
void prop_reindex_after_activate(int owner, int instanceIndex)
{
    (void)owner;
    DAT_80612eec = (float*)(*(int*)((int)DAT_80612ed8 + 0xc) + instanceIndex * 0x4c);
    int prio = *(int*)((int)DAT_80612eec + 0x18);
    int off = 0;
    for (int i = 0; i < *(int*)((int)DAT_80612ed8 + 0x10); ++i) {
        int p = *(int*)(*(int*)((int)DAT_80612ed8 + 0xc) + off + 0x18);
        if (prio < p)
            *(int*)(*(int*)((int)DAT_80612ed8 + 0xc) + off + 0x18) = p - 1;
        off += 0x4c;
    }
    *(int*)((int)DAT_80612eec + 0x18) = *(int*)((int)DAT_80612ed8 + 0x10) - 1;
}

// ---------------------------------------------------------------------------
// prop_entry_flag_bit1 0x803733f0
// Read bit1 of the flags word for the owner's index'th instance (the entry's
// instance id is stored in the owner id list at +0xb4).
// ---------------------------------------------------------------------------
unsigned prop_entry_flag_bit1(int owner, int index)
{
    return *(unsigned*)(*(int*)((int)DAT_80612ed8 + 0xc) +
                        *(int*)(owner + index * 4 + 0xb4) * 0x4c + 0x14) >> 1 & 1;
}

// ---------------------------------------------------------------------------
// prop_activate_entry 0x80373418
// Activate an owner entry: begin a drag on the query entity, set the instance's
// flag2 (+0x14 bit1), raise it to the top of the priority list, and if it was
// not already flagged, kick off its open animation.
// ---------------------------------------------------------------------------
void prop_activate_entry(void)
{
    unsigned long long ctx = __save_gpr_28();
    int qent = (int)(ctx >> 0x20);
    int index = (int)ctx;
    int addr = qent + index * 4;
    int rec = *(int*)((int)DAT_80612ed8 + 0xc) + *(int*)(addr + 0xb4) * 0x4c;
    unsigned prev = *(unsigned*)(rec + 0x14);
    prop_set_drag_cooldown(qent);
    *(unsigned*)(rec + 0x14) = *(unsigned*)(rec + 0x14) | 2;
    prop_reindex_after_activate(qent, *(int*)(addr + 0xb4));
    if ((prev & 2) == 0)
        prop_begin_open_anim(qent, rec);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// prop_render_owner_entries 0x803734a4
// Render one owner's entries. First the open-anim factor gates whether the info
// panel is drawn (prop_panel_draw). Then each 0x18-stride entry is drawn: text
// entries (id < 0) get a beveled tag frame (highlighted variant when this entry
// is the active link and the panel is live) with the string laid out right of
// the box; sprite entries get the style draw when active plus the base sprite.
// A trailing owner callback (+0x24) runs last. Restores the full scissor.
// ---------------------------------------------------------------------------
void prop_render_owner_entries(void)
{
    unsigned long long ctx = FUN_803826e4();
    int owner = (int)(ctx >> 0x20);
    int index = (int)ctx;
    float* rec = (float*)(DAT_80612ed8[3] + index * 0x4c);

    double anim = prop_open_anim_factor(owner, (int)rec);
    int visible = 1;
    if (rec[0] != FLOAT_8061b464)
        visible = prop_panel_draw_flag(rec, owner, anim);

    if (visible != 0) {
        int off = 0;
        for (int i = 0; i < (int)rec[8]; ++i) {
            int e = (int)rec[7] + off;
            if (*(int*)(e + 8) < 0) {
                unsigned char* str = (unsigned char*)get_string_by_id(*(int*)(e + 0xc));
                float* box = (float*)((int)rec[7] + off);
                float hw = box[2];
                int x0 = (int)(rec[0] + box[0] + (float)((double)(unsigned)((int)hw ^ 0x80000000) - DOUBLE_8061b480));
                int y0 = (int)((rec[1] + box[1]) - FLOAT_8061b4d0);
                int x1 = (int)((rec[0] + box[0]) - (float)((double)(unsigned)(unsigned)(-(int)hw) - DOUBLE_8061b480));
                int y1 = (int)(FLOAT_8061b4d0 + rec[1] + box[1]);
                if (*(float**)(owner + 0x28) == box &&
                    *(float**)(owner + 0x28) == *(float**)(owner + 0x2c) &&
                    *(short*)(owner + 0x18) != 0) {
                    prop_panel_draw_bevel(x0, y0, x1, y1);
                } else {
                    prop_panel_draw_bevel_col(x0, y0, x1, y1, (unsigned)box[5]);
                }
                double w = text_get_char_width(str);
                text_draw_char(-(double)(float)(w * (double)FLOAT_8061b454 -
                                    (double)(rec[0] + *(float*)((int)rec[7] + off))),
                               (double)((rec[1] + *(float*)((int)rec[7] + off + 4)) - FLOAT_8061b4d4),
                               str, *(int*)(*DAT_80612ed8 + 0x2c));
            } else {
                int draw = 1;
                if (e == *(int*)(owner + 0x2c))
                    draw = prop_draw_instance_flag(owner, e, index);
                if (draw != 0) {
                    float* box = (float*)((int)rec[7] + off);
                    prop_sprite_draw((int)(rec[0] + box[0]), (int)(rec[1] + box[1]), (int)box[2]);
                }
            }
            off += 0x18;
        }
        void (*ownerCb)(int, float*) = (void(*)(int, float*))(int)rec[9];
        if (ownerCb != (void(*)(int, float*))0)
            ownerCb(owner, rec);
    }
    prop_scissor_full();
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// prop_render_all 0x8037378c
// Render every owned, flagged instance in ascending priority order (so higher
// priorities draw last / on top).
// ---------------------------------------------------------------------------
void prop_render_all(void)
{
    int qent = (int)__save_gpr_28();
    for (int prio = 0; prio < *(int*)((int)DAT_80612ed8 + 0x10); ++prio) {
        int off = 0;
        for (int i = 0; i < *(int*)((int)DAT_80612ed8 + 0x10); ++i) {
            int rec = *(int*)((int)DAT_80612ed8 + 0xc) + off;
            if (prio == *(int*)(rec + 0x18) && (*(unsigned*)(rec + 0x14) & 2) != 0 &&
                prop_owner_has_instance(qent, i) != 0) {
                prop_render_owner_entries();
            }
            off += 0x4c;
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// prop_rt_slot_reset 0x8037383c
// Reset the round-robin render-target slot cursor.
// ---------------------------------------------------------------------------
void prop_rt_slot_reset(void)
{
    DAT_80612ed0 = 0;
}

// ---------------------------------------------------------------------------
// prop_rt_slot_next 0x80373848
// Hand out the next composite render-target handle, wrapping the cursor at the
// active slot count (DAT_80612ee0).
// ---------------------------------------------------------------------------
int prop_rt_slot_next(void)
{
    if (DAT_80612ee0 <= DAT_80612ed0)
        DAT_80612ed0 = 0;
    int i = DAT_80612ed0;
    DAT_80612ed0 = DAT_80612ed0 + 1;
    return (&DAT_805ecc10)[i];
}
// ============================================================================
// Address range: 0x80373880 - 0x803741fc | batch_1042 | 2026-09-12
//
// The top-level prop render driver plus the prop entity lifecycle (spawn/init),
// the drag cooldown seed, and the scrollable text-box interaction helpers used
// by the panel entries. prop_render_driver is the render entry point: it sets up
// the prop's own render target, draws the background, the wrapped dialogue lines
// and every owner's entries, then composites the result. prop_entity_spawn
// allocates the per-prop type record and registers the entity's update hooks.
//
//   prop_pick_player_bit      0x80373880  lowest player-owned bit set in the instance mask (+0x10) via clz
//   prop_render_driver        0x803738d0  top-level render: RT bind, background, panel lines, entries, composite
//   prop_entity_spawn         0x80373b00  allocate + zero the 0x158 prop-type record, seed anim/rng, register hooks
//   prop_set_drag_cooldown    0x80373c5c  seed the +0x50 drag/interaction cooldown from descriptor rate
//   prop_entity_init_state    0x80373ca8  init the panel state on activation (fields, sound fade, spawn sound)
//   prop_draw_progress_bar    0x80373dac  beveled progress bar: outer frame + interpolated inner fill
//   prop_lerp_field           0x80373e70  lerp between +0x14 and +0x18 by the +0x1c factor
//   prop_lerp_offset_y        0x80373e90  y minus the lerp of +0x04 / +0x0c by the +0x1c factor
//   prop_textbox_hit_drag     0x80373eec  hit-test / drag a scrollable text-box entry, return "consumed"
//   prop_mount_camera_fixup   0x803741fc  match a camera entry to the mounted prop and snap the camera pose
//
// NOTE: non-matching decomp. Callee-save spill kept as opaque scratch markers
// (FUN_803826e4/f0 save, __save_gpr_28 save; FUN_80382730/8038273c/__restore
// restore); the packed return supplies the (entity, id) pair. Named engine
// helpers whose generated prototype takes an argument the decompiler could not
// recover are called with the recovered value (0 where none). countLeadingZeros
// is the common.h intrinsic. All struct offsets, the 0x158 type-record stride,
// the +0x50 cooldown / +0x1c interpolation factor and the register/hook side
// effects match the original.
// ---------------------------------------------------------------------------

extern int    DAT_80612610;                 // active local-player count
extern void   FUN_80374b88(int st, int* ctx);           // alt (mounted) foreground draw
extern float  FLOAT_8061b4d8, FLOAT_8061b4dc, FLOAT_8061b4e0;

// ---------------------------------------------------------------------------
// prop_pick_player_bit 0x80373880
// Build a mask of the render-priority bits (+0x214) owned by the active local
// players, AND it with the instance's candidate mask (+0x10), and return the
// index of the lowest set bit (clz>>5 gives 1 when the AND is empty).
// ---------------------------------------------------------------------------
unsigned prop_pick_player_bit(int inst)
{
    unsigned mask = 0;
    int off = 0;
    for (int i = 0; i < DAT_80612610; ++i) {
        mask |= 1u << *(int*)(*(int*)(DAT_80611d78 + off + 8) + 0x214);
        off += 0x141c;
    }
    return countLeadingZeros(*(unsigned*)(inst + 0x10) & mask) >> 5;
}

// ---------------------------------------------------------------------------
// prop_render_driver 0x803738d0
// Top-level per-prop render. When this prop owns none of the active players'
// render bits (and the optional descriptor gate passes): pick a render-target
// slot, bind it, paint the 0x80 background, then either draw the descriptor
// background sprite + wrapped dialogue lines + every owner's entries + the
// icon-open hook (unmounted) or the alternate mounted foreground; finally
// composite the whole target with the saved blend mode.
// ---------------------------------------------------------------------------
void prop_render_driver(void)
{
    int qent = (int)FUN_803826e4();
    int st = *(int*)(qent + 0x7c);
    unsigned owned = prop_pick_player_bit(qent);
    if (owned == 0 &&
        ((void*)DAT_80612ed8[6] == (void*)0 ||
         ((int(*)(int))DAT_80612ed8[6])(qent) != 0)) {
        int desc = *(int*)(st + 0x30);
        int blend = gx_set_blend_mode(0);
        int ctx[8];
        ctx[0] = 0x385;
        int prevFont = text_set_font_table(0x1c);
        gx_set_scissor_rect(0, 0, 0xff, 0xff);
        int slot = prop_rt_slot_next();
        *(int*)(st + 0xc) = slot;
        collision_set_strip_entry(*(unsigned*)(qent + 4),
                                  *(unsigned*)(*(int*)(st + 0x10) + 0x28),
                                  *(unsigned*)(st + 0xc));
        gx_set_color_update_mode(0);
        gx_set_sub_material_id(1);
        gx_fill_rect_clipped(0, 0, 0x100, 0x100, 0x80);
        gx_set_color_update_mode(0);

        int panel = *(int*)(*DAT_80612ed8 + 0x48);
        if (panel == 0 || *(int*)(st + 0x20) < 0x7d1) {
            int bg = *(int*)(*DAT_80612ed8 + 4);
            if (-1 < bg) {
                int tex = sprite_get_texture_id(bg);
                gx_set_active_material(tex);
                int mat = *(int*)DAT_80610a20 + tex * 0x58;
                gx_draw_sprite_rect(0, 0, 0x100, 0x100, 0,
                                    (unsigned)*(unsigned short*)(mat + 0x52) << 4,
                                    (unsigned)*(unsigned short*)(mat + 0x4e) << 4, 0);
            }
            if (*(int*)(st + 0x70) == 0) {
                prop_panel_draw_lines(st);
                void (*cb1)(int, int*) = *(void(**)(int, int*))(desc + 8);
                if (cb1 != (void(*)(int,int*))0) cb1(st, ctx);
                prop_render_all();
                prop_scissor_full();
                prop_icon_run_hook(st);
                void (*cb2)(int, int*) = *(void(**)(int, int*))(desc + 0xc);
                if (cb2 != (void(*)(int,int*))0) cb2(st, ctx);
            } else {
                FUN_80374b88(st, ctx);
                prop_scissor_full();
            }
        } else {
            void (*cb)(void) = *(void(**)(void))(panel + 0xc);
            if (cb != (void(*)(void))0) cb();
            ctx[0] = -1;
        }
        prop_composite_render(ctx[0], st, blend);
        text_set_font_table(prevFont);
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// prop_entity_spawn 0x80373b00
// Allocate the next 0x158 prop-type record (from DAT_80612ed4, count DAT_80612f14),
// zero it, back-reference the entity, register the three per-frame update hooks,
// preload the level's swarm sound, seed two random offset params (+0x38/+0x3c),
// set the initial dwell timer to 0x7d1, and run the descriptor +0x1c init hook.
// ---------------------------------------------------------------------------
void prop_entity_spawn(void)
{
    unsigned long long ctx = FUN_803826f0();
    int ent = (int)(ctx >> 0x20);
    int recIndex = DAT_80612f14 * 0x158;
    DAT_80612f14 = DAT_80612f14 + 1;
    *(int*)(ent + 0x7c) = DAT_80612ed4 + recIndex;
    memset_wrapper(*(int*)(ent + 0x7c), 0, 0x158);
    int* rec = *(int**)(ent + 0x7c);
    rec[4] = (int)ctx;
    rec[1] = -1;
    *(int*)(ent + 0x2d8) = (int)ctx;
    entity_register_for_update(ent, 8, -0x7fc8c730);
    entity_register_for_update(ent, 4, -0x7fc8d2d8);
    flock_sound_id_for_level((unsigned)*(unsigned short*)(ent + 0x94), 1);
    *(unsigned*)(ent + 0x28) = *(unsigned*)(ent + 0x28) | 4;
    entity_register_for_update(ent, 0, -0x7fc90304);
    rec[0] = ent;
    rec[0xe] = (int)(FLOAT_8061b4dc *
                     ((float)((double)(unsigned)rng_counter_tick() - DOUBLE_8061b490) / FLOAT_8061b498) + FLOAT_8061b4d8);
    rec[0xf] = (int)(FLOAT_8061b4dc *
                     ((float)((double)(unsigned)rng_counter_tick() - DOUBLE_8061b490) / FLOAT_8061b498) + FLOAT_8061b4d8);
    rec[8] = 0x7d1;
    ((void(*)(int, int))*(void(**)(int, int))((int)DAT_80612ed8 + 0x1c))(*(int*)(ent + 0x7c), ent + 0x64);
    rec[0x1a] = 0;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_set_drag_cooldown 0x80373c5c
// Seed the interaction cooldown (+0x50) from the descriptor rate (+0x54) scaled
// by the global frame span (DAT_806109c0).
// ---------------------------------------------------------------------------
void prop_set_drag_cooldown(int st)
{
    *(int*)(st + 0x50) = (int)(*(float*)(*DAT_80612ed8 + 0x54) *
                               (float)((double)(unsigned)(DAT_806109c0) - DOUBLE_8061b480));
}

// ---------------------------------------------------------------------------
// prop_entity_init_state 0x80373ca8
// Initialise the panel state when a prop is activated: latch the descriptor
// (+0x30) and open params, clear the animation fields, set the initial reveal
// factors, seed the drag cooldown, register the sound fade-out, run the
// descriptor open hook, and either play the open sound (param5==0) or copy the
// queued line count.
// ---------------------------------------------------------------------------
void prop_entity_init_state(int a, int b, int param3, unsigned* pos, int param5)
{
    unsigned long long ctx = __save_gpr_28();
    int st = (int)(ctx >> 0x20);
    (void)a; (void)b;
    *(int*)(st + 0x30) = (int)ctx;
    float base = FLOAT_8061b464;
    *(int*)(st + 0x24) = param3;
    *(int*)(st + 0x34) = 0;
    *(int*)(st + 0x60) = 0;
    *(int*)(st + 0x64) = 0;
    *(float*)(st + 8) = base;
    *(float*)(st + 0x48) = base;
    *(int*)(st + 0x40) = -1;
    *(int*)(st + 0x2c) = 0;
    prop_set_drag_cooldown(st);
    sound_register_fade_out((double)FLOAT_8061b458, (double)FLOAT_8061b48c, (int*)(st + 4), pos);
    void (*openCb)(int, unsigned*) = *(void(**)(int, unsigned*))ctx;
    if (openCb != (void(*)(int, unsigned*))0)
        openCb(st, pos);
    if (*(int*)(st + 0x24) != 0) {
        if (param5 == 0) {
            int sid = *(int*)(*DAT_80612ed8 + 0x44);
            int params[13];
            sound_init_play_params((int)params);
            *(char*)((char*)params + 0x20) = 1;
            params[2] = pos[0];
            params[3] = pos[1];
            params[4] = pos[2];
            params[0] = sid;
            *(unsigned*)(st + 4) = sound_play_request(params);
        } else {
            *(int*)(st + 0x34) = **(int**)(st + 0x24);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// prop_draw_progress_bar 0x80373dac
// Draw a beveled progress bar: the outer frame at [0..2]x[1..3+8] and an inner
// fill whose top edge is interpolated between rows 1 and 3 by the +0x1c (index 7)
// factor, in colour 0x46.
// ---------------------------------------------------------------------------
void prop_draw_progress_bar(int* bar)
{
    int mid = (int)((float)((double)(unsigned)bar[1] - DOUBLE_8061b480) * (FLOAT_8061b458 - (float)bar[7]) +
                    (float)((double)(unsigned)bar[3] - DOUBLE_8061b480) * (float)bar[7]);
    prop_panel_draw_bevel(bar[0], bar[1], bar[2], bar[3] + bar[8]);
    prop_panel_draw_bevel_col(bar[0] + 2, mid + 2, bar[2] - 2, (mid + bar[8]) - 2, 0x46);
}

// ---------------------------------------------------------------------------
// prop_lerp_field 0x80373e70
// Linear blend of the two floats at +0x14 / +0x18 by the +0x1c factor.
// ---------------------------------------------------------------------------
double prop_lerp_field(int p)
{
    return (double)(*(float*)(p + 0x14) * (FLOAT_8061b458 - *(float*)(p + 0x1c)) +
                    *(float*)(p + 0x18) * *(float*)(p + 0x1c));
}

// ---------------------------------------------------------------------------
// prop_lerp_offset_y 0x80373e90
// Subtract from y the lerp of the +0x04 / +0x0c integer rows by the +0x1c factor.
// ---------------------------------------------------------------------------
double prop_lerp_offset_y(double y, int p)
{
    return (double)(float)(y -
        (double)((float)((double)(unsigned)*(unsigned*)(p + 4) - DOUBLE_8061b480) * (FLOAT_8061b458 - *(float*)(p + 0x1c)) +
                 (float)((double)(unsigned)*(unsigned*)(p + 0xc) - DOUBLE_8061b480) * *(float*)(p + 0x1c)));
}

// ---------------------------------------------------------------------------
// prop_textbox_hit_drag 0x80373eec
// Hit-test / scroll a text-box entry. When the box has overflow (row5 < row6):
// style 3 hit-tests the pointer inside the expanded box and scrolls the reveal
// (+0x1c index 7) toward the pointer, clamping the scroll factor (index9);
// style 2 drags the scroll to follow the pointer. Otherwise a plain
// point-in-box test. Returns whether the pointer was consumed.
// ---------------------------------------------------------------------------
unsigned prop_textbox_hit_drag(int qent, unsigned* box, int style)
{
    unsigned consumed;
    if ((float)box[5] < (float)box[6]) {
        if (style == 3) {
            double py = (double)*(float*)(qent + 0x3c);
            if ((float)((double)(unsigned)box[0] - DOUBLE_8061b480) <= *(float*)(qent + 0x38) &&
                (double)(float)((double)(unsigned)box[1] - DOUBLE_8061b480) <= py &&
                *(float*)(qent + 0x38) <= (float)((double)(unsigned)box[2] - DOUBLE_8061b480) &&
                py <= (double)(float)((double)(unsigned)(box[3] + box[8]) - DOUBLE_8061b480)) {
                box[9] = (unsigned)(float)prop_lerp_offset_y(py, (int)box);
                if ((float)box[9] < FLOAT_8061b464) {
                    box[7] = (unsigned)((float)box[7] - FLOAT_8061b4bc);
                    box[9] = (unsigned)FLOAT_8061b4e0;
                }
                if ((float)((double)(unsigned)box[8] - DOUBLE_8061b480) < (float)box[9]) {
                    box[7] = (unsigned)((float)box[7] + FLOAT_8061b4bc);
                    box[9] = (unsigned)FLOAT_8061b4e0;
                }
                return 1;
            }
        } else if (style < 3 && 1 < style && FLOAT_8061b45c <= (float)box[9]) {
            unsigned top = box[1];
            float rel = *(float*)(qent + 0x3c) - (float)box[9];
            float bot = (float)((double)(unsigned)box[3] - DOUBLE_8061b480);
            float topf = (float)((double)(unsigned)top - DOUBLE_8061b480);
            float clamped = rel;
            if (bot < rel) clamped = bot;
            float v = clamped;
            if (topf <= clamped) { v = rel; if (bot < rel) v = bot; }
            box[7] = (unsigned)((v - (float)((double)(unsigned)top - DOUBLE_8061b480)) /
                                (float)((double)(unsigned)(box[3] - top) - DOUBLE_8061b480));
            return 1;
        }
        consumed = 0;
        box[9] = (unsigned)FLOAT_8061b4e0;
    } else {
        consumed = 0;
        if ((float)((double)(unsigned)box[0] - DOUBLE_8061b480) <= *(float*)(qent + 0x38) &&
            (float)((double)(unsigned)box[1] - DOUBLE_8061b480) <= *(float*)(qent + 0x3c) &&
            *(float*)(qent + 0x38) <= (float)((double)(unsigned)box[2] - DOUBLE_8061b480) &&
            *(float*)(qent + 0x3c) <= (float)((double)(unsigned)(box[3] + box[8]) - DOUBLE_8061b480))
            consumed = 1;
    }
    return consumed;
}

// ---------------------------------------------------------------------------
// prop_mount_camera_fixup 0x803741fc
// When a player mounts a prop, find the camera entry (0xc4 stride in the
// descriptor +0x24 table, count +0x28) matching the prop's +0x60 handle, apply
// it to the player's camera and snap the interpolated pose to the target.
// ---------------------------------------------------------------------------
void prop_mount_camera_fixup(int st, int player)
{
    int n = *(int*)((int)DAT_80612ed8 + 0x28);
    int off = 0;
    while (0 < n) {
        if (*(int*)(st + 0x60) == *(int*)(*(int*)((int)DAT_80612ed8 + 0x24) + off)) {
            int cam = *(int*)((int)DAT_80612ed8 + 0x24) + off + 4;
            camera_set_mode(*(int*)(*(int*)(player + 0x150) + 8), cam, player);
            *(int*)(cam + 0x20) = *(int*)(cam + 8);
            *(int*)(cam + 0x24) = *(int*)(cam + 0xc);
            *(int*)(cam + 0x28) = *(int*)(cam + 0x10);
            *(int*)(cam + 0x50) = *(int*)(cam + 0x38);
            *(int*)(cam + 0x54) = *(int*)(cam + 0x3c);
            *(int*)(cam + 0x58) = *(int*)(cam + 0x40);
            return;
        }
        off += 0xc4;
        --n;
    }
}

// ============================================================================
// batch_1044 | 2026-09-12 | 0x803748b0 - 0x80374e68
//
// Panel-framework tail: the descriptor-driven panel vtable dispatchers
// (bind / update / query / close / noop), the label-button and centered-caption
// draw helpers, the sprite-set loader, and the full-screen fade overlay + its
// spectator registration. These complete the prop/panel object framework whose
// lifecycle and draw helpers were decompiled in batches 1036-1043.
//
// Panel-instance layout (relevant offsets):
//   +0x20   state scratch cleared on close
//   +0x50   drag/interaction cooldown (seeded by prop_set_drag_cooldown)
//   +0x70   bound descriptor* (a small vtable):
//              +0x04 init hook    +0x08 close hook   +0x0c query hook
//              +0x10 update hook  +0x14 result field
//   +0x74   0x40-byte per-descriptor state block (zeroed on bind)
// Label-button record (float view): [0..3] bounds, [8] string id, [9] sprite id,
//   [0xb] enabled flag.
// ============================================================================

extern int    DAT_80612f18;      // sprite-set table base (8-byte stride, -1 sentinel)
extern double DOUBLE_8061b4f0;   // int<->double bias (0x43300000 pair)
extern float  FLOAT_8061b4e8;    // caption baseline y offset
extern float  FLOAT_8061b4ec;    // caption reveal-scale factor
// gx_*, text_*, spectator_*, sprite_load_texture_set, sound_clear_bank_ref
// resolve through the shared generated headers (do not re-declare).

// prop_label_button_draw 0x803748b0
void prop_label_button_draw(void) {
    float* w = (float*)__save_gpr_25();
    unsigned char* s = (unsigned char*)get_string_by_id((int)w[8]);
    double cw = text_get_char_width(s);
    float x1 = w[2];
    float x0 = w[0];
    double ch = text_get_char_height(s);
    float y1 = w[3];
    float y0 = w[1];

    if (w[9] == 0.0f) {
        prop_panel_draw_bevel_col((int)w[0], (int)y0, (int)w[2], (int)y1, 0x46);
    } else {
        gx_set_active_material((int)w[9]);
        int mat = *(int*)DAT_80610a20 + (int)w[9] * 0x58;
        gx_draw_sprite_quad((double)w[0], (double)w[1], (double)w[2], (double)w[3],
                            (unsigned)*(unsigned short*)(mat + 0x52) << 4,
                            (unsigned)*(unsigned short*)(mat + 0x4e) << 4, 0, 0);
    }

    double bx = (double)((int)(x1 - x0) - (int)cw ^ 0x80000000);
    unsigned int col = (w[0xb] != 0.0f) ? 0x7f7f7f80u : 0x7f7f7f28u;
    double by = (double)((int)(y1 - y0) - (int)ch ^ 0x80000000);
    text_draw_char((double)(w[0] + (float)(bx - DOUBLE_8061b480) * FLOAT_8061b454),
                   (double)(w[1] + (float)(by - DOUBLE_8061b480) * FLOAT_8061b454),
                   s, col);
    __restore_gpr_25();
}

// prop_draw_centered_caption 0x80374a64
void prop_draw_centered_caption(void) {
    int st = FUN_803826f0();
    int prevFont = text_set_font_table(0x1a);
    unsigned char* s = (unsigned char*)get_string_by_id(*(int*)(st + 0x60));
    double dW = text_get_char_width(s);
    unsigned w = (unsigned)dW;

    // centered x = anchor - width/2, clamped to [3, 0xfd-width]
    int half = ((int)w >> 1) + (((int)w < 0 && (w & 1) != 0) ? 1 : 0);
    unsigned x = (unsigned)(*(float*)(st + 0x38) -
                 (float)((double)(half ^ 0x80000000) - DOUBLE_8061b4f0));
    if ((int)x < 3) {
        x = 3;
    } else if ((int)(0xfd - w) < (int)x) {
        x = 0xfd - w;
    }

    text_draw_char_outline(
        (double)(float)((double)(x ^ 0x80000000) - DOUBLE_8061b4f0),
        (double)(FLOAT_8061b4e8 + *(float*)(st + 0x3c)), s, 0x80787800,
        (int)(FLOAT_8061b4ec *
              (float)((double)(*(unsigned*)(st + 0x64) ^ 0x80000000) - DOUBLE_8061b4f0)));
    text_set_font_table(prevFont);
    FUN_8038273c();
}

// prop_panel_noop 0x80374b84  (empty vtable stub)
void FUN_80374b84(void) {
}

// prop_panel_query_result 0x80374b88
// Existing extern contract in this file: void FUN_80374b88(int, int*).
void FUN_80374b88(int st, int* ctx) {
    if (st == 0 || *(int*)(st + 0x70) == 0) {
        return;
    }
    (**(void (***)(void))(*(int*)(st + 0x70) + 0xc))();
    *ctx = *(int*)(*(int*)(st + 0x70) + 0x14);
}

// prop_panel_close 0x80374bf0
int FUN_80374bf0(int st) {
    int bound = *(int*)(st + 0x70) != 0;
    if (bound) {
        *(int*)(st + 0x20) = 0;
        (**(void (***)(void))(*(int*)(st + 0x70) + 8))();
    }
    return bound;
}

// prop_panel_bind_descriptor 0x80374c3c
int prop_panel_bind_descriptor(int descriptor, int st) {
    typedef void (*hook_fn)(int);
    if (st != 0) {
        memset_wrapper(st + 0x74, 0, 0x40);
        *(int*)(st + 0x70) = descriptor;
        if (*(int*)(st + 0x70) != 0) {
            prop_set_drag_cooldown(st);
            hook_fn init = *(hook_fn*)(*(int*)(st + 0x70) + 4);
            if (init != (hook_fn)0) {
                init(st);
            }
            return 1;
        }
    }
    return 0;
}

// prop_panel_update_loop 0x80374cc4
void FUN_80374cc4(void) {
    typedef int (*update_fn)(int);
    unsigned long long ctx = FUN_803826f0();
    int st = (int)(ctx >> 0x20);
    int firstPass = (int)ctx;   // 0 on the initial (voice-triggering) pass
    if (*(int*)(st + 0x70) != 0) {
        if (firstPass == 0) {
            prop_play_random_voice();
        }
        do {
            update_fn upd = *(update_fn*)(*(int*)(st + 0x70) + 0x10);
            if (upd == (update_fn)0 || upd(st) == 0) {
                *(int*)(st + 0x70) = 0;
            }
        } while (*(int*)(st + 0x70) != 0 && firstPass != 0);
    }
    FUN_8038273c();
}

// prop_load_sprite_set 0x80374d50
// Existing extern contract: int FUN_80374d50(9 ints); returns the (updated) ctx.
int FUN_80374d50(int a, int b, int c, int d, int e, int f, int g, int h, int table) {
    DAT_80612f18 = table;
    if (table != 0) {
        int i = 0;
        int off = 0;
        do {
            if (*(int*)(DAT_80612f18 + off + 4) == -1) break;
            a = sprite_load_texture_set(a, b, c, d, e, f, g, h);  // in-file 8-arg macro
            i++;
            off += 8;
        } while (i < 6);
        sound_clear_bank_ref(0x361);
        sound_clear_bank_ref(0x362);
    }
    return a;
}

// prop_draw_fade_overlay 0x80374dc8
void prop_draw_fade_overlay(void) {
    FUN_803826f0();
    int prevBlend = gx_set_blend_mode(0);
    unsigned int prevMat = gx_set_sub_material_id(0xe);
    gx_set_color_update_mode(0);
    gx_fill_rect_clipped(0, 0, 0x100, 0x100, 0x7f);
    gx_set_blend_mode(prevBlend);
    gx_set_sub_material_id((int)prevMat);
    gx_set_color_update_mode(0);
    if (DAT_80612614 == 6) {
        int cb = spectator_get_render_callback();
        spectator_set_viewport(cb);
    }
    FUN_8038273c();
}

// prop_register_fade_overlay 0x80374e68
void prop_register_fade_overlay(void) {
    void (*cb)(void) = &prop_draw_fade_overlay;
    spectator_set_user_data((int)(size_t)cb);
}

// ============================================================================
// batch_1045 | 2026-09-12 | 0x80374e90 - 0x80375924
//
// Prop "mount" panel controller: a spectator-camera-driven interaction where
// the player mounts a prop (turret/vehicle/objective) and cycles through a
// table of selectable target options. Option table lives at DAT_80612f18
// (0x1c-stride records, -1 sentinel); the panel instance keeps the table base
// at +0x74, the wrapped selection index at +0x84, the option count at +0x88,
// the resolved selection at +0x78, the loop-sound handle at +0xb0, and camera
// zoom/cursor/fov scratch at +0x7c/+0x90/+0x94.
// ============================================================================

extern int    DAT_80612f1c;      // currently-selected mount option record
extern int    DAT_805ecc20;      // entity-target scratch record (0x1dc bytes)
extern int    DAT_80495e38;      // entity-target panel descriptor
extern int    DAT_80495e64;      // cached option-table base for the renderer
extern int    DAT_80402700;      // spectator mode-data block (default)
extern int    DAT_80402c40;      // spectator mode-data block (entity variant)
extern float  FLOAT_8061b4f8, FLOAT_8061b4fc;
extern float  FLOAT_8061b500, FLOAT_8061b504, FLOAT_8061b508, FLOAT_8061b50c;
extern float  FLOAT_8061b510, FLOAT_8061b514, FLOAT_8061b518, FLOAT_8061b51c;
extern float  FLOAT_8061b520, FLOAT_8061b524;
extern double DOUBLE_8061b528;
extern float  FLOAT_8061b538, FLOAT_8061b53c, FLOAT_8061b540;

// Arity-adapting cast macros for helpers whose real (header) signature differs
// from these call sites (same idiom as prop_panel_draw_flag / sprite_load_texture_set).
typedef int  (*PanelDrawNoArg)(void);
#define prop_panel_draw_ret()          (((PanelDrawNoArg)&prop_panel_draw)())
typedef void (*SndStopFn)(void);
#define prop_mount_sound_stop()        (((SndStopFn)&sound_stop_handle)())
typedef void (*SpecModeFn)(double, void*, int);
#define prop_spec_set_mode(a,b,c)      (((SpecModeFn)&spectator_set_mode)((a),(b),(c)))
typedef void (*Snd3dFn)(double, double, double, int, int, int, int);
#define prop_snd_3d(a,b,c,d,e,f,g)     (((Snd3dFn)&sound_update_3d_source)((a),(b),(c),(d),(e),(f),(g)))

// prop_mount_noop 0x80374e90  (empty stub)
void prop_mount_noop(void) {
}

// prop_mount_stop_sound 0x80374e94
void prop_mount_stop_sound(int st) {
    if (-1 < (int)*(unsigned*)(st + 0xb0) && sound_is_playing(*(unsigned*)(st + 0xb0)) != 0) {
        prop_mount_sound_stop();
    }
}

// prop_mount_select_next 0x80374edc
void prop_mount_select_next(void) {
    int local_48[18];
    unsigned long long ctx = __save_gpr_28();
    int idx = (int)(ctx >> 0x20);
    int st = (int)ctx;
    int* sel = (int*)0;

    spectator_instant_show();
    while (sel == (int*)0) {
        *(int*)(st + 0x84) = idx;
        int i = *(int*)(st + 0x84);
        int cnt = *(int*)(st + 0x88);
        if (i < cnt) {
            if (i < 0) *(int*)(st + 0x84) = i + cnt;
        } else {
            *(int*)(st + 0x84) = i - cnt;
        }
        unsigned* opt = (unsigned*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c);
        if (opt[5] == 0x1f) {
            int ent = entity_handle_resolve(*opt);
            if (ent == 0) goto done;
            sel = &DAT_805ecc20;
            memset_wrapper((int)&DAT_805ecc20, 0, 0x1dc);
            DAT_805ecc20 = ent;
        } else {
            sel = (int*)entity_get_field_7c(*opt);
        }
        idx = idx + 1;
    }
    *(int**)(st + 0x78) = sel;
    DAT_80612f1c = *(int*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c);
    *(float*)(st + 0x94) = FLOAT_8061b4f8;
    *(float*)(st + 0x7c) = FLOAT_8061b4fc;
    *(int*)(st + 0x80) = 0;
    sound_init_play_params((int)local_48);
    local_48[0] = 0x361;
    sound_play_request(local_48);
done:
    __restore_gpr_28();
}

// prop_mount_action_toggles 0x80375018
unsigned int prop_mount_action_toggles(int st, int action) {
    if (action != 4 && (3 < action || action != 0)) {
        return 1;
    }
    unsigned int v = *(unsigned*)(st + 0x88) ^ 1;
    return (unsigned int)(((int)v >> 1) - (v & *(unsigned*)(st + 0x88))) >> 0x1f;
}

// prop_mount_panel_update 0x80375058
void prop_mount_panel_update(void) {
    unsigned long long ctx = FUN_803826f0();
    int st = (int)(ctx >> 0x20);
    int style = (int)ctx;
    double zoomStep = (double)FLOAT_8061b500;

    if ((*(unsigned*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c + 0x18) & 2) == 0) {
        *(int*)(st + 0x8c) = -1;
        int px = 0, off = 0, slots = 6;
        *(float*)(st + 0x9c) = FLOAT_8061b500;
        *(float*)(st + 0xa0) = FLOAT_8061b500;
        do {
            if (*(int*)(DAT_80612f18 + off + 4) == -1) break;
            float lo = (float)((double)((px + 0xe) ^ 0x80000000) - DOUBLE_8061b528);
            float hi = (float)((double)((px + 0x2e) ^ 0x80000000) - DOUBLE_8061b528);
            if (lo <= *(float*)(st + 0x38) && *(float*)(st + 0x38) <= hi &&
                FLOAT_8061b504 <= *(float*)(st + 0x3c) && *(float*)(st + 0x3c) <= FLOAT_8061b508) {
                *(int*)(st + 0x8c) = *(int*)(DAT_80612f18 + off + 8);
            }
            px += 0x23;
            off += 8;
            slots--;
        } while (slots != 0);

        float denom = (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b528);
        if (style == 2) {
            *(int*)(st + 0xac) = 0;
            int mode = *(int*)(st + 0x98);
            if (mode == 2) {
                zoomStep = (double)(FLOAT_8061b50c * (FLOAT_8061b510 / denom));
            } else if (mode < 2) {
                if (0 < mode) {
                    *(float*)(st + 0x9c) = *(float*)(st + 0x38) - *(float*)(st + 0xa4);
                    *(float*)(st + 0xa0) = *(float*)(st + 0x3c) - *(float*)(st + 0xa8);
                    *(int*)(st + 0x38) = *(int*)(st + 0xa4);
                    *(int*)(st + 0x3c) = *(int*)(st + 0xa8);
                }
            } else if (mode < 4) {
                zoomStep = (double)(FLOAT_8061b514 * (FLOAT_8061b510 / denom));
            }
        } else if (style == 3) {
            *(int*)(st + 0x98) = 0;
            *(int*)(st + 0xa4) = *(int*)(st + 0x38);
            *(int*)(st + 0xa8) = *(int*)(st + 0x3c);
            *(int*)(st + 0xac) = 0;
            if (prop_mount_action_toggles(st, *(int*)(st + 0x8c)) != 0) {
                int action = *(int*)(st + 0x8c);
                if (action == 2) {
                    *(int*)(st + 0x98) = 2;
                } else if (action < 2) {
                    if (action == 0) {
                        prop_mount_noop();
                        prop_mount_select_next();
                    } else if (-1 < action) {
                        if ((*(unsigned*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c + 0x18) & 4) != 0) {
                            prop_mount_noop();
                            prop_mount_select_next();
                        } else if (prop_is_state_idle(**(int**)(st + 0x78)) == 0) {
                            prop_set_idle_if_valid(*(int**)(st + 0x78));
                        } else {
                            prop_reset_active_state(*(int**)(st + 0x78));
                        }
                    }
                } else if (action == 4) {
                    prop_mount_noop();
                    prop_mount_select_next();
                } else if (action < 4) {
                    *(int*)(st + 0x98) = 3;
                }
            }
        }

        // Advance the zoom, clamped to [FLOAT_8061b518, FLOAT_8061b4f8].
        *(float*)(st + 0x94) =
            (float)(zoomStep * (double)FLOAT_806108ec + (double)*(float*)(st + 0x94));
        if (FLOAT_8061b518 <= *(float*)(st + 0x94)) {
            if (FLOAT_8061b4f8 < *(float*)(st + 0x94)) *(float*)(st + 0x94) = FLOAT_8061b4f8;
        } else {
            *(float*)(st + 0x94) = FLOAT_8061b518;
        }
    }

    *(int*)(st + 0x20) = 0;
    if (*(int**)(st + 0x78) != (int*)0) {
        float denom = (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b528);
        double cursorStep = (double)(FLOAT_8061b51c * FLOAT_806108ec * (FLOAT_8061b510 / denom));
        bool retreat = false;
        if (prop_is_destroyed(**(int**)(st + 0x78)) != 0 ||
            prop_is_state_idle(**(int**)(st + 0x78)) != 0) {
            retreat = true;
        }
        double target = (double)(retreat ? FLOAT_8061b510 : FLOAT_8061b500);

        if (spectator_is_ready() != 0) {
            int* modeData = &DAT_80402700;
            if (*(int*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c + 0x14) == 0x1f) {
                modeData = &DAT_80402c40;
            }
            *(unsigned*)((char*)modeData + 4) |= 0xa00;
            prop_spec_set_mode((double)FLOAT_8061b520, modeData, **(int**)(st + 0x78));
            spectator_set_user_data((int)(size_t)&prop_draw_fade_overlay);
            spectator_set_flags(0, 0x4000);
        }

        // Ease the camera cursor (+0x7c) toward the target.
        if (cursorStep < ABS((double)(float)((double)*(float*)(st + 0x7c) - target))) {
            if (target <= (double)*(float*)(st + 0x7c)) cursorStep = -cursorStep;
            *(float*)(st + 0x7c) = (float)((double)*(float*)(st + 0x7c) + cursorStep);
        } else {
            *(float*)(st + 0x7c) = (float)target;
        }
        *(float*)(st + 0x90) =
            FLOAT_806108ec * (float)((double)FLOAT_8061b510 / (double)denom) + *(float*)(st + 0x90);
        prop_snd_3d((double)*(float*)(st + 0x7c), (double)FLOAT_8061b524,
                    (double)FLOAT_8061b510, st + 0xb0, 0, 0x362, 0);
    }

    if (*(int*)(DAT_80612f18 + 0x34) != 0) {
        typedef void (*FrameHook)(int, int, int);
        (*(FrameHook)(size_t)*(int*)(DAT_80612f18 + 0x34))(
            st, *(int*)(st + 0x78), (FLOAT_8061b510 <= *(float*)(st + 0x7c)) ? 1 : 0);
    }
    spectator_set_fov((double)*(float*)(st + 0x94));
    prop_check_activate_region(st, style, 0);
    FUN_8038273c();
}

// prop_mount_panel_render 0x8037556c
void prop_mount_panel_render(void) {
    int st = (int)__save_gpr_25();
    DAT_80495e64 = *(int*)DAT_80612f18;
    if (prop_panel_draw_ret() != 0) {
        int prevBlend = gx_set_blend_mode(0);
        unsigned int prevMat = gx_set_sub_material_id(1);
        int str;
        if (FLOAT_8061b510 <= *(float*)(st + 0x7c)) {
            gx_fill_rect_clipped(0, 0, 0x100, 0x100, 0x80);
            str = get_string_by_id(*(int*)(DAT_80612f18 + 0x1d * 4));
        } else {
            gx_set_sub_material_id(0xe);
            int bg = spectator_get_render_callback();
            gx_set_active_material(bg);
            if (*(int*)(st + 0x78) == 0 || *(int*)(*(int*)(st + 0x78) + 0x10) != 9) {
                gx_draw_sprite_rect(0, 0, 0x100, 0x100, 0, 0, 0x1000, 0x1000);
            }
            gx_set_sub_material_id(2);
            gx_fill_rect_clipped(0, 0, 0x100, 0x100, 0x7f7f7f08);
            if (*(int*)(st + 0x78) == 0) {
                str = get_string_by_id(0x1130);
            } else {
                int idx = *(int*)(*(int*)(st + 0x78) + 0x10) + 0xe;
                str = get_string_by_id(*(int*)(DAT_80612f18 + idx * 4));
            }
        }
        gx_set_sub_material_id(1);
        if (*(int*)(st + 0x78) != 0 &&
            ((*(unsigned*)(*(int*)(st + 0x78) + 8) & 4) != 0 ||
             *(int*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c + 0x14) == 0x1f)) {
            hud_timer_icon_render(*(unsigned*)(st + 0x14), 0x100, 0x100);
        }
        if ((*(unsigned*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c + 0x18) & 2) == 0) {
            int i = 0, off = 0, ox = 0;
            do {
                if (*(int*)(DAT_80612f18 + off + 4) == -1) break;
                prop_mount_action_toggles(st, *(int*)(DAT_80612f18 + off + 8));
                prop_sprite_draw(ox + 0x1e, 0xe2, *(int*)(DAT_80612f18 + off + 4));
                i++;
                off += 8;
                ox += 0x23;
            } while (i < 6);
            int prevFont = text_set_font_table(0x1a);
            int title = get_string_by_id(*(int*)(*(int*)(st + 0x74) + *(int*)(st + 0x84) * 0x1c + 4));
            text_draw_char_outline((double)FLOAT_8061b538, (double)FLOAT_8061b53c, (unsigned char*)title, 0x50505000, 0x80);
            text_draw_char_outline((double)FLOAT_8061b538, (double)FLOAT_8061b540, (unsigned char*)str, 0x50505000, 0x80);
            text_set_font_table(prevFont);
        }
        gx_set_blend_mode(prevBlend);
        gx_set_sub_material_id((int)prevMat);
    }
    prop_scissor_full();
    __restore_gpr_25();
}

// prop_mount_get_selected 0x8037581c
int prop_mount_get_selected(void) {
    return DAT_80612f1c;
}

// prop_mount_action_enter 0x80375824
int prop_mount_action_enter(int st) {
    if (*(int*)(*(int*)(st + 0x14) + 0x114) != 0x2000 &&
        prop_player_mount_entity(*(int*)(st + 0x14)) != 0) {
        player_set_state(*(int*)(st + 0x14), 0x2000);
    }
    prop_mount_noop();
    spectator_instant_show();
    prop_mount_stop_sound(st);
    return 0;
}

// prop_mount_panel_open 0x80375888
void prop_mount_panel_open(void) {
    unsigned long long ctx = FUN_803826f0();
    int table = (int)(ctx >> 0x20);
    int st = (int)ctx;
    int count = 0;
    for (int off = 0; *(int*)(table + off) != -1; off += 0x1c) {
        count++;
    }
    if (count != 0) {
        int obj = prop_player_mount_entity(*(int*)(st + 0x14));
        prop_panel_bind_descriptor((int)&DAT_80495e38, *(int*)(obj + 0x7c));
        *(int*)(st + 0x74) = table;
        *(int*)(st + 0x88) = count;
        prop_mount_select_next();
    }
    FUN_8038273c();
}

// prop_mount_selected_bit 0x80375924
unsigned int prop_mount_selected_bit(int st, int query) {
    if (*(int**)(st + 0x70) == &DAT_80495e38) {
        int rec = *(int*)(st + 0x84) * 0x1c;
        if (*(int*)(*(int*)(st + 0x74) + rec + 0x14) == 0x1f) {
            int ent = entity_handle_resolve(*(unsigned*)(*(int*)(st + 0x74) + rec));
            return (unsigned int)countLeadingZeros(ent - query) >> 5;
        }
    }
    return 0;
}

// ============================================================================
// batch_1046 | 2026-09-12 | 0x80375998 - 0x80375f18
//
// Two coupled sub-systems used by the prop panel:
//   * Plasma / electric-arc FX overlay (plasma_fx_* + plasma_arc_*): a lazily-
//     allocated 0x424-byte effect object plus a 0x20-segment animated arc drawn
//     over the panel backdrop. Frame/timer state lives in the DAT_805ece00/04
//     short pairs; the arc point arrays and 2-entry color pair live at
//     DAT_805ecf00.
//   * Prop "script" step runner (prop_script_*): advances a cursored opcode
//     program on the mounted prop object (+0x7c). The script cursor is at
//     object +0x58 (0xc-byte opcodes) with a step timer at +0x5c; opcode 1 is a
//     move-toward-point step, 0/2 wait, >=4 ends the program.
// ============================================================================

extern int   DAT_80612f20;       // plasma-fx singleton state block (0x424 bytes)
extern int   DAT_80611330, DAT_80611334;   // cached plasma origin params
extern short DAT_805ece00[2];    // arc segment x endpoints (also frame idx / timer via casts)
extern short DAT_805ece04[2];    // arc segment y endpoints
extern unsigned char DAT_8061b548, DAT_8061b54c;  // arc color-pair seed bytes
extern int   DAT_805ecf00, DAT_805ecf02, DAT_805ecf04;  // arc buffer head
extern float FLOAT_8061b550, FLOAT_8061b560, FLOAT_8061b564, FLOAT_8061b568;
extern float FLOAT_8061b56c, FLOAT_8061b570, FLOAT_8061b574, FLOAT_8061b578;
extern double DOUBLE_8061b558, DOUBLE_8061b580;

// mem_alloc_aligned real (header) sig is (s32,s32,s32); this call site uses the
// 16-arg spill-forwarding form returning a pointer (same idiom as mem_alloc).
typedef int (*MemAllocAlignedFn)(int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int);
#define prop_mem_alloc_aligned(...)  (((MemAllocAlignedFn)&mem_alloc_aligned)(__VA_ARGS__))

// prop_plasma_spawn 0x80375998
// Existing extern contract: void FUN_80375998(16 ints).
void FUN_80375998(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                  int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16) {
    (void)a9; (void)a10;
    DAT_80612f20 = 0;
    plasma_fx_create((unsigned)a1, (unsigned)a2, (unsigned)a3, (unsigned)a4,
                     (unsigned)a5, (unsigned)a6, (unsigned)a7, (unsigned)a8,
                     0xa0, 0xa0, a11, a12, a13, a14, a15, a16);
}

// prop_plasma_alloc 0x803759c8
void prop_plasma_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                       int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16) {
    if (DAT_80612f20 == 0) {
        DAT_80612f20 = prop_mem_alloc_aligned(a1, a2, a3, a4, a5, a6, a7, a8,
                                              4, 0x424, 1, a12, a13, a14, a15, a16);
        memset_wrapper(DAT_80612f20, 0, 0x424);
        DAT_80611330 = a9;
        DAT_80611334 = a10;
    }
}

// plasma_arc_init 0x80375a30
void plasma_arc_init(void) {
    memset_wrapper((int)&DAT_805ecf00, 0, 0x106);
    unsigned char* src0 = &DAT_8061b548;
    unsigned char* src2 = &DAT_8061b54c;
    unsigned char* dst0 = (unsigned char*)&DAT_805ecf00;
    unsigned char* dst2 = (unsigned char*)&DAT_805ecf02;
    int n = 2, i = 0;
    do {
        unsigned char c0 = *src0;
        unsigned char c2 = *src2;
        src0++;
        dst0[i] = c0;
        src2++;
        dst2[i] = c2;
        n--;
        i++;
    } while (n != 0);
    DAT_805ecf04 = 0;
    DAT_805ece00[0] = 10;
    DAT_805ece04[0] = 10;
    DAT_805ece00[1] = 0x40;
    DAT_805ece04[1] = 0x20;
}

// plasma_arc_render 0x80375bf8
void plasma_arc_render(void) {
    __save_gpr_28();
    unsigned int alpha = 8;
    gx_fill_rect_clipped(0, 0, 0x100, 0x100, 0x80);
    int i = 0, off = 0;
    do {
        gx_draw_line_i(
            (int)*(short*)((int)&DAT_805ece00 + off),
            (int)*(short*)((int)&DAT_805ece04 + off),
            (int)*(short*)((int)&DAT_805ece00 + off + 2),
            (int)*(short*)((int)&DAT_805ece04 + off + 2),
            (alpha & 0xff) | 0x4040d200);
        i++;
        off += 8;
        alpha += 8;
    } while (i < 0x20);
    __restore_gpr_28();
}

// plasma_fx_reset 0x80375c88
void plasma_fx_reset(void) {
    memset_wrapper((int)&DAT_805ece00, 0, 8);
    *(float*)&DAT_805ece04 = FLOAT_8061b560;
    *(int*)&DAT_805ece00 = 0;
    plasma_fx_init(0xa0);
    plasma_fx_update();
}

// plasma_fx_tick 0x80375cec
void plasma_fx_tick(void) {
    float denom = (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b558);
    *(float*)&DAT_805ece04 =
        -(FLOAT_806108ec * (FLOAT_8061b564 / denom) - *(float*)&DAT_805ece04);
    if (*(float*)&DAT_805ece04 < FLOAT_8061b550) {
        *(int*)&DAT_805ece00 = *(int*)&DAT_805ece00 + 1;
        if (*(int*)&DAT_805ece00 == 6) {
            *(int*)&DAT_805ece00 = 0;
        }
        plasma_fx_update();
        *(float*)&DAT_805ece04 = FLOAT_8061b560;
    }
    plasma_fx_update();
}

// plasma_fx_draw 0x80375d98
void plasma_fx_draw(void) {
    plasma_fx_render();
}

// prop_script_advance_step 0x80375dc0
void prop_script_advance_step(int obj) {
    *(int*)(obj + 0x58) = *(int*)(obj + 0x58) + 0xc;
    *(float*)(obj + 0x5c) = FLOAT_8061b568;
}

// prop_script_move_toward 0x80375dd8
int prop_script_move_toward(double dt, int obj, float* target) {
    double dy = (double)(target[1] - *(float*)(obj + 0x3c));
    double dx = (double)(target[0] - *(float*)(obj + 0x38));
    double denom = (double)(float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b580);
    double maxStep = (double)(FLOAT_8061b56c * FLOAT_806108ec * (float)(dt / denom));
    double dist = math_sqrt((double)(float)(dx * dx + (double)(float)(dy * dy)));
    float snap = FLOAT_8061b578;

    if ((double)FLOAT_8061b570 < dist) {
        dx = (double)(float)(dx / dist);
    }
    double step = (double)(float)(dist * (double)FLOAT_8061b574);
    if (maxStep < step) {
        step = maxStep;
    }
    *(float*)(obj + 0x38) = (float)(step * dx + (double)*(float*)(obj + 0x38));
    *(float*)(obj + 0x3c) = (float)(step * (double)(float)(dy / dist) + (double)*(float*)(obj + 0x3c));

    int done = 0;
    if (ABS(target[0] - *(float*)(obj + 0x38)) < snap &&
        ABS(target[1] - *(float*)(obj + 0x3c)) < snap) {
        done = 1;
    }
    return done;
}

// prop_script_update 0x80375f18
// Existing extern contract: int FUN_80375f18(int ent, unsigned* outFlag).
int FUN_80375f18(int ent, unsigned int* outFlag) {
    int obj = *(int*)(ent + 0x7c);
    unsigned char* op = *(unsigned char**)(obj + 0x58);
    if (op == (unsigned char*)0) {
        return 0;
    }
    float denom = (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b580);
    *(float*)(obj + 0x5c) = *(float*)(obj + 0x5c) + FLOAT_806108ec / denom;

    unsigned char code = *op;
    if (code == 2) {
        *outFlag = 1;
        if (*(float*)(op + 4) <= *(float*)(obj + 0x5c)) {
            prop_script_advance_step(obj);
        }
    } else if (code < 2) {
        if (code == 0) {
            if (*(float*)(op + 4) <= *(float*)(obj + 0x5c)) {
                prop_script_advance_step(obj);
            }
        } else {
            if (prop_script_move_toward((double)*(float*)(obj + 0x5c), obj, (float*)(op + 4)) != 0) {
                prop_script_advance_step(obj);
            }
        }
    } else if (code < 4) {
        *(int*)(obj + 0x58) = 0;
    } else {
        *(int*)(obj + 0x58) = 0;
    }
    return 1;
}

// ============================================================================
// Address range: 0x80376038 - 0x803765e8 | batch_1047 | 2026-09-13
//
// Prop-panel "objective / help-text" HUD layer. This slice sits directly after
// the plasma-FX overlay (batch_1046) and drives the panel's textual content: a
// reveal-animated help caption, the elapsed-mission timer readout, a status
// line, and two rounded-frame draws. It shares the prop_panel_* primitives
// (bevel/cursor/reveal) and the engine text stack (text_set_font_table /
// get_string_by_id / text_draw_char[_outline] / a printf-into-buffer helper).
//
// Cached panel state (word globals near DAT_804964c8 / DAT_80612f4x):
//   DAT_804964c8/cc/d0/d4  panel rect x0,x1,y0,y1 (ints, updated each layout)
//   DAT_80612f48           reveal-complete flag (1 once the caption fully fits)
//   DAT_80612f4c           caption string id
//   DAT_80612f44           active sound handle (-1 == none)
//   DAT_80612f40           mission-start timestamp; DAT_80611928 the live clock
//   DAT_806109c0           ticks-per-second divisor for the mm:ss readout
//
//   prop_panel_help_init    0x80376038  init the panel prop-entity (script &DAT_80495f54)
//   prop_panel_help_draw    0x8037606c  draw bevel+cursor, then the caption (outline when hidden bit clear)
//   prop_panel_help_layout  0x803761e4  fetch caption, measure it, cache the rect, tick reveal, set fit flag
//   prop_panel_timer_draw   0x803762ac  format+draw the mm:ss (or intro) mission-timer line
//   prop_panel_sound_stop   0x80376410  stop the active panel sound handle and clear it
//   prop_panel_sound_poll   0x80376444  clear the handle once the sound stops playing
//   prop_panel_status_draw  0x80376480  copy the 0x122-byte status glyph buffer and draw it
//   prop_panel_frame_draw_a 0x80376538  build a rounded frame rect (&DAT_805ed228) and stroke it (variant A)
//   prop_panel_reset        0x803765c0  reset the panel (delegates to 0x8037bc00)
//   prop_panel_frame_draw_b 0x803765e8  build a rounded frame rect (&DAT_805ed2b8) and stroke it (variant B)
//
// NOTE: non-matching decomp. Register-spilled varargs of the printf helper are
// modeled as explicit leading dummy params (the true GameCube ABI passes the
// first 8 in r3..r10); the format side effects and buffer semantics match.
// ============================================================================

// ---- panel text/rect state ------------------------------------------------
extern int   DAT_804964c8;   // panel rect x0
extern int   DAT_804964cc;   // panel rect x1
extern int   DAT_804964d0;   // panel rect y0
extern int   DAT_804964d4;   // panel rect y1
extern int   DAT_80612f48;   // reveal-complete flag
extern int   DAT_80612f4c;   // caption string id
extern int   DAT_80612f44;   // active sound handle (-1 none)
extern int   DAT_80612f40;   // mission-start timestamp
extern int   DAT_80611928;   // live mission clock / state word
extern int   DAT_806109c0;   // ticks-per-second divisor
extern char  DAT_80495f54;   // help-panel prop script table

// double-bias constant used by the int->float rect conversions
extern double DOUBLE_8061b588;
extern float  FLOAT_8061b590;
extern float  FLOAT_8061b594;

// status-glyph source buffer + draw origin (0x122 bytes copied to the stack)
extern int    DAT_803e0d54;      // source glyph record base
extern float  DAT_805ed250;      // status draw x
extern float  DAT_805ed254;      // status draw y
extern int    DAT_805ed228;      // frame-rect scratch (variant A)
extern int    DAT_805ed2b8;      // frame-rect scratch (variant B)
extern int    DAT_80611340;      // ": " separator glyph run
extern int    DAT_80611338;      // "0" pad glyph
extern int    DAT_8061133c;      // "" no-pad glyph

// format string literals
extern char s__s__s_80611344[];        // "%s %s %s"
extern char s__s__d__s_d_80496518[];   // "%s %d%s%d"

// callees
extern void prop_entity_init_state(int a, void* script, int b, void* out, int c);   // 0x8037c... family
extern void prop_panel_draw_bevel(int x0, int y0, int x1, int y1);
extern void prop_panel_draw_cursor(double x, double y, int arg);
extern void prop_panel_reveal_tick(void);
extern int  text_draw_char_outline(double x, double y, int str, int fill, int outline);
extern void sound_stop_handle(void);
extern int  sound_is_playing(int handle);

// strlen; the format-frame helpers (FUN_8038b510 void, FUN_803826e8/
// __save_gpr_28 unsigned long long, FUN_80382734/__restore_gpr_28) are already
// declared earlier in this file (batch_1046 block) - do not redeclare.
extern int  FUN_8038b5c8(int str);                 // strlen

// printf-into-buffer: r3..r10 are the register-spilled varargs, then buf,fmt,args
extern void FUN_80387fb8(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                         int buf, char* fmt, int v1, int v2, int v3, int v4, int v5, int v6);

// rounded-frame primitives
extern void FUN_8037c8d0(double x, double y, double w, double h, int* outRect);
extern void FUN_8037c54c(int* rect, int arg, int mode);   // stroke variant A
extern void FUN_8037bb9c(int* rect, int arg, int mode);   // stroke variant B
extern void FUN_8037c024(void);                           // status-draw finalizer
extern void FUN_8037bc00(void);                           // panel reset target

// ---------------------------------------------------------------------------
// prop_panel_help_init 0x80376038
// Initialise the help-panel prop entity from its script table (&DAT_80495f54).
// ---------------------------------------------------------------------------
void prop_panel_help_init(int owner, void* out)
{
    prop_entity_init_state(owner, &DAT_80495f54, 0, out, 1);
}

// ---------------------------------------------------------------------------
// prop_panel_help_draw 0x8037606c
// Draw the panel bevel and reveal cursor, then (if the reveal is running) the
// caption string: solid while bit5 of the state word is clear, outlined once
// the player has acknowledged it.
// ---------------------------------------------------------------------------
void prop_panel_help_draw(int cursorArg)
{
    prop_panel_draw_bevel(DAT_804964c8 + 0xf, DAT_804964d0 + 0x23,
                          DAT_804964c8 + 0x78, DAT_804964d0 + 0x14);
    prop_panel_draw_cursor((double)(float)(DAT_804964c8 + 0x11) - DOUBLE_8061b588,
                           (double)(float)(DAT_804964d0 + 0x14) - DOUBLE_8061b588,
                           cursorArg);
    if (DAT_80612f48 != 0) {
        int prevFont = text_set_font_table(0x1a);
        int str = get_string_by_id(0x1137);
        if (((unsigned int)DAT_80611928 >> 5 & 1) == 0) {
            text_draw_char_outline((double)(float)(DAT_804964c8 + 0xf) - DOUBLE_8061b588,
                                   (double)(float)(DAT_804964d0 + 0x2d) - DOUBLE_8061b588,
                                   str, 0x7f17177f, 0x7f);
        } else {
            text_draw_char_outline((double)(float)(DAT_804964c8 + 0xf) - DOUBLE_8061b588,
                                   (double)(float)(DAT_804964d0 + 0x2d) - DOUBLE_8061b588,
                                   str, 0, 0);
        }
        text_set_font_table(prevFont);
    }
}

// ---------------------------------------------------------------------------
// prop_panel_help_layout 0x803761e4
// Fetch the caption, measure its width, cache the panel rect from the incoming
// layout record, tick the reveal animation, and set the "fully revealed" flag
// once the measured width has been reached.
// ---------------------------------------------------------------------------
void prop_panel_help_layout(void)
{
    // __save_gpr_28 returns the packed r28-spill frame: low half is the layout
    // record pointer, high half is the caller's frame base (record at +0x44).
    unsigned long long frame = __save_gpr_28();
    float* rect = (float*)(int)frame;
    int recBase = (int)(frame >> 0x20);
    int str = get_string_by_id(DAT_80612f4c);
    unsigned int width = (unsigned int)FUN_8038b5c8(str);
    DAT_804964c8 = (int)rect[0];
    DAT_804964cc = (int)rect[2];
    DAT_804964d0 = (int)rect[1];
    DAT_804964d4 = (int)rect[3];
    prop_panel_reveal_tick();
    unsigned int progress = *(unsigned int*)(recBase + 0x44);
    DAT_80612f48 = (progress <= width) ? 0 : 1;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// prop_panel_timer_draw 0x803762ac
// Render the mission-timer line. Before the mission proper (DAT_80612f44 < 0)
// it prints the two intro strings; once running it formats mm:ss with a
// zero-pad on the seconds, or an ellipsis placeholder while still counting in.
// ---------------------------------------------------------------------------
void prop_panel_timer_draw(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    unsigned long long ctx = FUN_803826e8();
    int prevFont = text_set_font_table(0x1b);
    char buf[136];

    if (DAT_80612f44 < 0) {
        int label = get_string_by_id(0x1283);
        int value = get_string_by_id(0x1281);
        FUN_80387fb8((int)ctx, a2, a3, a4, a5, a6, a7, a8, (int)buf,
                     s__s__s_80611344, value, label, 0, 0, 0, 0);
    } else {
        int seconds = (int)(DAT_80611928 - DAT_80612f40) / DAT_806109c0;
        if (seconds < 1) {
            int* base = (int*)get_string_by_id(0x1282);
            FUN_8038b510((int*)buf, base);
            int count = (int)((unsigned int)DAT_80611928 >> 1 & 3);
            for (int i = 0; i < count; i++) {
                int len = FUN_8038b5c8((int)buf);
                FUN_8038b510((int*)(buf + len), (int*)&DAT_80611340);
            }
        } else {
            int pad = (int)&DAT_8061133c;
            int secs = (seconds - 1) % 0x3c;
            if (secs < 10) {
                pad = (int)&DAT_80611338;
            }
            int value = get_string_by_id(0x1281);
            FUN_80387fb8((int)ctx, a2, a3, a4, a5, a6, a7, a8, (int)buf,
                         s__s__d__s_d_80496518, value, (seconds - 1) / 0x3c, pad, secs, 0, 0);
        }
    }
    // draw origin comes from the format-frame result (extraout_r4 in raw decomp)
    float* origin = (float*)(int)ctx;
    text_draw_char((double)(FLOAT_8061b590 + origin[0]),
                   (double)(FLOAT_8061b594 + origin[1]),
                   (u8*)buf, 0x80808080);
    text_set_font_table(prevFont);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// prop_panel_sound_stop 0x80376410
// Stop the active panel sound (if any) and mark the handle free.
// ---------------------------------------------------------------------------
void prop_panel_sound_stop(void)
{
    if (-1 < DAT_80612f44) {
        sound_stop_handle();
        DAT_80612f44 = -1;
    }
}

// ---------------------------------------------------------------------------
// prop_panel_sound_poll 0x80376444
// Release the panel sound handle once the underlying voice has finished.
// ---------------------------------------------------------------------------
void prop_panel_sound_poll(void)
{
    if (-1 < DAT_80612f44 && sound_is_playing(DAT_80612f44) == 0) {
        DAT_80612f44 = -1;
    }
}

// ---------------------------------------------------------------------------
// prop_panel_status_draw 0x80376480
// Copy the 0x122-byte pre-built status glyph record onto the stack and draw it
// at the fixed status origin, then run the status-draw finalizer.
// ---------------------------------------------------------------------------
void prop_panel_status_draw(void)
{
    int prevFont = text_set_font_table(0x1b);
    unsigned char buf[0x122];
    // 0x1f * 8 + tail: verbatim 0x122-byte copy of the source glyph record
    const unsigned char* src = (const unsigned char*)&DAT_803e0d54;
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = src[i];
    }
    text_draw_char((double)DAT_805ed250, (double)DAT_805ed254, buf, 0x80787800);
    FUN_8037c024();
    text_set_font_table(prevFont);
}

// ---------------------------------------------------------------------------
// prop_panel_frame_draw_a 0x80376538
// Build a rounded frame rect from the incoming (x,y,w,h) layout floats into the
// variant-A scratch (&DAT_805ed228) and stroke it.
// ---------------------------------------------------------------------------
void prop_panel_frame_draw_a(int arg, float* rect, int mode)
{
    FUN_8037c8d0((double)(FLOAT_8061b594 + rect[0]),
                 (double)(FLOAT_8061b590 + rect[1]),
                 (double)(rect[2] - FLOAT_8061b594),
                 (double)(rect[3] - FLOAT_8061b590),
                 (int*)&DAT_805ed228);
    FUN_8037c54c((int*)&DAT_805ed228, arg, mode);
}

// ---------------------------------------------------------------------------
// prop_panel_reset 0x803765c0
// Reset the panel state (delegates to the shared reset routine).
// ---------------------------------------------------------------------------
void prop_panel_reset(void)
{
    FUN_8037bc00();
}

// ---------------------------------------------------------------------------
// prop_panel_frame_draw_b 0x803765e8
// Variant-B rounded frame: same geometry as _a but into the &DAT_805ed2b8
// scratch and stroked through the variant-B path.
// ---------------------------------------------------------------------------
void prop_panel_frame_draw_b(int arg, float* rect, int mode)
{
    FUN_8037c8d0((double)(FLOAT_8061b594 + rect[0]),
                 (double)(FLOAT_8061b590 + rect[1]),
                 (double)(rect[2] - FLOAT_8061b594),
                 (double)(rect[3] - FLOAT_8061b590),
                 (int*)&DAT_805ed2b8);
    FUN_8037bb9c((int*)&DAT_805ed2b8, arg, mode);
}

// ============================================================================
// Address range: 0x80376668 - 0x803769c8 | batch_1048 | 2026-09-13
//
// More prop-panel HUD content, continuing the batch_1047 objective/help layer:
// a second rect setter, several rounded-frame draw variants (each stroking a
// distinct &DAT_805ed3xx/4xx scratch), a couple of reset/init delegates, an
// owner-slot-conditional frame draw, and an ambient 3D-sound updater that ties
// a prop instance to a positional voice on the easiest difficulty.
//
//   prop_panel_init_b         0x80376668  init delegate (-> 0x8037b688)
//   prop_panel_set_rect2      0x80376694  cache a second panel rect (DAT_80496378..84)
//   prop_panel_reset2         0x803766f8  reset delegate (-> 0x8037bc00)
//   prop_panel_frame_draw_c   0x80376720  rounded frame into &DAT_805ed348, stroke it
//   prop_panel_reset_by_slot  0x80376798  reset the panel keyed on owner slot 0xb match
//   prop_panel_frame_draw_owned 0x803767ec owner-slot-conditional frame (805ed3d8 vs 805ed468)
//   prop_panel_reset3         0x803768b8  reset delegate (-> 0x8037bc00)
//   prop_ambient_sound_update 0x803768e0  bind a prop instance to a 3D voice (easy difficulty only)
//   prop_panel_frame_draw_d   0x80376950  rounded frame into &DAT_805ed4f8, stroke it
//   prop_panel_init_c         0x803769c8  init delegate (-> 0x8037b688)
//
// NOTE: non-matching decomp. Frame-draw geometry (x,y from FLOAT_8061b590 bias,
// raw w, h reduced by the bias) matches the original; the sound updater's OR of
// entity flag bit6 on a successful voice bind is preserved.
// ============================================================================

// second panel rect (x0/x1/y0/y1) cached by prop_panel_set_rect2
extern int DAT_80496378;   // rect x0
extern int DAT_8049637c;   // rect x1
extern int DAT_80496380;   // rect y0
extern int DAT_80496384;   // rect y1

// per-variant rounded-frame scratch rects
extern int DAT_805ed348;   // frame scratch C
extern int DAT_805ed3d8;   // frame scratch owned-A
extern int DAT_805ed468;   // frame scratch owned-B
extern int DAT_805ed4f8;   // frame scratch D

// ambient-sound world/listener constants
extern float FLOAT_8061b598;
extern float FLOAT_8061b59c;

// callees new to this file (entity_get_world_position is canonically float*
// in generated_decls.h; do not redeclare it)
extern void FUN_8037b688(void);                       // panel init target
extern unsigned int difficulty_index(void);           // current difficulty tier

// sound_update_3d_source used for its int result (voice-bound flag); the shared
// prop_snd_3d macro above is void, so reach it through an int-returning cast.
typedef int (*Snd3dFnI)(double, double, double, int, int*, int, int);
#define prop_snd_3d_i(a,b,c,d,e,f,g)  (((Snd3dFnI)&sound_update_3d_source)((a),(b),(c),(d),(e),(f),(g)))

// ---------------------------------------------------------------------------
// prop_panel_init_b 0x80376668
// ---------------------------------------------------------------------------
void prop_panel_init_b(void)
{
    FUN_8037b688();
}

// ---------------------------------------------------------------------------
// prop_panel_set_rect2 0x80376694
// Cache a second panel rect from the incoming layout floats (x0,x1,y0,y1).
// ---------------------------------------------------------------------------
void prop_panel_set_rect2(int arg, float* rect)
{
    (void)arg;
    DAT_80496378 = (int)rect[0];
    DAT_8049637c = (int)rect[2];
    DAT_80496380 = (int)rect[1];
    DAT_80496384 = (int)rect[3];
}

// ---------------------------------------------------------------------------
// prop_panel_reset2 0x803766f8
// ---------------------------------------------------------------------------
void prop_panel_reset2(void)
{
    FUN_8037bc00();
}

// ---------------------------------------------------------------------------
// prop_panel_frame_draw_c 0x80376720
// Rounded frame into the variant-C scratch (&DAT_805ed348) and stroke it. Note
// this variant biases BOTH x and y by FLOAT_8061b590 and passes the raw width.
// ---------------------------------------------------------------------------
void prop_panel_frame_draw_c(int arg, float* rect, int mode)
{
    FUN_8037c8d0((double)(FLOAT_8061b590 + rect[0]),
                 (double)(FLOAT_8061b590 + rect[1]),
                 (double)rect[2],
                 (double)(rect[3] - FLOAT_8061b590),
                 (int*)&DAT_805ed348);
    FUN_8037bb9c((int*)&DAT_805ed348, arg, mode);
}

// ---------------------------------------------------------------------------
// prop_panel_reset_by_slot 0x80376798
// Reset the panel; the two branches (owner slot 0xb match vs not) both delegate
// to the same reset in this build - the branch is kept for fidelity.
// ---------------------------------------------------------------------------
void prop_panel_reset_by_slot(int owner, int instance)
{
    int slotRec = prop_owner_slot_record(owner, 0xb);
    if (instance == slotRec) {
        FUN_8037bc00();
    } else {
        FUN_8037bc00();
    }
}

// ---------------------------------------------------------------------------
// prop_panel_frame_draw_owned 0x803767ec
// Owner-slot-conditional rounded frame: when the packed (owner,rect) pair's
// rect matches the owner's slot-0xb record, stroke into scratch owned-A
// (805ed3d8), otherwise scratch owned-B (805ed468).
// ---------------------------------------------------------------------------
void prop_panel_frame_draw_owned(int arg1, int arg2, int mode)
{
    (void)arg1; (void)arg2;
    unsigned long long packed = (unsigned long long)FUN_803826f0();
    int owner = (int)(packed >> 0x20);
    float* rect = (float*)(int)packed;
    float* slotRec = (float*)prop_owner_slot_record(owner, 0xb);
    if (rect == slotRec) {
        FUN_8037c8d0((double)(FLOAT_8061b590 + rect[0]),
                     (double)(FLOAT_8061b590 + rect[1]),
                     (double)rect[2],
                     (double)(rect[3] - FLOAT_8061b590),
                     (int*)&DAT_805ed3d8);
        FUN_8037bb9c((int*)&DAT_805ed3d8, owner, mode);
    } else {
        FUN_8037c8d0((double)(FLOAT_8061b590 + rect[0]),
                     (double)(FLOAT_8061b590 + rect[1]),
                     (double)rect[2],
                     (double)(rect[3] - FLOAT_8061b590),
                     (int*)&DAT_805ed468);
        FUN_8037bb9c((int*)&DAT_805ed468, owner, mode);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_reset3 0x803768b8
// ---------------------------------------------------------------------------
void prop_panel_reset3(void)
{
    FUN_8037bc00();
}

// ---------------------------------------------------------------------------
// prop_ambient_sound_update 0x803768e0
// On the easiest difficulty only, update/attach a positional 3D voice (sound id
// 0x1621) at the prop entity's world position; on a successful bind set entity
// flag bit6 (+0x18 |= 0x40).
// ---------------------------------------------------------------------------
void prop_ambient_sound_update(int* inst)
{
    int entity = *inst;
    if (difficulty_index() == 0) {
        int* pos = (int*)entity_get_world_position(entity);
        int bound = prop_snd_3d_i((double)FLOAT_8061b598, (double)FLOAT_8061b59c,
                                  (double)FLOAT_8061b598, entity + 0x228, pos, 0x1621, 0);
        if (bound != 0) {
            *(unsigned int*)(entity + 0x18) |= 0x40;
        }
    }
}

// ---------------------------------------------------------------------------
// prop_panel_frame_draw_d 0x80376950
// Rounded frame into the variant-D scratch (&DAT_805ed4f8) and stroke it.
// ---------------------------------------------------------------------------
void prop_panel_frame_draw_d(int arg, float* rect, int mode)
{
    FUN_8037c8d0((double)(FLOAT_8061b590 + rect[0]),
                 (double)(FLOAT_8061b590 + rect[1]),
                 (double)rect[2],
                 (double)(rect[3] - FLOAT_8061b590),
                 (int*)&DAT_805ed4f8);
    FUN_8037bb9c((int*)&DAT_805ed4f8, arg, mode);
}

// ---------------------------------------------------------------------------
// prop_panel_init_c 0x803769c8
// ---------------------------------------------------------------------------
void prop_panel_init_c(void)
{
    FUN_8037b688();
}

// ============================================================================
// Address range: 0x80376a04 - 0x8037793c | batch_1049 | 2026-09-13
//
// Prop-panel LAYOUT BUILDERS. These are the routines that actually construct
// the objective/help panel: they spawn each prop instance (prop_instance_spawn),
// pose the sub-glyphs (prop_set_pose), then wire every slot's draw/update
// callback pair into the instance scale-vec slots (prop_set_scale_vec) - the
// callbacks are the prop_panel_* functions decompiled in batch_1047/1048. Each
// builder grabs the packed (owner,rect) pair via FUN_803826f0, spawns a fixed
// set of owner slots, and finishes with FUN_8038273c. Also here: the animated
// "insert-coin / press-A" prompt draw, an RNG caption picker, the panel timer
// scroll, and the active-panel set/clear setters.
//
//   prop_panel_set_rect_indexed 0x80376a04  indexed rect setter (stride 0x15 words)
//   prop_panel_draw_coin_prompt 0x80376a74  animate + draw the blinking A-button prompt sprite/text
//   prop_panel_build_main       0x80376c14  spawn+wire the main panel body (slots 0-6)
//   prop_panel_build_help       0x80376f98  spawn+wire the help caption (slot 7)
//   prop_panel_pick_caption     0x80377028  choose a random caption string id (0x1131 + rand)
//   prop_panel_build_status     0x80377088  spawn+wire the status/frame cluster (slots 0x10-0x14)
//   prop_panel_build_row_a      0x80377430  spawn+wire objective row A (slots 8-0xb)
//   prop_panel_build_row_b      0x803776ac  spawn+wire objective row B (slots 0xc-0xf)
//   prop_panel_set_active       0x80377928  set the active-panel obj + clear its cursor
//   prop_panel_clear_active     0x8037793c  clear the active-panel obj
//
// NOTE: non-matching decomp. The descriptor float-bit-pattern literals the
// original passes to prop_set_pose (z/w/extra) are preserved verbatim; slot
// callbacks are stored as int-cast function pointers, matching how the engine
// stashes them in the +0x24/+0x28/+0x2c scale-vec slots.
// ============================================================================

// indexed rect table state
extern int DAT_80612f3c;   // current rect index (stride 0x15 into DAT_80496378..84)

// active-panel object pointer
extern int DAT_80612f38;   // active prop-panel object

// coin-prompt animation state
extern float FLOAT_8061134c;   // blink phase accumulator
extern float FLOAT_80612f28;   // blink toggle latch

// timer-scroll / panel-frame copy state
extern int DAT_80496524;   // frame-copy source word
extern int DAT_80496538;   // frame-copy dest word

// prompt geometry / animation constants
extern float FLOAT_8061b5a0;   // blink lower bound
extern float FLOAT_8061b5a4;   // 0.5 (rounding bias / reset)
extern float FLOAT_8061b5a8;   // pose const 0
extern float FLOAT_8061b5ac;   // pose const 1
extern float FLOAT_8061b5b0, FLOAT_8061b5b4, FLOAT_8061b5b8, FLOAT_8061b5bc;
extern float FLOAT_8061b5c0, FLOAT_8061b5c4, FLOAT_8061b5c8, FLOAT_8061b5cc;
extern float FLOAT_8061b5d0, FLOAT_8061b5d4, FLOAT_8061b5d8, FLOAT_8061b5dc;
extern float FLOAT_8061b5e0, FLOAT_8061b5e4, FLOAT_8061b5e8, FLOAT_8061b5ec;
extern float FLOAT_8061b5f0, FLOAT_8061b5f4, FLOAT_8061b5f8, FLOAT_8061b5fc;
extern float FLOAT_8061b600, FLOAT_8061b604, FLOAT_8061b608, FLOAT_8061b60c;
extern float FLOAT_8061b610, FLOAT_8061b614, FLOAT_8061b620, FLOAT_8061b624;
extern float FLOAT_8061b628, FLOAT_8061b62c, FLOAT_8061b630;
extern double DOUBLE_8061b618;

// per-instance descriptor glyph blocks posed by the builders
extern int DAT_805ed588, DAT_805ed5a0;
extern int DAT_805edbf4, DAT_805edc0c, DAT_805edc24, DAT_805edc3c;
extern int DAT_805edc54, DAT_805edc6c, DAT_805edc84, DAT_805edc9c, DAT_805edcb4;
extern int DAT_805edccc, DAT_805edce4, DAT_805edcfc, DAT_805edd14, DAT_805edd2c, DAT_805edd44;
extern int DAT_805edd5c, DAT_805edd74, DAT_805edd8c, DAT_805edda4, DAT_805eddbc, DAT_805eddd4;

// frame-builder helpers (raw ABI, reached permissively via generated_externs)
extern int FUN_8037b830(int scratch, float* rec, int x, int y, int w, int h, int strId, int a, int b);
extern int FUN_8037bd80(int scratch, float* rec, int x, int y, int w, int h, int a, int b, int c);
extern int FUN_8037bec0(int dst, int* src);

// ---------------------------------------------------------------------------
// prop_panel_set_rect_indexed 0x80376a04
// Store a rect (x0,x1,y0,y1) into the indexed rect table (0x15-word stride).
// ---------------------------------------------------------------------------
void prop_panel_set_rect_indexed(int arg, float* rect)
{
    (void)arg;
    int i = DAT_80612f3c;
    (&DAT_80496378)[i * 0x15] = (int)rect[0];
    (&DAT_8049637c)[i * 0x15] = (int)rect[2];
    (&DAT_80496380)[i * 0x15] = (int)rect[1];
    (&DAT_80496384)[i * 0x15] = (int)rect[3];
}

// ---------------------------------------------------------------------------
// prop_panel_draw_coin_prompt 0x80376a74
// Advance the blink phase, pick the lit/unlit A-button sprite (tex 0x364/0x365)
// and draw it, then right-align the prompt caption (string 0x1247) beneath it.
// ---------------------------------------------------------------------------
void prop_panel_draw_coin_prompt(void)
{
    unsigned long long ctx = (unsigned long long)FUN_803826f0();
    float* r = (float*)(int)(ctx & 0xffffffffu);  // draw rect from the format frame
    int prevFont = text_set_font_table(0x1b);

    // decay the blink accumulator by the frame-scaled rate
    FLOAT_8061134c = -(FLOAT_806108ec *
                       (FLOAT_8061b598 / (float)((double)(unsigned)DAT_806109c0 - DOUBLE_8061b588))
                       - FLOAT_8061134c);
    if (FLOAT_8061134c <= FLOAT_8061b5a0) {
        FLOAT_8061134c = FLOAT_8061b5a4;
        FLOAT_80612f28 = (float)(FLOAT_8061b598 != FLOAT_80612f28 ? 0 : 1);
    }

    int tex = (FLOAT_8061b598 == FLOAT_80612f28) ? sprite_get_texture_id(0x364)
                                                 : sprite_get_texture_id(0x365);
    gx_set_active_material(tex);
    int md = *(int*)DAT_80610a20 + tex * 0x58;
    gx_draw_sprite_quad((double)r[0], (double)r[1], (double)r[2], (double)r[3], 0,
                        (unsigned)*(unsigned short*)(md + 0x52) << 4,
                        (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);

    unsigned char* s = (unsigned char*)get_string_by_id(0x1247);
    double w = text_get_char_width(s);
    double off = (double)((float)((double)(r[2] - r[0]) - w) * FLOAT_8061b5a4);
    s = (unsigned char*)get_string_by_id(0x1247);
    text_draw_char((double)(float)((double)r[0] + off),
                   (double)(FLOAT_8061b594 + r[1]), s, 0x80808080);
    text_set_font_table(prevFont);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_build_main 0x80376c14
// Spawn the main panel body: two backing glyphs, the panel frame (slot 3/5 with
// their draw callbacks + rounded-frame build), the timer readout (slot 4), and
// the coin-prompt (slot 6); slot 0 is the container (field14 = 3).
// ---------------------------------------------------------------------------
void prop_panel_build_main(void)
{
    int owner = FUN_803826f0();
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805ed588, 1.23454e-42f, 6.53846e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805ed5a0, 1.24015e-42f, 6.55387e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b5b0, (double)FLOAT_8061b5b4, (double)FLOAT_8061b5b8, (double)FLOAT_8061b5bc, -0x7fa12a48, owner, 1, 6.55527e-42f, -8.709137e-39f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edbf4, 1.23595e-42f, 6.63375e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805edc0c, 1.24295e-42f, 6.54547e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5ac, (float*)&DAT_805edc24, 1.23735e-42f, 6.63515e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5ac, (float*)&DAT_805edc3c, 1.23875e-42f, 6.52164e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b5c0, (double)FLOAT_8061b5c4, (double)FLOAT_8061b5c8, (double)FLOAT_8061b5cc, -0x7fa12a48, owner, 2, 6.54266e-42f, -8.711441e-39f, 5.60519e-45f);
    prop_instance_spawn((double)FLOAT_8061b594, (double)FLOAT_8061b5bc, (double)FLOAT_8061b5d0, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 3, 6.63375e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 3);
    prop_set_scale_vec(rec, (int)&prop_panel_init_b, (int)&prop_panel_set_rect2, 0);
    prop_instance_spawn((double)FLOAT_8061b594, (double)FLOAT_8061b5bc, (double)FLOAT_8061b5d0, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 5, 6.52164e-42f, 0.0f, 0.0f);
    float* r5 = (float*)prop_owner_slot_record(owner, 5);
    prop_set_scale_vec((int)r5, (int)&prop_panel_reset2, (int)&prop_panel_frame_draw_c, 0);
    memset_wrapper(-0x7fa12cb8, 0, 0x90);
    FUN_8037b830((int)&DAT_805ed348, r5, (int)(FLOAT_8061b590 + r5[0]), (int)(FLOAT_8061b590 + r5[1]), (int)r5[2], (int)(r5[3] - FLOAT_8061b590), 0x122f, 0x1a, 0xf);
    prop_instance_spawn((double)FLOAT_8061b5d8, (double)FLOAT_8061b5dc, (double)FLOAT_8061b5e0, (double)FLOAT_8061b5e4, -0x7fa12a48, owner, 4, 6.63515e-42f, 0.0f, 0.0f);
    rec = prop_owner_slot_record(owner, 4);
    prop_set_scale_vec(rec, (int)&prop_panel_timer_draw, (int)&prop_panel_sound_poll, (int)&prop_panel_sound_stop);
    prop_instance_spawn((double)FLOAT_8061b5e8, (double)FLOAT_8061b5ec, (double)FLOAT_8061b5f0, (double)FLOAT_8061b5b4, -0x7fa12a48, owner, 6, 6.55107e-42f, 0.0f, 0.0f);
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edc54, 1.24155e-42f, 6.55527e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805edc6c, 1.23314e-42f, 6.54266e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5e4, (double)FLOAT_8061b5a8, (float*)&DAT_805edc84, 1.22473e-42f, 6.55107e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b5f4, (double)FLOAT_8061b5f8, (double)FLOAT_8061b5fc, (double)FLOAT_8061b600, -0x7fa12a48, owner, 0, 6.54126e-42f, -8.711575e-39f, 4.2039e-45f);
    int slot0 = prop_owner_slot_record(owner, 0);
    prop_set_field14(slot0, 3);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_build_help 0x80376f98
// Spawn+wire the help caption (slot 7) with its draw/layout callbacks.
// ---------------------------------------------------------------------------
void prop_panel_build_help(int owner)
{
    prop_instance_spawn((double)FLOAT_8061b604, (double)FLOAT_8061b608, (double)FLOAT_8061b60c, (double)FLOAT_8061b60c, -0x7fa12a48, owner, 7, 6.55948e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 7);
    prop_set_scale_vec(rec, (int)&prop_panel_help_draw, (int)&prop_panel_help_layout, 0);
    rec = prop_owner_slot_record(owner, 7);
    prop_set_field14(rec, 3);
}

// ---------------------------------------------------------------------------
// prop_panel_pick_caption 0x80377028
// Pick a random caption string id in [0x1131 .. 0x1131 + range].
// ---------------------------------------------------------------------------
void prop_panel_pick_caption(void)
{
    unsigned r = rng_counter_tick();
    DAT_80612f4c = (int)(FLOAT_8061b610 *
                         ((float)((double)(unsigned)r - DOUBLE_8061b618) / FLOAT_8061b614)
                         + FLOAT_8061b5a4) + 0x1131;
}

// ---------------------------------------------------------------------------
// prop_panel_build_status 0x80377088
// Spawn+wire the status/frame cluster (slots 0x10-0x14): status readout, two
// rounded frames (A/B/D scratches), and a frame-copy of the theme color.
// ---------------------------------------------------------------------------
void prop_panel_build_status(void)
{
    int owner = FUN_803826f0();
    prop_instance_spawn((double)FLOAT_8061b594, (double)FLOAT_8061b5d8, (double)FLOAT_8061b620, (double)FLOAT_8061b620, -0x7fa12a48, owner, 0x14, 6.55808e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 0x14);
    prop_set_field14(rec, 1);
    prop_set_scale_vec(rec, (int)&prop_panel_draw_coin_prompt, (int)&prop_ambient_sound_update, 0);
    prop_instance_spawn((double)FLOAT_8061b594, (double)FLOAT_8061b5d8, (double)FLOAT_8061b620, (double)FLOAT_8061b620, -0x7fa12a48, owner, 0x11, 6.53285e-42f, 0.0f, 0.0f);
    float* r11 = (float*)prop_owner_slot_record(owner, 0x11);
    prop_set_scale_vec((int)r11, (int)&prop_panel_reset3, (int)&prop_panel_frame_draw_d, 0);
    memset_wrapper(-0x7fa12b08, 0, 0x90);
    FUN_8037b830((int)&DAT_805ed4f8, r11, (int)(FLOAT_8061b590 + r11[0]), (int)(FLOAT_8061b590 + r11[1]), (int)r11[2], (int)(r11[3] - FLOAT_8061b590), 0x1237, 0x1a, 0xf);
    memset_wrapper(-0x7fa12dd8, 0, 0x90);
    memset_wrapper(-0x7fa12d48, 0, 0x90);
    prop_instance_spawn((double)FLOAT_8061b590, (double)FLOAT_8061b5b0, (double)FLOAT_8061b5c8, (double)FLOAT_8061b5c0, -0x7fa12a48, owner, 0x12, 6.1489e-42f, 0.0f, 0.0f);
    float* r12 = (float*)prop_owner_slot_record(owner, 0x12);
    prop_set_scale_vec((int)r12, (int)&prop_panel_status_draw, (int)&prop_panel_frame_draw_a, 0);
    FUN_8037bd80((int)&DAT_805ed228, r12, (int)(FLOAT_8061b594 + r12[0]), (int)(FLOAT_8061b590 + r12[1]), (int)(r12[2] - FLOAT_8061b594), (int)(r12[3] - FLOAT_8061b590), 0x1a, 0xf, 0x13);
    DAT_80496538 = DAT_80496524;
    FUN_8037bec0(-0x7fa12dd8, &DAT_80496524);
    prop_instance_spawn((double)FLOAT_8061b590, (double)FLOAT_8061b5b0, (double)FLOAT_8061b5c8, (double)FLOAT_8061b5c0, -0x7fa12a48, owner, 0x13, 6.1489e-42f, 0.0f, 0.0f);
    float* r13 = (float*)prop_owner_slot_record(owner, 0x13);
    prop_set_scale_vec((int)r13, (int)&prop_panel_reset, (int)&prop_panel_frame_draw_b, 0);
    FUN_8037b830((int)&DAT_805ed2b8, r13, (int)(FLOAT_8061b594 + r13[0]), (int)(FLOAT_8061b590 + r13[1]), (int)(r13[2] - FLOAT_8061b594), (int)(r13[3] - FLOAT_8061b590), 0, 0x1a, 0xf);
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edc9c, 1.22754e-42f, 6.1489e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805edcb4, 1.23174e-42f, 6.53285e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b5f4, (double)FLOAT_8061b5f8, (double)FLOAT_8061b624, (double)FLOAT_8061b5c0, -0x7fa12a48, owner, 0x10, 6.54126e-42f, -8.711676e-39f, 2.8026e-45f);
    int slot10 = prop_owner_slot_record(owner, 0x10);
    prop_set_field14(slot10, 3);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_build_row_a 0x80377430
// Objective row A (slots 8-0xb): a label triple, an indexed-rect background
// (slot 10), the row frame (slot 0xb, owned-A scratch), container slot 8.
// ---------------------------------------------------------------------------
void prop_panel_build_row_a(void)
{
    int owner = FUN_803826f0();
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edccc, 1.23875e-42f, 6.56929e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805edce4, 1.23875e-42f, 6.57489e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5e4, (double)FLOAT_8061b5a8, (float*)&DAT_805edcfc, 1.23875e-42f, 6.5805e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b594, (double)FLOAT_8061b5bc, (double)FLOAT_8061b5d0, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 9, 6.56789e-42f, -8.711744e-39f, 4.2039e-45f);
    prop_instance_spawn((double)FLOAT_8061b628, (double)FLOAT_8061b62c, (double)FLOAT_8061b630, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 10, 6.56929e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 10);
    prop_set_scale_vec(rec, (int)&prop_panel_init_c, (int)&prop_panel_set_rect_indexed, 0);
    prop_instance_spawn((double)FLOAT_8061b628, (double)FLOAT_8061b62c, (double)FLOAT_8061b630, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 0xb, 6.53005e-42f, 0.0f, 0.0f);
    float* rb = (float*)prop_owner_slot_record(owner, 0xb);
    prop_set_scale_vec((int)rb, (int)&prop_panel_reset_by_slot, (int)&prop_panel_frame_draw_owned, 0);
    memset_wrapper(-0x7fa12c28, 0, 0x90);
    FUN_8037b830((int)&DAT_805ed3d8, rb, (int)(FLOAT_8061b590 + rb[0]), (int)(FLOAT_8061b590 + rb[1]), (int)rb[2], (int)(rb[3] - FLOAT_8061b590), 0x1235, 0x1a, 0xf);
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edd14, 1.24435e-42f, 6.56789e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805edd44, 1.23034e-42f, 6.53005e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5e4, (double)FLOAT_8061b5a8, (float*)&DAT_805edd2c, 1.22894e-42f, 6.56508e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b5f4, (double)FLOAT_8061b5f8, (double)FLOAT_8061b5b8, (double)FLOAT_8061b5c0, -0x7fa12a48, owner, 8, 6.56368e-42f, -8.711845e-39f, 4.2039e-45f);
    int slot8 = prop_owner_slot_record(owner, 8);
    prop_set_field14(slot8, 3);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_build_row_b 0x803776ac
// Objective row B (slots 0xc-0xf): same shape as row A but stroking the owned-B
// frame scratch (805ed468).
// ---------------------------------------------------------------------------
void prop_panel_build_row_b(void)
{
    int owner = FUN_803826f0();
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edd5c, 1.23875e-42f, 6.57069e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805edd74, 1.23875e-42f, 6.57629e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5e4, (double)FLOAT_8061b5a8, (float*)&DAT_805edd8c, 1.23875e-42f, 6.5819e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b594, (double)FLOAT_8061b5bc, (double)FLOAT_8061b5d0, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 0xd, 6.56789e-42f, -8.711945e-39f, 4.2039e-45f);
    prop_instance_spawn((double)FLOAT_8061b628, (double)FLOAT_8061b62c, (double)FLOAT_8061b630, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 0xe, 6.56929e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 0xe);
    prop_set_scale_vec(rec, (int)&prop_panel_init_c, (int)&prop_panel_set_rect_indexed, 0);
    prop_instance_spawn((double)FLOAT_8061b628, (double)FLOAT_8061b62c, (double)FLOAT_8061b630, (double)FLOAT_8061b5d4, -0x7fa12a48, owner, 0xf, 6.53005e-42f, 0.0f, 0.0f);
    float* rf = (float*)prop_owner_slot_record(owner, 0xf);
    prop_set_scale_vec((int)rf, (int)&prop_panel_reset_by_slot, (int)&prop_panel_frame_draw_owned, 0);
    memset_wrapper(-0x7fa12b98, 0, 0x90);
    FUN_8037b830((int)&DAT_805ed468, rf, (int)(FLOAT_8061b590 + rf[0]), (int)(FLOAT_8061b590 + rf[1]), (int)rf[2], (int)(rf[3] - FLOAT_8061b590), 0x1235, 0x1a, 0xf);
    prop_set_pose((double)FLOAT_8061b5a8, (double)FLOAT_8061b5a8, (float*)&DAT_805edda4, 1.24435e-42f, 6.56648e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5ac, (double)FLOAT_8061b5a8, (float*)&DAT_805eddd4, 1.23034e-42f, 6.52865e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b5e4, (double)FLOAT_8061b5a8, (float*)&DAT_805eddbc, 1.22894e-42f, 6.56508e-42f, 2.8026e-45f);
    prop_instance_spawn((double)FLOAT_8061b5f4, (double)FLOAT_8061b5f8, (double)FLOAT_8061b5b8, (double)FLOAT_8061b5c0, -0x7fa12a48, owner, 0xc, 6.56368e-42f, -8.712046e-39f, 4.2039e-45f);
    int slotc = prop_owner_slot_record(owner, 0xc);
    prop_set_field14(slotc, 3);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_set_active 0x80377928
// Set the active prop-panel object and clear its cursor field (*(obj+0x7c)+0x20).
// ---------------------------------------------------------------------------
void prop_panel_set_active(int obj)
{
    DAT_80612f38 = obj;
    *(int*)(*(int*)(obj + 0x7c) + 0x20) = 0;
}

// ---------------------------------------------------------------------------
// prop_panel_clear_active 0x8037793c
// Clear the active prop-panel object.
// ---------------------------------------------------------------------------
void prop_panel_clear_active(void)
{
    DAT_80612f38 = 0;
}

// ============================================================================
// Address range: 0x80377948 - 0x80378530 | batch_1050 | 2026-09-13
//
// Prop-panel HIGH-LEVEL LOGIC: the scripted-object callbacks and per-frame ticks
// that sit above the layout builders (batch_1049). These are wired in by the
// engine's scripted-prop dispatcher: each level "prop" object carries a 4-char
// magic tag at *(obj+0x10) (e.g. '3PCL', 'L437', 'AGUN') and prop_panel_type_init
// selects the right builder script (the batch_1049 PTR_prop_panel_build_* tables).
//
//   prop_reactor_end_match    0x80377948  GHZ reactor: enable the spark jet + end the match
//   prop_countdown_play_cue   0x80377990  play the 3-2-1 countdown sound cue (0x564/0x565/0x566)
//   prop_reactor_tick         0x80377a0c  '3PCL' reactor countdown tick -> activate + hud countdown
//   prop_panel_select_dispatch 0x80377adc key the packed (string-id) selection to a panel action
//   prop_panel_hit_test       0x80377f08  panel/pad hit-test ('3PCL' zone vs active-panel compare)
//   prop_ltd_sequence_tick    0x80377f6c  'LTD1'/'ZLFT' scripted trigger + bark sequence tick
//   prop_panel_type_init      0x803780d8  magic-tag -> builder script selector
//   prop_preload_panel_textures 0x80378298 preload the 18-strong panel texture set
//   prop_player_mount_tick    0x80378358  'AGUN'/'NFC4' player mount/dismount state machine
//   prop_options_screen_draw  0x80378530  draw the options/toggles screen (strings 0x1038-0x1040)
//
// NOTE: non-matching decomp. The large select dispatch preserves the original's
// exact string-id comparison tree; the mount tick preserves the entity flag
// OR/AND masks (0x80/0x4000000/0xfbffffff) verbatim.
// ============================================================================

// scripted-object / match state
extern int   DAT_805015c4;    // GHZ reactor world flags
extern int   DAT_805015bd;    // level variant byte
extern int   DAT_80496580;    // panel control flags
extern int   DAT_80612f54;    // "class complete" latch
extern int   DAT_80612f50;    // LTD sequence latch
extern int   DAT_80612f2c;    // bark-played latch
extern float FLOAT_80612f34;  // reactor countdown phase
extern float FLOAT_80612f30;  // LTD countdown phase
extern float FLOAT_80612f70;  // mount animation phase
extern float FLOAT_80611350;  // options blink phase
extern int   DAT_80611d74;    // active class-complete owner

// select-dispatch / mount constants
extern float FLOAT_8061b634, FLOAT_8061b638, FLOAT_8061b640, FLOAT_8061b644, FLOAT_8061b648;
extern float FLOAT_8061b680, FLOAT_8061b684, FLOAT_8061b688, FLOAT_8061b68c, FLOAT_8061b690;
extern double DOUBLE_8061b698;

// options-screen layout + sprite constants
extern float FLOAT_8061b6a0, FLOAT_8061b6a4, FLOAT_8061b6a8, FLOAT_8061b6ac;
extern int   DAT_805eddf0, DAT_805eddf8, DAT_805edff8, DAT_805edf98;
extern int   DAT_805edfa0, DAT_805edfa4, DAT_805edfa8, DAT_805edfac;

// type-init theme rect words + theme float constants
extern int   DAT_804961f8, DAT_804961fc, DAT_80496200, DAT_80496228, DAT_8049622c, DAT_80496230;
extern float FLOAT_8061b64c, FLOAT_8061b650, FLOAT_8061b654, FLOAT_8061b658, FLOAT_8061b65c, FLOAT_8061b660;
extern float FLOAT_8061b664, FLOAT_8061b668, FLOAT_8061b66c, FLOAT_8061b670, FLOAT_8061b674, FLOAT_8061b678;

// builder script pointer tables (built by the batch_1049 build_* callbacks)
extern int PTR_prop_panel_build_main_8049653c;
extern int PTR_prop_panel_build_help_80496554;
extern int PTR_prop_panel_build_status_8049656c;
extern int PTR_prop_panel_build_row_a_80496584;
extern int PTR_prop_panel_build_row_b_8049659c;

// callees new to this file
// (ghz_reactor_spark_jet_enable and hud_text_submit come from generated_decls.h
// as void / int(int,int,u32*) respectively - do not redeclare)
extern int  DAT_80611934;   // GHZ reactor stage counter
extern void match_process_end(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9);
extern void sound_set_voice_volume_scale(int scale);
extern void hud_countdown_set(double t, void* onDone, void* onTick);
extern void statlvl_class_complete(double a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8);
extern void statlvl_class_arm(int n);
extern unsigned statlvl_class_is_state5(int n);
extern int  trigger_slot_resolve_entity(int tag);
extern void trigger_clear_flags(int ent, int mask);
extern void trigger_open_full(int ent, int mode);
extern void FUN_8037d238(int obj);
extern int  sound_resolve_sample_id(int id);
extern int  soundpad_get_entity_id_from_zone(int zone);
extern void aiactor_play_bark_global(int barkId, int prio, int* opt);
extern void text_apply_alpha_fade(double w, unsigned char* str, int a, int b);
extern void text_set_color_rgb(double r, double g, double b);
extern void toggle_button_draw(void);

// ---------------------------------------------------------------------------
// prop_reactor_end_match 0x80377948
// On GHZ reactor stage 7: enable the spark-jet FX and end the match.
// ---------------------------------------------------------------------------
void prop_reactor_end_match(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)a1;
    if (DAT_80611934 == 7) {
        // spark-jet enable returns void in decls; the original forwards its EAX
        // result as match_process_end's first arg - read it via an int cast.
        typedef int (*SparkJetFnI)(void);
        int fx = ((SparkJetFnI)&ghz_reactor_spark_jet_enable)();
        DAT_805015c4 = DAT_805015c4 | 0x10000;
        match_process_end(fx, a2, a3, a4, a5, a6, a7, a8, 0);
    }
}

// ---------------------------------------------------------------------------
// prop_countdown_play_cue 0x80377990
// Play the 3/2/1 countdown sound cue depending on the remaining count parity.
// ---------------------------------------------------------------------------
void prop_countdown_play_cue(unsigned count)
{
    int params[14];
    if (count == 0) {
        sound_init_play_params((int)params);
        params[0] = 0x566;
        sound_play_request(params);
    } else if ((count & 1) == 0) {
        sound_init_play_params((int)params);
        params[0] = 0x565;
        sound_play_request(params);
    } else {
        sound_init_play_params((int)params);
        params[0] = 0x564;
        sound_play_request(params);
    }
}

// ---------------------------------------------------------------------------
// prop_reactor_tick 0x80377a0c
// '3PCL' reactor: decay a countdown phase; on underflow activate the panel,
// duck the voice mix, and arm the HUD countdown with the end/cue callbacks.
// ---------------------------------------------------------------------------
void prop_reactor_tick(int obj)
{
    if (**(int**)(obj + 0x10) == 0x4c435033) {   // '3PCL'
        FLOAT_80612f34 = -(FLOAT_806108ec *
                           (FLOAT_8061b598 / (float)((double)(unsigned)DAT_806109c0 - DOUBLE_8061b588))
                           - FLOAT_80612f34);
        if (FLOAT_80612f34 <= FLOAT_8061b5a0) {
            FLOAT_80612f34 = FLOAT_8061b5a0;
            prop_panel_update_loop();
            prop_activate_entry();
            sound_set_voice_volume_scale(3);
            hud_countdown_set((double)FLOAT_8061b5b4, (void*)&prop_reactor_end_match, (void*)&prop_countdown_play_cue);
            DAT_80496580 = DAT_80496580 & 0xfffffffe;
        }
    }
}

// ---------------------------------------------------------------------------
// prop_panel_select_dispatch 0x80377adc
// Map the packed selection string-id (low word of the format frame) to a panel
// action - most ids just re-activate the entry; a handful open sub-panels, mark
// class completion, drive the DAT_80612f3c page index, or start a voice sample.
// ---------------------------------------------------------------------------
void prop_panel_select_dispatch(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)a1;
    unsigned long long packed = (unsigned long long)FUN_803826f0();
    int owner = (int)(packed >> 0x20);
    int id = (int)packed;
    int st = *(int*)(owner + 0x7c);
    if (id == 0x1245) {
        prop_mount_panel_open();
    } else if (id < 0x1245) {
        if (id == 0x1236) {
            statlvl_class_complete((double)FLOAT_8061b610, a2, a3, a4, a5, a6, a7, a8);
            prop_activate_entry();
            DAT_80612f54 = 1;
        } else if (id < 0x1236) {
            if (id == 0x122e) {
                prop_activate_entry();
            } else if (id < 0x122e) {
                if (id == 0x1124) prop_activate_entry();
            } else if (id == 0x1234) {
                prop_activate_entry();
            } else if (id < 0x1234 && 0x1232 < id) {
                prop_activate_entry();
            }
        } else if (id != 0x123e) {
            if (id < 0x123e) {
                if (id == 0x123a) {
                    int ent = trigger_slot_resolve_entity(0x53434431);   // 'SCD1'
                    trigger_clear_flags(ent, 2);
                    ent = trigger_slot_resolve_entity(0x53434431);
                    trigger_open_full(ent, 1);
                    statlvl_class_complete((double)FLOAT_8061b5a0, a2, a3, a4, a5, a6, a7, a8);
                    if (DAT_80611d74 != 0) {
                        unsigned* s = (unsigned*)get_string_by_id(0x123b);
                        hud_text_submit(*(int*)(DAT_80611d74 + 4), 2, s);
                    }
                    prop_mount_panel_open();
                } else if (0x1239 < id && 0x123c < id) {
                    prop_activate_entry();
                }
            } else if (id == 0x1243) {
                prop_activate_entry();
            } else if (id < 0x1243 && id < 0x1240) {
                prop_mount_panel_open();
            }
        }
    } else if (id == 0x1254) {
        int rec = prop_owner_slot_record(st, 10);
        *(int*)(rec + 0x10) = 0x1254;
        prop_activate_entry();
        DAT_80612f3c = 2;
    } else if (id < 0x1254) {
        if (id == 0x124f) {
            prop_activate_entry();
        } else if (id < 0x124f) {
            if (id == 0x124d) {
                FUN_8037d238(st);
            } else if (id < 0x124d) {
                if (id < 0x1247) prop_activate_entry();
            } else {
                prop_activate_entry();
            }
        } else if (id == 0x1251) {
            int rec = prop_owner_slot_record(st, 0xe);
            *(int*)(rec + 0x10) = 0x1250;
            prop_activate_entry();
            DAT_80612f3c = 1;
        } else if (id < 0x1251) {
            int rec = prop_owner_slot_record(st, 10);
            *(int*)(rec + 0x10) = 0x1250;
            prop_activate_entry();
            DAT_80612f3c = 1;
        }
    } else if (id == 0x1259) {
        int rec = prop_owner_slot_record(st, 0xe);
        *(int*)(rec + 0x10) = 0x1258;
        prop_activate_entry();
        DAT_80612f3c = 3;
    } else if (id < 0x1259) {
        if (id < 0x1258) {
            if (id < 0x1256) {
                int rec = prop_owner_slot_record(st, 0xe);
                *(int*)(rec + 0x10) = 0x1254;
                prop_activate_entry();
                DAT_80612f3c = 2;
            }
        } else {
            int rec = prop_owner_slot_record(st, 10);
            *(int*)(rec + 0x10) = 0x1258;
            prop_activate_entry();
            DAT_80612f3c = 3;
        }
    } else if (id == 0x127f) {
        // prop_activate_entry is void here; the original reads its EAX result as
        // an "activated" flag. Reach an int-returning view via a cast, and use
        // the owner record base for the positional voice params.
        typedef int (*ActEntryFnI)(void);
        int rec = owner;
        if (((ActEntryFnI)&prop_activate_entry)() != 0) {
            int sample = sound_resolve_sample_id(0x1280);
            int params[18];
            sound_init_play_params((int)params);
            *(unsigned*)((char*)params + 0x18) |= 0x2a0;
            *(unsigned char*)((char*)params + 0x30) = 1;
            *(float*)((char*)params + 0x34) = FLOAT_8061b634;
            *(float*)((char*)params + 0x20) = FLOAT_8061b638;
            *(float*)((char*)params + 0x10) = FLOAT_8061b5a0;
            *(int*)((char*)params + 0x1c) = *(int*)(rec + 100);
            *(int*)((char*)params + 0x24) = *(int*)(rec + 0x68);
            *(int*)((char*)params + 0x28) = *(int*)(rec + 0x6c);
            params[0] = sample;
            DAT_80612f44 = sound_play_request(params);
            DAT_80612f40 = DAT_80611928 - DAT_806109c0;
        }
    } else if (id < 0x127f && 0x127d < id) {
        prop_activate_entry();
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_panel_hit_test 0x80377f08
// For a '3PCL' pad, resolve the entity in the pad zone and test id 0x5a1;
// otherwise report whether the object is NOT the active panel.
// ---------------------------------------------------------------------------
unsigned prop_panel_hit_test(int obj, int ctx)
{
    unsigned r;
    if (**(int**)(obj + 0x2d8) == 0x4c435033) {   // '3PCL'
        int id = soundpad_get_entity_id_from_zone(*(int*)(*(int*)(ctx + 0x7c) + 0x678));
        r = (unsigned)countLeadingZeros(0x5a1 - id) >> 5;
    } else {
        r = (unsigned)(obj != DAT_80612f38);
    }
    return r;
}

// ---------------------------------------------------------------------------
// prop_ltd_sequence_tick 0x80377f6c
// Once the class is complete: arm the 'LTD1' trigger + pose the 'ZLFT' entity,
// then run a countdown that fires a global bark (0x1204) when it elapses.
// ---------------------------------------------------------------------------
void prop_ltd_sequence_tick(int obj)
{
    if (DAT_80612f54 != 0 && prop_entry_flag_bit1(obj, 0x11) == 0 && DAT_80612f50 == 0) {
        statlvl_class_arm(3);
        prop_mount_panel_open();
        int t = trigger_slot_resolve_entity(0x4c544431);   // 'LTD1'
        trigger_clear_flags(t, 2);
        t = trigger_slot_resolve_entity(0x4c544431);
        trigger_open_full(t, 1);
        int e = entity_handle_resolve(0x5a4c4654);          // 'ZLFT'
        *(float*)(e + 100) = FLOAT_8061b640;
        *(float*)(e + 0x68) = FLOAT_8061b644;
        *(float*)(e + 0x6c) = FLOAT_8061b648;
        int st = *(int*)(e + 0x7c);
        *(int*)(st + 0x38) = 0;
        *(int*)(st + 8) = 4;
        DAT_80612f50 = 1;
        DAT_80496580 = DAT_80496580 | 1;
    }
    if (DAT_80612f54 != 0 && DAT_80612f50 != 0) {
        FLOAT_80612f30 = -(FLOAT_806108ec *
                          (FLOAT_8061b598 / (float)((double)(unsigned)DAT_806109c0 - DOUBLE_8061b588))
                          - FLOAT_80612f30);
    }
    if (FLOAT_80612f30 < FLOAT_8061b5a0 && DAT_80612f2c == 0) {
        aiactor_play_bark_global(0x1204, 0x100, (int*)0);
        DAT_80612f2c = 1;
    }
}

// ---------------------------------------------------------------------------
// prop_panel_type_init 0x803780d8
// Select the panel builder script from the object's 4-char magic tag, and (for
// the 'L437' variant) pick one of two theme-rect layouts based on level state.
// ---------------------------------------------------------------------------
void prop_panel_type_init(int obj, unsigned* pos)
{
    int tag = **(int**)(obj + 0x10);
    if (tag == 0x4c435035) {              // 'L4C5' -> row B
        prop_entity_init_state(obj, &PTR_prop_panel_build_row_b_8049659c, 0, pos, 1);
    } else if (tag < 0x4c435035) {
        if (tag == 0x4c435033) {          // '3PCL' -> status
            prop_entity_init_state(obj, &PTR_prop_panel_build_status_8049656c, 0, pos, 1);
        } else if (0x4c435032 < tag) {    // 'L4C4' -> row A (+ theme rect)
            prop_entity_init_state(obj, &PTR_prop_panel_build_row_a_80496584, 0, pos, 1);
            if (DAT_805015bd == '\n' && 1 < DAT_80612610) {
                DAT_804961f8 = (int)FLOAT_8061b664;  DAT_804961fc = (int)FLOAT_8061b668;
                DAT_80496200 = (int)FLOAT_8061b66c;  DAT_80496228 = (int)FLOAT_8061b670;
                DAT_8049622c = (int)FLOAT_8061b674;  DAT_80496230 = (int)FLOAT_8061b678;
            } else {
                DAT_804961f8 = (int)FLOAT_8061b64c;  DAT_804961fc = (int)FLOAT_8061b650;
                DAT_80496200 = (int)FLOAT_8061b654;  DAT_80496228 = (int)FLOAT_8061b658;
                DAT_8049622c = (int)FLOAT_8061b65c;  DAT_80496230 = (int)FLOAT_8061b660;
            }
        }
    } else if (tag == 0x4c637032) {       // 'Lcp2' -> help
        prop_entity_init_state(obj, &PTR_prop_panel_build_help_80496554, 0, pos, 1);
    } else if (tag < 0x4c637032 && 0x4c637030 < tag) {  // 'Lcp1' -> main
        prop_entity_init_state(obj, &PTR_prop_panel_build_main_8049653c, 0, pos, 1);
    }
}

// ---------------------------------------------------------------------------
// prop_preload_panel_textures 0x80378298
// Zero the 0x4c load context then chain 18 sprite_load_texture_set calls to
// resident-load the whole panel texture set.
// ---------------------------------------------------------------------------
void prop_preload_panel_textures(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    memset_wrapper(-0x7fa12a48, 0, 0x4c);
    int h = sprite_load_texture_set(a1, a2, a3, a4, a5, a6, a7, a8);
    for (int i = 0; i < 17; i++) {
        h = sprite_load_texture_set(h, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// prop_player_mount_tick 0x80378358
// 'AGUN'/'NFC4' mounted-turret state machine: animate the mount phase, and on
// mount/dismount set the player + turret entity state flags and back-references.
// ---------------------------------------------------------------------------
void prop_player_mount_tick(void)
{
    int obj = FUN_803826f0();
    if (**(int**)(obj + 0x10) == 0x4e464334              // 'NFC4'
        && FLOAT_8061b680 != FLOAT_80612f70
        && FLOAT_8061b684 != FLOAT_80612f70) {
        FLOAT_80612f70 = -(FLOAT_806108ec *
                          (FLOAT_8061b688 / (float)((double)(unsigned)DAT_806109c0 - DOUBLE_8061b698))
                          - FLOAT_80612f70);
        if (FLOAT_8061b68c < FLOAT_80612f70) {
            if (FLOAT_80612f70 < FLOAT_8061b690) {
                prop_set_idle_if_valid(*(int**)(obj + 0x78));
            }
        } else {
            prop_mount_noop();
            prop_mount_select_next();
            prop_reset_active_state(*(int**)(obj + 0x78));
            FLOAT_80612f70 = FLOAT_8061b680;
        }
    }
    if (FLOAT_8061b680 == FLOAT_80612f70 && statlvl_class_is_state5(2) != 0) {
        int st = *(int*)(*(int*)(*(int*)(obj + 0x14) + 0x14) + 0x7c);
        int* gun = (int*)entity_get_field_7c(0x4147554e);   // 'AGUN'
        FLOAT_80612f70 = FLOAT_8061b684;
        player_set_state(*(int*)(obj + 0x14), 0x2000);
        *(int*)(st + 0xc14) = 0;
        gun[0x36] = 0;
        gun[0x37] = (int)FLOAT_8061b68c;
        *(unsigned*)(*gun + 0x28) = *(unsigned*)(*gun + 0x28) & 0xfbffffff;
    } else if (FLOAT_8061b680 == FLOAT_80612f70) {
        int* gun = (int*)entity_get_field_7c(0x4147554e);
        if (prop_player_mount_entity(*(int*)(obj + 0x14)) != 0) {
            int st = *(int*)(*(int*)(*(int*)(obj + 0x14) + 0x14) + 0x7c);
            player_set_state(*(int*)(obj + 0x14), 0x80);
            *(unsigned*)(*(int*)(obj + 0x14) + 0xc) = *(unsigned*)(*(int*)(obj + 0x14) + 0xc) | 0x80;
            *(int*)(st + 0xc14) = *gun;
            gun[0x36] = *(int*)(*(int*)(obj + 0x14) + 0x14);
            gun[0x37] = (int)FLOAT_8061b68c;
            *(unsigned*)(*gun + 0x28) = *(unsigned*)(*gun + 0x28) | 0x4000000;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// prop_options_screen_draw 0x80378530
// Draw the options/toggles screen: title (0x1038/0x1039), a column of labelled
// toggle rows (0x103a-0x1040), and an optional animated hint sprite (tex 0x369).
// ---------------------------------------------------------------------------
void prop_options_screen_draw(void)
{
    FUN_803826e4();
    int prevFont = text_set_font_table(0x1b);
    double baseY = (double)FLOAT_8061b6a0;
    int color = prop_get_text_color();

    unsigned char* s = (unsigned char*)get_string_by_id(0x1039);
    double w = text_get_char_width(s);
    float span = (float)(DAT_805eddf8 - DAT_805eddf0);
    text_get_char_height(s);
    double col2 = (double)((float)((double)((int)span - (int)w) - DOUBLE_8061b698) * FLOAT_8061b6a4);

    // draw origin (extraout_r4 in the raw decomp) comes from the format frame
    float* org = (float*)(int)((unsigned long long)FUN_803826f0());

    s = (unsigned char*)get_string_by_id(0x1038);
    text_apply_alpha_fade((double)((org[2] - org[0]) * FLOAT_8061b6a4), s, 0, 1);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(org[1] + FLOAT_8061b6a0), s, color);

    double gray = (double)FLOAT_8061b688;
    text_set_color_rgb(gray, gray, gray);
    s = (unsigned char*)get_string_by_id(0x1039);
    text_draw_char((double)(float)((double)DAT_805eddf0 + col2), (double)(org[1] + FLOAT_8061b6a0), s, color);

    text_set_font_table(0x1a);
    double y = (double)(float)(baseY + (double)FLOAT_8061b6a8);
    s = (unsigned char*)get_string_by_id(0x103a);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
    toggle_button_draw();

    y = (double)(float)(y + (double)FLOAT_8061b6ac);
    prop_label_button_draw();
    y = (double)(float)(y + (double)FLOAT_8061b6ac);
    // rows 0x103c .. 0x1040 each: label + toggle
    for (int id = 0x103c; id <= 0x1040; id++) {
        s = (unsigned char*)get_string_by_id(id);
        text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
        toggle_button_draw();
        y = (double)(float)(y + (double)FLOAT_8061b6ac);
    }
    text_set_font_table(prevFont);

    if (DAT_805edff8 == 0 && DAT_805edf98 != 0) {
        int tex = sprite_get_texture_id(0x369);
        FLOAT_80611350 = -(FLOAT_806108ec *
                          (FLOAT_8061b688 / (float)((double)(unsigned)DAT_806109c0 - DOUBLE_8061b698))
                          - FLOAT_80611350);
        if (FLOAT_8061b68c < FLOAT_80611350) {
            if (FLOAT_80611350 <= FLOAT_8061b6a4) {
                gx_set_active_material(tex);
                int md = *(int*)DAT_80610a20 + tex * 0x58;
                gx_draw_sprite_quad((double)DAT_805edfa0, (double)DAT_805edfa4, (double)DAT_805edfa8,
                                    (double)DAT_805edfac, 0,
                                    (unsigned)*(unsigned short*)(md + 0x52) << 4,
                                    (unsigned)*(unsigned short*)(md + 0x4e) << 4, 0);
            }
        } else {
            FLOAT_80611350 = FLOAT_8061b688;
        }
    }
    FUN_80382730();
}

// ============================================================================
// Address range: 0x803788bc - 0x80378ec8 | batch_1051 | 2026-09-13
//
// prop-panel BUTTON-ACTION CALLBACKS. These are the off/on click handlers the
// layout builders wire into the option-row toggle/icon buttons (the button
// records at DAT_805eddf0.., 0x60 stride). Each handler resolves the level-prop
// entities that carry a given 4-char magic tag (via entity_tag_resolve_all,
// which fills a small stack array of resolved records; each record's object
// pointer is at +0x7c) and pushes a state change into them, then latches the
// paired toggle-button off/on callbacks so the button reflects the new state.
//
// Magic tags used here (big-endian 4CC): 'ROOF' 0x524f4f46, 'G101' 0x47313031,
// 'LAS1'..'LAS5' 0x4c415331 + n. Button-record addresses are passed to
// toggle_button_set_callbacks as raw ints (the DAT_805ed... address literal).
//
//   prop_options_toggle_set_bounds 0x803788bc  reposition the 7 option buttons by panel delta
//   prop_roof_enable               0x80378990  ROOF props -> reset-active, toggle on
//   prop_roof_disable              0x80378a24  ROOF props -> set-idle, toggle off
//   prop_mount_open_action         0x80378ab8  open the mount panel + one-shot global bark 0x1059
//   prop_g101_enable               0x80378b08  G101 interactive props -> state 1, toggle on
//   prop_g101_disable              0x80378bac  G101 props -> state 8, toggle off
//   prop_las_link_disable          0x80378c1c  LAS1-5: on an active emitter, clear the paired receiver
//   prop_las_link_enable           0x80378d18  LAS1-5: on an active emitter, arm the paired receiver
//   prop_turret_dismount_all       0x80378e18  dismount every mounted-turret player
//   prop_turret_reload_all         0x80378ec8  reload every mounted-turret player
//
// NOTE: non-matching decomp. Struct offsets (object +0x7c, entity +0x94 tag id,
// +0x18 flags, +0x1cc activation float, receiver +0x04 state / +0x0c timer),
// the entity_tag_resolve_all 4CC ids and the button-callback latch order all
// match the original. The GameCube callee-save spill is dropped.

// ---- batch_1051 raw externs ------------------------------------------------
extern int   DAT_805ede50, DAT_805ede80, DAT_805edee0, DAT_805edf40, DAT_805ee000;
extern int   DAT_80612f6c;    // one-shot "mount hint bark played" flag
extern int   DAT_80588610;    // local-player object-pointer table base
extern int   DAT_80612098;    // total player-slot count (upper bound)

// ============================================================================
// Address: 0x803788bc | Name: prop_options_toggle_set_bounds
// Reposition all seven option-panel buttons: shift each button's live bounds by
// the panel-origin delta (current top-left param_2[0]/[1] minus the cached
// origin param_2[0xf]/[0x10]). Six are toggle buttons, one (icon) is the
// mount/action push button.
// ============================================================================
void prop_options_toggle_set_bounds(int panel, float* p) {
    (void)panel;
    double dx = (double)(p[0] - p[0xf]);
    double dy = (double)(p[1] - p[0x10]);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805eddf0);
    icon_button_set_bounds  (dx, dy, (float*)&DAT_805ede50);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ede80);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805edee0);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805edf40);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805edfa0);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ee000);
}

// ============================================================================
// Address: 0x80378990 | Name: prop_roof_enable
// "Enable" click for the ROOF prop group: reset each ROOF prop back to its
// active/default state, then latch the button's on-callback pair.
// ============================================================================
void prop_roof_enable(void) {
    int recs[14];
    __save_gpr_28();
    int n = entity_tag_resolve_all(0x524f4f46 /*'ROOF'*/, (int)recs, 7);
    for (int i = 0, off = 0; i < n; ++i, off += 4) {
        prop_reset_active_state(*(int**)(*(int*)((int)recs + off) + 0x7c));
    }
    toggle_button_set_callbacks((int)&DAT_805eddf0, 1, 1);
    icon_button_set_callback((int)&DAT_805ede50, 1);
    __restore_gpr_28();
}

// ============================================================================
// Address: 0x80378a24 | Name: prop_roof_disable
// "Disable" click for the ROOF prop group: idle each ROOF prop (if valid),
// then latch the button's off-callback pair.
// ============================================================================
void prop_roof_disable(void) {
    int recs[14];
    __save_gpr_28();
    int n = entity_tag_resolve_all(0x524f4f46 /*'ROOF'*/, (int)recs, 7);
    for (int i = 0, off = 0; i < n; ++i, off += 4) {
        prop_set_idle_if_valid(*(int**)(*(int*)((int)recs + off) + 0x7c));
    }
    toggle_button_set_callbacks((int)&DAT_805eddf0, 0, 1);
    icon_button_set_callback((int)&DAT_805ede50, 0);
    __restore_gpr_28();
}

// ============================================================================
// Address: 0x80378ab8 | Name: prop_mount_open_action
// The mount/action icon-button click: open the mount selection panel, and the
// first time only, play the "you can mount this" global voice bark (id 0x1059).
// ============================================================================
void prop_mount_open_action(void) {
    prop_mount_panel_open();
    if (DAT_80612f6c == 0) {
        aiactor_play_bark_global(0x1059, 0x100, (int*)0);
        DAT_80612f6c = 1;
    }
}

// ============================================================================
// Address: 0x80378b08 | Name: prop_g101_enable
// "Enable" click for the G101 prop group: for each interactive-type G101 prop
// not already in state 9, set its state field (+0x10) to 1; then latch on.
// ============================================================================
void prop_g101_enable(void) {
    int recs[10];
    FUN_803826e4();
    int n = entity_tag_resolve_all(0x47313031 /*'G101'*/, (int)recs, 4);
    for (int i = 0, off = 0; i < n; ++i, off += 4) {
        int* obj = *(int**)(*(int*)((int)recs + off) + 0x7c);
        if (prop_is_interactive_type(*obj) != 0 && obj[4] != 9) {
            obj[4] = 1;
        }
    }
    toggle_button_set_callbacks((int)&DAT_805ede80, 1, 1);
    FUN_80382730();
}

// ============================================================================
// Address: 0x80378bac | Name: prop_g101_disable
// "Disable" click for the G101 prop group: force every G101 prop's state field
// (+0x10) to 8; then latch off.
// ============================================================================
void prop_g101_disable(void) {
    int recs[6];
    int n = entity_tag_resolve_all(0x47313031 /*'G101'*/, (int)recs, 4);
    for (int i = 0, off = 0; i < n; ++i, off += 4) {
        int* rec = (int*)((int)recs + off);
        *(int*)(*(int*)(*rec + 0x7c) + 0x10) = 8;
    }
    toggle_button_set_callbacks((int)&DAT_805ede80, 0, 1);
}

// ============================================================================
// Address: 0x80378c1c | Name: prop_las_link_disable
// "Disable" click for the laser-link groups LAS1..LAS5. In each group, if an
// active emitter (tag id 0x4e2/0x4e3 with activation float +0x1cc above the
// global threshold) is present, walk the same group for its receiver (tag id
// 0x4e1): clear the receiver state (+0x04 -> 0) and set the emitter's link flag
// bit (+0x18 |= 0x40). Then latch the group's toggle on.
// ============================================================================
void prop_las_link_disable(void) {
    int recs[8];
    for (int g = 0; g < 5; ++g) {
        int n = entity_tag_resolve_all(g + 0x4c415331 /*'LAS1'+g*/, (int)recs, 5);
        for (int i = 0, off = 0; i < n; ++i, off += 4) {
            short tag = *(short*)(*(int*)((int)recs + off) + 0x94);
            if ((tag == 0x4e2 || tag == 0x4e3) &&
                FLOAT_8061b68c < *(float*)(*(int*)((int)recs + off) + 0x1cc)) {
                for (int j = 0, jo = 0; j < n; ++j, jo += 4) {
                    if (*(short*)(*(int*)((int)recs + jo) + 0x94) == 0x4e1) {
                        int rx = *(int*)(*(int*)((int)recs + jo) + 0x7c);
                        if (rx != 0) {
                            *(int*)(rx + 4) = 0;
                            *(unsigned int*)(*(int*)((int)recs + jo) + 0x18) |= 0x40;
                        }
                    }
                }
            }
        }
    }
    toggle_button_set_callbacks((int)&DAT_805edee0, 1, 1);
}

// ============================================================================
// Address: 0x80378d18 | Name: prop_las_link_enable
// "Enable" click for the laser-link groups LAS1..LAS5. Same emitter scan as the
// disable handler, but for each paired receiver (tag 0x4e1) it arms the link:
// receiver state (+0x04 -> 2), emitter link flag bit (+0x18 |= 0x40), and seeds
// the receiver timer (+0x0c) from the global threshold float. Then latch off.
// ============================================================================
void prop_las_link_enable(void) {
    int recs[8];
    for (int g = 0; g < 5; ++g) {
        int n = entity_tag_resolve_all(g + 0x4c415331 /*'LAS1'+g*/, (int)recs, 5);
        for (int i = 0, off = 0; i < n; ++i, off += 4) {
            float thr = FLOAT_8061b68c;
            short tag = *(short*)(*(int*)((int)recs + off) + 0x94);
            if ((tag == 0x4e2 || tag == 0x4e3) &&
                FLOAT_8061b68c < *(float*)(*(int*)((int)recs + off) + 0x1cc)) {
                for (int j = 0, jo = 0; j < n; ++j, jo += 4) {
                    if (*(short*)(*(int*)((int)recs + jo) + 0x94) == 0x4e1) {
                        int rx = *(int*)(*(int*)((int)recs + jo) + 0x7c);
                        if (rx != 0) {
                            *(int*)(rx + 4) = 2;
                            *(unsigned int*)(*(int*)((int)recs + jo) + 0x18) |= 0x40;
                            *(float*)(rx + 0xc) = thr;
                        }
                    }
                }
            }
        }
    }
    toggle_button_set_callbacks((int)&DAT_805edf40, 0, 1);
}

// ============================================================================
// Address: 0x80378e18 | Name: prop_turret_dismount_all
// "On" click of the mounted-turret toggle pair: latch both callbacks, then scan
// every active local-player slot (table DAT_80588610, [4] flags word) and, for
// each player currently flagged as mounted (bit 0x4000), request a dismount.
// ============================================================================
void prop_turret_dismount_all(void) {
    FUN_803826e8();
    toggle_button_set_callbacks((int)&DAT_805edfa0, 1, 1);
    toggle_button_set_callbacks((int)&DAT_805ee000, 1, 1);
    for (int i = DAT_80612610, off = DAT_80612610 << 2; i < DAT_80612098; ++i, off += 4) {
        int* pl = *(int**)((int)&DAT_80588610 + off);
        if ((pl[4] & 0x4000U) != 0) {
            aiactor_mount_request_dismount(*pl);
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address: 0x80378ec8 | Name: prop_turret_reload_all
// "Off" click of the mounted-turret toggle: latch off, then scan the active
// local-player slots and request a reload on each mounted-turret player.
// ============================================================================
void prop_turret_reload_all(void) {
    FUN_803826e8();
    toggle_button_set_callbacks((int)&DAT_805edfa0, 0, 1);
    for (int i = DAT_80612610, off = DAT_80612610 << 2; i < DAT_80612098; ++i, off += 4) {
        int* pl = *(int**)((int)&DAT_80588610 + off);
        if ((pl[4] & 0x4000U) != 0) {
            aiactor_mount_request_reload(*pl);
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address range: 0x80378f64 - 0x8037949c | batch_1052 | 2026-09-13
//
// prop-panel OPTIONS BUILDER + the second option-row's button callbacks. This
// closes out the prop options screen: batch_1051 was the first button group's
// handlers, batch_1050 the screen draw; this batch adds a second button group
// (records at DAT_805ee060..0x1e0), the trigger-gated door toggle, and the
// master builder prop_options_panel_build 0x8037949c which spawns the panel
// prop instance and calls toggle_button_init/icon_button_init to construct and
// wire EVERY option row - including the batch_1051 callbacks (prop_roof_enable,
// prop_mount_open_action, prop_g101_enable, prop_las_link_disable,
// prop_turret_dismount_all) - proving those names correct.
//
//   prop_options2_ff_reset            0x80378f64  clear the FF flag, latch button on
//   prop_options2_ff_bark             0x80378f9c  clear FF flag, one-shot bark 0x1061, latch off
//   prop_door_open_action             0x80378ff8  open trigger '0001', open mount panel, bark 0x105a
//   prop_door_close_action            0x80379090  close trigger '0001' if idle, re-arm flag 2
//   prop_options2_panel_draw          0x803790e4  draw the 2nd option group's 5 toggle rows
//   prop_options2_toggle_set_bounds   0x8037925c  reposition the 2nd group's 5 buttons by panel delta
//   prop_wdcg_trigger_open            0x80379310  trigger 'WDCG': set 0x4000, clear 2, open
//   prop_options2_turret_dismount_all 0x80379364  dismount every mounted-turret player (2nd panel)
//   prop_options2_turret_reload_all   0x80379400  reload every mounted-turret player (2nd panel)
//   prop_options_panel_build          0x8037949c  spawn+wire the whole options panel (all buttons)
//
// NOTE: non-matching decomp. entity_tag_resolve/trigger 4CC ids ('0001'
// 0x30303031, 'WDCG' 0x43445744), the button-record addresses, string ids
// (0x1041-0x104a) and callback wiring order match the original. The button
// callbacks passed to toggle_button_init are function pointers, packed to the
// widget's unsigned-int slots as (unsigned)(uintptr_t)&fn. icon_button_init is
// reached through a typed pointer cast matching the true 5-arg ABI (the batch
// _1043 definition decompiled with a narrower/register-mangled signature).

// ---- batch_1052 raw externs ------------------------------------------------
extern int   DAT_80612f60, DAT_80612f64, DAT_80612f68;   // options-panel one-shot flags
extern int   DAT_805ee060, DAT_805ee0c0, DAT_805ee120, DAT_805ee180, DAT_805ee1e0; // 2nd-group button records
extern float FLOAT_8061b6b0, FLOAT_8061b6b4, FLOAT_8061b6b8, FLOAT_8061b6bc, FLOAT_8061b6c0; // layout floats
extern int   DAT_803e0e58, DAT_803e0e5c, DAT_803e0e60, DAT_803e0e64;  // initial panel rect consts
extern void  trigger_set_flags(int ent, int mask);
extern void  trigger_close_if_idle(int ent);

// packed function-pointer helper for the widget callback slots
#define PROP_CB(fn) ((unsigned int)(unsigned long long)(void*)&(fn))

// ============================================================================
// Address: 0x80378f64 | Name: prop_options2_ff_reset
// Second option group, "on" click: clear the friendly-fire flag and latch the
// button's on-callback pair.
// ============================================================================
void prop_options2_ff_reset(void) {
    DAT_80612f64 = 0;
    toggle_button_set_callbacks((int)&DAT_805edfa0, 1, 1);
}

// ============================================================================
// Address: 0x80378f9c | Name: prop_options2_ff_bark
// Second option group, "off" click: clear the flag; the first time only, play
// the global voice bark 0x1061; then latch the off-callback pair.
// ============================================================================
void prop_options2_ff_bark(void) {
    DAT_80612f64 = 0;
    if (DAT_80612f68 == 0) {
        aiactor_play_bark_global(0x1061, 0x100, (int*)0);
        DAT_80612f68 = 1;
    }
    toggle_button_set_callbacks((int)&DAT_805edfa0, 0, 1);
}

// ============================================================================
// Address: 0x80378ff8 | Name: prop_door_open_action
// Door toggle "open" click (one-shot, guarded by DAT_80612f60): unlock the
// '0001' trigger (clear flag 2), fully open it, open the mount panel, and play
// the bark 0x105a. Then latch the button (on-state, no repeat).
// ============================================================================
void prop_door_open_action(void) {
    if (DAT_80612f60 == 0) {
        int t = trigger_slot_resolve_entity(0x30303031 /*'0001'*/);
        trigger_clear_flags(t, 2);
        t = trigger_slot_resolve_entity(0x30303031);
        trigger_open_full(t, 1);
        prop_mount_panel_open();
        aiactor_play_bark_global(0x105a, 0x100, (int*)0);
        DAT_80612f60 = 1;
    }
    toggle_button_set_callbacks((int)&DAT_805ee000, 1, 0);
}

// ============================================================================
// Address: 0x80379090 | Name: prop_door_close_action
// Door toggle "close" click: close the '0001' trigger if idle and re-arm its
// lock flag (bit 2). Latch the button off.
// ============================================================================
void prop_door_close_action(void) {
    int t = trigger_slot_resolve_entity(0x30303031 /*'0001'*/);
    trigger_close_if_idle(t);
    t = trigger_slot_resolve_entity(0x30303031);
    trigger_set_flags(t, 2);
    toggle_button_set_callbacks((int)&DAT_805ee000, 0, 1);
}

// ============================================================================
// Address: 0x803790e4 | Name: prop_options2_panel_draw
// Draw the second option group: five label+toggle rows (string ids 0x1046..
// 0x104a) laid out down the panel in font table 0x1a, spacing FLOAT_8061b6ac
// with a wider gap (FLOAT_8061b6b4) before the middle row. Panel origin comes
// from the caller's rect pointer (r4). Font table saved/restored around.
// ============================================================================
void prop_options2_panel_draw(float* org) {
    __save_gpr_28();
    int prevFont = text_set_font_table(0x1b);
    double y = (double)FLOAT_8061b6b0;
    unsigned int color = (unsigned int)prop_get_text_color();
    text_set_font_table(0x1a);
    y = (double)(float)(y + (double)FLOAT_8061b6ac);
    unsigned char* s = (unsigned char*)get_string_by_id(0x1048);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
    toggle_button_draw();
    y = (double)(float)(y + (double)FLOAT_8061b6ac);
    s = (unsigned char*)get_string_by_id(0x104a);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
    toggle_button_draw();
    y = (double)(float)(y + (double)FLOAT_8061b6b4);
    s = (unsigned char*)get_string_by_id(0x1046);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
    toggle_button_draw();
    y = (double)(float)(y + (double)FLOAT_8061b6ac);
    s = (unsigned char*)get_string_by_id(0x1047);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
    toggle_button_draw();
    y = (double)(float)(y + (double)FLOAT_8061b6ac);
    s = (unsigned char*)get_string_by_id(0x1049);
    text_draw_char((double)(FLOAT_8061b6a0 + org[0]), (double)(float)((double)org[1] + y), s, color);
    toggle_button_draw();
    text_set_font_table(prevFont);
    __restore_gpr_28();
}

// ============================================================================
// Address: 0x8037925c | Name: prop_options2_toggle_set_bounds
// Reposition the second option group's five toggle buttons by the panel-origin
// delta (current top-left p[0]/[1] minus cached origin p[0xf]/[0x10]).
// ============================================================================
void prop_options2_toggle_set_bounds(int panel, float* p) {
    (void)panel;
    double dx = (double)(p[0] - p[0xf]);
    double dy = (double)(p[1] - p[0x10]);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ee120);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ee180);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ee060);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ee1e0);
    toggle_button_set_bounds(dx, dy, (float*)&DAT_805ee0c0);
}

// ============================================================================
// Address: 0x80379310 | Name: prop_wdcg_trigger_open
// Force-open the 'WDCG' trigger: set its scripted-open flag 0x4000, clear the
// lock flag 2, and fully open it.
// ============================================================================
void prop_wdcg_trigger_open(void) {
    int t = trigger_slot_resolve_entity(0x43445744 /*'WDCG'*/);
    trigger_set_flags(t, 0x4000);
    trigger_clear_flags(t, 2);
    trigger_open_full(t, 1);
}

// ============================================================================
// Address: 0x80379364 | Name: prop_options2_turret_dismount_all
// Second-panel turret toggle "on": latch on, then dismount every mounted-turret
// player (flag 0x4000) in the active local-player slot range.
// ============================================================================
void prop_options2_turret_dismount_all(void) {
    FUN_803826e8();
    toggle_button_set_callbacks((int)&DAT_805ee0c0, 1, 1);
    for (int i = DAT_80612610, off = DAT_80612610 << 2; i < DAT_80612098; ++i, off += 4) {
        int* pl = *(int**)((int)&DAT_80588610 + off);
        if ((pl[4] & 0x4000U) != 0) {
            aiactor_mount_request_dismount(*pl);
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address: 0x80379400 | Name: prop_options2_turret_reload_all
// Second-panel turret toggle "off": latch off, then reload every mounted-turret
// player in the active local-player slot range.
// ============================================================================
void prop_options2_turret_reload_all(void) {
    FUN_803826e8();
    toggle_button_set_callbacks((int)&DAT_805ee0c0, 0, 1);
    for (int i = DAT_80612610, off = DAT_80612610 << 2; i < DAT_80612098; ++i, off += 4) {
        int* pl = *(int**)((int)&DAT_80588610 + off);
        if ((pl[4] & 0x4000U) != 0) {
            aiactor_mount_request_reload(*pl);
        }
    }
    FUN_80382734();
}

// ============================================================================
// Address: 0x8037949c | Name: prop_options_panel_build
// The master options-panel builder. Spawns the panel prop instance (descriptor
// DAT_805ee2xx, priority 7), then walks a running rect (seeded from the const
// block DAT_803e0e58.., advanced by FLOAT_8061b6ac per row) calling
// toggle_button_init / icon_button_init to construct and wire every option row:
//   row 0  toggle -> prop_roof_enable      / prop_roof_disable         (0x1041/0x1042)
//   row 1  icon   -> prop_mount_open_action                            (0x103b)
//   row 2  toggle -> prop_g101_enable       / prop_g101_disable
//   row 3  toggle -> prop_las_link_disable  / prop_las_link_enable
//   row 4  toggle -> prop_turret_dismount_all / prop_turret_reload_all
//   row 5  toggle -> prop_options2_ff_reset / prop_options2_ff_bark
//   row 6  toggle -> prop_door_open_action  / prop_door_close_action   (0x1043/0x1044)
// Each toggle latches its initial on-state via toggle_button_set_callbacks.
// ============================================================================
void prop_options_panel_build(int owner) {
    typedef void (*icon_init_fn)(unsigned int recPtr, unsigned int rectPtr,
                                 unsigned int strId, unsigned int clickCb, int spriteId);
    struct { unsigned int x0; float y0; unsigned int x1; float y1; } rect;

    prop_instance_spawn((double)FLOAT_8061b6b8, (double)FLOAT_8061b6a8, (double)FLOAT_8061b6bc,
                        (double)FLOAT_8061b6c0, (int)0x805ee240, owner, 0, 5.81679e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 0);
    prop_set_field14(rec, 7);
    prop_set_scale_vec(rec, (int)&prop_options_screen_draw, (int)&prop_options_toggle_set_bounds, 0);

    rect.x0 = (unsigned int)DAT_803e0e58;
    rect.y0 = *(float*)&DAT_803e0e5c;
    rect.x1 = (unsigned int)DAT_803e0e60;
    rect.y1 = *(float*)&DAT_803e0e64;

    toggle_button_init((unsigned int*)&DAT_805eddf0, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_roof_enable), PROP_CB(prop_roof_disable), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805eddf0, 1, 1);
    rect.y0 += FLOAT_8061b6ac;  rect.y1 += FLOAT_8061b6ac;
    ((icon_init_fn)&icon_button_init)((unsigned int)(unsigned long long)&DAT_805ede50,
                                      (unsigned int)(unsigned long long)&rect, 0x103b,
                                      PROP_CB(prop_mount_open_action), 0x368);
    rect.y0 += FLOAT_8061b6ac;  rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ede80, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_g101_enable), PROP_CB(prop_g101_disable), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805ede80, 1, 1);
    rect.y0 += FLOAT_8061b6ac;  rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805edee0, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_las_link_disable), PROP_CB(prop_las_link_enable), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805edee0, 1, 1);
    rect.y0 += FLOAT_8061b6ac;  rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805edf40, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_turret_dismount_all), PROP_CB(prop_turret_reload_all), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805edf40, 1, 1);
    rect.y0 += FLOAT_8061b6ac;  rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805edfa0, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_options2_ff_reset), PROP_CB(prop_options2_ff_bark), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805edfa0, 1, 1);
    rect.y0 += FLOAT_8061b6ac;  rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee000, (unsigned int*)&rect, 0x1043, 0x1044,
                       PROP_CB(prop_door_open_action), PROP_CB(prop_door_close_action), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805ee000, 0, 1);
    DAT_80612f64 = 1;
}

// ============================================================================
// Address range: 0x80379790 - 0x8037a28c | batch_1053 | 2026-09-13
//
// prop-panel OPTIONS SCREEN: per-frame hit-test dispatchers, two more panel
// builder variants, the second-panel per-frame update/state driver, the mount
// CONFIRM action, an NFC4 dismount, the prop-object TYPE dispatcher that binds
// each object magic tag to its panel vtable, an option-texture preload, a label
// draw pair, and a sub-panel hit-test. This is the tail of the options screen
// begun in batch_1050-1052.
//
//   prop_options_panel_hit_test   0x80379790  route a pointer click to the 7 option-row buttons
//   prop_options_panel2_build     0x803798ac  build panel VARIANT 2 (owner slot 1, rect DAT_803e0e68)
//   prop_options_panel3_build     0x80379aec  build panel VARIANT 3 (owner slot 2, rect DAT_803e0e78)
//   prop_options2_panel_update    0x80379d34  per-frame: recolor buttons from world state, hit-test, autoclose bark
//   prop_mount_confirm_action     0x80379f08  confirm mount: bind the AGUN turret to the player or reopen the panel
//   prop_nfc4_dismount            0x8037a028  dismount the NFC4 turret and reactivate its guard actors
//   prop_object_type_dispatch     0x8037a0dc  bind a prop object's panel vtable by its 4-char magic tag
//   prop_preload_option_textures  0x8037a1e0  zero the load ctx then chain 3 sprite_load_texture_set calls
//   prop_labels_draw_pair         0x8037a228  reset then draw two label buttons in font 0x1a
//   prop_subpanel_hit_test        0x8037a28c  frame the sub-panel rect, offset 2 icon buttons, hit-test
//
// NOTE: non-matching decomp. Object magic tags ('0012' 0x30303132, 'TPCB'
// 0x54504342, 'CR01' 0x43523031, 'NFC4' 0x4e464334, 'NFC2'/'NF1C'/type 0x4f1),
// struct offsets (obj +0x7c, entity +0x14/+0x28/+0xbf0, player-record 0xea0
// stride at DAT_80611d90, +0xbf0 class-record whose +0x08 is the tag), the
// button-record addresses and callback-latch order all match the original.
// Callbacks/scripts passed to init helpers are function pointers cast (int)&fn;
// vtable data pointers are cast (int)&SYM. icon_button_init keeps the typed-cast
// ABI shim from batch_1052.

// ---- batch_1053 raw externs ------------------------------------------------
extern int   DAT_80612f58;    // options2 autoclose-bark one-shot flag
extern int   DAT_80612798;    // "any CR01 present" world query result
extern int   DAT_806120fc;    // guard-actor slot count for the mounted turret
extern int   DAT_803e0e68, DAT_803e0e6c, DAT_803e0e70, DAT_803e0e74;  // panel-2 rect consts
extern int   DAT_803e0e78, DAT_803e0e7c, DAT_803e0e80, DAT_803e0e84;  // panel-3 rect consts
extern int   DAT_805ee328, DAT_805ee3b8, DAT_805ee3e8;   // sub-panel scratch rect + 2 icon records
extern float FLOAT_8061b6c8, FLOAT_8061b6cc, FLOAT_8061b6d0;  // sub-panel frame insets
// panel vtable data blocks (object-type -> panel behaviour table)
extern int   PTR_prop_options_panel_build_80496c34;
extern int   PTR_FUN_80496c4c;
extern int   PTR_FUN_80496c64;
extern int   DAT_80496c7c;
// soundpad / actor-activation helpers (defined in sibling modules)
extern int   spawnpoint_count_active_for_zone(void);
extern int*  soundpad_resolve_zone_node(int zone_id);
extern int   soundpad_get_entity_zone_id(int pad_entry);
extern int   soundpad_find_zone_for_entity(int entity_id);
extern unsigned int aiactor_deactivate(int actor);
extern unsigned int aiactor_activate(int actor);

// ============================================================================
// Address: 0x80379790 | Name: prop_options_panel_hit_test
// Per-frame click router for the first options panel: pull the pointer state
// (record via FUN_803826f0, hi=entity, lo=click-style), refresh the friendly-
// fire toggle's disabled state from the zone spawn count, then hit-test all
// seven option-row buttons in order.
// ============================================================================
void prop_options_panel_hit_test(void) {
    unsigned long long rec = FUN_803826f0();
    int ent   = (int)(rec >> 0x20);
    int style = (int)rec;
    spawnpoint_count_active_for_zone();
    if (DAT_805edf98 == 0) {
        toggle_button_set_callbacks((int)&DAT_805edfa0, 0, 0);
    }
    toggle_button_hit_test((float*)&DAT_805eddf0, ent, style);
    icon_button_hit_test  ((float*)&DAT_805ede50, ent, style);
    toggle_button_hit_test((float*)&DAT_805ede80, ent, style);
    toggle_button_hit_test((float*)&DAT_805edee0, ent, style);
    toggle_button_hit_test((float*)&DAT_805edf40, ent, style);
    toggle_button_hit_test((float*)&DAT_805edfa0, ent, style);
    toggle_button_hit_test((float*)&DAT_805ee000, ent, style);
    FUN_8038273c();
}

// ============================================================================
// Address: 0x803798ac | Name: prop_options_panel2_build
// Build options-panel VARIANT 2 (owner slot 1). Same shape as the batch_1052
// master builder: spawn the panel prop, wire the panel draw/bounds hooks, then
// init five rows (WDCG open, turret dismount/reload, and three display-only
// status toggles 0x104b-0x1052). Rect seeded from DAT_803e0e68.
// ============================================================================
void prop_options_panel2_build(int owner) {
    struct { unsigned int x0; float y0; unsigned int x1; float y1; } rect;
    prop_instance_spawn((double)FLOAT_8061b6b8, (double)FLOAT_8061b6a8, (double)FLOAT_8061b6bc,
                        (double)FLOAT_8061b6c0, (int)0x805ee240, owner, 1, 5.83641e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 1);
    prop_set_field14(rec, 7);
    prop_set_scale_vec(rec, (int)&prop_options2_panel_draw, (int)&prop_options2_toggle_set_bounds, 0);

    rect.x0 = (unsigned int)DAT_803e0e68; rect.y0 = *(float*)&DAT_803e0e6c;
    rect.x1 = (unsigned int)DAT_803e0e70; rect.y1 = *(float*)&DAT_803e0e74;

    toggle_button_init((unsigned int*)&DAT_805ee060, (unsigned int*)&rect, 0x1051, 0x1052,
                       PROP_CB(prop_wdcg_trigger_open), 0, 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805ee060, 0, 1);
    rect.y0 += FLOAT_8061b6ac; rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee0c0, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_options2_turret_dismount_all), PROP_CB(prop_options2_turret_reload_all), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805ee0c0, 1, 1);
    rect.y0 += FLOAT_8061b6b4; rect.y1 += FLOAT_8061b6b4;
    toggle_button_init((unsigned int*)&DAT_805ee120, (unsigned int*)&rect, 0x104b, 0x104c, 0, 0, 0, 0);
    toggle_button_set_callbacks((int)&DAT_805ee120, 0, 0);
    rect.y0 += FLOAT_8061b6ac; rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee180, (unsigned int*)&rect, 0x104d, 0x104e, 0, 0, 0, 0);
    toggle_button_set_callbacks((int)&DAT_805ee180, 1, 0);
    rect.y0 += FLOAT_8061b6ac; rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee1e0, (unsigned int*)&rect, 0x104f, 0x1052, 0, 0, 0, 0);
    toggle_button_set_callbacks((int)&DAT_805ee1e0, 0, 1);
}

// ============================================================================
// Address: 0x80379aec | Name: prop_options_panel3_build
// Build options-panel VARIANT 3 (owner slot 2). Identical row layout to variant
// 2 but resets the autoclose-bark flag DAT_80612f58 first and seeds the rect
// from the panel-3 const block DAT_803e0e78.
// ============================================================================
void prop_options_panel3_build(int owner) {
    struct { unsigned int x0; float y0; unsigned int x1; float y1; } rect;
    DAT_80612f58 = 0;
    prop_instance_spawn((double)FLOAT_8061b6b8, (double)FLOAT_8061b6a8, (double)FLOAT_8061b6bc,
                        (double)FLOAT_8061b6c0, (int)0x805ee240, owner, 2, 5.83641e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(owner, 2);
    prop_set_field14(rec, 7);
    prop_set_scale_vec(rec, (int)&prop_options2_panel_draw, (int)&prop_options2_toggle_set_bounds, 0);

    rect.x0 = (unsigned int)DAT_803e0e78; rect.y0 = *(float*)&DAT_803e0e7c;
    rect.x1 = (unsigned int)DAT_803e0e80; rect.y1 = *(float*)&DAT_803e0e84;

    toggle_button_init((unsigned int*)&DAT_805ee060, (unsigned int*)&rect, 0x1051, 0x1052,
                       PROP_CB(prop_wdcg_trigger_open), 0, 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805ee060, 0, 1);
    rect.y0 += FLOAT_8061b6ac; rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee0c0, (unsigned int*)&rect, 0x1041, 0x1042,
                       PROP_CB(prop_options2_turret_dismount_all), PROP_CB(prop_options2_turret_reload_all), 0x367, 0x368);
    toggle_button_set_callbacks((int)&DAT_805ee0c0, 1, 1);
    rect.y0 += FLOAT_8061b6b4; rect.y1 += FLOAT_8061b6b4;
    toggle_button_init((unsigned int*)&DAT_805ee120, (unsigned int*)&rect, 0x104b, 0x104c, 0, 0, 0, 0);
    toggle_button_set_callbacks((int)&DAT_805ee120, 0, 0);
    rect.y0 += FLOAT_8061b6ac; rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee180, (unsigned int*)&rect, 0x104d, 0x104e, 0, 0, 0, 0);
    toggle_button_set_callbacks((int)&DAT_805ee180, 1, 0);
    rect.y0 += FLOAT_8061b6ac; rect.y1 += FLOAT_8061b6ac;
    toggle_button_init((unsigned int*)&DAT_805ee1e0, (unsigned int*)&rect, 0x104f, 0x1052, 0, 0, 0, 0);
    toggle_button_set_callbacks((int)&DAT_805ee1e0, 0, 1);
}

// ============================================================================
// Address: 0x80379d34 | Name: prop_options2_panel_update
// Per-frame update for the second options panel. Refreshes each button's
// enabled/on state from live world queries: the '0012' trigger's flag 2, the
// zone query DAT_80612798, and a scan of the active local-player class records
// (+0xbf0, class tag at +0x08) for a 'TPCB' (enable WDCG) or 'CR01' (enable the
// turret row). Then hit-tests the two live buttons and, once, plays the panel
// autoclose voice bark 0x10c0.
// ============================================================================
void prop_options2_panel_update(void) {
    unsigned long long rec = FUN_803826e4();
    int ent = (int)(rec >> 0x20);
    int t = trigger_slot_resolve_entity(0x30303132 /*'0012'*/);
    bool haveCR01 = false;
    t = *(int*)(t + 0x7c);
    bool haveTPCB = false;
    if (DAT_80612798 == 0) toggle_button_set_callbacks((int)&DAT_805ee1e0, 0, 0);
    else                   toggle_button_set_callbacks((int)&DAT_805ee1e0, 1, 0);
    if ((*(unsigned int*)(t + 0xc) & 2) == 0) toggle_button_set_callbacks((int)&DAT_805ee180, 0, 0);
    else                                      toggle_button_set_callbacks((int)&DAT_805ee180, 1, 0);
    toggle_button_hit_test((float*)&DAT_805ee060, ent, (int)rec);
    toggle_button_hit_test((float*)&DAT_805ee0c0, ent, (int)rec);
    for (int i = DAT_80612610, off = DAT_80612610 << 2; i < DAT_80612098; ++i, off += 4) {
        int pl = *(int*)((int)&DAT_80588610 + off);
        if (pl == 0 || *(int*)(pl + 0xbf0) == 0 || *(int*)(*(int*)(pl + 0xbf0) + 8) != 0x54504342 /*'TPCB'*/)
            haveTPCB = false;
        else
            haveTPCB = true;
        if (pl != 0 && *(int*)(pl + 0xbf0) != 0 && *(int*)(*(int*)(pl + 0xbf0) + 8) == 0x43523031 /*'CR01'*/)
            haveCR01 = true;
    }
    if (!haveCR01) toggle_button_set_callbacks((int)&DAT_805ee0c0, 0, 0);
    if (haveTPCB)  toggle_button_set_callbacks((int)&DAT_805ee060, 1, 0);
    else           toggle_button_set_callbacks((int)&DAT_805ee060, 0, 1);
    if (DAT_80612f58 == 0 && prop_panel_should_autoclose(ent) == 0) {
        aiactor_play_bark_global(0x10c0, 0x100, (int*)0);
        DAT_80612f58 = 1;
    }
    FUN_80382730();
}

// ============================================================================
// Address: 0x80379f08 | Name: prop_mount_confirm_action
// Confirm the mount selection. If the mount animation has reached its end phase
// (FLOAT_8061b680 == FLOAT_80612f70): bind the 'AGUN' turret entity to the
// selected player - set player state 0x80, flags |= 0x80, cross-link the turret
// (+0xc14) and player (+0x36/+0x37) records and set turret flag 0x4000000.
// Otherwise reopen the mount panel, reset the prop, and mark the class complete.
// Finally deactivate any active players whose sound-zone is not zone 0xf.
// ============================================================================
void prop_mount_confirm_action(void) {
    int obj = FUN_803826f0();
    obj = *(int*)(obj + 0x7c);
    int i;
    if (FLOAT_8061b680 == FLOAT_80612f70) {
        int* gun = (int*)entity_get_field_7c(0x4147554e /*'AGUN'*/);
        int prec = *(int*)(*(int*)(*(int*)(obj + 0x14) + 0x14) + 0x7c);
        player_set_state(*(int*)(obj + 0x14), 0x80);
        float t = FLOAT_8061b68c;
        *(unsigned int*)(*(int*)(obj + 0x14) + 0xc) |= 0x80;
        *(int*)(prec + 0xc14) = *gun;
        gun[0x36] = *(int*)(*(int*)(obj + 0x14) + 0x14);
        gun[0x37] = (int)t;
        *(unsigned int*)(*gun + 0x28) |= 0x4000000;
        i = DAT_80612610;
    } else {
        prop_mount_panel_open();
        prop_reset_active_state(*(int**)(obj + 0x78));
        statlvl_class_complete((double)FLOAT_8061b68c, 0, 0, 0, 0, 0, 0, 0);
        i = DAT_80612610;
    }
    for (; i < DAT_80612098; ++i) {
        int* pl = (int*)(&DAT_80588610)[i];
        int* node = soundpad_resolve_zone_node(pl[0x19e]);
        int z = soundpad_get_entity_zone_id((int)node);
        z = soundpad_find_zone_for_entity(z);
        if (z != 0xf && aiactor_deactivate(*pl) != 0) {
            --i;
        }
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8037a028 | Name: prop_nfc4_dismount
// Dismount the 'NFC4' mounted turret: if its panel is open close the update
// loop, dismount the player from the prop, clear the entity's mounted flag
// (bit 2), then re-activate the turret's guard actors (0xea0-stride records in
// the session table whose +0x2c flags carry bit 4).
// ============================================================================
void prop_nfc4_dismount(void) {
    int ent = entity_handle_resolve(0x4e464334 /*'NFC4'*/);
    if (*(int*)(*(int*)(ent + 0x7c) + 0x70) != 0) {
        prop_panel_update_loop();
    }
    prop_player_dismount(ent);
    *(unsigned int*)(ent + 0x28) &= 0xfffffffb;
    int off = DAT_80612610 * 0xea0;
    for (int i = DAT_80612610; i < DAT_80612610 + DAT_806120fc; ++i) {
        int actor = *(int*)(DAT_80611d90 + off);
        if (actor != 0 && (((int*)(DAT_80611d90 + off))[0xb] & 4U) != 0) {
            aiactor_activate(actor);
        }
        off += 0xea0;
    }
}

// ============================================================================
// Address: 0x8037a0dc | Name: prop_object_type_dispatch
// Bind a level-prop object to its panel behaviour vtable based on its magic
// tag. Type 0x4f1 and 'NF1C' use the options-panel vtable; 'NFC2' and the
// lower/higher NFC family map to their own vtables; 'NFC4' uses DAT_80496c7c.
// param_1 is the object (tag at *(obj+0x10)+... ; type word at [+0x10][3]/[0]).
// ============================================================================
void prop_object_type_dispatch(int obj, unsigned int* out) {
    if ((*(int**)(obj + 0x10))[3] == 0x4f1) {
        prop_entity_init_state(obj, (void*)&PTR_prop_options_panel_build_80496c34, 0, out, 1);
    } else {
        int tag = **(int**)(obj + 0x10);
        if (tag == 0x4e464332 /*'NFC2'*/) {
            prop_entity_init_state(obj, (void*)&PTR_FUN_80496c4c, 0, out, 1);
        } else if (tag < 0x4e464332) {
            if (tag == 0x4e463143 /*'NF1C'*/) {
                prop_entity_init_state(obj, (void*)&PTR_prop_options_panel_build_80496c34, 0, out, 1);
            }
        } else if (tag == 0x4e464334 /*'NFC4'*/) {
            prop_entity_init_state(obj, (void*)&DAT_80496c7c, 0, out, 1);
        } else if (tag < 0x4e464334) {
            prop_entity_init_state(obj, (void*)&PTR_FUN_80496c64, 0, out, 1);
        }
    }
}

// ============================================================================
// Address: 0x8037a1e0 | Name: prop_preload_option_textures
// Zero the 0x4c-byte sprite load context then chain three sprite_load_texture_set
// calls, threading the returned context forward.
// ============================================================================
void prop_preload_option_textures(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
    memset_wrapper((int)0x805ee240, 0, 0x4c);
    int h = sprite_load_texture_set(a1, a2, a3, a4, a5, a6, a7, a8);
    h = sprite_load_texture_set(h, a2, a3, a4, a5, a6, a7, a8);
    sprite_load_texture_set(h, a2, a3, a4, a5, a6, a7, a8);
}

// ============================================================================
// Address: 0x8037a228 | Name: prop_labels_draw_pair
// Reset the panel (FUN_8037bc00) then draw two label buttons in font table 0x1a,
// restoring the previous font.
// ============================================================================
void prop_labels_draw_pair(void) {
    FUN_8037bc00();
    int prevFont = text_set_font_table(0x1a);
    prop_label_button_draw();
    prop_label_button_draw();
    text_set_font_table(prevFont);
}

// ============================================================================
// Address: 0x8037a28c | Name: prop_subpanel_hit_test
// Frame the sub-panel background rect (inset by FLOAT_8061b6c8/cc/d0 from the
// panel bounds) into scratch DAT_805ee328, offset the two icon buttons by the
// panel-origin delta, then hit-test the sub-panel via FUN_8037bb9c.
// ============================================================================
void prop_subpanel_hit_test(int a, int b, int param3) {
    (void)a; (void)b;
    unsigned long long rec = FUN_803826f0();
    float* p = (float*)(unsigned int)rec;   // low dword = pointer state / rect base
    double dx = (double)(p[0] - p[0xf]);
    double dy = (double)(p[1] - p[0x10]);
    FUN_8037c8d0((double)(FLOAT_8061b6c8 + p[0]), (double)(FLOAT_8061b6c8 + p[1]),
                 (double)(p[2] - FLOAT_8061b6c8),
                 (double)((p[3] - FLOAT_8061b6cc) - FLOAT_8061b6d0), (int*)&DAT_805ee328);
    icon_button_set_bounds(dx, dy, (float*)&DAT_805ee3b8);
    icon_button_set_bounds(dx, dy, (float*)&DAT_805ee3e8);
    FUN_8037bb9c(&DAT_805ee328, (int)(rec >> 0x20), param3);   // high dword = entity handle
    FUN_8038273c();
}

// ============================================================================
// Address range: 0x8037a360 - 0x8037a838 | batch_1054 | 2026-09-13
//
// prop-panel MULTI-PAGE SELECTION SCREEN. A separate prop panel (from the
// options screen) presenting a paged, scrolling selection list with left/right
// page arrows, a header caption, a body list and a footer. The page index is
// DAT_80612fb0 (0/1, wrapping); each page's row-template pointer comes from the
// table &DAT_80497164 (8-byte stride). The builder prop_select_panel_build
// 0x8037a838 spawns four prop instances (list body, arrows row, footer, frame)
// and wires each slot's draw/hit-test pair plus the two page-arrow icon buttons.
//
//   prop_page_prev          0x8037a360  page index - 1 (wrap to 1), refresh
//   prop_page_next          0x8037a3b8  page index + 1 (wrap to 0), refresh
//   prop_select_list_draw   0x8037a414  copy the 0xf9-byte list glyph record to stack, draw caption
//   prop_select_panel_a_hit 0x8037a4cc  frame the list rect (scratch 418), hit-test via FUN_8037c54c
//   prop_select_panel_reset 0x8037a558  reset delegate (FUN_8037bc00)
//   prop_select_panel_b_hit 0x8037a580  frame the footer rect (scratch 4a8), hit-test via FUN_8037bb9c
//   prop_page_arrows_draw   0x8037a604  draw the arrows-row bevel+cursor and optional caption 0x1137
//   prop_select_set_active  0x8037a77c  latch the active selection record, mark the panel dirty
//   prop_select_dispatch    0x8037a7b4  route a selection id (0x1124/0x112c/0x1234) to activate/mount
//   prop_select_panel_build 0x8037a838  spawn+wire the whole selection panel (body/arrows/footer/frame)
//
// NOTE: non-matching decomp. Selection ids (0x1120/0x1121 arrows, 0x1124/0x112c/
// 0x1234 actions, 0x1137 caption), the page table stride (8), scratch rect
// addresses and struct offsets all match. The PPC unsigned-int->double magic
// (CONCAT44(0x43300000, v^0x80000000) - bias) is written as (double)(int)v, exact
// for the small positive UI coordinates. Unused longlong scratch temps the
// decompiler emitted are dropped. init helpers take function pointers cast (int)&fn.

// ---- batch_1054 raw externs ------------------------------------------------
extern int   DAT_80612fb0;    // current page index (0/1, wraps)
extern int   DAT_80612f9c;    // "show arrows-row caption" flag
extern int   DAT_80612fac;    // selection-dirty flag
extern int   DAT_804971a4, DAT_804971ac;   // arrows-row cursor origin (x,y)
extern int   DAT_80497164;    // page row-template table base (8-byte stride)
extern int   DAT_80497170, DAT_80497184;   // footer glyph src / dst records
extern int   DAT_805ee418, DAT_805ee4a8;   // list / footer scratch rects
extern int   DAT_805ee440, DAT_805ee444;   // list caption origin (x,y)
extern int   DAT_805ee7e4, DAT_805ee7fc, DAT_805ee814, DAT_805ee82c;  // footer glyph slots
extern float FLOAT_8061b6d4, FLOAT_8061b6d8, FLOAT_8061b6e8, FLOAT_8061b6ec;
extern float FLOAT_8061b6f0, FLOAT_8061b6f4, FLOAT_8061b6f8, FLOAT_8061b6fc, FLOAT_8061b700;
extern float FLOAT_8061b704, FLOAT_8061b708, FLOAT_8061b70c, FLOAT_8061b710, FLOAT_8061b714;
extern float FLOAT_8061b718, FLOAT_8061b71c, FLOAT_8061b720, FLOAT_8061b724;
extern void  FUN_8037ba28(void);   // selection-panel refresh
extern void  FUN_8038035c(void);   // commit the active selection

// ============================================================================
// Address: 0x8037a360 | Name: prop_page_prev
// Page-left arrow click: decrement the page index, wrapping -1 back to 1, then
// refresh the panel.
// ============================================================================
void prop_page_prev(void) {
    DAT_80612fb0 -= 1;
    if (DAT_80612fb0 < 0) DAT_80612fb0 = 1;
    FUN_8037ba28();
}

// ============================================================================
// Address: 0x8037a3b8 | Name: prop_page_next
// Page-right arrow click: increment the page index, wrapping >1 back to 0, then
// refresh the panel.
// ============================================================================
void prop_page_next(void) {
    DAT_80612fb0 += 1;
    if (DAT_80612fb0 > 1) DAT_80612fb0 = 0;
    FUN_8037ba28();
}

// ============================================================================
// Address: 0x8037a414 | Name: prop_select_list_draw
// Copy the 0xf9-byte list-header glyph record (from the const block at
// &DAT_803e0e84) to the stack, draw its caption at the list origin
// (DAT_805ee440/444), then run the status finalizer. Font table saved/restored.
// ============================================================================
void prop_select_list_draw(void) {
    unsigned char buf[0x100];
    int prevFont = text_set_font_table(0x1b);
    // copy 0xf9 bytes of the glyph record template
    memset_wrapper((int)buf, 0, 0x100);
    for (int i = 0; i < 0xf9; ++i) {
        buf[i] = ((unsigned char*)&DAT_803e0e84)[i];
    }
    text_draw_char((double)DAT_805ee440, (double)DAT_805ee444, buf, 0x80787800);
    FUN_8037c024();
    text_set_font_table(prevFont);
}

// ============================================================================
// Address: 0x8037a4cc | Name: prop_select_panel_a_hit
// Frame the list-body rect (inset from the panel bounds) into scratch
// DAT_805ee418 and run the list hit-test (FUN_8037c54c).
// ============================================================================
void prop_select_panel_a_hit(int arg, float* p, int mode) {
    FUN_8037c8d0((double)(FLOAT_8061b6c8 + p[0]), (double)(FLOAT_8061b6d4 + p[1]),
                 (double)(p[2] - FLOAT_8061b6d8), (double)(p[3] - FLOAT_8061b6c8),
                 (int*)&DAT_805ee418);
    FUN_8037c54c(&DAT_805ee418, arg, mode);
}

// ============================================================================
// Address: 0x8037a558 | Name: prop_select_panel_reset
// Reset delegate for the selection panel (FUN_8037bc00).
// ============================================================================
void prop_select_panel_reset(void) {
    FUN_8037bc00();
}

// ============================================================================
// Address: 0x8037a580 | Name: prop_select_panel_b_hit
// Frame the footer rect into scratch DAT_805ee4a8 and run the footer hit-test
// (FUN_8037bb9c).
// ============================================================================
void prop_select_panel_b_hit(int arg, float* p, int mode) {
    FUN_8037c8d0((double)(FLOAT_8061b6c8 + p[0]), (double)(FLOAT_8061b6d4 + p[1]),
                 (double)(p[2] - FLOAT_8061b6d8), (double)(p[3] - FLOAT_8061b6c8),
                 (int*)&DAT_805ee4a8);
    FUN_8037bb9c(&DAT_805ee4a8, arg, mode);
}

// ============================================================================
// Address: 0x8037a604 | Name: prop_page_arrows_draw
// Draw the page-arrows row: a bevel and reveal cursor at the row origin
// (DAT_804971a4/ac), and when the caption flag DAT_80612f9c is set, the caption
// string 0x1137 - solid (0,0) when ui-flag bit5 is set, otherwise outlined.
// ============================================================================
void prop_page_arrows_draw(int arg) {
    prop_panel_draw_bevel(DAT_804971a4 + 0xf, DAT_804971ac + 0x23,
                          DAT_804971a4 + 0x78, DAT_804971ac + 0x14);
    prop_panel_draw_cursor((double)(int)(DAT_804971a4 + 0x11),
                           (double)(int)(DAT_804971ac + 0x14), arg);
    if (DAT_80612f9c != 0) {
        int prevFont = text_set_font_table(0x1a);
        int str = get_string_by_id(0x1137);
        if ((DAT_80611928 >> 5 & 1) == 0) {
            text_draw_char_outline((double)(int)(DAT_804971a4 + 0xf),
                                   (double)(int)(DAT_804971ac + 0x2d), str, 0x7f17177f, 0x7f);
        } else {
            text_draw_char_outline((double)(int)(DAT_804971a4 + 0xf),
                                   (double)(int)(DAT_804971ac + 0x2d), str, 0, 0);
        }
        text_set_font_table(prevFont);
    }
}

// ============================================================================
// Address: 0x8037a77c | Name: prop_select_set_active
// Latch the active-selection owner (entity handle at obj +0x14) into
// DAT_80611d74, commit it (FUN_8038035c), and mark the panel dirty.
// ============================================================================
void prop_select_set_active(int obj) {
    DAT_80611d74 = *(int*)(obj + 0x14);
    FUN_8038035c();
    DAT_80612fac = 1;
}

// ============================================================================
// Address: 0x8037a7b4 | Name: prop_select_dispatch
// Route a selection-list action id (from the resolved object at obj +0x7c):
// 0x112c -> open the mount panel and set that entry active; 0x1124 or 0x1234 ->
// activate the entry directly.
// ============================================================================
void prop_select_dispatch(int obj, int selId) {
    int sub = *(int*)(obj + 0x7c);
    if (selId == 0x112c) {
        prop_mount_panel_open();
        prop_select_set_active(sub);
    } else if (selId < 0x112c) {
        if (selId == 0x1124) prop_activate_entry();
    } else if (selId == 0x1234) {
        prop_activate_entry();
    }
}

// ============================================================================
// Address: 0x8037a838 | Name: prop_select_panel_build
// Build the whole multi-page selection panel. Zeroes three 0x90-byte glyph
// blocks, then spawns four prop instances into owner slots 1/2/3/0:
//   slot 1  list body   -> draw prop_select_list_draw / hit prop_select_panel_a_hit,
//                          background built via FUN_8037b830 using the page-table
//                          row template &DAT_80497164[page*8], plus the two page-
//                          arrow icon buttons (ids 0x1121 prev / 0x1120 next).
//   slot 2  arrows row   -> draw prop_select_list_draw / hit prop_select_panel_a_hit,
//                          FUN_8037bd80 frame, copies two 0x18 footer glyphs.
//   slot 3  footer       -> reset prop_select_panel_reset / hit prop_select_panel_b_hit,
//                          FUN_8037b830 frame, and four posed footer glyphs.
//   slot 0  frame        -> priority-3 backing frame.
// ============================================================================
void prop_select_panel_build(void) {
    int owner = (int)FUN_803826e8();
    memset_wrapper((int)0x805ee328, 0, 0x90);
    memset_wrapper((int)0x805ee418, 0, 0x90);
    memset_wrapper((int)0x805ee4a8, 0, 0x90);

    // --- slot 1: list body ---
    prop_instance_spawn((double)FLOAT_8061b6d0, (double)FLOAT_8061b6c8, (double)FLOAT_8061b6e8,
                        (double)FLOAT_8061b6ec, (int)0x805ee538, owner, 1, 6.55527e-42f, 0.0f, 0.0f);
    float* p = (float*)prop_owner_slot_record(owner, 1);
    prop_set_scale_vec((int)p, (int)&prop_select_list_draw, (int)&prop_select_panel_a_hit, 0);
    FUN_8037b830((int)&DAT_805ee328, p,
                 (int)(FLOAT_8061b6d4 + FLOAT_8061b6f0 + p[0]), (int)(FLOAT_8061b6d0 + p[1]),
                 (int)(p[2] - FLOAT_8061b6f4), (int)((p[3] - FLOAT_8061b6f8) - FLOAT_8061b6d0),
                 *(int*)(&DAT_80497164 + DAT_80612fb0 * 8), 0x1a, 0xf);
    {
        float rect[4];
        rect[0] = FLOAT_8061b6c8 + p[0];
        rect[2] = FLOAT_8061b6fc + rect[0];
        rect[3] = p[3] - FLOAT_8061b6c8;
        rect[1] = (p[3] - FLOAT_8061b6d0) - FLOAT_8061b6cc;
        ((void(*)(unsigned int,unsigned int,unsigned int,unsigned int,int))&icon_button_init)
            ((unsigned int)(unsigned long long)&DAT_805ee3e8, (unsigned int)(unsigned long long)rect,
             0x1121, PROP_CB(prop_page_prev), 0x368);
        rect[0] += FLOAT_8061b6fc;
        rect[2] += FLOAT_8061b700;
        ((void(*)(unsigned int,unsigned int,unsigned int,unsigned int,int))&icon_button_init)
            ((unsigned int)(unsigned long long)&DAT_805ee3b8, (unsigned int)(unsigned long long)rect,
             0x1120, PROP_CB(prop_page_next), 0x368);
    }

    // --- slot 2: arrows / status row ---
    prop_instance_spawn((double)FLOAT_8061b6d0, (double)FLOAT_8061b6c8, (double)FLOAT_8061b704,
                        (double)FLOAT_8061b708, (int)0x805ee538, owner, 2, 6.1489e-42f, 0.0f, 0.0f);
    p = (float*)prop_owner_slot_record(owner, 2);
    prop_set_scale_vec((int)p, (int)&prop_select_list_draw, (int)&prop_select_panel_a_hit, 0);
    FUN_8037bd80((int)&DAT_805ee418, p,
                 (int)(FLOAT_8061b6f4 + p[0]), (int)(FLOAT_8061b6d0 + p[1]),
                 (int)(p[2] - FLOAT_8061b6f4), (int)(p[3] - FLOAT_8061b6d0), 0x1a, 0xf, 3);
    for (int i = 0, off = 0; i < 2; ++i, off += 0x18) {
        (&DAT_80497184)[off] = (&DAT_80497170)[off];
        FUN_8037bec0((int)0x805ee418, &DAT_80497170 + off);
    }

    // --- slot 3: footer ---
    prop_instance_spawn((double)FLOAT_8061b6d0, (double)FLOAT_8061b6c8, (double)FLOAT_8061b704,
                        (double)FLOAT_8061b708, (int)0x805ee538, owner, 3, 6.1489e-42f, 0.0f, 0.0f);
    p = (float*)prop_owner_slot_record(owner, 3);
    prop_set_scale_vec((int)p, (int)&prop_select_panel_reset, (int)&prop_select_panel_b_hit, 0);
    FUN_8037b830((int)&DAT_805ee4a8, p,
                 (int)(FLOAT_8061b6f4 + p[0]), (int)(FLOAT_8061b6d0 + p[1]),
                 (int)(p[2] - FLOAT_8061b6f4), (int)(p[3] - FLOAT_8061b6d0), 0, 0x1a, 0xf);
    prop_set_pose((double)FLOAT_8061b70c, (double)FLOAT_8061b6cc, (float*)&DAT_805ee7e4, 1.26117e-42f, 6.53005e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b710, (double)FLOAT_8061b6cc, (float*)&DAT_805ee7fc, 1.25696e-42f, 6.1489e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b714, (double)FLOAT_8061b6cc, (float*)&DAT_805ee814, 1.25837e-42f, 6.55107e-42f, 2.8026e-45f);
    prop_set_pose((double)FLOAT_8061b70c, (double)FLOAT_8061b718, (float*)&DAT_805ee82c, 1.25977e-42f, 6.16011e-42f, 2.8026e-45f);

    // --- slot 0: backing frame ---
    prop_instance_spawn((double)FLOAT_8061b71c, (double)FLOAT_8061b720, (double)FLOAT_8061b724,
                        (double)FLOAT_8061b724, (int)0x805ee538, owner, 0, 6.54126e-42f, -8.715723e-39f, 5.60519e-45f);
    int rec0 = prop_owner_slot_record(owner, 0);
    prop_set_field14(rec0, 3);
    FUN_80382734();
}

// ============================================================================
// Address range: 0x8037b49c - 0x8037bd80 | batch_1056 | 2026-09-13
//
// Prop text-box widget family - the scrolling caption / description panels used
// by the prop selection screens (built by prop_select_panel_build in
// batch_1054) and the ekun grid confirm menu. This block picks a random caption
// id, routes a level object to the right sub-panel by its four-char tag, loads
// the panel sprite set, measures + lays out a text box (line count -> scroll
// geometry), renders the visible scrolled window with a progress bar, and drives
// the footer drag hit-test.
//
//   prop_textbox_pick_random_caption 0x8037b49c  RNG-pick a caption string id
//   prop_textbox_route_by_tag        0x8037b4fc  route level obj by tag -> panel state
//   prop_textbox_load_sprites        0x8037b5b4  clear record + load 8 sprite sets
//   prop_textbox_draw_caption_list   0x8037b688  draw up to 16 caption rows (GX)
//   prop_textbox_list_insert         0x8037b7fc  insert value into first free list slot
//   prop_textbox_build               0x8037b830  measure string, build box geometry
//   prop_textbox_relayout            0x8037ba28  re-measure/layout an existing box
//   prop_textbox_footer_hit          0x8037bb9c  footer drag hit-test delegate
//   prop_textbox_draw_scrolled       0x8037bc00  draw the visible scrolled text window
//   prop_textbox_build_simple        0x8037bd80  alternate (single-scale) box builder
//
// NOTE: non-matching decomp. The CONCAT44/0x43300000 int->float conversions are
// reproduced as equivalent float casts; the text-box record is an int[] with the
// original field offsets (rows at +0x1f, scroll at +0x20/+0x21, geometry at
// +0x28..+0x38). Control flow, string control-char stripping, entity/tag values
// and side effects match the original. rng_counter_tick / sprite_load_texture_set
// use the file-scope raw-ABI macros defined above.

// ---- text-box rodata constants (0x8061b7xx / 0x806116xx) ------------------
extern float  FLOAT_8061b758;        // caption RNG span scale
extern float  FLOAT_8061b760, FLOAT_8061b764;   // caption-list origin / row pitch
extern double DOUBLE_8061b768;       // 2^52 bias (caption list)
extern float  FLOAT_8061b770, FLOAT_8061b774, FLOAT_8061b778;   // box margins
extern double DOUBLE_8061b780;       // 2^52 bias (box geometry)
extern float  FLOAT_8061b788;        // simple-box row-height divisor
extern float  FLOAT_80611674;        // font row height
extern float  FLOAT_8061b72c, FLOAT_8061b730;   // (rng scale/bias, already used above)
extern double DOUBLE_8061b740;       // 2^52 bias (caption RNG int->float conv)
extern int    DAT_80612fa0;          // last-picked caption string id
extern int    PTR_prop_select_panel_build_804972dc;  // select-panel vtable/state ptr
extern int    PTR_grid_minigame_spawn_ui_804972f4;   // grid-minigame vtable/state ptr
// ---- callees missing from generated_decls ---------------------------------
extern int  text_set_font_table(int table);      // set font table, return previous
extern unsigned long long FUN_803826cc(void);    // scratch enter (build: record pair)
extern unsigned long long FUN_803826d8(void);    // scratch enter (simple build: record pair)
extern void FUN_80382718(void);                  // scratch leave (build)
extern void FUN_80382724(void);                  // scratch leave (simple build)

// Pick a random caption string id in [0x1131 .. 0x1131+span] and stash it.
void prop_textbox_pick_random_caption(void)
{
    unsigned int r = rng_counter_tick();
    DAT_80612fa0 = (int)(FLOAT_8061b758 *
                   ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061b740) / FLOAT_8061b72c)
                   + FLOAT_8061b730) + 0x1131;
}

// Route a level object to its sub-panel state by the four-char tag at *(*(obj+0x10)):
// 'TOSS'/'TOSS2' -> the grid minigame, 'CLP1'..'CLP4' -> the select panel.
void prop_textbox_route_by_tag(int obj, unsigned int *pos)
{
    int tag = **(int **)(obj + 0x10);
    if (tag != 0x544f5332) {                 // != 'TOSS2'
        if (tag < 0x544f5332) {
            if (0x4c435034 < tag) return;    // > 'CLP4'
            if (tag < 0x4c435031) return;    // < 'CLP1'
            prop_entity_init_state(obj, (int)&PTR_prop_select_panel_build_804972dc, 0, pos, 1);
            return;
        }
        if (tag != 0x544f5353) return;       // != 'TOSS'
    }
    prop_entity_init_state(obj, (int)&PTR_grid_minigame_spawn_ui_804972f4, 0, pos, 1);
}

// Zero the 0x2ac panel record then load its eight sprite texture sets.
void prop_textbox_load_sprites(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                               unsigned long long a4, unsigned long long a5, unsigned long long a6,
                               unsigned long long a7, unsigned long long a8)
{
    int h = (int)FUN_803826f0();
    memset_wrapper(h, 0, 0x2ac);
    for (int i = 0; i < 8; i++) {
        h = sprite_load_texture_set(h, (int)a2, (int)a3, (int)a4, (int)a5, (int)a6, (int)a7, (int)a8);
    }
    FUN_8038273c();
}

// Draw up to 16 caption rows down the panel, stripping the inline control chars
// ('!', '$', '*') from each string before drawing.
void prop_textbox_draw_caption_list(void)
{
    unsigned int *rec = (unsigned int *)FUN_803826e8();
    int prevFont = text_set_font_table(0x1a);
    int row = 0;
    int off = 0;
    double pitch = (double)FLOAT_8061b764;

    float y = FLOAT_8061b760 +
              (float)((double)CONCAT44(0x43300000, rec[3] ^ 0x80000000) - DOUBLE_8061b768);
    double yBottom;
    while ((row < 0x10) &&
           ((double)y < (double)(float)((double)CONCAT44(0x43300000, rec[4] ^ 0x80000000) - DOUBLE_8061b768))) {
        unsigned char *s = (unsigned char *)get_string_by_id(*(int *)((int)rec + off + 0x14));
        bool skip = true;
        while (skip) {
            unsigned char c = *s;
            skip = false;
            if (c == 0x24 || c == 0x21 || c == 0x2a) {   // '$' '!' '*'
                skip = true;
                s = s + 1;
            }
        }
        text_draw_char((double)(float)((double)CONCAT44(0x43300000, rec[1] + 4 ^ 0x80000000) - DOUBLE_8061b768),
                       (double)y, s, rec[0]);
        row++;
        off += 4;
        y = (float)((double)y + pitch);
        yBottom = (double)y;
        (void)yBottom;
    }
    text_set_font_table(prevFont);
    prop_scissor_full();
    FUN_80382734();
}

// Insert a value into the first free slot of a 16-entry (4-byte stride) list.
void prop_textbox_list_insert(int list, int value)
{
    int off = 0;
    int n = 0x10;
    do {
        if (*(int *)(list + off + 0x14) == 0) {
            *(int *)(list + off + 0x14) = value;
            return;
        }
        off += 4;
        n--;
    } while (n != 0);
}

// Build a scrolling text box: measure the source string's line count, derive the
// visible-row count and scroll geometry from the box rect, and fill the record.
void prop_textbox_build(int a1, int a2, unsigned int x0, unsigned int y0, unsigned int x1,
                        unsigned int y1, int strId, int font, int pad)
{
    unsigned long long pair = FUN_803826cc();
    int *rec = (int *)((unsigned long long)pair >> 0x20);
    int lines = 1;
    int prevFont = text_set_font_table(font);
    unsigned int h = y1 - y0;
    double y0f = (double)CONCAT44(0x43300000, y0 ^ 0x80000000) - DOUBLE_8061b780;

    rec[10] = (int)(float)((double)CONCAT44(0x43300000, x0 ^ 0x80000000) - DOUBLE_8061b780);
    rec[0xb] = (int)(float)y0f;
    rec[0xc] = (int)(float)((double)CONCAT44(0x43300000, x1 ^ 0x80000000) - DOUBLE_8061b780);
    rec[0xd] = (int)(float)((double)CONCAT44(0x43300000, y1 ^ 0x80000000) - DOUBLE_8061b780);
    rec[0x23] = (int)pair;
    rec[0x20] = font;
    rec[0x21] = pad;
    rec[0xe] = strId;
    rec[0] = x1 - pad;
    rec[1] = y0;
    rec[2] = x1;

    unsigned char *raw = (unsigned char *)get_string_by_id(strId);
    unsigned char *s = text_check_scale((double)(float)((double)CONCAT44(0x43300000,
                                        (x1 - x0) - pad ^ 0x80000000) - DOUBLE_8061b780), raw);
    text_get_char_height(s);
    rec[0x1f] = (int)((float)((double)CONCAT44(0x43300000, h ^ 0x80000000) - DOUBLE_8061b780) / FLOAT_80611674);
    for (; *s != 0; s++) {
        if (*s == 10) lines++;
    }
    if (rec[0x1f] < lines) {
        double extra = (double)(CONCAT44(0x43300000, lines - rec[0x1f]) ^ 0x80000000);
        rec[6] = (int)(FLOAT_8061b770 + (float)(extra - DOUBLE_8061b780));
        rec[8] = (int)(h * rec[0x1f]) / lines;
    } else {
        rec[6] = (int)FLOAT_8061b774;
        rec[8] = h;
    }
    rec[9] = (int)FLOAT_8061b774;
    rec[7] = (int)FLOAT_8061b778;
    rec[3] = y1 - rec[8];
    text_set_font_table(prevFont);
    FUN_80382718();
}

// Re-measure and re-lay-out an existing text box, reading the rect/font from the
// record itself (used when the box content or size changes at runtime).
void prop_textbox_relayout(void)
{
    unsigned long long pair = FUN_803826e8();
    int rec = (int)((unsigned long long)pair >> 0x20);
    int lines = 1;
    int prevFont = text_set_font_table(*(int *)(rec + 0x80));
    float x1 = *(float *)(rec + 0x30);
    float x0 = *(float *)(rec + 0x28);
    *(int *)(rec + 0x38) = (int)pair;
    int h = (int)(*(float *)(rec + 0x34) - *(float *)(rec + 0x2c));

    unsigned char *raw = (unsigned char *)get_string_by_id((int)pair);
    unsigned char *s = text_check_scale((double)(float)((double)CONCAT44(0x43300000,
                        (int)(x1 - x0) - *(int *)(rec + 0x84) ^ 0x80000000) - DOUBLE_8061b780), raw);
    for (; *s != 0; s++) {
        if (*s == '\n') lines++;
    }
    if (*(int *)(rec + 0x7c) < lines) {
        *(float *)(rec + 0x18) = FLOAT_8061b770 +
            (float)((double)CONCAT44(0x43300000, lines - *(int *)(rec + 0x7c) ^ 0x80000000) - DOUBLE_8061b780);
        *(int *)(rec + 0x20) = (h * *(int *)(rec + 0x7c)) / lines;
    } else {
        *(float *)(rec + 0x18) = FLOAT_8061b774;
        *(int *)(rec + 0x20) = h;
    }
    *(float *)(rec + 0x24) = FLOAT_8061b774;
    *(float *)(rec + 0x1c) = FLOAT_8061b778;
    *(int *)(rec + 0xc) = (int)(*(float *)(rec + 0x34) -
        (float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x20) ^ 0x80000000) - DOUBLE_8061b780));
    text_set_font_table(prevFont);
    FUN_80382734();
}

// Footer drag hit-test delegate: only hit-tests when the box is on its last page
// (scroll index == line-object count - 1), then forwards to prop_textbox_hit_drag.
void prop_textbox_footer_hit(int a1, int a2, int style)
{
    unsigned long long pair = FUN_803826f0();
    unsigned int *rec = (unsigned int *)((unsigned long long)pair >> 0x20);
    int obj = prop_get_level_object();
    if (*(int *)(rec[0x23] + 0x18) == *(int *)(obj + 0x10) - 1) {
        prop_textbox_hit_drag((int)pair, rec, style);
    }
    FUN_8038273c();
}

// Draw the currently-visible scrolled window of the text box: optional progress
// bar, then the slice of lines between the current scroll offset and the visible
// row count, terminated in place with a NUL.
void prop_textbox_draw_scrolled(void)
{
    int *rec = (int *)__save_gpr_28();
    unsigned int color = prop_get_text_color();
    double lerp = prop_lerp_field((int)rec);
    int scroll = (int)lerp;
    float top = FLOAT_8061b778;
    if (FLOAT_8061b774 != (float)rec[6]) {
        prop_draw_progress_bar(rec);
        double bar = (double)(CONCAT44(0x43300000, rec[0x21]) ^ 0x80000000);
        top = (float)(bar - DOUBLE_8061b780);
    }
    int prevFont = text_set_font_table(0x1a);
    unsigned char *raw = (unsigned char *)get_string_by_id(rec[0xe]);
    unsigned char *s = text_check_scale((double)(float)((double)((float)rec[0xc] - (float)rec[10]) - (double)top), raw);

    // Skip the first `scroll` lines.
    int i = 0;
    while (scroll != 0) {
        char c = s[i];
        if (c == '\n') scroll--;
        else if (c == '\0') { scroll = 0; i--; }
        i++;
    }
    // Walk forward `visibleRows` more lines and NUL-terminate the slice.
    int rows = rec[0x1f];
    int end = i;
    unsigned char *p = s + i;
    while (rows != 0) {
        char c = *p;
        if (c == '\n') rows--;
        else if (c == '\0') rows = 0;
        end++;
        p++;
    }
    s[end - 1] = 0;
    text_draw_char((double)(float)rec[10], (double)(float)rec[0xb], s + i, color);
    text_set_font_table(prevFont);
    __restore_gpr_28();
}

// Alternate text-box builder (single fixed scale, no line-count fitting) used by
// the shorter panels; lays out the box geometry directly from the rect.
void prop_textbox_build_simple(int a1, int a2, unsigned int x0, unsigned int y0, unsigned int x1,
                               unsigned int y1, int font, int pad, int a9)
{
    unsigned long long pair = FUN_803826d8();
    int *rec = (int *)((unsigned long long)pair >> 0x20);
    int prevFont = text_set_font_table(font);
    float rowDiv = FLOAT_8061b788;
    float margin = FLOAT_8061b774;
    float top = FLOAT_8061b778;
    double y0f = (double)CONCAT44(0x43300000, y0 ^ 0x80000000) - DOUBLE_8061b780;

    rec[10] = (int)(float)((double)CONCAT44(0x43300000, x0 ^ 0x80000000) - DOUBLE_8061b780);
    rec[0xb] = (int)(float)y0f;
    rec[0xc] = (int)(float)((double)CONCAT44(0x43300000, x1 ^ 0x80000000) - DOUBLE_8061b780);
    rec[0xd] = (int)(float)((double)CONCAT44(0x43300000, y1 ^ 0x80000000) - DOUBLE_8061b780);
    rec[0x22] = a9;
    rec[0x23] = (int)pair;
    rec[0x20] = font;
    rec[0x21] = pad;
    rec[0] = x1 - pad;
    rec[1] = y0;
    rec[2] = x1;
    rec[0x1f] = (int)((float)((double)CONCAT44(0x43300000, y1 - y0 ^ 0x80000000) - DOUBLE_8061b780)
                      / (rowDiv * FLOAT_80611674));
    rec[6] = (int)margin;
    rec[8] = y1 - y0;
    rec[9] = (int)margin;
    rec[7] = (int)top;
    rec[3] = y1 - rec[8];
    text_set_font_table(prevFont);
    FUN_80382724();
}

// ============================================================================
// Address range: 0x8037bec0 - 0x8037cbd0 | batch_1057 | 2026-09-13
//
// Two coupled prop sub-systems that share the text-box record built above.
//
// (a) The scrollable MENU LIST used by the selection panels: append an entry,
//     recompute scroll geometry from the count of visible (non-hidden) entries,
//     render the two-column paged list with a selection highlight, and hit-test
//     the entries on the last page (this is the FUN_8037c54c delegate wired up
//     by prop_select_panel_a_hit in batch_1054).
//
// (b) The ekun grid SEQUENCE CONTROLLER: a small 4-state machine (states in
//     record+0x1d, an animation timer in +0x1e) that plays positional sounds,
//     fires per-step character FX through the ghz thunk table, drives the grid
//     drag/select, and streams a looped 3d sound.
//
//   prop_menulist_add_entry  0x8037bec0  append an entry then relayout
//   prop_menulist_relayout   0x8037bef8  recompute scroll geometry (visible count)
//   prop_menulist_draw       0x8037c024  render the paged two-column list (GX)
//   prop_menulist_hit_test   0x8037c54c  hit-test list entries on the last page
//   prop_textbox_set_rect    0x8037c8d0  set a text-box rect from four doubles
//   prop_seq_play_sound      0x8037c96c  start a positional sound + fade, store handle
//   prop_seq_init            0x8037ca48  init the sequence controller record
//   prop_seq_table_set       0x8037caec  set a value in the id/value pair table
//   prop_seq_play_gated      0x8037cb34  play a difficulty-gated one-shot sound
//   prop_seq_tick            0x8037cbd0  the sequence controller state machine
//
// NOTE: non-matching decomp. CONCAT44/0x43300000 int->float conversions are
// reproduced as float casts; the negative-address literals (e.g. -0x7fb68be4)
// are the original's signed forms of the DAT_ scratch bases and are kept as-is.
// Control flow, state transitions, sound ids and side effects match the original.

// ---- text-box / menu-list rodata (0x8061b7xx) -----------------------------
extern float  FLOAT_8061b78c, FLOAT_8061b790, FLOAT_8061b794, FLOAT_8061b798;
extern float  FLOAT_8061b7a0, FLOAT_8061b7a8, FLOAT_8061b7ac, FLOAT_8061b7b0, FLOAT_8061b7b4;
// ---- sequence-controller rodata -------------------------------------------
extern float  FLOAT_8061b7b8, FLOAT_8061b7bc, FLOAT_8061b7c0, FLOAT_8061b7c4, FLOAT_8061b7c8;
extern float  FLOAT_8061b7cc, FLOAT_8061b7d0, FLOAT_8061b7d4, FLOAT_8061b7d8, FLOAT_8061b7dc;
extern float  FLOAT_8061b7e0;
extern double DOUBLE_8061b7e8;
// ---- sequence-controller state / tables -----------------------------------
extern int    DAT_8060fa8c;          // sound descriptor table (0x14 stride)
extern int    DAT_80497430;          // grid seq caption id slot
extern int    DAT_804973e8;          // id/value pair table sentinel (id==0 ends)
extern int    DAT_804973ec, DAT_804973f4;   // id/value pair table entries
extern int    DAT_80497354;          // grid seq sound-id table (0x18 stride)
extern char   DAT_8049734c;          // grid seq per-slot enable flags (0x18 stride)
extern float  DAT_80497344;          // grid seq per-slot fx amount (0x18 stride)
extern int    DAT_80611360;          // active drag slot (-1 == none)
extern int    DAT_80612fbc;          // per-step fired bitmask
extern int    PTR_ghz_char_fx_thunk_0_80497350;   // per-slot char-fx callback table
extern float  DAT_803e0f88;          // per-state timer increment table
// ---- callees missing from generated_decls ---------------------------------
extern unsigned long long FUN_803826d0(void);    // scratch enter (menu draw: record pair)
extern unsigned long long FUN_803826d4(void);    // scratch enter (menu hit: record+qent pair)
extern void FUN_8038271c(void);                  // scratch leave (menu draw)
extern void FUN_80382720(void);                  // scratch leave (menu hit)
extern void FUN_8037d594(int rec);               // seq: sample analog input into +0xe/+0xf
extern int  FUN_8037d264(int rec);               // seq: locate grid press slot (-1 none)
extern int  FUN_8037d310(int rec);               // seq: locate grid toggle slot (-1 none)
// text_truncate_ellipsis resolves through generated_externs; do not re-declare.
// sound_update_3d_source's generated prototype drops its int return; call it
// through a typed function-pointer cast to its DOL address so we can read the
// "audible" flag without clashing with the shared header's void prototype.
#define snd_update_3d(a,b,c,handle,pos,sampleId,flags) \
    ( ((int (*)(double,double,double,int*,float*,int,int))0x800102e4)(a,b,c,handle,pos,sampleId,flags) )

// Append an entry pointer to the menu list (slot array at +0x3c, count at +0x78)
// then recompute the scroll geometry.
void prop_menulist_add_entry(int rec, int entry)
{
    int n = *(int *)(rec + 0x78);
    *(int *)(rec + 0x78) = n + 1;
    *(int *)(rec + n * 4 + 0x3c) = entry;
    prop_menulist_relayout(rec);
}

// Recompute the menu-list scroll geometry from the number of visible (not
// hidden, flag bit0 clear) entries; each visible entry occupies two rows.
void prop_menulist_relayout(int rec)
{
    int prevFont = text_set_font_table(*(int *)(rec + 0x80));
    int rows = 0;
    int off = 0;
    int n = *(int *)(rec + 0x78);
    int h = (int)(*(float *)(rec + 0x34) - *(float *)(rec + 0x2c));
    for (; n > 0; n--) {
        if ((*(unsigned char *)(*(int *)(rec + off + 0x3c) + 0x14) & 1) == 0) {
            rows += 2;
        }
        off += 4;
    }
    if (*(int *)(rec + 0x7c) < rows) {
        double extra = (double)(CONCAT44(0x43300000, rows - *(int *)(rec + 0x7c)) ^ 0x80000000);
        *(float *)(rec + 0x18) = FLOAT_8061b770 + (float)(extra - DOUBLE_8061b780);
        *(int *)(rec + 0x20) = (h * *(int *)(rec + 0x7c)) / rows;
    } else {
        *(float *)(rec + 0x18) = FLOAT_8061b774;
        *(int *)(rec + 0x20) = h;
    }
    *(float *)(rec + 0x24) = FLOAT_8061b774;
    *(float *)(rec + 0x1c) = FLOAT_8061b778;
    double scroll = (double)(CONCAT44(0x43300000, *(int *)(rec + 0x20)) ^ 0x80000000);
    *(int *)(rec + 0xc) = (int)(*(float *)(rec + 0x34) - (float)(scroll - DOUBLE_8061b780));
    text_set_font_table(prevFont);
}

// Render the paged two-column menu list: entries are drawn from the scroll
// offset (prop_lerp_field), each visible entry as a highlight rect + caption
// (and an optional icon 0x389), with the row under the cursor drawn highlighted
// (style 0x28 vs 0x14).
void prop_menulist_draw(void)
{
    unsigned long long pair = FUN_803826d0();
    int *rec = (int *)((unsigned long long)pair >> 0x20);
    unsigned int color = prop_get_text_color();
    int prevFont = text_set_font_table(0x1a);
    unsigned int scroll = (unsigned int)prop_lerp_field((int)rec);
    int drawn = 0;
    unsigned int y = (unsigned int)(float)rec[0xb];
    double rowH = (double)(FLOAT_8061b78c * FLOAT_80611674);
    float top = FLOAT_8061b778;
    if (FLOAT_8061b774 != (float)rec[6]) {
        prop_draw_progress_bar(rec);
        double bar = (double)(CONCAT44(0x43300000, rec[0x21]) ^ 0x80000000);
        top = (float)(bar - DOUBLE_8061b780);
    }
    double topD = (double)top;
    int idx = ((int)scroll >> 1) + (unsigned int)((int)scroll < 0 && (scroll & 1) != 0);
    double stepA = (double)(float)((double)FLOAT_8061b788 * rowH);
    double stepB = (double)(float)((double)FLOAT_8061b790 * rowH);
    int off = idx * 4;
    for (; idx < rec[0x1e]; idx++) {
        unsigned int style = 0x14;
        int slot = off + 0x3c;
        if ((drawn == 0) && ((scroll & 1 ^ scroll >> 0x1f) != scroll >> 0x1f)) {
            if (rec[0x1f] < 1) break;
            if ((*(unsigned char *)(*(int *)((int)rec + slot) + 0x14) & 1) == 0) {
                float cy = *(float *)((int)pair + 0x3c);
                double yf = (double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_8061b780;
                if (((float)yf <= cy) && (cy <= (float)((double)(float)yf + rowH))) style = 0x28;
                int x1 = (int)((float)((double)(float)rec[0xc] - topD) - FLOAT_8061b794);
                gx_fill_rect_clipped((int)(float)rec[10], y, x1,
                                     (unsigned int)((double)(float)yf + rowH), style);
                if ((*(unsigned char *)(*(int *)((int)rec + slot) + 0x14) & 8) != 0) {
                    prop_sprite_draw((int)(FLOAT_8061b798 + (float)rec[10]), y, 0x389);
                }
                unsigned char *s = (unsigned char *)get_string_by_id(*(int *)(*(int *)((int)rec + slot) + 8));
                text_draw_char((double)(FLOAT_8061b7a0 + (float)rec[10]), (double)(float)yf, s, color);
                drawn = 1;
                y = (unsigned int)((double)(float)yf + stepA);
            }
        } else {
            if (rec[0x1f] <= drawn) break;
            if ((*(unsigned char *)(*(int *)((int)rec + slot) + 0x14) & 1) == 0) {
                get_string_by_id(*(int *)(*(int *)((int)rec + slot) + 4));
                text_truncate_ellipsis();
                float cy = *(float *)((int)pair + 0x3c);
                double yf = (double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_8061b780;
                if (((float)yf <= cy) && (cy <= (float)((double)(float)yf + stepB))) style = 0x28;
                int x1 = (int)((float)((double)(float)rec[0xc] - topD) - FLOAT_8061b794);
                gx_fill_rect_clipped((int)(float)rec[10], y, x1,
                                     (unsigned int)((double)(float)yf + stepB), style);
                if ((*(unsigned char *)(*(int *)((int)rec + slot) + 0x14) & 8) != 0) {
                    int iy = (int)((double)(float)yf + rowH);
                    prop_sprite_draw((int)(FLOAT_8061b798 + (float)rec[10]), iy, 0x389);
                }
                text_draw_char((double)(FLOAT_8061b7a0 + (float)rec[10]), (double)(float)yf,
                               (unsigned char *)0 /* line-1 buffer */, color);
                y = (unsigned int)((double)(float)yf + rowH);
                if (rec[0x1f] <= drawn + 1) break;
                unsigned char *s = (unsigned char *)get_string_by_id(*(int *)(*(int *)((int)rec + slot) + 8));
                text_draw_char((double)(FLOAT_8061b7a0 + (float)rec[10]), (double)(float)((double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_8061b780), s, color);
                drawn += 2;
                y = (unsigned int)((double)(float)((double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_8061b780) + stepA);
            }
        }
        off += 4;
    }
    text_set_font_table(prevFont);
    FUN_8038271c();
}

// Hit-test the menu-list entries (only on the last page). On a style-3 (confirm)
// hit inside a visible entry's rect, activate the entry and relayout.
void prop_menulist_hit_test(int a1, int a2, int style)
{
    unsigned long long pair = FUN_803826d4();
    unsigned int *rec = (unsigned int *)((unsigned long long)pair >> 0x20);
    int qent = (int)pair;
    unsigned int scroll = (unsigned int)prop_lerp_field((int)rec);
    int prevFont = text_set_font_table(0x1a);
    int drawn = 0;
    unsigned int y = (unsigned int)(float)rec[0xb];
    double top = (double)FLOAT_8061b778;
    double rowH = (double)(FLOAT_8061b78c * FLOAT_80611674);
    int obj = prop_get_level_object();
    if (*(int *)(rec[0x23] + 0x18) == *(int *)(obj + 0x10) - 1) {
        if (FLOAT_8061b774 != (float)rec[6]) {
            prop_textbox_hit_drag(qent, rec, style);
            double bar = (double)(CONCAT44(0x43300000, rec[0x21]) ^ 0x80000000);
            top = (double)(float)(bar - DOUBLE_8061b780);
        }
        if (style == 3) {
            int idx = ((int)scroll >> 1) + (unsigned int)((int)scroll < 0 && (scroll & 1) != 0);
            double stepA = (double)(float)((double)FLOAT_8061b788 * rowH);
            double stepB = (double)(float)((double)FLOAT_8061b790 * rowH);
            int off = idx * 4;
            for (; idx < (int)rec[0x1e]; idx++) {
                int slot = off + 0x3c;
                bool first = (drawn == 0) && ((scroll & 1 ^ scroll >> 0x1f) != scroll >> 0x1f);
                if (first) {
                    if ((int)rec[0x1f] < 1) break;
                } else {
                    if ((int)rec[0x1f] <= drawn) break;
                }
                if ((*(unsigned char *)(*(int *)((int)rec + slot) + 0x14) & 1) == 0) {
                    double yf = (double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_8061b780;
                    float cy = *(float *)(qent + 0x3c);
                    float cx = *(float *)(qent + 0x38);
                    double band = first ? stepA : stepB;
                    if (((float)yf <= cy) && (cy <= (float)((double)(float)yf + band)) &&
                        ((float)rec[10] <= cx) &&
                        (cx <= (float)((double)(float)rec[0xc] - top) - FLOAT_8061b794)) {
                        prop_activate_entry();
                        prop_textbox_relayout();
                    }
                    if (first) {
                        drawn = 1;
                        y = (unsigned int)((double)(float)yf + stepA);
                    } else {
                        if ((int)rec[0x1f] <= drawn + 1) break;
                        drawn += 2;
                        double y2 = (double)(float)((double)(float)yf + rowH);
                        y = (unsigned int)(y2 + stepA);
                    }
                }
                off += 4;
            }
            text_set_font_table(prevFont);
        }
    }
    FUN_80382720();
}

// Set a text-box rect (x0,y0,x1,y1) from four doubles and derive the two inner
// edges from the pad (+0x21) and scroll height (+0x20 stored at [8]) fields.
void prop_textbox_set_rect(double x0, double y0, double x1, double y1, int *rec)
{
    rec[10] = (int)(float)x0;
    rec[0xb] = (int)(float)y0;
    rec[0xc] = (int)(float)x1;
    rec[0xd] = (int)(float)y1;
    rec[0] = (int)(x1 - (double)(float)((double)CONCAT44(0x43300000, rec[0x21] ^ 0x80000000) - DOUBLE_8061b780));
    rec[1] = (int)y0;
    rec[2] = (int)x1;
    rec[3] = (int)(y1 - (double)(float)((double)CONCAT44(0x43300000, rec[8] ^ 0x80000000) - DOUBLE_8061b780));
}

// Start a positional sound for the sequence controller: register a fade handle
// tracking the entity position, then fire the one-shot at that position and
// stash the play handle (+0x23) and the loop sample id (+0x24) when looped.
void prop_seq_play_sound(int a1, int a2, int soundId)
{
    unsigned long long pair = FUN_803826f0();
    int *rec = (int *)((unsigned long long)pair >> 0x20);
    int entity = *rec;
    rec[0x1d] = (int)pair;
    rec[0x1e] = (int)FLOAT_8061b7a8;

    float *pos = entity_get_world_position(entity);
    sound_register_fade_out((double)FLOAT_8061b7ac, (double)FLOAT_8061b7b0, rec + 0x23, (unsigned int *)pos);
    pos = entity_get_world_position(entity);

    // params buffer: [0]=sound id; the source also fills a volume/flag/position
    // block at higher offsets before issuing the request.
    int params[16];
    sound_init_play_params((int)params);
    *(unsigned char *)((int)params + 0x20) = 1;          // active flag
    *(float *)((int)params + 0x24) = FLOAT_8061b7b4;     // range/pitch
    *(float *)((int)params + 0x18) = FLOAT_8061b7ac;     // volume
    *(unsigned int *)((int)params + 0x1c) = pos[0];      // position xyz
    *(unsigned int *)((int)params + 0x14) = pos[1];
    *(unsigned int *)((int)params + 0x10) = pos[2];
    params[0] = soundId;
    unsigned int handle = sound_play_request(params);
    rec[0x23] = handle;
    rec[0x24] = ((*(unsigned int *)(DAT_8060fa8c + soundId * 0x14 + 8) & 0x10) != 0) ? soundId : -1;
    FUN_8038273c();
}

// Initialise the sequence controller record: clear the working block (+0x74..),
// invalidate the two entity handles (+0x8c/+0x90), enter state 0 sound 0x5ce,
// clear the id/value pair table and seed its two known ids.
void prop_seq_init(int rec)
{
    memset_wrapper(rec + 0x74, 0, 0x20);
    *(int *)(rec + 0x8c) = -1;
    *(int *)(rec + 0x90) = -1;
    prop_seq_play_sound(rec, 0, 0x5ce);
    *(float *)(rec + 0x78) = FLOAT_8061b7a8;
    memset_wrapper(-0x7fb68bd0 /*id/value table base*/, 0, 0x40);
    DAT_80497430 = 0x1266;
    DAT_804973ec = 0x5a504434;   // 'ZPD4'
    DAT_804973f4 = 0x5a504435;   // 'ZPD5'
}

// Set the value paired with `id` in the DAT_804973ec id/value table (stride 8),
// stopping at the first empty id slot.
void prop_seq_table_set(int id, int value)
{
    int off = 0;
    while (true) {
        if (*(int *)((int)&DAT_804973e8 + off) == 0) return;
        if (id == *(int *)((int)&DAT_804973ec + off)) break;
        off += 8;
    }
    *(int *)((int)&DAT_804973ec + off) = value;
}

// Play a difficulty-gated one-shot sound at the prop's position: skip the sound
// id 0x5e9 on non-easy difficulty, otherwise fire it at the stored position.
void prop_seq_play_gated(void)
{
    unsigned long long pair = FUN_803826f0();
    unsigned int *pos = (unsigned int *)pair;
    int idx = (int)((unsigned long long)pair >> 0x20);
    int soundId = *(int *)(&DAT_80497354 + idx * 0x18);
    if ((soundId == 0x5e9) && (difficulty_index() != 0)) {
        soundId = -1;
    }
    if (soundId != -1) {
        soundId = *(int *)(&DAT_80497354 + idx * 0x18);
        int params[7];
        sound_init_play_params((int)params);
        params[0] = soundId;
        // params carry the position words from the scratch pair.
        params[4] = pos[0];
        params[5] = pos[1];
        params[6] = pos[2];
        sound_play_request(params);
    }
    FUN_8038273c();
}

// The ekun grid SEQUENCE CONTROLLER state machine. State is rec+0x1d, the
// animation timer rec+0x1e. State 2 handles the interactive grid (press/toggle
// slots, drag), states 0/1/3 advance a timed intro that spawns per-slot
// character FX through the ghz thunk table and updates the looped 3d sound.
void prop_seq_tick(void)
{
    unsigned long long pair = FUN_803826d8();
    int *rec = (int *)((unsigned long long)pair >> 0x20);
    int input = (int)pair;
    int entity = *rec;
    FUN_8037d594((int)rec);
    int state = rec[0x1d];

    if (state == 2) {
        if (input == 3) {
            int slot = FUN_8037d264((int)rec);
            if (slot == -1) {
                slot = FUN_8037d310((int)rec);
                if (slot == -1) {
                    if ((FLOAT_8061b7d4 <= (float)rec[0xe]) && ((float)rec[0xe] <= FLOAT_8061b7dc) &&
                        (FLOAT_8061b7e0 <= (float)rec[0xf]) && ((float)rec[0xf] <= FLOAT_8061b7dc)) {
                        prop_seq_play_sound((int)rec, 3, 0x5cc);
                        DAT_80612fbc = 0;
                        int mount = prop_player_mount_entity(rec[5]);
                        prop_player_dismount(mount);
                    }
                } else {
                    (&DAT_8049734c)[slot * 0x18] ^= 1;
                }
            } else {
                DAT_80611360 = slot;
                (&DAT_80497344)[slot * 6] = ((float)rec[0xe] - FLOAT_8061b7d4) / FLOAT_8061b7d8;
            }
        } else if (input == 1) {
            DAT_80611360 = -1;
        } else if ((input == 2) && (DAT_80611360 != -1)) {
            float v = ((float)rec[0xe] - FLOAT_8061b7d4) / FLOAT_8061b7d8;
            float cl = FLOAT_8061b7a8;
            if ((FLOAT_8061b7a8 <= v) && (cl = v, FLOAT_8061b7cc < v)) cl = FLOAT_8061b7cc;
            (&DAT_80497344)[DAT_80611360 * 6] = cl;
        }
    } else {
        bool bail = false;
        if (state < 2) {
            if (state == 0) {
                float v = (float)rec[0x1e];
                if (FLOAT_8061b7bc <= ABS(FLOAT_8061b7b8 - v)) {
                    if (FLOAT_8061b7bc <= ABS(FLOAT_8061b7c0 - v)) {
                        if (FLOAT_8061b7bc <= ABS(FLOAT_8061b7c4 - v)) {
                            if (ABS(FLOAT_8061b7c8 - v) < FLOAT_8061b7bc)
                                prop_textbox_list_insert(-0x7fb68be4, 0x126a);
                        } else prop_textbox_list_insert(-0x7fb68be4, 0x1269);
                    } else prop_textbox_list_insert(-0x7fb68be4, 0x1268);
                } else prop_textbox_list_insert(-0x7fb68be4, 0x1267);
            } else if (state < 0) bail = true;
        } else if (3 < state) bail = true;

        if (!bail) {
            if (FLOAT_8061b7cc <= (float)rec[0x1e]) {
                state = rec[0x1d];
                if (state != 2) {
                    if (state < 2) {
                        if (state == 0) { prop_seq_play_sound((int)rec, 1, 0x5cd); bail = true; }
                        else if (state < 0) bail = true;
                    } else if (3 < state) bail = true;
                    else {
                        for (int i = 0; *(int *)((int)&DAT_804973e8 + i) != 0; i += 8) {
                            if (*(int *)rec[4] == *(int *)((int)&DAT_804973e8 + i)) {
                                int e = entity_handle_resolve(*(unsigned int *)((int)&DAT_804973ec + i));
                                if ((e != 0) && ((*(unsigned int *)(*(int *)(e + 0x7c) + 0x10) & 0x400) != 0))
                                    ghz_char_fx_thunk_7();
                                break;
                            }
                        }
                    }
                    if (!bail) prop_seq_play_sound((int)rec, 2, 0x5cf);
                }
            } else {
                rec[0x1e] = (int)((float)rec[0x1e] + *(float *)(&DAT_803e0f88 + rec[0x1d] * 4));
                if (rec[0x1d] == 3) {
                    *(unsigned int *)(entity + 0x18) |= 0x40;
                    double baseT = (double)FLOAT_8061b7d0;
                    int fxOff = 0;
                    for (unsigned int step = 0; (int)step < 7; step++, fxOff += 0x18) {
                        double t = baseT * (double)(float)((double)CONCAT44(0x43300000, step ^ 0x80000000) - DOUBLE_8061b7e8);
                        if (((float)t < (float)rec[0x1e]) && ((1 << step & DAT_80612fbc) == 0)) {
                            DAT_80612fbc |= 1 << step;
                            if (((&DAT_8049734c)[fxOff] == 0) ||
                                (*(int *)((int)&PTR_ghz_char_fx_thunk_0_80497350 + fxOff) == 0)) {
                                rec[0x1e] = (int)((float)rec[0x1e] + FLOAT_8061b7d0);
                            } else {
                                for (int i = 0; *(int *)((int)&DAT_804973e8 + i) != 0; i += 8) {
                                    if (*(int *)rec[4] == *(int *)((int)&DAT_804973e8 + i)) {
                                        int e = entity_handle_resolve(*(unsigned int *)((int)&DAT_804973ec + i));
                                        if ((e != 0) && ((*(unsigned int *)(*(int *)(e + 0x7c) + 0x10) & 0x400) != 0)) {
                                            (**(void (**)(double))((int)&PTR_ghz_char_fx_thunk_0_80497350 + fxOff))
                                                ((double)*(float *)((int)&DAT_80497344 + fxOff));
                                            entity_get_world_position(e);
                                            prop_seq_play_gated();
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Update the looped 3d sound and re-flag the entity as audible.
    int loop = rec[0x24];
    if (loop != -1) {
        float *pos = entity_get_world_position(entity);
        int audible = snd_update_3d((double)FLOAT_8061b7ac, (double)FLOAT_8061b7b4,
                                    (double)FLOAT_8061b7cc, rec + 0x23, pos, loop, 0);
        if (audible != 0) *(unsigned int *)(entity + 0x18) |= 0x40;
    }
    FUN_80382724();
}

// ============================================================================
// Address range: 0x8037d0d0 - 0x8037d7b0 | batch_1058 | 2026-09-13
//
// The ekun grid SEQUENCE CONTROLLER's render + input helpers - the pieces the
// prop_seq_tick state machine (batch_1057) and the panel draw dispatcher call.
// This block stops the loop sound, dispatches the per-state panel draw, binds
// the panel descriptor, locates the grid press/toggle slot under the cursor,
// draws the waveform graph, the level bars, the per-slot indicator pairs and the
// labelled icon rows, and advances the 11-slot animation trail every 6 frames.
//
//   prop_seq_stop_sound        0x8037d0d0  stop the loop sound handle, return 0
//   prop_seq_draw_dispatch     0x8037d0f8  per-state panel draw dispatcher (+0x74)
//   prop_seq_bind_panel        0x8037d238  bind the grid panel descriptor
//   prop_seq_find_press_slot   0x8037d264  locate the grid "press" slot (-1 none)
//   prop_seq_find_toggle_slot  0x8037d310  locate the grid "toggle" slot (-1 none)
//   prop_seq_draw_waveform     0x8037d3bc  draw the 11-point waveform graph (GX)
//   prop_seq_draw_level_bar    0x8037d514  draw one horizontal level bar
//   prop_seq_tick_animation    0x8037d594  advance the 11-slot animation trail
//   prop_seq_draw_slots        0x8037d660  draw the 7 slot indicator pairs
//   prop_seq_draw_labels       0x8037d7b0  draw the 7 labelled icon rows
//
// NOTE: non-matching decomp. Colour words (0x17ff17xx) and CONCAT44 int->float
// conversions are reproduced literally. Control flow, GX call sequences, hit
// bands and the trail-fade arithmetic match the original. prop_panel_draw is
// invoked through the file-scope prop_panel_draw_ret() macro (its generated
// prototype takes args / returns void).

// ---- grid seq render rodata / tables --------------------------------------
extern int    DAT_803e0f98, DAT_803e0f9c;   // waveform y-offset tables (stride 4)
extern float  FLOAT_8061b7f4;        // press-slot x hit-band hi
extern float  FLOAT_8061b7f8, FLOAT_8061b7fc;   // toggle-slot x hit-band lo/hi
extern float  FLOAT_8061b800, FLOAT_8061b804;   // level-bar scale / divisor
extern double DOUBLE_8061b808;       // 2^52 bias (level-bar byte->float)
extern float  FLOAT_8061b810, FLOAT_8061b814, FLOAT_8061b818;   // slot fx colour scales
extern int    DAT_80497340;          // grid seq row-label string-id table (0x18 stride)
extern char   DAT_80612fb8;          // 6-frame animation frame counter
extern int    PTR_LAB_80497400;      // grid panel descriptor (bind target)
extern void FUN_8037d8f0(void);      // draw the grid-panel header row (batch_1059)

// Stop the sequence controller's looped sound handle.
unsigned int prop_seq_stop_sound(void)
{
    sound_stop_handle(0);
    return 0;
}

// Per-state panel draw dispatcher (state at +0x74): state 1 draws the confirm
// bar, state <0 draws the caption list, states 1..3 draw the interactive grid
// (waveform + header + level bars + slot indicators) with a confirm bar on
// state 3.
void prop_seq_draw_dispatch(int rec)
{
    int state = *(int *)(rec + 0x74);
    if (state == 1) {
        if (prop_panel_draw_ret() != 0) {
            button_draw_progress_split(0x40, 0x80, 0xc0, 0x8a, 0x7f);
        }
    } else if (state < 1) {
        if ((-1 < state) && (prop_panel_draw_ret() != 0)) {
            prop_textbox_draw_caption_list();
        }
    } else if (state < 4) {
        if (prop_panel_draw_ret() != 0) {
            prop_seq_draw_labels();
            FUN_8037d8f0();
            prop_seq_draw_waveform(0xac, 0x36, rec);
            prop_seq_draw_level_bar(0xac, 0x4e, rec);
            prop_seq_draw_slots();
        }
        if ((*(int *)(rec + 0x74) == 3) && (prop_panel_draw_ret() != 0)) {
            button_draw_progress_split(0x40, 0x80, 0xa8, 0x8a, 0x7f);
        }
    }
}

// Bind the grid panel descriptor for this prop state.
void prop_seq_bind_panel(int st)
{
    prop_panel_bind_descriptor((int)&PTR_LAB_80497400, st);
}

// Locate the grid "press" slot (0..6) whose hit rect contains the cursor
// (+0x38/+0x3c). Returns -1 if none.
int prop_seq_find_press_slot(int rec)
{
    int slot = 0;
    int yOff = 0;
    int n = 7;
    while (true) {
        if ((FLOAT_8061b7d4 <= *(float *)(rec + 0x38)) &&
            (*(float *)(rec + 0x38) <= FLOAT_8061b7f4) &&
            ((float)((double)CONCAT44(0x43300000, yOff + 0x1cU ^ 0x80000000) - DOUBLE_8061b7e8) <= *(float *)(rec + 0x3c)) &&
            (*(float *)(rec + 0x3c) <= (float)((double)CONCAT44(0x43300000, yOff + 0x26U ^ 0x80000000) - DOUBLE_8061b7e8)))
            break;
        slot++;
        yOff += 0x1c;
        n--;
        if (n == 0) return -1;
    }
    return slot;
}

// Locate the grid "toggle" slot (0..6) whose hit rect contains the cursor.
// Returns -1 if none.
int prop_seq_find_toggle_slot(int rec)
{
    int slot = 0;
    int yOff = 0;
    int n = 7;
    while (true) {
        if ((FLOAT_8061b7f8 <= *(float *)(rec + 0x38)) &&
            (*(float *)(rec + 0x38) <= FLOAT_8061b7fc) &&
            ((float)((double)CONCAT44(0x43300000, yOff + 0x1cU ^ 0x80000000) - DOUBLE_8061b7e8) <= *(float *)(rec + 0x3c)) &&
            (*(float *)(rec + 0x3c) <= (float)((double)CONCAT44(0x43300000, yOff + 0x30U ^ 0x80000000) - DOUBLE_8061b7e8)))
            break;
        slot++;
        yOff += 0x1c;
        n--;
        if (n == 0) return -1;
    }
    return slot;
}

// Draw the 11-point waveform graph: a bordered box (frame + 5 horizontal grid
// lines) then the 11 connected segments coloured by the per-slot trail byte.
void prop_seq_draw_waveform(int a1, int a2, int rec)
{
    unsigned long long pair = FUN_803826e4();
    int x = (int)((unsigned long long)pair >> 0x20);
    int y = (int)pair;
    gx_draw_line_i(x, y - 0x10, x, y + 0x10, 0x17ff1710);
    gx_draw_line_i(x + 0x2c, y - 0x10, x + 0x2c, y + 0x10, 0x17ff1710);
    gx_draw_line_i(x, y - 0x10, x + 0x2c, y - 0x10, 0x17ff1720);
    gx_draw_line_i(x, y - 8, x + 0x2c, y - 8, 0x17ff1720);
    gx_draw_line_i(x, y, x + 0x2c, y, 0x17ff1720);
    gx_draw_line_i(x, y + 8, x + 0x2c, y + 8, 0x17ff1720);
    gx_draw_line_i(x, y + 0x10, x + 0x2c, y + 0x10, 0x17ff1720);
    int i = 0;
    int off = 0;
    do {
        gx_draw_line_counted(x + off, y + *(int *)((int)&DAT_803e0f98 + off) * 4,
                             x + (i + 1) * 4, y + *(int *)((int)&DAT_803e0f9c + off) * 4,
                             *(unsigned char *)(rec + i + 0x7c) | 0x17ff1700);
        i++;
        off += 4;
    } while (i < 0xb);
    FUN_80382730();
}

// Draw one horizontal level bar whose width is the slot byte (+0x88) scaled into
// the bar's pixel range.
void prop_seq_draw_level_bar(unsigned int x, int y, int rec)
{
    unsigned char lvl = *(unsigned char *)(rec + 0x88);
    int x1 = (int)((float)((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061b7e8)
                   + (FLOAT_8061b800 * (float)((double)CONCAT44(0x43300000, (unsigned int)lvl) - DOUBLE_8061b808)) / FLOAT_8061b804);
    gx_fill_rect_clipped(x, y, x1, y + 4, lvl | 0x17ff1700);
}

// Advance the 11-slot animation trail once every 6 frames: bump the head slot
// (wrapping at 11), set the head to full (0x7f), fade the others down by 0x20
// (clamped to 0), and pulse the +0x88 level byte.
void prop_seq_tick_animation(int rec)
{
    DAT_80612fb8 = DAT_80612fb8 + 1;
    if (DAT_80612fb8 != 6) return;
    DAT_80612fb8 = 0;

    char head = *(char *)(rec + 0x87) + 1;
    *(char *)(rec + 0x87) = head;
    if (head == 0xb) *(unsigned char *)(rec + 0x87) = 0;

    int n = 0xb;
    for (unsigned int i = 0; n != 0; i++, n--) {
        if (i == *(unsigned char *)(rec + 0x87)) {
            *(unsigned char *)(rec + i + 0x7c) = 0x7f;
        } else {
            int o = i + 0x7c;
            if (*(unsigned char *)(rec + o) < 0x21) *(unsigned char *)(rec + o) = 0;
            else *(unsigned char *)(rec + o) = *(unsigned char *)(rec + o) - 0x20;
        }
    }
    if (*(char *)(rec + 0x87) == 4) {
        *(unsigned char *)(rec + 0x88) = 0xff;
        return;
    }
    if (0x20 < *(unsigned char *)(rec + 0x88)) {
        *(unsigned char *)(rec + 0x88) = *(unsigned char *)(rec + 0x88) - 0x20;
        return;
    }
    *(unsigned char *)(rec + 0x88) = 0;
}

// Draw the 7 slot indicator pairs: an enable box (dim/bright by the per-slot
// flag) and an fx-amount colour swatch whose RGB is derived from DAT_80497344.
void prop_seq_draw_slots(void)
{
    unsigned long long pair = FUN_803826d4();
    int x = (int)((unsigned long long)pair >> 0x20);
    double sR = (double)FLOAT_8061b810;
    double sG = (double)FLOAT_8061b814;
    double sB = (double)FLOAT_8061b818;
    double sInv = (double)FLOAT_8061b804;
    int i = 0;
    int yOff = 0;
    int fxOff = 0;
    do {
        unsigned int enCol = ((&DAT_8049734c)[fxOff] != 0) ? 0x17ff177f : 0x17ff1717;
        int y = (int)pair + yOff;
        int y0 = y + 2;
        unsigned int y1 = y + 6;
        gx_fill_rect_clipped(x + 2, y0, x + 0x12, y1, enCol);
        double g = (double)(float)(sG * (double)*(float *)((int)&DAT_80497344 + fxOff));
        unsigned int col = ((unsigned int)(int)(sR * (double)*(float *)((int)&DAT_80497344 + fxOff)) & 0xff)
                         | ((unsigned int)(int)(sB + g) << 0x18)
                         | (((unsigned int)(int)(sInv - g) & 0xff) << 0x10)
                         | 0x1700;
        gx_fill_rect_clipped(x + 0x1a, y0, x + 0x2a, y1, col);
        i++;
        fxOff += 0x18;
        yOff += 8;
    } while (i < 7);
    FUN_80382720();
}

// Draw the 7 labelled icon rows: a progress-split bevel, the row label string
// (outlined), and the row's sprite (texture 0x388) for each of the 7 slots.
void prop_seq_draw_labels(void)
{
    FUN_803826d0();
    int prevFont = text_set_font_table(0x1a);
    int i = 0;
    int strOff = 0;
    int yOff = 0;
    double bias = DOUBLE_8061b7e8;
    do {
        button_draw_progress_split(0xd, yOff + 0x1c, 0x8c, yOff + 0x26U, 0x7f);
        int str = get_string_by_id(*(int *)((int)&DAT_80497340 + strOff));
        text_draw_char_outline((double)FLOAT_8061b7b4,
                               (double)(float)((double)CONCAT44(0x43300000, yOff + 0x26U ^ 0x80000000) - bias),
                               str, 0x7f7f7f7f, 0xff);
        int tex = sprite_get_texture_id(0x388);
        gx_set_active_material(tex);
        int mat = *(int *)DAT_80610a20 + tex * 0x58;
        gx_draw_sprite_rect(0x90, yOff + 0x1c, 0xa4, yOff + 0x30, 0,
                            (unsigned int)*(unsigned short *)(mat + 0x52) << 4,
                            (unsigned int)*(unsigned short *)(mat + 0x4e) << 4, 0);
        i++;
        yOff += 0x1c;
        strOff += 0x18;
    } while (i < 7);
    text_set_font_table(prevFont);
    FUN_8038271c();
}
