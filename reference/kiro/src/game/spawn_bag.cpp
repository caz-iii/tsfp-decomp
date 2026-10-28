// Address: 0x8034fe04 | Name: spawn_slot_mode3_type2_active | Date: 2026-09-12
// Batch: batch_1000
//
// Spawn-bag / marker type classification module.
//
// Entity records carry a 16-bit "type code" at offset +0x0e whose meaningful
// values are 5..9. These map to the bag-slot layout used by the objective /
// pickup spawn system. DAT_805015bd is the current game-mode byte (mode 3 and
// mode 7 gate different marker classes). The bag tables scanned here are:
//   DAT_805c7bcc / DAT_805c7bd0  - active team-marker table (0x10-byte stride)
//   DAT_805c7bd4 / DAT_805c7bd8  - resolved slot table       (0x20-byte stride)

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern char DAT_805015bd;      // current game-mode byte
extern int  DAT_805c7bcc;      // base of active team-marker table
extern int  DAT_805c7bd0;      // entry count of team-marker table
extern int  DAT_805c7bd4;      // base of resolved slot table
extern int  DAT_805c7bd8;      // entry count of resolved slot table

// Cross-module helpers (objective marker file, batch_999).
extern int  objective_find_marker_record(int pRecord, int pQuery);
extern void tracker_bind_record(void);

// Tiny register-pair bag helpers (stack allocator stubs). Each returns a
// packed u64 whose high dword is a record pointer and low dword is a type code.
extern "C" unsigned long long FUN_803826f0(void);   // push new bag record
extern "C" void               FUN_8038273c(void);   // commit / pop cleanup
extern "C" int                FUN_803826e8(void);    // current query entity
extern "C" void               FUN_80382734(void);    // release query entity
extern "C" unsigned long long __save_gpr_28(void);   // prologue: packed dest+code
extern "C" void               __restore_gpr_28(void);

// ---------------------------------------------------------------------------
// True when the game is in mode 3 and the entity's linked owner record has
// type-code 2. Used to decide pickup/objective spawn eligibility.
uint spawn_slot_mode3_type2_active(int pEntity)
{
    int eligible = 0;
    if ((*(int *)(*(int *)(pEntity + 0x7c) + 0x84) == 2) && (DAT_805015bd == '\x03')) {
        eligible = 1;
    }
    // countLeadingZeros(1)>>5 == 0, countLeadingZeros(0)>>5 == 1 : invert.
    uint clz = countLeadingZeros((uint)eligible);
    return clz >> 5;
}

// ---------------------------------------------------------------------------
// Base allocation size for a marker of the given type code.
u32 spawn_type_base_size(int pRecord)
{
    short typecode = *(short *)(pRecord + 0xe);
    if (typecode == 7) {
        return 0x80;
    }
    if (typecode < 7) {
        if (typecode == 5) {
            return 0x80;
        }
        if (typecode < 5) {
            return 0xffffffff;
        }
    } else if (8 < typecode) {
        return 0xffffffff;
    }
    return 0x100;
}

// ---------------------------------------------------------------------------
// Map (type code, index) onto the linear bag-slot index. Returns -1 for codes
// outside the 5..9 range.
int spawn_type_slot_index(int typecode, int index)
{
    int slot = -1;
    if (typecode == 7) {
        slot = index * 3;
    } else if (typecode < 7) {
        if (typecode == 5) {
            slot = index << 1;
        } else if (4 < typecode) {
            slot = index * 2 + 1;
        }
    } else if (typecode == 9) {
        slot = index * 3 + 2;
    } else if (typecode < 9) {
        slot = index * 3 + 1;
    }
    return slot;
}

// ---------------------------------------------------------------------------
// Team flag for a type code: code 6 -> team 0, code 5 -> team 1, else -1.
u32 spawn_type_team_flag(int typecode)
{
    u32 flag = 0xffffffff;
    if (typecode == 6) {
        flag = 0;
    } else if ((typecode < 6) && (4 < typecode)) {
        flag = 1;
    }
    return flag;
}

// Second (identical) copy emitted at a distinct call site in the original.
u32 spawn_type_team_flag2(int typecode)
{
    u32 flag = 0xffffffff;
    if (typecode == 6) {
        flag = 0;
    } else if ((typecode < 6) && (4 < typecode)) {
        flag = 1;
    }
    return flag;
}

// ---------------------------------------------------------------------------
// Initialise a bag record allocated on the caller stack (dest pointer arrives
// in the saved GPR pair). Fills team flag, slot index and a 16-bit marker id.
void spawn_bag_record_init(int typecode, int index, unsigned short markerId)
{
    unsigned long long saved = __save_gpr_28();
    u32 *pRecord = (u32 *)(unsigned)(saved >> 0x20);

    *pRecord = (u32)saved;                          // preserve packed code word
    u32 team = spawn_type_team_flag(typecode);
    *(short *)(pRecord + 2) = (short)team;
    *(unsigned short *)((int)pRecord + 10) = markerId;
    int slot = spawn_type_slot_index(typecode, index);
    pRecord[1] = (u32)slot;

    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Push a freshly allocated bag record for the current type code, seeding its
// team flag, slot index and empty marker id.
void spawn_bag_record_push(int index)
{
    unsigned long long alloc = FUN_803826f0();
    int *pRecord = (int *)(unsigned)(alloc >> 0x20);
    int typecode = (int)alloc;

    u32 team = spawn_type_team_flag2(typecode);
    *(short *)(pRecord + 1) = (short)team;
    int slot = spawn_type_slot_index(typecode, index);
    *pRecord = slot;
    *(unsigned short *)((int)pRecord + 6) = 0xffff;
    pRecord[2] = 0;

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// True if the active team-marker table holds a live entry matching the given
// entity's type code and team, in the current game mode.
u32 spawn_bag_has_active_team_type(int pEntity)
{
    int off = 0;
    int remaining = DAT_805c7bd0;
    if (0 < DAT_805c7bd0) {
        do {
            short typecode = *(short *)(pEntity + 0xe);
            int *pEntry = (int *)(DAT_805c7bcc + off);
            if (typecode == 6) {
                if ((*(short *)(pEntry + 2) == 0) && (0 < *pEntry) && (DAT_805015bd == '\x03')) {
                    return 1;
                }
            } else if ((typecode < 6) && (4 < typecode) &&
                       (*(short *)(pEntry + 2) == 1) && (0 < *pEntry) && (DAT_805015bd == '\x03')) {
                return 1;
            }
            off += 0x10;
            remaining += -1;
        } while (remaining != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// True if the resolved slot table contains an entry whose stored slot index
// matches the id computed from this record's type code and id, in the mode
// required for that code (mode 7 for codes 7/8, mode 3 for codes 5/6).
u32 spawn_bag_slot_matches(int *pRecord)
{
    int off = 0;
    int remaining = DAT_805c7bd8;
    if (0 < DAT_805c7bd8) {
        do {
            short typecode = *(short *)((int)pRecord + 0xe);
            if (typecode == 7) {
                if ((*(int *)(DAT_805c7bd4 + off) == *pRecord * 3) && (DAT_805015bd == '\a')) {
                    return 1;
                }
            } else if (typecode < 7) {
                if (typecode == 5) {
                    if ((*(int *)(DAT_805c7bd4 + off) == *pRecord << 1) && (DAT_805015bd == '\x03')) {
                        return 1;
                    }
                } else if ((4 < typecode) &&
                           (*(int *)(DAT_805c7bd4 + off) == *pRecord * 2 + 1) &&
                           (DAT_805015bd == '\x03')) {
                    return 1;
                }
            } else if ((typecode < 9) &&
                       (*(int *)(DAT_805c7bd4 + off) == *pRecord * 3 + 1) &&
                       (DAT_805015bd == '\a')) {
                return 1;
            }
            off += 0x20;
            remaining += -1;
        } while (remaining != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Re-bind every active team-marker record to the current query entity.
void spawn_bag_rebind_all(void)
{
    int pQuery = FUN_803826e8();
    int off = 0;
    for (int i = 0; i < DAT_805c7bd0; i = i + 1) {
        objective_find_marker_record(DAT_805c7bcc + off, pQuery);
        tracker_bind_record();
        off = off + 0x10;
    }
    FUN_80382734();
}
