// Module: entity_group_registry
// TSFP GameCube main.dol non-matching decompilation.
//
// Entity handle / tag / group registry. Central lookup layer that maps 32-bit
// entity handles and gameplay "tag" ids to live entity records in the entity
// pool. Used pervasively across the engine (the core resolver alone has 300+
// call sites).
//
// Handle encoding: a 32-bit handle packs a pool index in its top bits
//   (handle >> 0x16) and a generation/uniqueness token in the low bits. The
//   pool record at (DAT_80611c24 + index * 0x2e8) stores its own handle at
//   +0x2c; a lookup is only valid when that stored handle matches, which
//   detects stale handles to freed/recycled slots.
//
// Three parallel tables:
//   - group table  (DAT_805c7c34, 0xfa slots x 0x10) : id->handle groups built
//     from the level command stream (e.g. for sound-zone portals).
//   - tag table    (DAT_805c8bd4, 0x96 slots x 8)    : tag id -> entity list.
//   - aux table    (DAT_805c9084, 0xfa slots x 8)    : secondary id table.
//
// Batch: batch_708  |  Addresses: 0x8023c090 - 0x8023c59c

#include "common.h"

// ---------------------------------------------------------------------------
// Registry tables and cursors (file-local externs to raw symbols).
// ---------------------------------------------------------------------------

// Group table: 0xfa slots, 0x10-byte stride. Slot +0 == 0 means free.
extern unsigned char DAT_805c7c34[];
extern unsigned char DAT_805c7c38;
extern unsigned char DAT_805c7c3c;
extern int           DAT_8061295c;   // group-table round-robin alloc cursor

// Sorted (ascending by id) singly-linked list used by the handle resolver.
// Node layout: [0]=handle  [1]=id  [2]=next.
extern unsigned int *DAT_80612960;    // list head

// Tag table: 0x96 slots, 8-byte stride. Slot +0 == id (-1 free), +4 == list.
extern unsigned char DAT_805c8bd4[];
extern unsigned char DAT_805c8bd8;
extern int           DAT_80612958;    // tag-table alloc cursor

// Aux table: 0xfa slots, 8-byte stride. Slot +0 == 0 means free.
extern unsigned char DAT_805c9084[];
extern unsigned char DAT_805c9088;
extern int           DAT_80612954;    // aux-table alloc cursor

// Entity pool base and item-def pointer table.
extern int           DAT_80611c24;    // entity pool base (0x2e8 stride)
extern int          *PTR_DAT_80477bd0; // item-def pointer table (0x31 int stride)

// Level command stream (see level_def_loader.cpp) walked by the group builder.
extern unsigned char *DAT_80612be8;

// ---------------------------------------------------------------------------
// Cross-module / register-spill helpers (file-local).
// ---------------------------------------------------------------------------
extern int  worm_igc_resolve_zone(int entityId);
extern int  FUN_80343c4c(int a);        // entity-list per-entry stride A
extern int  FUN_8030f8f4(void);         // entity-list per-entry stride B
extern unsigned long long FUN_803826cc(void); // group-build context (hi=out ptr)
extern void FUN_80382718(void);         // release group-build context

#define REG_GROUP_SLOTS   0xfa
#define REG_GROUP_STRIDE  0x10
#define REG_TAG_SLOTS     0x96
#define REG_TAG_STRIDE    8
#define REG_AUX_SLOTS     0xfa
#define REG_AUX_STRIDE    8
#define ENTITY_POOL_STRIDE 0x2e8
#define ENTITY_HANDLE_INDEX_SHIFT 0x16
#define ENTITY_RECORD_HANDLE_OFF  0x2c

// ===========================================================================
// 0x8023c32c  entity_handle_resolve
//   Resolve an entity id to its live pool record. Walks the sorted id list;
//   validates the stored handle against the pool record so stale handles to
//   recycled slots return 0.
// ===========================================================================
int entity_handle_resolve(unsigned int id)
{
    unsigned int *node = DAT_80612960;
    while (true) {
        if (node == 0) return 0;
        if (node[1] == id) break;
        if ((int)id < (int)node[1]) return 0;  // list is sorted ascending
        node = (unsigned int *)node[2];
    }

    unsigned int handle = node[0];
    if (handle == 0) return 0;

    int record = DAT_80611c24 + (handle >> ENTITY_HANDLE_INDEX_SHIFT) * ENTITY_POOL_STRIDE;
    if (*(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF) == handle) {
        return record;
    }
    node[0] = 0;  // stale: clear it
    return 0;
}

// ===========================================================================
// 0x8023c39c  entity_index_by_id
//   Return the group-table index for an entity id, or -1 if absent. Uses the
//   same sorted list; the node address maps back to a table slot index.
// ===========================================================================
int entity_index_by_id(unsigned int id)
{
    int node = (int)DAT_80612960;
    while (true) {
        if (node == 0) return -1;
        if (*(unsigned int *)(node + 4) == id) break;
        if (id < *(unsigned int *)(node + 4)) return -1;
        node = *(int *)(node + 8);
    }
    // (node - &DAT_805c7c34) / 0x10, rounding toward zero for negatives.
    unsigned int rel = (unsigned int)(node - (int)&DAT_805c7c34[0]);
    return ((int)rel >> 4) + (unsigned int)((int)rel < 0 && (rel & 0xf) != 0);
}

// ===========================================================================
// 0x8023c090  entity_group_build_from_stream
//   Walk the level command stream and, for each entity of type 10..14 in an
//   entity-list block, record its resolved zone and item-def pointer into the
//   caller's output arrays. Used to auto-create sound-zone portals.
// ===========================================================================
void entity_group_build_from_stream(int a, int b, int outDefs)
{
    (void)a; (void)b;
    unsigned long long ctx = FUN_803826cc();
    int *pOutCount = (int *)((unsigned long long)ctx >> 0x20);
    int outZones = (int)ctx;

    int blockCount = 0;
    int *rec;
    for (rec = (int *)(DAT_80612be8 + 8);
         rec < (int *)(DAT_80612be8 + 0x7ffc);
         rec = (int *)((int)(rec + 2) + rec[1])) {
        int tag = *rec;
        if (tag == 0xc7766a) break;

        if (tag == 0xc77668) {
            blockCount = rec[3];
        } else if (tag == 0xc77669) {
            int *ent = rec + 2;
            for (int bl = 0; bl < blockCount; bl++) {
                unsigned short entCount = *(unsigned short *)((int)ent + 0x1e);
                int strideA = FUN_80343c4c(*ent);
                int strideB = FUN_8030f8f4();
                ent = ent + (strideB + strideA) * 2 + 10;

                for (int j = 0; j < (int)(unsigned int)entCount; j++) {
                    int type = (int)*(short *)((int)ent + 0xe);
                    unsigned char *itemDef = (unsigned char *)(&PTR_DAT_80477bd0)[type * 0x31];
                    if (type < 0xf && type > 9) {
                        int zone = worm_igc_resolve_zone(ent[6]);
                        *(int *)(outZones + *pOutCount * 4) = zone;
                        *(unsigned char **)(outDefs + *pOutCount * 4) = itemDef;
                        *pOutCount = *pOutCount + 1;
                    }
                    ent = ent + *(unsigned char *)(ent + 5) + 6;
                }
            }
        }
    }
    FUN_80382718();
}

// ===========================================================================
// 0x8023c1e0  entity_group_reset
//   Clear the group table (0xfa slots x 0x10) and reset its cursors.
// ===========================================================================
void entity_group_reset(void)
{
    int off = 0;
    for (int i = 0xfa; i != 0; i--) {
        *(int *)((int)&DAT_805c7c34[0] + off) = 0;
        *(int *)((int)&DAT_805c7c38 + off) = 0;
        *(int *)((int)&DAT_805c7c3c + off) = 0;
        off += REG_GROUP_STRIDE;
    }
    DAT_80612960 = 0;
    DAT_8061295c = 0;
}

// ===========================================================================
// 0x8023c228  entity_tag_table_reset
//   Clear the tag table (0x96 slots, id=-1) and aux table (0xfa slots) and
//   reset their cursors.
// ===========================================================================
void entity_tag_table_reset(void)
{
    int off = 0;
    for (int i = 0x96; i != 0; i--) {
        *(int *)((int)&DAT_805c8bd4[0] + off) = -1;
        *(int *)((int)&DAT_805c8bd8 + off) = 0;
        off += REG_TAG_STRIDE;
    }
    off = 0;
    for (int i = 0xfa; i != 0; i--) {
        *(int *)((int)&DAT_805c9084[0] + off) = 0;
        *(int *)((int)&DAT_805c9088 + off) = 0;
        off += REG_AUX_STRIDE;
    }
    DAT_80612958 = 0;
    DAT_80612954 = 0;
}

// ===========================================================================
// 0x8023c294  entity_group_alloc_slot
//   Round-robin allocate the next free group slot (slot +0 == 0). Scans from
//   the cursor to the end, then wraps to the start. Returns index or -1.
// ===========================================================================
int entity_group_alloc_slot(void)
{
    int idx = DAT_8061295c;
    int off = DAT_8061295c << 4;
    if (DAT_8061295c < REG_GROUP_SLOTS) {
        for (int remaining = REG_GROUP_SLOTS - DAT_8061295c; remaining != 0; remaining--) {
            if (*(int *)((int)&DAT_805c7c34[0] + off) == 0) {
                DAT_8061295c = DAT_8061295c + 1;
                return idx;
            }
            idx++;
            off += REG_GROUP_STRIDE;
        }
    }
    idx = 0;
    off = 0;
    for (int remaining = DAT_8061295c; remaining > 0; remaining--) {
        if (*(int *)((int)&DAT_805c7c34[0] + off) == 0) {
            DAT_8061295c = DAT_8061295c + 1;
            return idx;
        }
        idx++;
        off += REG_GROUP_STRIDE;
    }
    return -1;
}

// ===========================================================================
// 0x8023c3e8  entity_tag_find_or_alloc
//   Find the tag slot whose id matches, else allocate the next free slot
//   (id < 0 and list empty), round-robin from the cursor. Returns index/-1.
// ===========================================================================
int entity_tag_find_or_alloc(int id)
{
    int idx = 0;
    int off = 0;
    for (int i = REG_TAG_SLOTS; i != 0; i--) {
        if (id == *(int *)((int)&DAT_805c8bd4[0] + off)) {
            return idx;
        }
        idx++;
        off += REG_TAG_STRIDE;
    }

    idx = DAT_80612958;
    off = DAT_80612958 << 3;
    if (DAT_80612958 < REG_TAG_SLOTS) {
        for (int remaining = REG_TAG_SLOTS - DAT_80612958; remaining != 0; remaining--) {
            if (*(int *)((int)&DAT_805c8bd4[0] + off) < 0 &&
                *(int *)((int)&DAT_805c8bd8 + off) == 0) {
                DAT_80612958 = DAT_80612958 + 1;
                return idx;
            }
            idx++;
            off += REG_TAG_STRIDE;
        }
    }
    idx = 0;
    off = 0;
    for (int remaining = DAT_80612958; remaining > 0; remaining--) {
        if (*(int *)((int)&DAT_805c8bd4[0] + off) < 0 &&
            *(int *)((int)&DAT_805c8bd8 + off) == 0) {
            DAT_80612958 = DAT_80612958 + 1;
            return idx;
        }
        idx++;
        off += REG_TAG_STRIDE;
    }
    return -1;
}

// ===========================================================================
// 0x8023c4d8  entity_aux_alloc_slot
//   Allocate the first free aux-table slot (slot +0 == 0). Returns index/-1.
// ===========================================================================
int entity_aux_alloc_slot(void)
{
    int idx = 0;
    int off = 0;
    for (int i = REG_AUX_SLOTS; i != 0; i--) {
        if (*(int *)((int)&DAT_805c9084[0] + off) == 0) {
            DAT_80612954 = DAT_80612954 + 1;
            return idx;
        }
        idx++;
        off += REG_AUX_STRIDE;
    }
    return -1;
}

// ===========================================================================
// 0x8023c520  entity_tag_resolve_first
//   Find the tag by id and resolve the first entity in its list to a live pool
//   record. Returns the record or 0.
// ===========================================================================
int entity_tag_resolve_first(int id)
{
    int off = 0;
    int remaining = REG_TAG_SLOTS;
    while (id != *(int *)((int)&DAT_805c8bd4[0] + off)) {
        off += REG_TAG_STRIDE;
        remaining--;
        if (remaining == 0) return 0;
    }

    unsigned int *list = *(unsigned int **)((int)&DAT_805c8bd8 + off);
    unsigned int handle = *list;
    if (handle == 0) return 0;

    int record = DAT_80611c24 + (handle >> ENTITY_HANDLE_INDEX_SHIFT) * ENTITY_POOL_STRIDE;
    if (*(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF) == handle) {
        return record;
    }
    *list = 0;  // stale
    return 0;
}

// ===========================================================================
// 0x8023c59c  entity_tag_resolve_all
//   Find the tag by id and resolve up to `maxCount` entities in its list into
//   the caller's record-pointer array. Stale entries resolve to 0 (and are
//   cleared). Returns the number of list entries walked.
// ===========================================================================
int entity_tag_resolve_all(int id, int outRecords, int maxCount)
{
    int off = 0;
    for (int i = REG_TAG_SLOTS; i != 0; i--) {
        if (*(int *)((int)&DAT_805c8bd4[0] + off) == id) {
            unsigned int *list = *(unsigned int **)((int)&DAT_805c8bd8 + off);
            int outOff = 0;
            int count = 0;
            for (; list != 0 && count < maxCount; count++) {
                unsigned int handle = *list;
                int record;
                if (handle == 0) {
                    record = 0;
                } else {
                    record = DAT_80611c24 + (handle >> ENTITY_HANDLE_INDEX_SHIFT) * ENTITY_POOL_STRIDE;
                    if (*(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF) != handle) {
                        *list = 0;  // stale
                        record = 0;
                    }
                }
                *(int *)(outRecords + outOff) = record;
                outOff += 4;
                list = (unsigned int *)list[1];
            }
            return count;
        }
        off += REG_TAG_STRIDE;
    }
    return 0;
}

// ===========================================================================
// batch_709 additions: registry membership management + tag queries.
// (Continuation of the entity handle/tag/group registry.)
// ===========================================================================

// Additional raw symbols referenced only by the batch_709 functions.
extern unsigned char DAT_805c7c40;    // group node "prev" pointer (node +0xc)
extern int           DAT_80610efc;    // global sequential registration id counter
extern int           DAT_80612614;    // live IGC actor count (budget max 0x18)
extern float         FLOAT_806188b8;  // nearest-search initial max distance^2
extern float         FLOAT_8061888c;  // activity threshold for nearest search

extern unsigned int rng_counter_tick(void);
extern unsigned long long FUN_803826f0(void); // ctx: hi=id, lo=record ptr
extern void         FUN_8038273c(void);        // release that context

// ===========================================================================
// 0x8023c644  entity_tag_count
//   Count how many entities are currently in the tag group named `tagId`.
// ===========================================================================
int entity_tag_count(int tagId)
{
    int off = 0;
    for (int i = REG_TAG_SLOTS; i != 0; i--) {
        if (*(int *)((int)&DAT_805c8bd4[0] + off) == tagId) {
            int count = 0;
            for (int node = *(int *)((int)&DAT_805c8bd8 + off); node != 0; node = *(int *)(node + 4)) {
                count++;
            }
            return count;
        }
        off += REG_TAG_STRIDE;
    }
    return 0;
}

// ===========================================================================
// 0x8023c69c  entity_tag_find_nearest
//   Return the entity in tag group `tagId` closest to position `pos`
//   (record fields +0x64/+0x68/+0x6c = x/y/z). When `activeOnly` is set, only
//   entities whose activity value (+0x1cc) exceeds the threshold qualify.
// ===========================================================================
int entity_tag_find_nearest(int tagId, float *pos, int activeOnly)
{
    int best = 0;
    float bestDistSq = FLOAT_806188b8;

    int off = 0;
    for (int i = REG_TAG_SLOTS; i != 0; i--) {
        if (*(int *)((int)&DAT_805c8bd4[0] + off) == tagId) {
            for (unsigned int *node = *(unsigned int **)((int)&DAT_805c8bd8 + off);
                 node != 0; node = (unsigned int *)node[1]) {
                unsigned int handle = *node;
                int record;
                if (handle == 0) {
                    record = 0;
                } else {
                    record = DAT_80611c24 + (handle >> ENTITY_HANDLE_INDEX_SHIFT) * ENTITY_POOL_STRIDE;
                    if (*(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF) != handle) {
                        record = 0;
                        *node = 0;
                    }
                }
                float dy = pos[1] - *(float *)(record + 0x68);
                float dx = pos[0] - *(float *)(record + 0x64);
                float dz = pos[2] - *(float *)(record + 0x6c);
                float distSq = dz * dz + dx * dx + dy * dy;
                if (distSq < bestDistSq &&
                    (activeOnly == 0 || FLOAT_8061888c < *(float *)(record + 0x1cc))) {
                    best = record;
                    bestDistSq = distSq;
                }
            }
        }
        off += REG_TAG_STRIDE;
    }
    return best;
}

// ===========================================================================
// 0x8023c77c  entity_tag_pick_random
//   Pick a pseudo-random live entity from tag group `tagId` (index chosen via
//   rng_counter_tick() modulo the group size). Returns the record or 0.
// ===========================================================================
int entity_tag_pick_random(int tagId)
{
    unsigned int count = 0;
    int *slot = 0;

    int off = 0;
    for (int i = REG_TAG_SLOTS; i != 0; i--) {
        if (tagId == *(int *)((int)&DAT_805c8bd4[0] + off)) {
            for (int node = *(int *)((int)&DAT_805c8bd8 + off);
                 slot = (int *)((int)&DAT_805c8bd4[0] + off), node != 0;
                 node = *(int *)(node + 4)) {
                count++;
            }
        }
        off += REG_TAG_STRIDE;
    }

    if (slot != 0 && count != 0) {
        unsigned int r = rng_counter_tick();
        unsigned int target = r - (r / count) * count;
        int idx = 0;
        for (unsigned int *node = (unsigned int *)slot[1]; node != 0; node = (unsigned int *)node[1]) {
            if ((unsigned int)idx == target) {
                unsigned int handle = *node;
                if (handle == 0) return 0;
                int record = DAT_80611c24 + (handle >> ENTITY_HANDLE_INDEX_SHIFT) * ENTITY_POOL_STRIDE;
                if (*(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF) != handle) {
                    *node = 0;
                    return 0;
                }
                return record;
            }
            idx++;
        }
    }
    return 0;
}

// ===========================================================================
// 0x8023c884  entity_assign_registration_id
//   Stamp the entity record with the next global sequential registration id
//   (record +0x230 <- DAT_80610efc++).
// ===========================================================================
void entity_assign_registration_id(int record)
{
    if (record == 0) return;
    *(int *)(record + 0x230) = DAT_80610efc;
    DAT_80610efc = DAT_80610efc + 1;
}

// ===========================================================================
// 0x8023c8a0  entity_group_register
//   Register an entity into the group table under id `id`, inserting its node
//   into the id-sorted doubly-linked list (head DAT_80612960; node layout
//   +0 handle, +4 id, +8 next, +0xc prev), then assign its registration id.
// ===========================================================================
void entity_group_register(int id, int record)
{
    if (id <= 0 || record == 0) {
        entity_assign_registration_id(record);
        return;
    }
    *(int *)(record + 0x60) = id;
    if (id == 0x3f3f3f3f) {
        entity_assign_registration_id(record);
        return;
    }

    int idx = entity_group_alloc_slot();
    unsigned int *node = (unsigned int *)(&DAT_805c7c34[0] + idx * 0x10);
    unsigned int *head = DAT_80612960;
    bool empty = (head == 0);
    *(int *)((int)&DAT_805c7c38 + idx * 0x10) = id;      // node +4 = id
    node[0] = *(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF);  // node +0 = handle

    if (empty) {
        DAT_80612960 = node;
        *(int *)((int)&DAT_805c7c3c + idx * 0x10) = 0;    // next = 0
        *(int *)(&DAT_805c7c40 + idx * 0x10) = 0;         // prev = 0
    } else {
        unsigned int *prev = 0;
        unsigned int *cur = head;
        while (cur != 0 && (int)cur[1] < id) {            // walk sorted by id (+4)
            prev = cur;
            cur = (unsigned int *)cur[2];                 // next (+8)
        }
        if (prev != 0) {
            prev[2] = (unsigned int)node;                 // prev->next = node
        } else {
            DAT_80612960 = node;                          // new head
        }
        *(unsigned int **)(&DAT_805c7c40 + idx * 0x10) = prev;   // node->prev
        *(unsigned int **)((int)&DAT_805c7c3c + idx * 0x10) = cur; // node->next
        if (cur != 0) {
            cur[3] = (unsigned int)node;                  // cur->prev = node (+0xc)
        }
    }
    entity_assign_registration_id(record);
}

// ===========================================================================
// 0x8023c990  entity_tag_add_member
//   Add the current context entity to its tag group. The context accessor
//   (FUN_803826f0) hands back the tag id (high word) and the entity record
//   (low word). Finds/allocates the tag slot and an aux node, appends the aux
//   node to the tag's member list, and stores the entity handle in it.
// ===========================================================================
void entity_tag_add_member(void)
{
    unsigned long long ctx = FUN_803826f0();
    int tagId  = (int)((unsigned long long)ctx >> 0x20);
    int record = (int)ctx;

    if (record >= 0 && record != 0) {
        int tagSlot = entity_tag_find_or_alloc(tagId);
        if (tagSlot >= 0) {
            int auxSlot = entity_aux_alloc_slot();
            if (auxSlot >= 0) {
                int auxNode = (int)(&DAT_805c9084[0] + auxSlot * 8);
                int listHead = *(int *)((int)&DAT_805c8bd8 + tagSlot * 8);
                if (listHead == 0) {
                    *(int *)((int)&DAT_805c8bd8 + tagSlot * 8) = auxNode;
                } else {
                    int node = listHead;
                    int nextNode = listHead;
                    while (nextNode != 0) {
                        node = nextNode;
                        nextNode = *(int *)(node + 4);
                    }
                    *(int *)(node + 4) = auxNode;
                }
                *(int *)((int)&DAT_805c8bd4[0] + tagSlot * 8) = tagId;
                *(int *)((int)&DAT_805c9084[0] + auxSlot * 8) = *(int *)(record + ENTITY_RECORD_HANDLE_OFF);
            }
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8023ca6c  entity_group_unregister
//   Remove an entity (by handle) from the group table id-list. Resolves the
//   record, reads its group id (+0x60), finds its node, and unlinks it from
//   the doubly-linked list, fixing head/prev/next as needed.
// ===========================================================================
void entity_group_unregister(unsigned int handle)
{
    int record;
    if (handle == 0) {
        record = 0;
    } else {
        record = DAT_80611c24 + (handle >> ENTITY_HANDLE_INDEX_SHIFT) * ENTITY_POOL_STRIDE;
        if (*(unsigned int *)(record + ENTITY_RECORD_HANDLE_OFF) != handle) {
            record = 0;
        }
    }
    if (record == 0) return;

    unsigned int id = *(unsigned int *)(record + 0x60);
    if ((int)id <= 0 || id == 0x3f3f3f3f) return;

    int idx = entity_index_by_id(id);
    unsigned int *head = DAT_80612960;
    if (idx == -1) return;

    DAT_8061295c = DAT_8061295c - 1;
    *(int *)((int)&DAT_805c7c38 + idx * 0x10) = 0;   // clear id (node +4)
    *(int *)((int)&DAT_805c7c34[0] + idx * 0x10) = 0;   // clear handle (node +0)

    int nodeNext = *(int *)((int)&DAT_805c7c3c + idx * 0x10);  // node->next (+8)
    int nodePrev = *(int *)(&DAT_805c7c40 + idx * 0x10);       // node->prev (+0xc)

    if (head == (unsigned int *)(&DAT_805c7c34[0] + idx * 0x10)) {
        DAT_80612960 = (unsigned int *)nodeNext;   // head = node->next
        if (DAT_80612960 != 0) {
            DAT_80612960[3] = 0;   // new head prev = 0
        }
    }
    if (nodePrev != 0) {
        *(int *)(nodePrev + 8) = nodeNext;   // prev->next = node->next
    }
    if (nodeNext != 0) {
        *(int *)(nodeNext + 0xc) = nodePrev;  // next->prev = node->prev
    }
}

// ===========================================================================
// 0x8023cb70  entity_tag_remove_member
//   Remove the aux node whose handle field matches `handle` from whichever tag
//   group list holds it, freeing the aux slot (and the tag slot if it becomes
//   empty).
// ===========================================================================
void entity_tag_remove_member(int handle)
{
    int off = 0;
    for (int i = REG_TAG_SLOTS; i != 0; i--) {
        if (*(int *)((int)&DAT_805c8bd4[0] + off) != 0) {
            int *node = *(int **)((int)&DAT_805c8bd8 + off);
            int *prev = 0;
            while (node != 0) {
                if (*node == handle) {
                    if (prev == 0) {
                        if (node[1] == 0) {
                            *(int *)((int)&DAT_805c8bd4[0] + off) = -1;
                            DAT_80612958 = DAT_80612958 - 1;
                            *(int *)((int)&DAT_805c8bd8 + off) = 0;
                        } else {
                            *(int *)((int)&DAT_805c8bd8 + off) = node[1];
                        }
                    } else {
                        prev[1] = node[1];
                    }
                    node[0] = 0;
                    node[1] = 0;
                    DAT_80612954 = DAT_80612954 - 1;
                    return;
                }
                prev = node;
                node = (int *)node[1];
            }
        }
        off += REG_TAG_STRIDE;
    }
}

// ===========================================================================
// 0x8023cc28  entity_get_field_7c
//   Resolve an entity handle and return its +0x7c field (or 0 if unresolved).
// ===========================================================================
int entity_get_field_7c(unsigned int handle)
{
    int record = entity_handle_resolve(handle);
    if (record == 0) return 0;
    return *(int *)(record + 0x7c);
}

// ===========================================================================
// 0x8023cc5c  igc_actor_budget_reached
//   Return 1 if the live IGC actor count (DAT_80612614) has reached the budget
//   of 0x18, else 0. Implemented via countLeadingZeros(0x18 - count) >> 5.
// ===========================================================================
unsigned int igc_actor_budget_reached(void)
{
    return countLeadingZeros(0x18 - DAT_80612614) >> 5;
}
