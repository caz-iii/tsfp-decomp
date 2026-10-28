// Address range: 0x8013fa80 - 0x8013ff27 | batch_374 | 2026-09-05
//
// AI actor alert / zone-group table management + zone proximity tests.
// Continuation of batches 364-373. Two data structures dominate this cluster:
//
//   1. Squad table B count accessor (see aiactor_lists.cpp for the table).
//   2. Mover passability flags: DAT_8053f2e8 is a table of mover indices
//      (count DAT_80611be8); each indexes a 0x14-stride record at DAT_80611b90
//      whose +0x00 flag word carries bit 0x20000 (blocked / non-passable).
//   3. The alert zone-group table at DAT_80587e10: 20 groups, 0x60 bytes each.
//      Group layout: +0x00 member count, +0x04 flagA, +0x08 flagB,
//      +0x0c timer(float), +0x10.. member id array (int, up to ~0x14 entries).
//
// Per-actor render object = *(actor + 0x7c); it holds a zone id at +0x678 and a
// pointer to an alert/state object at +0xbf0 whose +0x70 word is a 20-bit group
// mask. DAT_805015bd = game mode byte (0x0a coop). DAT_80611b8c = zone-node
// table base (0x20 stride). DAT_8048ca94/9c = per-weapon aim-height / range LUT
// (0x30 stride). countLeadingZeros idiom (clz>>5) produces a 0/1 boolean.
//
// Functions:
//   aiactor_squad_b_count            0x8013fa80  return squad table B entry count
//   aiactor_mover_set_passability    0x8013fa88  set/clear blocked bit on all movers
//   aiactor_resolve_zone_or_node     0x8013fb18  resolve actor zone node (coop-aware)
//   aiactor_zone_in_range            0x8013fba4  is actor within a zone/node radius?
//   aiactor_target_zone_reachable    0x8013fc64  weapon-range gate to actor's zone
//   aiactor_alert_group_remove       0x8013fcf8  remove actor from masked alert groups
//   aiactor_alert_group_add          0x8013fdac  add actor to its masked alert groups
//   aiactor_alert_group_flagA_any    0x8013fe18  any masked group has flagA set?
//   aiactor_alert_group_flagB_any    0x8013fe6c  any masked group has flagB set?
//   aiactor_alert_group_arm          0x8013fec0  arm masked groups (flags + timer)

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern int* soundpad_resolve_entity_to_zone(int entityId);   // soundpad_zones
extern int* soundpad_resolve_zone_node(int zoneId);          // soundpad_zones
extern void FUN_8025cbf8(void);                              // undecompiled alert-refresh
}

// ---- raw data symbols ------------------------------------------------------
extern int   DAT_806120b0;    // squad table B count (see aiactor_lists.cpp)

// Mover passability table.
extern int   DAT_8053f2e8;    // mover index table (4 stride)
extern int   DAT_80611be8;    // mover index count
extern int   DAT_80611b90;    // mover record base (0x14 stride; +0 flag word)

// Zone / node tables.
extern char  DAT_805015bd;    // game mode byte (0x0a coop)
extern int   DAT_80611b8c;    // zone-node table base (0x20 stride)
extern float DAT_8048ca94;    // per-weapon aim-height LUT (0x30 stride)
extern float DAT_8048ca9c;    // per-weapon range^2 LUT (0x30 stride)
extern float FLOAT_80615dd0;  // zone in-range radius^2 threshold
extern float FLOAT_80615dd4;  // alert-group arm timer value

// Alert zone-group table: 20 groups x 0x60 bytes.
extern int   DAT_80587e10;    // group[0].count  (+0x60 per group)
extern int   DAT_80587e14;    // group[0].flagA
extern int   DAT_80587e18;    // group[0].flagB
extern int   DAT_80587e1c;    // group[0].timer (float)

// ---------------------------------------------------------------------------
// aiactor_squad_b_count (0x8013fa80)
// Accessor: number of entries currently in squad table B.
u32 aiactor_squad_b_count(void)
{
    return DAT_806120b0;
}

// ---------------------------------------------------------------------------
// aiactor_mover_set_passability (0x8013fa88)
// Walk every mover in the index table and either clear (enable != 0) or set
// (enable == 0) the 0x20000 "blocked / non-passable" flag on its record.
void aiactor_mover_set_passability(int enable)
{
    int* idxPtr;
    int byteOff;
    int recOff;
    int i;

    if (enable != 0) {
        byteOff = 0;
        for (i = 0; i < DAT_80611be8; i = i + 1) {
            idxPtr = (int*)((int)&DAT_8053f2e8 + byteOff);
            byteOff = byteOff + 4;
            recOff = *idxPtr * 0x14;
            *(uint*)(DAT_80611b90 + recOff) = *(uint*)(DAT_80611b90 + recOff) & 0xfffdffff;
        }
        return;
    }
    byteOff = 0;
    for (i = 0; i < DAT_80611be8; i = i + 1) {
        idxPtr = (int*)((int)&DAT_8053f2e8 + byteOff);
        byteOff = byteOff + 4;
        recOff = *idxPtr * 0x14;
        *(uint*)(DAT_80611b90 + recOff) = *(uint*)(DAT_80611b90 + recOff) | 0x20000;
    }
}

// ---------------------------------------------------------------------------
// aiactor_resolve_zone_or_node (0x8013fb18)
// Default the actor's zone node from its render-object zone id (+0x678, into the
// 0x20-stride zone table). In coop (mode 0x0a) with an alert object present
// (+0xbf0), try to resolve the alert target's entity zone; accept it only if it
// is valid, has the 0x800 "open" flag, and its short id (+2) matches the base
// zone. Otherwise fall back to the default node.
int* aiactor_resolve_zone_or_node(int actor)
{
    int* alertNode;
    int* baseNode;

    baseNode = (int*)(DAT_80611b8c + *(int*)(actor + 0x678) * 0x20);
    alertNode = baseNode;
    if ((DAT_805015bd == '\n') && (*(int*)(actor + 0xbf0) != 0)) {
        alertNode = (int*)soundpad_resolve_entity_to_zone(*(int*)(*(int*)(actor + 0xbf0) + 0x10));
        if ((alertNode == (int*)0) ||
            ((alertNode[1] & 0x800U) == 0) ||
            (*baseNode != (int)*(short*)(alertNode + 2))) {
            alertNode = baseNode;
        }
    }
    return alertNode;
}

// ---------------------------------------------------------------------------
// aiactor_zone_in_range (0x8013fba4)
// If zoneId is a real sound-zone id (0x1f40..0x209d), resolve the node and test
// whether the actor's position (entity +0x64/+0x68/+0x6c) is within the radius^2
// threshold of the node center (+0xc/+0x10/+0x14). Otherwise treat zoneId as a
// direct match against the actor's cached zone (+0x19e word) via a clz compare.
// param_1[0] is the actor/entity pointer; returns 0/1.
u32 aiactor_zone_in_range(int* actorRef, int zoneId)
{
    float dx;
    float dy;
    float dz;
    bool isSoundZone;
    int* node;
    u32 result;
    int entity;

    isSoundZone = false;
    entity = *actorRef;
    if ((7999 < zoneId) && (zoneId < 0x209e)) {
        isSoundZone = true;
    }
    if (isSoundZone) {
        node = soundpad_resolve_zone_node(zoneId);
        if (node == (int*)0) {
            result = 0;
        } else {
            dx = *(float*)(entity + 100) - (float)node[3];
            dy = *(float*)(entity + 0x68) - (float)node[4];
            dz = *(float*)(entity + 0x6c) - (float)node[5];
            result = (u32)(dz * dz + dx * dx + dy * dy <= FLOAT_80615dd0);
        }
    } else {
        result = countLeadingZeros((u32)(zoneId - actorRef[0x19e]));
        result = result >> 5;
    }
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_target_zone_reachable (0x8013fc64)
// Weapon-range gate: using the actor's zone node (+0x678) and its weapon class
// (+0x48 -> 0x30-stride LUT), require the node to be at least the weapon's
// aim-height above the actor and within the weapon's range^2 horizontally.
// Returns 0 if out of range / no zone, else 1.
u32 aiactor_target_zone_reachable(int actor)
{
    float dz;
    float dx;
    u32 result;
    int zoneId;
    int lutOff;
    int node;

    zoneId = *(int*)(*(int*)(actor + 0x7c) + 0x678);
    if (zoneId < 0) {
        return 0;
    }
    lutOff = *(short*)(*(int*)(actor + 0x7c) + 0x48) * 0x30;
    node = DAT_80611b8c + zoneId * 0x20;
    if (*(float*)(node + 0x10) - *(float*)(actor + 0x68) < *(float*)(&DAT_8048ca94 + lutOff)) {
        return 0;
    }
    dz = *(float*)(node + 0x14) - *(float*)(actor + 0x6c);
    dx = *(float*)(node + 0xc) - *(float*)(actor + 100);
    result = countLeadingZeros((u32)(*(float*)(&DAT_8048ca9c + lutOff) < dx * dx + dz * dz));
    return result >> 5;
}

// ---------------------------------------------------------------------------
// aiactor_alert_group_remove (0x8013fcf8)
// For each set bit in `mask` (up to 20 groups, 0x60 stride), find the actor in
// that group's member array (+0x10) and swap-remove it (move last member into
// its slot, decrement the group count at +0x00).
void aiactor_alert_group_remove(int actor, u32 mask)
{
    int count;
    int memberOff;
    int found;
    int* group;
    int idx;
    int bit;
    int groupOff;
    int slot;

    bit = 0;
    groupOff = 0;
    do {
        slot = -1;
        found = slot;
        if ((mask & 1 << bit) != 0) {
            idx = 0;
            memberOff = 0;
            count = *(int*)((int)&DAT_80587e10 + groupOff);
            if (0 < count) {
                do {
                    found = idx;
                    if (*(int*)((int)&DAT_80587e10 + groupOff + memberOff + 0x10) == actor) break;
                    idx = idx + 1;
                    memberOff = memberOff + 4;
                    count = count + -1;
                    found = slot;
                } while (count != 0);
            }
        }
        if (-1 < found) {
            group = (int*)((int)&DAT_80587e10 + groupOff);
            group[found + 4] = group[*group + 3];
            *group = *group + -1;
        }
        bit = bit + 1;
        groupOff = groupOff + 0x60;
        if (0x13 < bit) {
            return;
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// aiactor_alert_group_add (0x8013fdac)
// For each set bit in the actor's alert group mask (render-obj +0xbf0, +0x70),
// append the actor to that group's member array (cap 0x14 members).
void aiactor_alert_group_add(int actor)
{
    int renderObj;
    int* group;
    int bit;
    int groupOff;
    int remaining;

    renderObj = *(int*)(actor + 0x7c);
    bit = 0;
    groupOff = 0;
    remaining = 0x14;
    do {
        if ((*(uint*)(*(int*)(renderObj + 0xbf0) + 0x70) & 1 << bit) != 0) {
            group = (int*)((int)&DAT_80587e10 + groupOff);
            if (0x13 < *group) {
                return;
            }
            group[*group + 4] = actor;
            *group = *group + 1;
        }
        bit = bit + 1;
        groupOff = groupOff + 0x60;
        remaining = remaining + -1;
    } while (remaining != 0);
}

// ---------------------------------------------------------------------------
// aiactor_alert_group_flagA_any (0x8013fe18)
// Return 1 if any group selected by `mask` has flagA (+0x04) set.
u32 aiactor_alert_group_flagA_any(u32 mask)
{
    int bit;
    int groupOff;
    int remaining;

    bit = 0;
    groupOff = 0;
    remaining = 0x14;
    do {
        if ((mask & 1 << bit) != 0) {
            if (*(int*)((int)&DAT_80587e14 + groupOff) != 0) {
                return 1;
            }
        }
        bit = bit + 1;
        groupOff = groupOff + 0x60;
        remaining = remaining + -1;
    } while (remaining != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_alert_group_flagB_any (0x8013fe6c)
// Return 1 if any group selected by `mask` has flagB (+0x08) set.
u32 aiactor_alert_group_flagB_any(u32 mask)
{
    int bit;
    int groupOff;
    int remaining;

    bit = 0;
    groupOff = 0;
    remaining = 0x14;
    do {
        if ((mask & 1 << bit) != 0) {
            if (*(int*)((int)&DAT_80587e18 + groupOff) != 0) {
                return 1;
            }
        }
        bit = bit + 1;
        groupOff = groupOff + 0x60;
        remaining = remaining + -1;
    } while (remaining != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_alert_group_arm (0x8013fec0)
// For each group selected by `mask`, set flagA and flagB and reset the group
// timer (+0x0c) to the arm value, then trigger the global alert refresh.
void aiactor_alert_group_arm(u32 mask)
{
    float armTimer;
    int bit;
    int groupOff;
    int remaining;

    armTimer = FLOAT_80615dd4;
    bit = 0;
    groupOff = 0;
    remaining = 0x14;
    do {
        if ((mask & 1 << bit) != 0) {
            *(int*)((int)&DAT_80587e14 + groupOff) = 1;
            *(int*)((int)&DAT_80587e18 + groupOff) = 1;
            *(float*)((int)&DAT_80587e1c + groupOff) = armTimer;
        }
        bit = bit + 1;
        groupOff = groupOff + 0x60;
        remaining = remaining + -1;
    } while (remaining != 0);
    FUN_8025cbf8();
}
