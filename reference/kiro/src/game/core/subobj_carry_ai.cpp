// subobj_carry_ai.cpp - AI actor "grab & carry sub-object" behaviour: the state
// machine that lets an AI pick up, haul, and drop a physics sub-object (crate,
// barrel, body) and the target-selection / navigation that supports it.
//
// The AI's carry state lives at actor+0x20 (a small enum) with a pending-swap
// record (prev/next/timestamp at +0x9d0/+0x9d4/+0x9d8 in the wide layout, or
// [0x274]/[0x275]/[0x276] in the packed int-array view). The carried sub-object
// handle is at +0xc08 / [0x302]; it is validated against the sub-object pool
// (DAT_80611c24, stride 0x2e8, generation check at +0x2c) every tick.
//
//   * subobj_slot_addr (0x802a472c) - index a 0x30-stride slot table relative to
//     its live-count header.
//   * carry_partner_wants_object (0x802a5840) - co-op check: is the carried
//     object the local partner's, or a PRAP/PRAF-tagged pickup?
//   * carry_acquire_target (0x802a58c8) - pick the AI's current attack/interact
//     target depending on co-op mode and level variant (DAT_80612614).
//   * carry_state_tick (0x802a4744) - the ~15-case carry FSM (approach / lift /
//     haul / drop / stuck), driven by subobj_carry_ai_state_tick transitions.
//   * carry_move_tick (0x802a59f0) - the movement/aim half: acquire a grab slot,
//     commit the attach, steer toward the grab point, and post animation flags.
//   * carry_soundpad_scan (0x802a6690) - when the carried object crosses a sound
//     trigger volume, retarget the AI's active audio zone.
//
// Address range: 0x802a472c - 0x802a6937 (non-contiguous; boss3 render 0x802a45b8
// -0x802a460b and prop-hinge advance 0x802a6440-0x802a668f live in sibling files)
// Subsystem: game (AI carry behaviour)

#include "common.h"

extern "C" {

// --- data globals -----------------------------------------------------------
extern int  DAT_80611c24;   // sub-object pool base (stride 0x2e8, gen at +0x2c)
extern int  DAT_80611824;   // (unused here) clip table - kept out
extern int  DAT_80611b8c;   // sound-zone record table base (stride 0x20)
extern int  DAT_80611d78;   // player/session record table base (+0x14 = leader rec)
extern int  DAT_80612614;   // level/creature variant selector
extern int  DAT_806120f8;   // "current follow leader" record handle
extern char DAT_805015bd;   // co-op sub-mode selector ('\n' = split-screen co-op)
extern int  DAT_806109c0;   // frame-delta ticks (xor-biased int->float source)
extern int  DAT_803e07bc;   // grab-slot preference ring (read as 6-word block)

extern const float  FLOAT_806198d0;  // 0.0 (state timestamp / "now")
extern const float  FLOAT_806198d4;  // delta-time scale A
extern const float  FLOAT_806198d8;  // approach speed
extern const float  FLOAT_806198dc;  // haul speed
extern const float  FLOAT_806198e0;  // re-plan cooldown
extern const float  FLOAT_806198e4;  // haul speed (obstructed)
extern const float  FLOAT_806198f4;  // floor-probe drop height
extern const float  FLOAT_806198f8;  // turn anim playback rate
extern const float  FLOAT_806198fc;  // squared "reached" distance
extern const float  FLOAT_806108ec;  // delta-time scale B
extern const double DOUBLE_806198e8; // int->float reconstruction bias

// --- frame-enter intrinsics (see aiactor_*.cpp) -----------------------------
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern int  FUN_803826d4(void);   // fp frame enter -> control ctx ptr
extern void FUN_80382720(void);   // paired restore

// --- shared AI helpers (declared consistently across the tree) --------------
extern int  aiactor_retarget_visible_teammate(void);
extern int  tracker_coop_scan_partner_nodes(void);
extern void aiactor_leader_zone_update(int ctx);
extern void aiactor_nav_advance_path(int ctx, int* zone);
extern int  aiactor_follow_zone_path(void);
extern unsigned long long aiactor_facing_update(void);
extern void aiactor_movement_tick(unsigned long long facing, double a2, double a3,
                                  double a4, double a5, double a6, double a7,
                                  unsigned long long a8);
extern void aiactor_set_stance_flags(int ctx, int flags);
extern void aiactor_play_turn_animation(int ctx);
extern bool aiactor_aim_alignment_test(double tol, int ctx, float* targetPos);
extern int  aiactor_acquire_nearest_target(int self, unsigned int relTable, int mode);
extern double floor_query_with_interact(double y, double maxDrop, int posPtr, int a4,
                                        int* a5, int a6);
extern void anim_set_playback_rate(double rate, int* animObj, int a3);
extern void tracker_play_locomotion_clip(void);
extern int  tracker_scan_nearest_relation_actor(void);

// --- sub-object grab/carry helpers ------------------------------------------
extern int  subobj_carry_channel_ptr(int subRec);
extern int  subobj_carry_ai_state_tick(int subRec, int ctx, int carryState);
extern int  subobj_carried_slot_value(int* leaderRec);
extern int  subobj_count_live_grabs(int subRec);
extern int  subobj_carry_nav_targets_valid(int subRec);
extern void subobj_carry_nav_resolve_zones(void);
extern void subobj_carry_speed_response(double speed, int subRec);
extern int  subobj_carry_zone_distance(void);
extern unsigned int subobj_carry_flags(int subRec);
extern int  subobj_find_active_grab(int subRec, int* outGrab);
extern unsigned long long subobj_carry_nearest_grab_scan(void);
extern int  subobj_grab_slot_ring_advance(int subRec, unsigned int* cursor);
extern int  subobj_grab_slot_world_point(int subRec, unsigned int slot, float* outPos);
extern int  subobj_grab_scan_release_zone(void);
extern int  subobj_grab_commit_attach(unsigned long long a1, double a2, int subRec,
                                      int ctx, unsigned int slot);
extern int  subobj_grab_try_auto_attach(double a1, double a2, int subRec, int ctx);

// --- soundpad helpers -------------------------------------------------------
extern int soundpad_find_nearest_entity(double now, float* pos);
extern int soundpad_resolve_zone_node(int entityId);
extern int soundpad_get_entity_id_from_zone(int zone);
extern int soundpad_find_trigger_by_id(int id);
extern int soundpad_query_adjacent_zones_simple(unsigned int zone, int* outList,
                                                int maxCount, int mask, int a5);
extern int soundpad_find_zone_by_id(int id);
extern int soundpad_trigger_valid(void); // 0x802a6a98 (aiactor_soundpad_nav.cpp)

// two boss3-adjacent helpers referenced by the carry FSM, resolved elsewhere
extern void FUN_802b6d9c(void);
extern void FUN_802b61cc(void);
extern int  ghz_reactor_tank_link_tick(void);
extern int  ghz_reactor_nearest_target_scan(void);
extern int  worm_swarm_los_cull_update(void);

} // extern "C"

// forward decls
int  subobj_slot_addr(int* slotTable, int slotIndex);          // 0x802a472c
int  carry_partner_wants_object(int ctx);                      // 0x802a5840
void carry_acquire_target(int ctx);                            // 0x802a58c8
void carry_state_tick(unsigned long long a1, double a2, double a3, double a4,
                      double a5, double a6, double a7, unsigned long long a8); // 0x802a4744
void carry_move_tick(unsigned long long a1, double a2, double a3, double a4,
                     double a5, double a6, double a7, unsigned long long a8);  // 0x802a59f0
void carry_soundpad_scan(void);                                // 0x802a6690

// ---------------------------------------------------------------------------
// 0x802a472c - address of a 0x30-stride slot, offset by the table's live-count
// header (*slotTable) so element `slotIndex` is addressed from the used region.
int subobj_slot_addr(int* slotTable, int slotIndex)
{
    return (int)(slotTable + *slotTable * -0x30 + slotIndex * 0x30);
}

// ---------------------------------------------------------------------------
// 0x802a5840 - co-op: does the current partner want the object the AI holds?
// True when the carried object is the partner's own, or is tagged 'PRAP'/'PRAF'.
int carry_partner_wants_object(int ctx)
{
    int carried = *(int*)(ctx + 0x7c);
    if (DAT_805015bd != '\n')
        return 0;

    int partner = tracker_coop_scan_partner_nodes();
    if (partner == carried)
        return 1;

    int owner = *(int*)(carried + 0xbf0);
    if (owner != 0) {
        int tag = *(int*)(owner + 0x28);
        if (tag == 0x50415250 /*'PRAP'*/ || tag == 0x50415246 /*'PRAF'*/)
            return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802a58c8 - pick the AI's current target actor (stored at rec[1]) depending
// on co-op mode, whether it is carrying, and the level/creature variant.
void carry_acquire_target(int ctx)
{
    int* rec = *(int**)(ctx + 0x7c);
    int carrying = carry_partner_wants_object(ctx);

    if (carrying == 0) {
        if (DAT_805015bd == '\n')
            rec[1] = aiactor_acquire_nearest_target(*rec, *(unsigned int*)(DAT_80611d78 + 0x14), 0);
        else
            FUN_802b61cc();
        return;
    }

    if (DAT_80612614 == 5) {
        if (subobj_carried_slot_value(rec) == 0) {
            rec[1] = ghz_reactor_tank_link_tick();
        } else {
            rec[1] = ghz_reactor_nearest_target_scan();
            if (rec[1] == 0)
                rec[1] = tracker_scan_nearest_relation_actor();
        }
    } else if (DAT_80612614 < 5 && 3 < DAT_80612614) {
        rec[1] = worm_swarm_los_cull_update();
        if (rec[1] == 0)
            rec[1] = tracker_scan_nearest_relation_actor();
    } else {
        rec[1] = tracker_scan_nearest_relation_actor();
    }
}

// helper: validate a carried-object handle against the pool, clearing it if the
// generation no longer matches. Returns the record pointer (0 if stale/empty).
static int carry_resolve_record(int* rec, int handleOffsetWords)
{
    unsigned int handle = (unsigned int)rec[handleOffsetWords];
    if (handle == 0)
        return 0;
    int subRec = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
    if (*(unsigned int*)(subRec + 0x2c) != handle) {
        rec[handleOffsetWords] = 0;
        return 0;
    }
    return subRec;
}

// helper: request a carry-state transition (records prev state + timestamp).
static void carry_request_state(int* rec, int newState)
{
    if (rec[8] != newState) {
        rec[0x275] = rec[8];
        rec[8] = newState;
        rec[0x276] = (int)FLOAT_806198d0;
        rec[0x274] = rec[8];
    }
}

// ---------------------------------------------------------------------------
// 0x802a4744 - the carry-state machine. `rec[8]` is the carry state; each case
// ticks the underlying sub-object grab logic and requests the next state.
// The float params carry the incoming movement vector for aiactor_movement_tick.
void carry_state_tick(unsigned long long a1, double a2, double a3, double a4,
                      double a5, double a6, double a7, unsigned long long a8)
{
    int ctx = __save_gpr_28();
    int* rec = *(int**)(ctx + 0x7c);

    aiactor_retarget_visible_teammate();

    // commit any pending state swap posted last tick
    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_806198d0;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }

    // integrate the carry timer while state flag 0x80 is set
    if ((rec[0xb] & 0x80U) == 0) {
        rec[0x2a4] = (int)FLOAT_806198d0;
    } else {
        double denom = (double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000);
        double dt = (double)(float)((double)FLOAT_806198d4 / (double)(float)((double)denom - DOUBLE_806198e8));
        rec[0x2a4] = (int)(float)((double)FLOAT_806108ec * dt + (double)(float)rec[0x2a4]);
    }

    aiactor_leader_zone_update(ctx);

    int subRec;
    int chan;
    int trans;
    int slotVal;

    switch (rec[8]) {
    case 0: // idle -> begin lift
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        chan  = subobj_carry_channel_ptr(subRec);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 2) {
            slotVal = subobj_carried_slot_value(rec);
            if (slotVal < 3) {
                if (slotVal == 0) {
                    *(float*)(chan + 0x394) = FLOAT_806198d8;
                    if (carry_partner_wants_object(ctx) == 0 || 1 < subobj_count_live_grabs(subRec))
                        carry_request_state(rec, 1);
                    else
                        carry_request_state(rec, 8);
                } else if (-1 < slotVal) {
                    carry_request_state(rec, 0xb);
                }
            } else if (slotVal < 5) {
                carry_request_state(rec, 9);
            }
        }
        break;

    case 1: // lifting
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 2) carry_request_state(rec, 4);
        break;

    case 2: // secured, clear "moving" bit
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        chan = subobj_carry_channel_ptr(subRec);
        *(unsigned int*)(chan + 0x390) &= 0xfffffffd;
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 2) carry_request_state(rec, 4);
        break;

    case 3: // plan a haul path
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        chan = subobj_carry_channel_ptr(subRec);
        if (subobj_carry_nav_targets_valid(subRec) == 0) {
            carry_request_state(rec, 1);
        } else {
            subobj_carry_nav_resolve_zones();
            *(unsigned int*)(chan + 0x390) |= 2;
            trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
            if (trans == 2) carry_request_state(rec, 4);
        }
        break;

    case 4: { // hauling: steer + advance path
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        chan = subobj_carry_channel_ptr(subRec);
        carry_acquire_target(ctx);
        float speedScale = FLOAT_806198d4;
        if ((*(unsigned int*)(chan + 0x390) & 1) != 0)
            speedScale = FLOAT_806198dc;
        subobj_carry_speed_response((double)(*(float*)(chan + 0x394) * speedScale), subRec);
        int turn = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        switch (turn) {
        case 0:
            if (FLOAT_806198d0 < (float)rec[0x278] && (*(unsigned int*)(chan + 0x390) & 1) != 0) {
                double denom = (double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000);
                rec[0x278] = (int)-(FLOAT_806108ec *
                    (FLOAT_806198d4 / (float)((double)denom - DOUBLE_806198e8)) -
                    (float)rec[0x278]);
                if ((float)rec[0x278] <= FLOAT_806198d0) {
                    aiactor_nav_advance_path(ctx, (int*)(chan + 0x48));
                    if (subobj_carry_zone_distance() == 0) {
                        *(unsigned int*)(chan + 0x390) &= 0xfffffffe;
                    } else {
                        *(unsigned int*)(chan + 0x390) |= 1;
                        rec[0x278] = (int)FLOAT_806198e0;
                    }
                }
            }
            break;
        case 1:
            aiactor_nav_advance_path(ctx, (int*)(chan + 0x48));
            if (subobj_carry_zone_distance() == 0) {
                *(unsigned int*)(chan + 0x390) &= 0xfffffffe;
            } else {
                *(unsigned int*)(chan + 0x390) |= 1;
                rec[0x278] = (int)FLOAT_806198e0;
            }
            break;
        case 2:
            if ((*(unsigned int*)(chan + 0x390) & 2) == 0)
                carry_request_state(rec, 6);
            else
                carry_request_state(rec, 2);
            break;
        case 5:
            carry_request_state(rec, 5);
            rec[0x278] = (int)FLOAT_806198e0;
            break;
        case 6:
            carry_request_state(rec, 3);
            break;
        case 10:
            rec[1] = 0;
            break;
        }
        break;
    }

    case 5: { // stuck / retry
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        chan = subobj_carry_channel_ptr(subRec);
        float speedScale = FLOAT_806198dc;
        if ((*(unsigned int*)(chan + 0x390) & 1) != 0)
            speedScale = FLOAT_806198e4;
        subobj_carry_speed_response((double)(*(float*)(chan + 0x394) * speedScale), subRec);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 5 || (trans <= 4 && trans == 2)) {
            carry_request_state(rec, 4);
        } else if (trans == 10) {
            rec[1] = 0;
        }
        break;
    }

    case 6: // arrived: choose drop vs deliver
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        chan = subobj_carry_channel_ptr(subRec);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 2 || trans == 5) {
            *(unsigned int*)(chan + 0x390) &= 0xfffffffc;
            if ((subobj_carry_flags(subRec) & 0x10) == 0)
                carry_request_state(rec, 0xd);
            else
                carry_request_state(rec, 7);
        }
        break;

    case 7: // deliver anim
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        carry_acquire_target(ctx);
        subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        break;

    case 8: // wait for a free grab slot
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        if (1 < subobj_count_live_grabs(subRec))
            carry_request_state(rec, 1);
        break;

    case 9: { // carried-by-other observation
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        carry_acquire_target(ctx);
        subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (rec[1] == 0) {
            int grab[1];
            if (carry_partner_wants_object(ctx) != 0 &&
                (subobj_carry_flags(subRec) & 0x10) == 0 &&
                (subobj_find_active_grab(subRec, grab) == 0 || *(int*)(grab[0] + 0x150) == 0))
                carry_request_state(rec, 0xd);
        } else {
            carry_request_state(rec, 10);
        }
        break;
    }

    case 10: { // release / handoff
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        carry_acquire_target(ctx);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        bool advance = false;
        if (trans == 6) {
            advance = true;
        } else if (trans < 6) {
            if (trans == 2) {
                advance = true;
            } else if (1 < trans && 4 < trans &&
                       (subobj_carry_flags(subRec) & 0x10) == 0) {
                carry_request_state(rec, 0xd);
            }
        } else if (trans == 10) {
            advance = true;
        }
        if (advance) {
            int local;
            if ((int*)tracker_coop_scan_partner_nodes() == rec) {
                if ((subobj_carry_flags(subRec) & 0x10) == 0 &&
                    (subobj_find_active_grab(subRec, &local) == 0 || *(int*)(local + 0x150) == 0))
                    carry_request_state(rec, 0xd);
                else
                    carry_request_state(rec, 9);
            } else {
                carry_request_state(rec, 9);
            }
        }
        break;
    }

    case 0xb: // pre-drop
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        carry_acquire_target(ctx);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 4 || (trans <= 3 && trans == 2)) {
            if ((subobj_carry_flags(subRec) & 0x10) == 0)
                carry_request_state(rec, 0xd);
        } else if (trans == 10) {
            rec[1] = 0;
        }
        break;

    case 0xd: // dropping
        subRec = carry_resolve_record(rec, 0x302);
        if (subRec == 0) { carry_request_state(rec, 0xe); break; }
        subobj_carry_channel_ptr(subRec);
        trans = subobj_carry_ai_state_tick(subRec, ctx, rec[8]);
        if (trans == 2) carry_request_state(rec, 0xe);
        break;

    case 0xe: { // released: pick a new audio zone and resume movement
        int nearest = soundpad_find_nearest_entity((double)FLOAT_806198d0, (float*)(ctx + 100));
        rec[0x19e] = nearest;
        int* node = (int*)soundpad_resolve_zone_node(rec[0x19e]);
        *(int*)(ctx + 0x34) = (int)*(short*)((int)node + 10);

        bool skipPost = false;
        if ((int*)tracker_coop_scan_partner_nodes() == rec) {
            if (rec[0x2ab] == 1 && rec[0x2aa] == 0) {
                unsigned int m = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
                if ((m & 1) == 0) skipPost = true;
            }
            if (!skipPost) { rec[0x2ab] = 0x2000; rec[0x2aa] = 0; }
        } else {
            rec[1] = 0;
            if (rec[0x2ab] == 1 && rec[0x2aa] == 0) {
                unsigned int m = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
                if ((m & 1) == 0) skipPost = true;
            }
            if (!skipPost) { rec[0x2ab] = 4; rec[0x2aa] = 0; }
        }
        aiactor_movement_tick(a1, a2, a3, a4, a5, a6, a7, a8);
        break;
    }
    }

    __restore_gpr_28();
}

// helper: post the "release/re-plan" animation-request flag pair unless it is
// suppressed by the current input mask. Returns true if the flag was posted.
static bool carry_post_request(int rec, int flagValue)
{
    if (*(int*)(rec + 0xaac) == 1 && *(int*)(rec + 0xaa8) == 0) {
        unsigned int m = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                 : *(unsigned int*)(rec + 0x1c);
        if ((m & 1) == 0)
            return false; // suppressed
    }
    *(int*)(rec + 0xaac) = flagValue;
    *(int*)(rec + 0xaa8) = 0;
    return true;
}

// helper: request a movement/pose-swap transition on the +0x20 state field.
static void carry_move_request_state(int rec, int newState)
{
    if (*(int*)(rec + 0x20) != newState) {
        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
        *(float*)(rec + 0x9d8) = FLOAT_806198d0;
        *(int*)(rec + 0x20) = newState;
        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
    }
}

// helper: run the "aim at target, request turn/stance" block shared by the
// carry-move states. Posts stance flags and either a turn or a locomotion clip.
static void carry_move_aim(int ctx, int rec, int teammate)
{
    if (DAT_805015bd == '\n' && carry_partner_wants_object(ctx) != 0) {
        *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
        *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_806198f8, *(int**)(ctx + 4), 1);
        tracker_play_locomotion_clip();
        return;
    }

    if (DAT_805015bd != '\n' && *(int*)(rec + 8) == DAT_806120f8)
        FUN_802b6d9c();

    if (*(int*)(rec + 4) == 0 ||
        !aiactor_aim_alignment_test((double)*(float*)(rec + 0x9bc), ctx,
                                    (float*)(*(int*)(rec + 4) + 100))) {
        *(unsigned int*)(rec + 0x28) |= 0x40000;
    } else if (teammate != 0) {
        *(unsigned int*)(rec + 0x28) |= 2;
        *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
    }
    aiactor_set_stance_flags(ctx, 1);
    aiactor_play_turn_animation(ctx);
}

// ---------------------------------------------------------------------------
// 0x802a59f0 - the movement/aim half of the carry behaviour. Reads its context
// via FUN_803826d4, drives approach/turn/attach based on the pending-swap state
// at +0x20, and posts animation-request flags.
void carry_move_tick(unsigned long long a1, double a2, double a3, double a4,
                     double a5, double a6, double a7, unsigned long long a8)
{
    int ctx  = FUN_803826d4();
    int rec  = *(int*)(ctx + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();

    // commit pending pose swap
    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_806198d0;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }

    // integrate carry timer
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_806198d0;
    } else {
        double denom = (double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000);
        double dt = (double)(float)((double)FLOAT_806198d4 / (double)(float)((double)denom - DOUBLE_806198e8));
        *(float*)(rec + 0xa90) = (float)((double)FLOAT_806108ec * dt + (double)*(float*)(rec + 0xa90));
    }

    int subRec = carry_resolve_record((int*)rec, 0xc08 / 4); // handle at +0xc08
    // (carry_resolve_record indexes by words; 0xc08/4 == 0x302)
    int state = *(int*)(rec + 0x20);

    if (state == 2) {
        // holding: keep the object's drop point floored, aim, and either move or
        // begin the settle (state 3) once close enough.
        if (subRec != 0) {
            subobj_grab_slot_world_point(subRec, (unsigned int)*(unsigned char*)(rec + 0xc0c),
                                         (float*)(rec + 0x30));
            double y = floor_query_with_interact((double)FLOAT_806198d0,
                                                 (double)FLOAT_806198f4,
                                                 rec + 0x30, 0, (int*)0, 1);
            *(float*)(rec + 0x34) = (float)y;
        }
        carry_move_aim(ctx, rec, teammate);
        if ((*(unsigned int*)(rec + 0x28) & 1) == 0) {
            unsigned long long facing = aiactor_facing_update();
            aiactor_movement_tick(facing, a2, a3, a4, a5, a6, a7, a8);
            float dx = *(float*)(rec + 0x34) - *(float*)(ctx + 0x68);
            float dy = *(float*)(rec + 0x30) - *(float*)(ctx + 100);
            float dz = *(float*)(rec + 0x38) - *(float*)(ctx + 0x6c);
            if (dz * dz + dy * dy + dx * dx < FLOAT_806198fc && *(int*)(rec + 0x20) != 3)
                carry_move_request_state(rec, 3);
        } else {
            unsigned int uHi = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x10)
                                                      : *(unsigned int*)(rec + 0x18);
            unsigned int uLo = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                      : *(unsigned int*)(rec + 0x1c);
            *(unsigned int*)(rec + 0xabc) = uLo;
            *(unsigned int*)(rec + 0xab8) = uHi;
            carry_post_request(rec, 0x20);
        }
    } else if (state < 2) {
        if (state != 0) {
            if (-1 < state) {
                // state 1: approach the object, then hand off to carry once the
                // follow path reports arrival.
                carry_move_aim(ctx, rec, teammate);
                if ((*(unsigned int*)(rec + 0x28) & 1) == 0) {
                    unsigned long long facing = aiactor_facing_update();
                    aiactor_movement_tick(facing, a2, a3, a4, a5, a6, a7, a8);
                    if (aiactor_follow_zone_path() != 0 && *(int*)(rec + 0x20) != 2)
                        carry_move_request_state(rec, 2);
                } else {
                    unsigned int uHi = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x10)
                                                              : *(unsigned int*)(rec + 0x18);
                    unsigned int uLo = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                              : *(unsigned int*)(rec + 0x1c);
                    *(unsigned int*)(rec + 0xabc) = uLo;
                    *(unsigned int*)(rec + 0xab8) = uHi;
                    carry_post_request(rec, 0x20);
                }
            }
        } else {
            // state 0: scan for the nearest grab slot on a candidate object and
            // attach if one is free; otherwise commit to approach.
            int scanRec = subRec;
            if (scanRec == 0)
                scanRec = (int)subobj_carry_nearest_grab_scan();
            int chosen = 0;
            if (scanRec != 0) {
                // copy the 3-entry slot-preference ring (6 words) into a local
                unsigned int prefRing[6];
                unsigned int* src = (unsigned int*)&DAT_803e07bc;
                for (int k = 0; k < 3; k++) {
                    prefRing[k * 2]     = src[k * 2];
                    prefRing[k * 2 + 1] = src[k * 2 + 1];
                }

                unsigned int slot = 0xffffffff;
                unsigned int* slotPtr = (unsigned int*)0;
                int ring = 0;
                for (int idx = 0; idx < 4; idx++) {
                    unsigned int want = prefRing[idx];
                    unsigned int cursor = 0;
                    unsigned int found = 0xffffffff;
                    do {
                        slotPtr = (unsigned int*)subobj_grab_slot_ring_advance(scanRec, &cursor);
                        if (slotPtr[0xc] == want) {
                            unsigned int h = slotPtr[0];
                            int hr = 0;
                            if (h != 0) {
                                hr = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                                if (*(unsigned int*)(hr + 0x2c) != h) { hr = 0; slotPtr[0] = 0; }
                            }
                            if (hr == 0) { found = cursor; break; }
                        }
                    } while (cursor != 0);
                    slot = found;
                    if (slot != 0xffffffff || (int)want < 0)
                        break;
                }

                if (slot != 0xffffffff && slotPtr != (unsigned int*)0) {
                    int zone = subobj_grab_scan_release_zone();
                    if (zone < 0) {
                        *(unsigned char*)(rec + 0xc0c) = 0xff;
                        chosen = 0;
                    } else {
                        *(char*)(rec + 0xc0c) = (char)slot;
                        chosen = scanRec;
                        if ((*(unsigned int*)(ctx + 0x10) & 0xffff) == 0 &&
                            (*(unsigned int*)(scanRec + 0x10) & 0xffff) == 0) {
                            subobj_grab_commit_attach(a1, a2, scanRec, ctx, slot);
                            carry_post_request(rec, 0x8000);
                        } else if (*(int*)(rec + 0x678) == zone) {
                            carry_move_request_state(rec, 2);
                        } else {
                            carry_move_request_state(rec, 1);
                            if (zone < 0)
                                *(int*)(rec + 0x684) =
                                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                            else
                                *(int*)(rec + 0x684) = zone;
                            *(int*)(rec + 0x9ac) = 0;
                            *(int*)(rec + 0x9a8) = 0;
                        }
                    }
                }
            }
            *(int*)(rec + 0xc08) = (chosen == 0) ? 0 : *(int*)(chosen + 0x2c);
        }
    } else if (state <= 3 && subRec != 0) {
        // state 3: settle - try the auto-attach, else post the appropriate
        // co-op / solo re-plan flag.
        if (subobj_grab_try_auto_attach(0.0, a2, subRec, ctx) != 0) {
            carry_post_request(rec, 0x8000);
        } else if (tracker_coop_scan_partner_nodes() == rec) {
            carry_post_request(rec, 0x2000);
            aiactor_movement_tick(a1, a2, a3, a4, a5, a6, a7, a8);
        } else {
            *(int*)(rec + 4) = 0;
            carry_post_request(rec, 4);
            aiactor_movement_tick(a1, a2, a3, a4, a5, a6, a7, a8);
        }
    }

    FUN_80382720();
}

// helper: retarget the AI's active audio zone to a resolved trigger and post
// the audio-request flag. Shared by both branches of carry_soundpad_scan.
static void carry_soundpad_apply_zone(int rec, int trigger)
{
    *(int*)(rec + 0x9c0) = trigger;
    int zoneIdx = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
    int zone = DAT_80611b8c + zoneIdx * 0x20;
    *(unsigned int*)(zone + 4) |= 0x40000000;

    zoneIdx = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
    if (zoneIdx < 0)
        *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
    else
        *(int*)(rec + 0x684) = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));

    *(int*)(rec + 0x9ac) = 0;
    *(int*)(rec + 0x9a8) = 0;
    carry_post_request(rec, 0x10000);
}

// ---------------------------------------------------------------------------
// 0x802a6690 - when the carried object crosses a sound-trigger volume, retarget
// the AI's active audio zone (rec+0x9c0) and post the audio-request flag.
void carry_soundpad_scan(void)
{
    int ctx = __save_gpr_25();
    int rec = *(int*)(ctx + 0x7c);

    if ((*(unsigned int*)(*(int*)(rec + 0xbf0) + 0x54) & 0x10000) == 0) {
        __restore_gpr_25();
        return;
    }

    int triggerEntity = soundpad_get_entity_id_from_zone(*(int*)(rec + 0x678));
    int trigger = soundpad_find_trigger_by_id(triggerEntity);
    // soundpad_trigger_valid (0x802a6a98) is the LOS/angle validity predicate,
    // defined in aiactor_soundpad_nav.cpp. Model it as a call so both branches stay.
    if (soundpad_trigger_valid() != 0) {
        carry_soundpad_apply_zone(rec, trigger);
        __restore_gpr_25();
        return;
    }

    int list[30];
    int count = soundpad_query_adjacent_zones_simple(
        (unsigned int)*(int*)(rec + 0x678), list, 0x14, 0x1c0020, 0);
    int off = 0;
    for (int i = 0; i < count; i++) {
        int adjTrigger = soundpad_find_trigger_by_id(
            *(int*)(DAT_80611b8c + *(int*)((int)list + off) * 0x20));
        if (soundpad_trigger_valid() != 0) {
            carry_soundpad_apply_zone(rec, adjTrigger);
            break;
        }
        off += 4;
    }

    __restore_gpr_25();
}
