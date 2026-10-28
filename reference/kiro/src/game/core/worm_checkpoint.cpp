// worm_checkpoint.cpp - Mission-mode checkpoint save/restore + player zone teleport.
//
// This module implements the single-player / co-op mission "checkpoint" system:
// a per-player snapshot record (0x238 bytes, table base DAT_806129b8) captures the
// player object transform, weapon slot, aim state, and the wider world state
// (level entities, stat snapshot, music track, IGC actor mask, sub-object physics).
// worm_checkpoint_save() / worm_checkpoint_restore() are driven by
// match_enter_gameplay (0x80031e94). The zone-teleport helpers move a player to an
// adjacent sound-zone and are driven by level_actor_load_for_players (0x8002998c).
//
// Address range: 0x8025b098 - 0x8025b8c0

#include "common.h"

// ---- raw cross-module symbols (typed per call site) ----
extern "C" {
// checkpoint record table + cursors
extern int   DAT_806129b8;    // checkpoint record table base (0x238 stride)
extern int   DAT_806129bc;    // current checkpoint id (+1 of record[0])
extern int   DAT_806129c0;    // "restore performed this frame" flag
extern int   DAT_80611d74;    // player-index scratch (int base, byte fields)
extern int   DAT_80611d78;    // player record table base (0x141c stride)
extern int   DAT_80611d54;    // camera-lerp source handle
extern int   DAT_80611b8c;    // sound-zone node table base (0x20 stride)
extern int   DAT_80611c24;    // sub-object handle table base (0x2e8 stride)
extern int   DAT_80610ba0;    // anim/state descriptor table (0x29c stride)
extern int   DAT_80612610;    // active player count
extern int   DAT_80612614;    // active game-mode id
extern unsigned char DAT_805015bd;  // co-op sub-mode selector
extern unsigned int  DAT_805015c0;  // co-op mode flag bits

extern const float FLOAT_80618f94;  // recoil scale threshold factor

// register-spill context accessors: return the player-control work context
// (hi word = per-player record, lo word = session context).
extern unsigned long long __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);
extern int* FUN_803826e4(void);
extern void FUN_80382730(void);

// sound-zone / adjacency queries
extern int  soundpad_zone_node_to_index(int* node);
extern int  soundpad_query_adjacent_zones_simple(int zoneIdx, int* out, int max, int a, int b);
extern int  soundpad_find_zone_for_entity(int entityId);
extern int  soundpad_find_zone_by_id(int id);

// player / object helpers
extern int  worm_igc_actor_id_present(int id);
extern unsigned int worm_igc_actor_get_active_mask(void);
extern unsigned long long worm_igc_actor_set_active_mask(int mask);
extern int  level_get_previous_id(void);
extern int  level_get_current_id(void);
extern int  level_get_player_actor_id(int slot);
extern int  spawn_find_team_character(void);
extern int  slot_get_active_player(void);
extern void slot_release_player(void);
extern void slot_release_all_players(void);
extern void player_set_state(int rec, int state);
extern void player_ai_teleport_to_zone(int object, int rec, unsigned int mode);
extern double player_get_camera_lerp_factor(int handle);
extern void camera_set_mode_by_index(int session, int mode, int rec);
extern void object_model_swap_out_and_clear(int object);
extern void object_model_swap_in(int object);
extern void object_swap_gate_enable(void);
extern void object_swap_gate_disable(void);
extern void object_set_weapon_slot(int object, int slot);
extern void subobj_grab_attach_release(int handle, int rec, int a, int b, int c);
extern int* subobj_grab_attach_point(int* object);
extern void object_weapon_recoil_render_tick(void);
extern void leaf_31e8(unsigned int dst, int src, int len);

extern int  FUN_8036fe90(int rec);   // resolve grab/attach handle for record
extern void FUN_8036fea4(int handle); // release grab/attach handle

// world-state snapshot save/restore (called from checkpoint save/restore)
extern void player_ai_save_weapon_state(void);
extern void player_ai_restore_weapons_on_respawn(unsigned long long a, double b, double c,
        double d, unsigned long long e, unsigned long long f, unsigned long long g,
        unsigned long long h);
extern void level_save_all_entity_states(void);
extern void level_restore_all_entity_states(void);
extern void statlvl_snapshot_save(void);
extern void statlvl_snapshot_restore(void);
extern void music_save_current_track(void);
extern void worm_event_pack_state_snapshot(void);
extern void subobj_snapshot_all(void);
extern void subobj_snapshot_rollback(void);
extern void trigger_toggle_checkpoint_flags(int id);
extern unsigned int* get_string_by_id(int id);
extern void hud_text_submit(int a, int b, unsigned int* text);

// wider event tick drivers (varargs float/int spill; typed to match call site)
extern void worm_event_master_tick(unsigned long long f1, double p2, double p3, double p4,
        double p5, double p6, unsigned long long p7, unsigned long long p8, int flag,
        int rec, int p11, unsigned int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
extern void worm_event_apply_snapshot_tick(unsigned long long f1, double p2, double p3, double p4,
        unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8,
        int p9, int p10, int p11, int p12, unsigned int p13, unsigned int p14,
        unsigned int p15, unsigned int p16);
}

// forward decls (intra-file callers)
int  worm_checkpoint_resolve_spawn_zone(int recIdx);  // 0x8025b5dc (called externally)
static int* worm_find_adjacent_matching_zone(void);
static void worm_checkpoint_capture_player(unsigned int state, unsigned int flagBit);
static void worm_player_teleport_to_zone_impl(unsigned int isCurrent, int zoneIdx,
        unsigned int mode, int noStateChange, int doModelSwap);
static void worm_checkpoint_apply_player_snapshot(void);

// ---------------------------------------------------------------------------
// 0x8025b8c0 - read a checkpoint record's "valid" flag (record+0x4).
unsigned int worm_checkpoint_get_flag(int recIdx)
{
    if (DAT_806129b8 != 0) {
        return *(unsigned int*)(DAT_806129b8 + recIdx * 0x238 + 4);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8025b5dc - resolve the sound-zone a player should respawn into (or -1 when
// the checkpoint slot has no valid saved state).
int worm_checkpoint_resolve_spawn_zone(int recIdx)
{
    int rec = DAT_806129b8 + recIdx * 0x238;
    int chr = spawn_find_team_character();
    int zone = soundpad_find_zone_by_id(*(int*)(chr + 4));
    int result = -1;
    if (*(int*)(rec + 4) != 0) {
        result = zone;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8025b2fc - find the first adjacent sound-zone whose owning zone matches the
// player's current zone (used to pick a teleport destination).
static int* worm_find_adjacent_matching_zone(void)
{
    int* node;
    int adjIdx;
    int cursor;
    int nCount;
    int i;
    int myZone;
    int adjZone;
    int adjacent[26];

    node = FUN_803826e4();
    adjIdx = soundpad_zone_node_to_index(node);
    nCount = soundpad_query_adjacent_zones_simple(adjIdx, adjacent, 0x14, 0, 0);
    cursor = 0;
    for (i = 0; i < nCount; i = i + 1) {
        myZone = soundpad_find_zone_for_entity((int)*(short*)(node + 2));
        adjZone = soundpad_find_zone_for_entity(
            (int)*(short*)(DAT_80611b8c + *(int*)((int)adjacent + cursor) * 0x20 + 8));
        if (adjZone == myZone) break;
        cursor = cursor + 4;
    }
    FUN_80382730();
    return node;
}

// ---------------------------------------------------------------------------
// 0x8025b148 - core zone-teleport: swap the player model out, move the player
// object to the target zone, restore camera/state, swap model back in.
static void worm_player_teleport_to_zone_impl(unsigned int isCurrent, int zoneIdx,
        unsigned int mode, int noStateChange, int doModelSwap)
{
    int iVar1;
    unsigned int uVar2;
    unsigned int savedModel;
    int rec;
    int object;
    double dVar5;
    unsigned long long ctx;

    (void)isCurrent;
    (void)zoneIdx;
    ctx = __save_gpr_25();
    rec = DAT_80611d78 + (int)(ctx >> 0x20) * 0x141c;
    object = *(int*)(*(int*)(rec + 0x14) + 0x7c);
    savedModel = *(unsigned int*)(object + 0x9c);
    if (doModelSwap != 0) {
        object_model_swap_out_and_clear(object);
        object_swap_gate_enable();
    }
    iVar1 = FUN_8036fe90(rec);
    FUN_8036fea4(iVar1);
    uVar2 = *(unsigned int*)(object + 0xc08);
    if (uVar2 == 0) {
        iVar1 = 0;
    } else {
        iVar1 = DAT_80611c24 + (uVar2 >> 0x16) * 0x2e8;
        if (*(unsigned int*)(iVar1 + 0x2c) != uVar2) {
            iVar1 = 0;
            *(unsigned int*)(object + 0xc08) = 0;
        }
    }
    if (iVar1 != 0) {
        subobj_grab_attach_release(iVar1, *(int*)(rec + 0x14), 0, 0, 0);
    }
    iVar1 = slot_get_active_player();
    if (iVar1 == *(int*)(rec + 0x14)) {
        slot_release_player();
    }
    slot_release_all_players();
    player_ai_teleport_to_zone(object, (int)ctx, mode);
    if (noStateChange == 0) {
        if ((DAT_80612614 != 4) || (DAT_805015bd != '\n')) goto LAB_camera;
        if ((DAT_80612610 < 2) || ((DAT_805015c0 & 0x200) == 0)) goto LAB_camera;
    }
    player_set_state(rec, 2);
    uVar2 = *(unsigned int*)(*(int*)(rec + 8) + 0x21c);
    if ((uVar2 & 0x10000) != 0) {
        *(unsigned int*)(*(int*)(rec + 8) + 0x21c) = uVar2 & 0xfffeffff;
    }
    camera_set_mode_by_index(*(int*)(rec + 8), 0, *(int*)(rec + 0x14));
LAB_camera:
    dVar5 = player_get_camera_lerp_factor(DAT_80611d54);
    *(float*)(*(int*)(rec + 8) + 900) = (float)dVar5;
    *(float*)(*(int*)(rec + 8) + 0x388) = (float)dVar5;
    if (doModelSwap != 0) {
        object_swap_gate_disable();
        object_model_swap_in(object);
        if (*(int*)(object + 0x94) == 0) {
            *(unsigned int*)(object + 0x94) = savedModel;
        }
        if (*(int*)(object + 0x94) == 0) {
            *(unsigned int*)(object + 0x94) = 1;
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x8025b098 - teleport the local player to the next adjacent zone at level
// transition (skipped when the previous level was an IGC actor).
void worm_player_teleport_next_zone(void)
{
    int prevId;
    int actorId;
    int curId;
    int present;
    int* node;
    int zoneIdx;

    FUN_803826f0();
    prevId = level_get_previous_id();
    actorId = level_get_player_actor_id(0);
    curId = level_get_current_id();
    present = worm_igc_actor_id_present(prevId);
    if (present == 0) {
        node = worm_find_adjacent_matching_zone();
        zoneIdx = soundpad_zone_node_to_index(node);
        worm_player_teleport_to_zone_impl((unsigned int)(curId == actorId), zoneIdx, 2, 1, 1);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8025b45c - capture the current player object into the checkpoint record
// (transform, weapon, aim, animation joint tables).
static void worm_checkpoint_capture_player(unsigned int state, unsigned int flagBit)
{
    unsigned int* rec;
    unsigned int* text;
    int session;
    int object;
    unsigned long long ctx;

    ctx = FUN_803826f0();
    rec = (unsigned int*)(unsigned int)(ctx >> 0x20);
    session = (int)ctx;
    object = *(int*)(*(int*)(session + 0x14) + 0x7c);
    rec[0] = state;
    ((unsigned int*)rec)[1] = ((unsigned int*)rec)[1] | flagBit;
    text = get_string_by_id(0x189d);
    hud_text_submit(0, 2, text);
    rec[2] = *(unsigned int*)(object + 0x78);
    rec[3] = *(unsigned int*)(object + 0x7c);
    rec[4] = *(unsigned int*)(object + 0x94);
    rec[7] = *(unsigned int*)(object + 0xa0);
    if (*(int*)(session + 0x114) != 0x100) {
        if ((*(int*)(session + 0x114) != 0x200) && ((DAT_805015c0 & 0x200) == 0)) {
            if (*(int*)(object + 0x204) == -1) {
                rec[0x8c] = 0;
            } else {
                rec[0x8c] = 1;
            }
            goto LAB_body;
        }
    }
    rec[4] = *(unsigned int*)(object + 0x9c);
    rec[0x8c] = 0;
LAB_body:
    rec[6] = *(unsigned int*)(object + 0x98);
    rec[5] = *(unsigned int*)(object + 0x9c);
    rec[0x30] = *(unsigned int*)(*(int*)(session + 0x14) + 0x1cc);
    rec[0x31] = *(unsigned int*)(*(int*)(session + 0x14) + 0x1d8);
    rec[0x32] = *(unsigned int*)(*(int*)(session + 0x14) + 0x1dc);
    rec[0x8d] = *(unsigned int*)(object + 0x544);
    leaf_31e8((unsigned int)(rec + 0x49), object + 0x20c, 0x58);
    leaf_31e8((unsigned int)(rec + 0x33), object + 0xdc, 0x58);
    leaf_31e8((unsigned int)(rec + 8), object + 0x61e, 0x58);
    leaf_31e8((unsigned int)(rec + 0x75), object + 0x264, 0x58);
    leaf_31e8((unsigned int)(rec + 0x5f), object + 0x134, 0x58);
    leaf_31e8((unsigned int)(rec + 0x1e), object + 0x54c, 0x46);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8025b3ac - CHECKPOINT SAVE: snapshot player + world state into the current
// checkpoint record (one-shot per checkpoint bit). Driven by match_enter_gameplay.
void worm_checkpoint_save(unsigned long long param_1, double param_2, double param_3,
        double param_4, double param_5, double param_6, unsigned long long param_7,
        unsigned long long param_8, unsigned int param_9, unsigned int param_10, int param_11,
        unsigned int param_12, unsigned int param_13,
        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9; (void)param_10;
    int bit;
    int recBase;
    unsigned int mask;
    unsigned int igcMask;
    unsigned long long f1;

    (void)param_1;
    f1 = FUN_803826f0();
    bit = (int)f1;
    recBase = DAT_806129b8;
    mask = 1u << bit;
    if ((*(unsigned int*)(DAT_806129b8 + 4) & mask) == 0) {
        worm_event_master_tick((unsigned long long)0, param_2, param_3, param_4, param_5, param_6,
            param_7, param_8, 1, DAT_806129b8, param_11, param_12, param_13, param_14,
            param_15, param_16);
        worm_checkpoint_capture_player((unsigned int)recBase, mask);  // rec ptr passed via gpr context
        player_ai_save_weapon_state();
        level_save_all_entity_states();
        statlvl_snapshot_save();
        music_save_current_track();
        worm_event_pack_state_snapshot();
        igcMask = worm_igc_actor_get_active_mask();
        *(unsigned int*)(recBase + 0x22c) = igcMask;
        subobj_snapshot_all();
        if (1 < DAT_80612610) {
            worm_checkpoint_capture_player((unsigned int)(DAT_806129b8 + 0x238), mask);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8025b700 - apply a saved player snapshot back onto the live player object
// (transform, weapon slot, aim state, recoil, joint tables). Called on restore.
static void worm_checkpoint_apply_player_snapshot(void)
{
    float scaled;
    int i;
    int j;
    int object;
    int session;
    int rec;
    unsigned long long ctx;

    ctx = FUN_803826f0();
    rec = (int)(ctx >> 0x20);
    session = (int)ctx;
    object = *(int*)(*(int*)(session + 0x14) + 0x7c);
    *(unsigned int*)(object + 0x78) = *(unsigned int*)(rec + 8);
    *(unsigned int*)(object + 0x7c) = *(unsigned int*)(rec + 0xc);
    *(unsigned int*)(object + 0x98) = *(unsigned int*)(rec + 0x18);
    *(unsigned int*)(object + 0x9c) = *(unsigned int*)(rec + 0x14);
    *(unsigned int*)(object + 0xa0) = *(unsigned int*)(rec + 0x1c);
    object_set_weapon_slot(object, *(int*)(rec + 0x10));
    scaled = FLOAT_80618f94 * *(float*)(rec + 0xc4);
    if (scaled <= *(float*)(rec + 0xc0)) {
        *(float*)(*(int*)(session + 0x14) + 0x1cc) = *(float*)(rec + 0xc0);
    } else {
        *(float*)(*(int*)(session + 0x14) + 0x1cc) = scaled;
    }
    *(unsigned int*)(*(int*)(session + 0x14) + 0x1d8) = *(unsigned int*)(rec + 0xc4);
    *(unsigned int*)(*(int*)(session + 0x14) + 0x1dc) = *(unsigned int*)(rec + 200);
    *(unsigned int*)(object + 0x544) = *(unsigned int*)(rec + 0x234);
    leaf_31e8(object + 0x61e, rec + 0x20, 0x58);
    leaf_31e8(object + 0x264, rec + 0x1d4, 0x58);
    leaf_31e8(object + 0x134, rec + 0x17c, 0x58);
    leaf_31e8(object + 0x54c, rec + 0x78, 0x46);
    leaf_31e8(object + 0x20c, rec + 0x124, 0x58);
    leaf_31e8(object + 0xdc, rec + 0xcc, 0x58);
    if (*(int*)(rec + 0x230) == 1) {
        object_weapon_recoil_render_tick();
        leaf_31e8(object + 0x20c, rec + 0x124, 0x58);
        leaf_31e8(object + 0xdc, rec + 0xcc, 0x58);
        i = 0;
        j = 0x2c;
        do {
            int p = object + i;
            i = i + 2;
            *(short*)(p + 0x61e) = (short)(*(short*)(p + 0x61e) + *(short*)(p + 0x20c));
            *(short*)(p + 0x61e) = (short)(*(short*)(p + 0x61e) + *(short*)(p + 0xdc));
            j = j + -1;
        } while (j != 0);
    }
    if (*(int*)(object + 0x94) == 0) {
        object_model_swap_in(object);
    }
    if (*(int*)(object + 0x94) == 0) {
        *(unsigned int*)(object + 0x94) = *(unsigned int*)(object + 0x9c);
    }
    if (*(int*)(object + 0x94) == 0) {
        *(unsigned int*)(object + 0x94) = 1;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8025b638 - CHECKPOINT RESTORE: roll the world back to the last saved
// checkpoint (player snapshot, level entities, stats, IGC actors, sub-objects).
// Driven by match_enter_gameplay.
void worm_checkpoint_restore(unsigned long long param_1, double param_2, double param_3,
        double param_4, unsigned long long param_5, unsigned long long param_6,
        unsigned long long param_7, unsigned long long param_8)
{
    int checkpointId;
    int* rec;
    unsigned long long mask;

    (void)param_1;
    rec = (int*)(DAT_806129b8 + DAT_80611d74 * 0x238);
    if (rec[1] != 0) {
        checkpointId = rec[0];
        DAT_806129bc = checkpointId + 1;
        worm_checkpoint_apply_player_snapshot();
        player_ai_restore_weapons_on_respawn((unsigned long long)0, param_2, param_3, param_4,
            param_5, param_6, param_7, param_8);
        level_restore_all_entity_states();
        statlvl_snapshot_restore();
        trigger_toggle_checkpoint_flags(DAT_806129bc);
        mask = worm_igc_actor_set_active_mask(rec[0x8b]);
        worm_event_apply_snapshot_tick((unsigned long long)0, param_2, param_3, param_4,
            param_5, param_6, param_7, param_8, (int)(mask >> 0x20), (int)mask, checkpointId,
            0, 0, 0, 0, 0);
        subobj_snapshot_rollback();
        if (1 < DAT_80612610) {
            worm_checkpoint_apply_player_snapshot();
        }
        DAT_806129c0 = 1;
    }
}

// ===========================================================================
// batch_736 additions: checkpoint id/state accessors + reset + auto-save tick.
// Address range: 0x8025b8e4 - 0x8025bb88
// ===========================================================================

extern "C" {
extern int   DAT_805015c4;   // extended co-op mode flag bits
extern int   DAT_80611934;   // stat/event phase gate (7=live, 8=finalize, <9 checkpoint-eligible)
extern int   DAT_80612098;   // AI actor list count
extern int   DAT_80588610;   // AI actor pointer list base (stride 4)

extern void  memset_wrapper(int dst, int val, unsigned int len);
extern int   spawn_always_true(void);
extern bool  worm_event_class_is_active(int classId);
extern unsigned int FUN_8027eb3c(int actor);       // actor needs checkpoint-id sync?
extern void  FUN_8027e970(int actorId, char id);   // push checkpoint id to actor
extern void  FUN_803826e8(void);                   // event-context enter (no return value)
extern void  FUN_80382734(void);                   // event-context leave
}

// forward decls
int worm_checkpoint_get_current_id(void);           // 0x8025b920

// ---------------------------------------------------------------------------
// 0x8025b8e4 - checkpoint id for a slot (record+0x0 + 1) when the slot has a
// valid saved state, else 0.
int worm_checkpoint_get_id_for_slot(int recIdx)
{
    if (DAT_806129b8 == 0) {
        return 0;
    }
    if (*(int*)(DAT_806129b8 + recIdx * 0x238 + 4) == 0) {
        return 0;
    }
    return *(int*)(DAT_806129b8 + recIdx * 0x238) + 1;
}

// ---------------------------------------------------------------------------
// 0x8025b920 - current checkpoint id (DAT_806129bc).
int worm_checkpoint_get_current_id(void)
{
    return DAT_806129bc;
}

// ---------------------------------------------------------------------------
// 0x8025b928 - clear all checkpoint records (per-player state/flag/igc-mask,
// then zero the whole table) and reset id/restore-flag. Called by
// match_end_trigger + player_character_system_alloc.
void worm_checkpoint_reset_all(void)
{
    int off;
    unsigned int* rec;
    int i;

    off = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        rec = (unsigned int*)(DAT_806129b8 + off);
        off = off + 0x238;
        rec[1] = 0;
        rec[0] = 0;
        rec[0x8b] = 0;
    }
    memset_wrapper(DAT_806129b8, 0, (unsigned int)(DAT_80612610 * 0x238 + 0xf) & 0xfffffff0);
    DAT_806129bc = 0;
    DAT_806129c0 = 0;
}

// ---------------------------------------------------------------------------
// 0x8025b9a8 - clear the "restore performed this frame" flag.
void worm_checkpoint_clear_restore_flag(void)
{
    DAT_806129c0 = 0;
}

// ---------------------------------------------------------------------------
// 0x8025b9b4 - CHECKPOINT AUTO-SAVE TICK. Runs each player frame:
//  (a) if a restore just happened, push the current checkpoint id to every
//      live AI actor that needs it, then clear the restore flag;
//  (b) in co-op story mode (not arcade/finalize), for each not-yet-saved
//      checkpoint bit, save a checkpoint for every team character that is
//      either in an active event class or standing in the player's zone.
// Driven by player_main_tick (0x800a9138).
void worm_checkpoint_autosave_tick(unsigned long long param_1, double param_2, double param_3,
        double param_4, double param_5, double param_6, unsigned long long param_7,
        unsigned long long param_8, unsigned int param_9, unsigned int param_10, int param_11,
        unsigned int param_12, unsigned int param_13, unsigned int param_14,
        unsigned int param_15, unsigned int param_16)
{
    bool matched;
    unsigned int needSync;
    int id;
    int chr;
    int chrHi;
    int chrLo;
    int* actor;
    int bit;
    int p;
    int off;
    unsigned long long chrPair;

    (void)param_1; (void)param_9; (void)param_10;
    FUN_803826e8();

    if (DAT_806129c0 != 0) {
        off = 0;
        for (p = 0; p < DAT_80612098; p = p + 1) {
            actor = *(int**)((int)&DAT_80588610 + off);
            if (((actor != (int*)0) && (actor[0x2fc] != 0)) &&
                (needSync = FUN_8027eb3c((int)actor), needSync != 0)) {
                id = worm_checkpoint_get_current_id();
                FUN_8027e970(*actor, (char)id);
                DAT_806129c0 = 0;
            }
            off = off + 4;
        }
    }

    chr = DAT_806129b8 + *(int*)&DAT_80611d74 * 0x238;
    if (((DAT_805015c0 & 0x40) == 0) && (DAT_805015bd == '\n') &&
        (DAT_80612614 != 100) && ((DAT_805015c4 & 0x10000) == 0) &&
        (id = spawn_always_true(), id != 0) && ((DAT_805015c0 & 0x200) == 0) &&
        (DAT_80611934 < 9)) {
        bit = 0;
        do {
            if ((*(unsigned int*)(chr + 4) & (1 << bit)) == 0) {
                for (int pi = 0; pi < DAT_80612610; pi = pi + 1) {
                    chrPair = (unsigned long long)(unsigned int)spawn_find_team_character();
                    chrHi = (int)(chrPair >> 0x20);
                    chrLo = (int)chrPair;
                    if (chrHi != 0) {
                        matched = false;
                        if (*(int*)(chrHi + 0x18) == -1) {
                            id = soundpad_find_zone_by_id(*(int*)(chrHi + 4));
                            if ((id != -1) &&
                                (chrLo = *(int*)((&DAT_80611d74)[5] + 0x7c),
                                 *(int*)(chrLo + 0x678) == id)) {
                                matched = true;
                            }
                        } else {
                            if (worm_event_class_is_active(*(int*)(chrHi + 0x18))) {
                                matched = true;
                            }
                        }
                        if (matched) {
                            worm_checkpoint_save((unsigned long long)0, param_2, param_3, param_4,
                                param_5, param_6, param_7, param_8, bit, chrLo, param_11,
                                param_12, param_13, param_14, param_15, param_16);
                        }
                    }
                }
            }
            bit = bit + 1;
        } while (bit < 0x20);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8025bb88 - state of a checkpoint bit for the local player:
//   2 = already saved, 0 = eligible-but-not-saved (normal spawn), 1 = otherwise.
unsigned int worm_checkpoint_get_bit_state(int bit)
{
    unsigned int result;
    int ok;

    if ((*(unsigned int*)(DAT_806129b8 + *(int*)&DAT_80611d74 * 0x238 + 4) & (1 << bit)) == 0) {
        if (((DAT_805015c4 & 0x10000) == 0) && (ok = spawn_always_true(), ok != 0)) {
            result = 0;
        } else {
            result = 1;
        }
    } else {
        result = 2;
    }
    return result;
}
