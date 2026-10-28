// collision_shape_table.cpp
// Collision-shape record table + per-room collision-layer-mask application.
// The shape table at 0x805aca34 holds 0x30-byte collision-shape records indexed
// by slot; collision_forward_shape_data forwards a shape blob into it.
// collision_apply_layer_mask_to_room sweeps every collision body in a room and
// stamps a new layer mask onto it (used when a room's collision passability
// changes).
//
// Batch: batch_494
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
extern int  entity_query_aabb(int roomId, float *aabb, int outList, int start, int max, int flag);
extern void collision_set_layer_mask(int *shape, unsigned short mask);

// Spawn-frame register accessor: r3 = room id, r4 = layer mask (extraout).
extern int  FUN_803826e4(void);
extern void FUN_80382730(void);

// Collision-shape record table base (0x805aca34, stride 0x30).
extern int  DAT_805aca34;
// Active player/link chain root.
extern int  DAT_80611d74;
}

// FUN_803826e4 returns a packed r3:r4 pair (room id in the high word, layer
// mask in the low word); call it through a typed alias to recover both halves.
typedef unsigned long long (*FramePairFn)(void);

// ---------------------------------------------------------------------------
// Address: 0x8018d610 | Name: collision_store_shape_record | Date: 2026-09-06
// Copy a 0x30-byte (48-byte) collision-shape record from a source blob into the
// shape table slot idx. srcEnd points one word past the record; the ROM walks
// it as 6 interleaved word pairs. Caller: collision_forward_shape_data.
// ---------------------------------------------------------------------------
void collision_store_shape_record(short idx, int srcEnd)
{
    unsigned int *src = (unsigned int *)(srcEnd - 4);
    unsigned int *dst = (unsigned int *)((int)&DAT_805aca34 + idx * 0x30);
    for (int i = 6; i != 0; i--) {
        unsigned int a = src[1];
        unsigned int b = src[2];
        src += 2;
        dst[1] = a;
        dst += 2;
        *dst = b;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018d64c | Name: collision_apply_layer_mask_to_room | Date: 2026-09-06
// Apply a collision layer mask to every collision body belonging to a room.
// The spawn frame supplies the room id (r3) and the mask (r4). Sweeps the room
// via entity_query_aabb (0x14-wide batches) matching bodies whose owner room
// (+0x34) equals the target room, plus the local player's linked bodies.
// Callers: FUN_801df470, FUN_801e1ff4.
// ---------------------------------------------------------------------------
void collision_apply_layer_mask_to_room(void)
{
    unsigned long long frame = ((FramePairFn)(void *)&FUN_803826e4)();
    int roomId = (int)((unsigned long long)frame >> 0x20);
    unsigned short mask = (unsigned short)frame;

    float aabb[6];
    int outList[28];

    if (roomId != 0xd5) {
        int got = 0;
        while (got != 0x14) {
            got = entity_query_aabb(roomId, aabb, (int)outList, got, 0x14, 0);
            for (int i = 0; i < got; i++) {
                int body = outList[i];
                if (*(char *)(body + 0x30) == '\x01' && *(int *)(body + 0x34) != 0xd5 &&
                    roomId == *(int *)(body + 0x34)) {
                    collision_set_layer_mask(*(int **)(body + 4), mask);
                }
            }
        }

        if (DAT_80611d74 != 0 && *(int *)(DAT_80611d74 + 0x14) != 0 &&
            *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x34) == roomId) {
            if (*(int *)(DAT_80611d74 + 0x538) != 0) {
                collision_set_layer_mask(*(int **)(*(int *)(DAT_80611d74 + 0x538) + 4), mask);
            }
            if (*(int *)(DAT_80611d74 + 0x1d8) != 0) {
                collision_set_layer_mask(*(int **)(*(int *)(DAT_80611d74 + 0x1d8) + 4), mask);
            }
            if (*(int *)(DAT_80611d74 + 0xd1c) != 0) {
                collision_set_layer_mask(*(int **)(*(int *)(DAT_80611d74 + 0xd1c) + 4), mask);
            }
        }
    }
    FUN_80382730();
}
