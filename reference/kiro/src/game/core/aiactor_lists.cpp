// Address range: 0x8013f774 - 0x8013fa3b | batch_373 | 2026-09-05
//
// AI actor squad / active-list table management. Continuation of batches
// 364-372. A family of small parallel-array managers for several capped
// registration tables in the DAT_805887xx / DAT_806120xx region, plus one
// sound-pad entity registration, a squad-membership query, and a mover
// passability-flag toggler. Per-actor render object = *(actor + 0x7c); that
// object holds a per-actor slot index at +0x1c. DAT_805015bd = game mode byte
// (0x0a == coop). Tables are stored as parallel arrays: an 8-byte-stride
// "value" array interleaved with a paired "entity/arg" array.
//
// Functions:
//   aiactor_active_swapremove_owner  0x8013f774  swap-remove actor from active tbl (cap 5)
//   aiactor_slot_table_store         0x8013f7ec  store value+actor at actor's slot index
//   aiactor_squad_a_push             0x8013f818  append (val,arg) to squad table A (cap 0x14)
//   aiactor_squad_c_push             0x8013f848  append (val,arg) to squad table C (cap 0x19)
//   aiactor_squad_b_push             0x8013f878  append (val,arg) to squad table B (cap 0x14)
//   aiactor_soundpad_register        0x8013f8a8  register a sound-zone actor (cap 10)
//   aiactor_pending_list_push        0x8013f920  append actor to pending list (cap 0x1e)
//   aiactor_pending_list_remove      0x8013f948  swap-remove actor from pending list
//   aiactor_squad_contains_member    0x8013f9bc  is entity id in any active squad roster?
//   aiactor_squad_b_find             0x8013fa3c  find squad table B entry by value

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
// soundpad_find_nearest_entity: (minDistSq, position[3]) -> nearest entity ptr.
extern int  soundpad_find_nearest_entity(double minDistSq, float* position);
}

// ---- raw data symbols ------------------------------------------------------
// Active table (owner-indexed, cap 5): 4-byte actor pointers.
extern int   DAT_805887d0;    // active-table actor pointer array (4 stride)
extern int   DAT_806120ec;    // active-table live count

// Per-actor slot table: paired value/actor arrays keyed by slot index (+0x1c).
extern int   DAT_805889ec;    // slot value array   (8 stride)
extern int   DAT_805889f0;    // slot actor array   (8 stride, +4 phase)

// Squad table A (cap 0x14): paired arg/value arrays.
extern int   DAT_806120b4;    // squad-A count
extern int   DAT_805887e4;    // squad-A arg array   (2 words / entry)
extern int   DAT_805887e8;    // squad-A value array (8 stride)

// Squad table C (cap 0x19): paired arg/value arrays.
extern int   DAT_806120ac;    // squad-C count
extern int   DAT_80588924;    // squad-C arg array   (2 words / entry)
extern int   DAT_80588928;    // squad-C value array (8 stride)

// Squad table B (cap 0x14): paired arg/value arrays.
extern int   DAT_806120b0;    // squad-B count
extern int   DAT_80588884;    // squad-B value array (8 stride)
extern int   DAT_80588888;    // squad-B arg array   (2 words / entry)

// Sound-zone actor registration (cap 10).
extern int   DAT_80612090;    // sound-zone actor count
extern int   DAT_805886e0;    // sound-zone actor pointer array (4 stride)
extern float FLOAT_80615da8;  // 0.0 (min-distance placeholder)

// Pending list (cap 0x1e): single actor-pointer array.
extern int   DAT_80588708;    // pending-list actor pointer array (4 stride)
extern int   DAT_80588704;    // pending-list array base - 4 (tail alias)
extern int   DAT_806120e0;    // pending-list count

// Player/actor state pool (squad roster source).
extern int   DAT_80611d58;    // player state pool count
extern int   DAT_80611d5c;    // player state pool base (0xffc stride)

// Mover passability table (subsystem:graphics-adjacent zone flags).
extern int   DAT_8053f2e8;    // mover index table (4 stride)
extern int   DAT_80611be8;    // mover index count
extern int   DAT_80611b90;    // mover record base (0x14 stride)

// ---------------------------------------------------------------------------
// aiactor_active_swapremove_owner (0x8013f774)
// If the actor's render object has the 0x40 "in active table" flag set, walk
// the owner-indexed active table (cap 5) and swap-remove every entry that
// points at this actor, compacting the tail down over the removed slots.
void aiactor_active_swapremove_owner(int actor)
{
    int idx;
    int byteOff;
    int removed;

    removed = 0;
    byteOff = 0;
    idx = 0;
    if ((*(uint*)(*(int*)(actor + 0x7c) + 8) & 0x40) == 0) {
        return;
    }
    for (; idx < DAT_806120ec; idx = idx + 1) {
        if (*(int*)((int)&DAT_805887d0 + byteOff) == actor) {
            removed = removed + 1;
            DAT_806120ec = DAT_806120ec + -1;
        }
        if ((removed != 0) && (idx + removed < 5)) {
            *(int*)((int)&DAT_805887d0 + byteOff) = (&DAT_805887d0)[idx + removed];
        }
        byteOff = byteOff + 4;
    }
}

// ---------------------------------------------------------------------------
// aiactor_slot_table_store (0x8013f7ec)
// Store (value, actor) into the per-actor slot table at the actor's own slot
// index (render object + 0x1c), 8-byte stride.
void aiactor_slot_table_store(int actor, int value)
{
    int renderObj;

    renderObj = *(int*)(actor + 0x7c);
    *(int*)(&DAT_805889ec + *(int*)(renderObj + 0x1c) * 8) = value;
    (&DAT_805889f0)[*(int*)(renderObj + 0x1c) * 2] = actor;
}

// ---------------------------------------------------------------------------
// aiactor_squad_a_push (0x8013f818)
// Append (arg, value) to squad table A. Capacity 0x14 (20) entries; the count
// index doubles as the arg index (2 words/entry) and *8 as the value index.
void aiactor_squad_a_push(int value, int arg)
{
    int valOff;
    int argIdx;

    if (0x13 < DAT_806120b4) {
        return;
    }
    valOff = DAT_806120b4 * 8;
    argIdx = DAT_806120b4 * 2;
    DAT_806120b4 = DAT_806120b4 + 1;
    (&DAT_805887e4)[argIdx] = arg;
    *(int*)(&DAT_805887e8 + valOff) = value;
}

// ---------------------------------------------------------------------------
// aiactor_squad_c_push (0x8013f848)
// Append (arg, value) to squad table C. Capacity 0x19 (25) entries.
void aiactor_squad_c_push(int value, int arg)
{
    int count;
    int argIdx;

    count = DAT_806120ac;
    if (0x18 < DAT_806120ac) {
        return;
    }
    argIdx = DAT_806120ac * 2;
    DAT_806120ac = DAT_806120ac + 1;
    (&DAT_80588924)[argIdx] = arg;
    (&DAT_80588928)[count * 2] = value;
}

// ---------------------------------------------------------------------------
// aiactor_squad_b_push (0x8013f878)
// Append (arg, value) to squad table B. Capacity 0x14 (20) entries.
void aiactor_squad_b_push(int value, int arg)
{
    int count;
    int argIdx;

    count = DAT_806120b0;
    if (0x13 < DAT_806120b0) {
        return;
    }
    argIdx = DAT_806120b0 * 2;
    DAT_806120b0 = DAT_806120b0 + 1;
    (&DAT_80588884)[argIdx] = arg;
    (&DAT_80588888)[count * 2] = value;
}

// ---------------------------------------------------------------------------
// aiactor_soundpad_register (0x8013f8a8)
// If the entity is a sound zone (type field +0x20 == 0x2000) and the sound-zone
// actor table has room (cap 10), resolve its nearest sound entity, cache the
// result on the render object (+4), and append the entity to the table.
void aiactor_soundpad_register(int unused, int entity)
{
    int nearest;
    int renderObj;

    if ((*(int*)(entity + 0x20) == 0x2000) && (DAT_80612090 < 10)) {
        renderObj = *(int*)(entity + 0x7c);
        nearest = soundpad_find_nearest_entity((double)FLOAT_80615da8, (float*)(entity + 100));
        *(int*)(renderObj + 4) = nearest;
        *(int*)(&DAT_805886e0 + DAT_80612090 * 4) = entity;
        DAT_80612090 = DAT_80612090 + 1;
    }
    (void)unused;
}

// ---------------------------------------------------------------------------
// aiactor_pending_list_push (0x8013f920)
// Append actor to the pending list (cap 0x1e). The store happens before the
// cap check (matches original), but the count only advances when under cap.
void aiactor_pending_list_push(int actor)
{
    int count;
    int atCap;

    count = DAT_806120e0;
    atCap = (0x1d < DAT_806120e0);
    (&DAT_80588708)[DAT_806120e0] = actor;
    if (atCap) {
        return;
    }
    DAT_806120e0 = count + 1;
}

// ---------------------------------------------------------------------------
// aiactor_pending_list_remove (0x8013f948)
// Find actor in the pending list; if present, swap-remove it by moving the
// current tail element into its slot and decrementing the count.
void aiactor_pending_list_remove(int actor)
{
    int foundIdx;
    int idx;
    int byteOff;
    int remaining;
    int* tail;

    foundIdx = -1;
    idx = 0;
    byteOff = 0;
    remaining = DAT_806120e0;
    if (0 < DAT_806120e0) {
        do {
            if (*(int*)((int)&DAT_80588708 + byteOff) == actor) {
                foundIdx = idx;
            }
            idx = idx + 1;
            byteOff = byteOff + 4;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    if (foundIdx == -1) {
        return;
    }
    tail = &DAT_80588704 + DAT_806120e0;
    DAT_806120e0 = DAT_806120e0 + -1;
    (&DAT_80588708)[foundIdx] = *tail;
}

// ---------------------------------------------------------------------------
// aiactor_squad_contains_member (0x8013f9bc)
// Scan every active player/actor state (pool base DAT_80611d5c, stride 0xffc)
// that has the 0x20 "squad active" flag. Each entry holds two rosters at
// +0xe0c: a short count at +0xe08 (arg!=0) or +0xe0a (arg==0), followed by
// packed 16-bit member ids. Returns 1 if entityId is in the selected roster.
u32 aiactor_squad_contains_member(int entityId, int arg)
{
    int memberCount;
    short packedCount;
    int poolIdx;
    int recOff;
    int rec;
    int memberOff;

    poolIdx = 0;
    recOff = 0;
    do {
        if (DAT_80611d58 <= poolIdx) {
            return 0;
        }
        rec = DAT_80611d5c + recOff;
        if ((*(uint*)(rec + 0x21c) & 0x20) != 0) {
            if (arg == 0) {
                packedCount = *(short*)(rec + 0xe0a);
            } else {
                packedCount = *(short*)(rec + 0xe08);
            }
            memberCount = (int)packedCount;
            memberOff = 0;
            if (0 < memberCount) {
                do {
                    if (*(short*)(rec + memberOff + 0xe0c) == entityId) {
                        return 1;
                    }
                    memberOff = memberOff + 2;
                    memberCount = memberCount + -1;
                } while (memberCount != 0);
            }
        }
        poolIdx = poolIdx + 1;
        recOff = recOff + 0xffc;
    } while (true);
}

// ---------------------------------------------------------------------------
// aiactor_squad_b_find (0x8013fa3c)
// Linear search squad table B for an entry whose value equals `value`.
// Returns a pointer to the matching value slot, or null.
int* aiactor_squad_b_find(int value)
{
    int byteOff;
    int remaining;

    byteOff = 0;
    remaining = DAT_806120b0;
    if (0 < DAT_806120b0) {
        do {
            if (value == *(int*)((int)&DAT_80588884 + byteOff)) {
                return (int*)((int)&DAT_80588884 + byteOff);
            }
            byteOff = byteOff + 8;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return (int*)0;
}

// ---------------------------------------------------------------------------
// aiactor_active_list_push 0x8013f73c | batch_401
// ---------------------------------------------------------------------------
// Append an actor to the active-actor table (DAT_805887d0, DAT_806120ec count),
// capped at 5 entries. Only actors whose render_data (+0x7c) flag word (+8) has
// bit 0x40 set are tracked. Counterpart to aiactor_active_swapremove_owner.
void aiactor_active_list_push(int actor)
{
    int* slot;

    if (4 < DAT_806120ec) {
        return;
    }
    if ((*(u32*)(*(int*)(actor + 0x7c) + 8) & 0x40) == 0) {
        return;
    }
    slot = &DAT_805887d0 + DAT_806120ec;
    DAT_806120ec = DAT_806120ec + 1;
    *slot = actor;
}
