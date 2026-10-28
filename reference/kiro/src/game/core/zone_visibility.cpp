// zone_visibility.cpp
// Per-map room/zone visibility adjacency system. Address range
// 0x801c2dc8 - 0x801c5404 (batch_590; the 0x801c2ed4..0x801c5404 gap is
// non-code / other modules, only the listed functions are in this batch).
//
// A level is partitioned into "zones" (0..N-1, N = DAT_80611ae4). Each render
// room (DAT_80611b04 table, count DAT_80611b14) belongs to one zone via the
// room->zone map DAT_80611ad8. At level-load the build pass constructs a dense
// NxN adjacency table (DAT_8061262c): entry [from][to] holds a list of the
// rooms in zone `from`, each tagged with a per-edge visibility level that says
// how visible zone `to`'s rooms are when the camera is standing in zone `from`.
// Per frame the apply pass reads the local player's current zone
// (DAT_80610d10) and writes each room's visibility mask (+0x30 of the room's
// object at +4) from that zone's adjacency row, so the portal/room renderer
// only draws rooms reachable from where the player is.
//
// The current-zone id is resolved from the player's floor room
// (DAT_80611d54+0x2a0 -> DAT_80611ad8[]) and cached to the player context
// (DAT_80611d74[0x4f8]) and slot (DAT_80611d54+0x2c0). 0xff means "show all".
//
// Two maps ship hand-authored adjacency tables keyed on the mode id
// DAT_80612614: mode 4 and mode 0xb and mode 0x12 (populated by the long
// zone_adj_set() call lists); mode 9 uses a symmetric neighbour rule computed
// in the build loop. Modes without a table leave DAT_80612628 == 0 and the
// apply pass is a no-op.
//
// Non-matching: the wide 16-arg PPC-ABI spill frame around the build
// allocation (FUN_803826e4 / FUN_80382730 open/close, and the mem_alloc
// register tail) is dead plumbing - it is kept as __zv_frame_open/close calls
// and mem_alloc is given its real (base,size) arguments. Raw FUN_/DAT_ symbols
// are declared file-locally per the compile-clean standard; named cross-module
// helpers (scene_portal_block/unblock, trigger_slot_resolve_entity,
// trigger_is_fully_open, mem_alloc, memset_wrapper) resolve through the shared
// headers.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
// --- module state ---
extern int  DAT_80610d08;    // last room-id passed to zone_set_interior_flag
extern int  DAT_8061260c;    // interior/indoor flag for the last room id
extern int  DAT_80612614;    // active game-mode id (map selector)
extern int  DAT_8061262c;    // adjacency table base (NxN entries, 8 bytes each)
extern int  DAT_80612628;    // 1 when an adjacency table has been built
extern int  DAT_80611ae4;    // zone count N
extern int  DAT_80611b14;    // render room count
extern int  DAT_80611ad8;    // room -> zone id map (int[room])
extern int  DAT_80611b04;    // render room object table (int[room])
extern uint DAT_80610d10;    // current active zone id (0xff => show all)
extern int  DAT_80612620;    // per-player last-zone array base
extern uint uRam80612624;    // (second slot of the per-player last-zone array)
extern int *DAT_80611d74;    // active local-player context table
extern int  DAT_80611d54;    // primary player slot pointer
extern int  DAT_806108e4;    // active-player count gate
extern float FLOAT_80616ec8; // camera-height threshold (mode 0xb flight gate)

// --- raw frontier helpers (typed to their call sites) ---
extern int  FUN_803826e4(void);           // wide FP-context spill frame open
extern void FUN_80382730(void);           // wide FP-context spill frame close
extern int  FUN_8023c32c(int tag);        // find scene object by fourcc tag
extern int  FUN_8023c59c(int tag, int outList, int max);  // gather objects by tag
}

// Map a raw room id to its owning zone via the room->zone map.
// (No standalone function in the ROM; inlined at every use site.)

// 0x801c2dc8 - empty init hook (no-op reserved slot).
void zone_visibility_stub_init(void)
{
    return;
}

// 0x801c2dcc - set the "interior" flag for a room id. Only recomputes when the
// id actually changes. A fixed set of room ids are flagged interior (1); all
// others clear it (0). Returns the resulting flag.
int zone_set_interior_flag(int roomId)
{
    if (roomId != DAT_80610d08) {
        DAT_80610d08 = roomId;
        switch (roomId) {
        case 8:  case 0x10: case 0x13: case 0x17: case 0x18:
        case 0x1e: case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x24: case 0x25: case 0x28: case 0x29: case 0x2b:
        case 0x2c: case 0x2d: case 0x2f: case 0x39: case 0x52:
            DAT_8061260c = 1;
            break;
        default:
            DAT_8061260c = 0;
        }
    }
    return DAT_8061260c;
}

// 0x801c2e1c - true when the current mode is one of the two front-end menu
// modes (0x65 / 0x66).
int zone_mode_is_frontend(void)
{
    if ((unsigned int)(DAT_80612614 - 0x65) < 2) {
        return 1;
    }
    return 0;
}

// 0x801c2e40 - reset the adjacency-table pointer/flag before a rebuild.
void zone_adj_reset(void)
{
    DAT_8061262c = 0;
    DAT_80612628 = 0;
    return;
}

// 0x801c2e50 - set the per-edge visibility value for one (fromZone,toZone,room)
// triple in the already-allocated adjacency table. Walks the room list of
// entry [from][to] to find the matching room id, then writes `value` into that
// entry's +4 slot. (Ghidra's iVar6 starts at -1 and is only updated inside the
// loop; when no room matches it writes to slot -1, matching the original.)
void zone_adj_set(int fromZone, int toZone, int roomId, int value)
{
    int idx = 0;
    int entryOff = (toZone + fromZone * DAT_80611ae4) * 8;
    int *entry = (int *)(DAT_8061262c + entryOff);
    int remaining = *entry;
    int matchIdx = -1;
    int cursor = 0;
    for (; ; cursor += 8) {
        matchIdx = -1;
        if (remaining == 0) break;
        matchIdx = idx;
        if (roomId == *(int *)(entry[1] + cursor)) break;
        idx = idx + 1;
        remaining = remaining + -1;
    }
    *(int *)(*(int *)(DAT_8061262c + entryOff + 4) + matchIdx * 8 + 4) = value;
    return;
}

// 0x801c2ec0 - clear the per-player "last zone" cache pair.
void zone_player_cache_reset(void)
{
    DAT_80612620 = 0;
    uRam80612624 = 0;
    return;
}

// 0x801c2ed4 - build the NxN zone adjacency table for the current map.
// Allocates the row directory (N*N*8) plus, per entry [from][to], a room list
// sized to the number of rooms that live in zone `from`. Each room-list slot is
// {roomId, visLevel}. mode 9 fills visLevel with a symmetric neighbour rule
// (self / +-1 => 1, a hand-listed set => 1 or 2, else 2); other populated modes
// (4, 0xb, 0x12) start every slot at the mode-9 defaults of 0 (0xff on the
// diagonal for non-mode-9) and then overwrite specific edges via zone_adj_set.
void zone_adj_build(void)
{
    unsigned int rowBytes;
    int zoneCount;
    int roomsInZone;
    int fromZone;
    int toZone;
    int listBytes;
    int listPtr;
    int slotOff;
    int r;

    (void)FUN_803826e4();  // spill-frame open (dead PPC ABI plumbing)
    zoneCount = DAT_80611ae4;
    DAT_80612628 = 0;
    if (0 < DAT_80611ae4) {
        rowBytes = (DAT_80611ae4 * DAT_80611ae4 * 8 + 0xf) & 0xfffffff0;
        DAT_8061262c = (int)mem_alloc(rowBytes, 1);
        memset_wrapper((void *)DAT_8061262c, 0, rowBytes);

        for (fromZone = 0; fromZone < zoneCount; fromZone = fromZone + 1) {
            // count rooms whose zone == fromZone
            roomsInZone = 0;
            for (r = 0; r < DAT_80611b14; r = r + 1) {
                if (fromZone == *(int *)(DAT_80611ad8 + r * 4)) {
                    roomsInZone = roomsInZone + 1;
                }
            }
            for (toZone = 0; toZone < zoneCount; toZone = toZone + 1) {
                slotOff = (fromZone + toZone * zoneCount) * 8;
                *(int *)(DAT_8061262c + slotOff) = roomsInZone;
                listBytes = (roomsInZone * 8 + 0xf) & 0xfffffff0;
                listPtr = (int)mem_alloc(listBytes, 1);
                *(int *)(DAT_8061262c + slotOff + 4) = listPtr;
                memset_wrapper((void *)listPtr, 0, listBytes);

                int writeOff = 0;
                for (r = 0; r < DAT_80611b14; r = r + 1) {
                    if (fromZone == *(int *)(DAT_80611ad8 + r * 4)) {
                        int *rec = (int *)(*(int *)(DAT_8061262c + slotOff + 4) + writeOff);
                        rec[0] = r;
                        if (DAT_80612614 == 9) {
                            if (((fromZone == 10 && toZone == 2)  || (fromZone == 0xd && toZone == 4)) ||
                                ((fromZone == 0xd && toZone == 5)  || (fromZone == 0xb && toZone == 9)) ||
                                ((fromZone == 2  && toZone == 10)  || (fromZone == 4  && toZone == 0xd)) ||
                                ((fromZone == 5  && toZone == 0xd) || (fromZone == 4  && toZone == 0xf))) {
                                rec[1] = 1;
                            } else if (((fromZone == 1  && toZone == 2)   || (fromZone == 0xc && toZone == 0xd)) ||
                                       ((fromZone == 0xe && toZone == 0xd) || (fromZone == 0xd && toZone == 0xe)) ||
                                       ((fromZone == 0xf && toZone == 0xe) || (fromZone == 6   && toZone == 5))) {
                                rec[1] = 2;
                            } else if (fromZone == toZone) {
                                rec[1] = 1;
                            } else if (fromZone - 1 == toZone) {
                                rec[1] = 1;
                            } else if (fromZone + 1 == toZone) {
                                rec[1] = 1;
                            } else {
                                rec[1] = 2;
                            }
                        } else if (fromZone == toZone) {
                            rec[1] = 0xff;
                        }
                        writeOff = writeOff + 8;
                    }
                }
            }
        }

        if (DAT_80612614 == 4) {
            zone_adj_set(0,1,0x3f,1);  zone_adj_set(0,7,0x4d,1);  zone_adj_set(0,9,0x51,1);
            zone_adj_set(0,10,0x52,5); zone_adj_set(0,10,0x53,1); zone_adj_set(0,10,0x54,1);
            zone_adj_set(0,10,0x55,3); zone_adj_set(0,0xc,0x58,1);
            zone_adj_set(1,2,0x41,1);  zone_adj_set(1,2,0x42,1);  zone_adj_set(1,4,0x47,1);
            zone_adj_set(1,10,0x52,5); zone_adj_set(1,10,0x53,1); zone_adj_set(1,10,0x54,1);
            zone_adj_set(1,10,0x55,2); zone_adj_set(1,0xc,0x58,1);
            zone_adj_set(2,1,0x3f,2);  zone_adj_set(2,1,0x40,1);  zone_adj_set(2,3,0x44,1);
            zone_adj_set(2,4,0x46,1);  zone_adj_set(2,4,0x47,3);
            zone_adj_set(3,2,0x41,3);  zone_adj_set(3,2,0x42,1);  zone_adj_set(3,4,0x46,1);
            zone_adj_set(3,4,0x47,3);  zone_adj_set(3,5,0x48,2);  zone_adj_set(3,6,0x49,1);
            zone_adj_set(4,2,0x42,1);  zone_adj_set(4,3,0x44,1);  zone_adj_set(4,3,0x45,1);
            zone_adj_set(4,5,0x48,2);
            zone_adj_set(5,3,0x44,1);  zone_adj_set(5,3,0x45,1);  zone_adj_set(5,4,0x46,1);
            zone_adj_set(5,4,0x47,3);  zone_adj_set(5,6,0x49,1);  zone_adj_set(5,6,0x4a,3);
            zone_adj_set(5,6,0x4b,2);  zone_adj_set(5,8,0x4f,1);  zone_adj_set(5,8,0x50,1);
            zone_adj_set(6,3,0x45,1);  zone_adj_set(6,5,0x48,2);  zone_adj_set(6,7,0x4c,1);
            zone_adj_set(6,7,0x4d,3);  zone_adj_set(6,8,0x4e,1);  zone_adj_set(6,8,0x4f,1);
            zone_adj_set(6,8,0x50,1);  zone_adj_set(6,9,0x51,2);  zone_adj_set(6,10,0x52,5);
            zone_adj_set(6,10,0x54,1);
            zone_adj_set(7,0,0x3e,1);  zone_adj_set(7,1,0x3f,1);  zone_adj_set(7,6,0x49,1);
            zone_adj_set(7,6,0x4a,3);  zone_adj_set(7,6,0x4b,1);  zone_adj_set(7,8,0x4e,1);
            zone_adj_set(7,8,0x4f,1);  zone_adj_set(7,9,0x51,3);  zone_adj_set(7,10,0x52,7);
            zone_adj_set(7,10,0x53,2); zone_adj_set(7,10,0x54,1); zone_adj_set(7,10,0x55,3);
            zone_adj_set(8,4,0x47,4);  zone_adj_set(8,6,0x49,1);  zone_adj_set(8,6,0x4a,1);
            zone_adj_set(8,6,0x4b,1);  zone_adj_set(8,7,0x4c,1);  zone_adj_set(8,7,0x4d,3);
            zone_adj_set(8,9,0x51,2);  zone_adj_set(8,10,0x52,3);
            zone_adj_set(9,0,0x3e,1);  zone_adj_set(9,1,0x3f,1);  zone_adj_set(9,6,0x4a,1);
            zone_adj_set(9,6,0x4b,1);  zone_adj_set(9,7,0x4c,1);  zone_adj_set(9,7,0x4d,3);
            zone_adj_set(9,8,0x4e,1);  zone_adj_set(9,10,0x52,3); zone_adj_set(9,10,0x53,1);
            zone_adj_set(9,10,0x54,1); zone_adj_set(9,10,0x55,3); zone_adj_set(9,0xc,0x58,1);
            zone_adj_set(10,0,0x3e,1); zone_adj_set(10,1,0x3f,3); zone_adj_set(10,1,0x40,1);
            zone_adj_set(10,2,0x42,1); zone_adj_set(10,7,0x4d,3); zone_adj_set(10,8,0x4e,2);
            zone_adj_set(10,9,0x51,3); zone_adj_set(10,10,0x52,3);zone_adj_set(10,0xb,0x57,1);
            zone_adj_set(10,0xb,0x56,1);zone_adj_set(10,0xc,0x58,1);
            zone_adj_set(0xb,10,0x52,3);zone_adj_set(0xb,10,0x53,2);zone_adj_set(0xb,10,0x54,2);
            zone_adj_set(0xb,10,0x55,3);
            zone_adj_set(0xc,0,0x3e,1); zone_adj_set(0xc,1,0x3f,2); zone_adj_set(0xc,1,0x40,1);
            zone_adj_set(0xc,2,0x42,1); zone_adj_set(0xc,4,0x47,1); zone_adj_set(0xc,9,0x51,1);
            zone_adj_set(0xc,10,0x52,3);zone_adj_set(0xc,10,0x53,1);zone_adj_set(0xc,10,0x54,1);
            zone_adj_set(0xc,10,0x55,3);
            DAT_80612628 = 1;
        }
        if (DAT_80612614 == 0xb) {
            zone_adj_set(0x14,0x13,0x5e,1);
            zone_adj_set(0x13,0x12,0x5d,1);
            zone_adj_set(0x12,0x10,0x5b,1); zone_adj_set(0x12,0x11,0x5c,1); zone_adj_set(0x12,0x13,0x5e,1);
            zone_adj_set(0x11,0x10,0x5b,1); zone_adj_set(0x11,0x12,0x5d,1);
            zone_adj_set(0x10,0x11,0x5c,1); zone_adj_set(0x10,0x12,0x5d,1);
            zone_adj_set(0x10,0xf,0x56,1);  zone_adj_set(0x10,0xf,0x58,1);  zone_adj_set(0x10,0xf,0x57,1);
            zone_adj_set(0x10,0xf,0x59,1);  zone_adj_set(0x10,0xf,0x5a,1);
            zone_adj_set(0xf,0x10,0x5b,1);  zone_adj_set(0xf,0x11,0x5c,1);  zone_adj_set(0xf,0x12,0x5d,1);
            zone_adj_set(0xf,0xe,0x46,1);   zone_adj_set(0xf,0xe,0x54,1);   zone_adj_set(0xf,0xe,0x53,1);
            zone_adj_set(0xf,0xe,0x52,1);   zone_adj_set(0xf,0xe,0x51,1);   zone_adj_set(0xf,0xe,0x50,1);
            zone_adj_set(0xf,0xe,0x4f,1);   zone_adj_set(0xf,0xe,0x4e,1);   zone_adj_set(0xf,0xe,0x4d,1);
            zone_adj_set(0xf,0xe,0x4c,1);   zone_adj_set(0xf,0xe,0x4b,1);   zone_adj_set(0xf,0xe,0x4a,1);
            zone_adj_set(0xf,0xe,0x49,1);   zone_adj_set(0xf,0xe,0x47,1);   zone_adj_set(0xf,0xe,0x48,1);
            zone_adj_set(0xf,0xe,0x55,1);
            zone_adj_set(0xe,0xf,0x56,1);   zone_adj_set(0xe,0xf,0x58,1);   zone_adj_set(0xe,0xf,0x57,1);
            zone_adj_set(0xe,0xf,0x59,1);   zone_adj_set(0xe,0xf,0x5a,1);
            zone_adj_set(0xe,0xd,0x43,1);   zone_adj_set(0xe,0xd,0x44,1);   zone_adj_set(0xe,0xd,0x45,1);
            zone_adj_set(0xd,0xc,0x31,1);   zone_adj_set(0xd,0xc,0x32,1);   zone_adj_set(0xd,0xc,0x33,1);
            zone_adj_set(0xd,0xc,0x34,1);   zone_adj_set(0xd,0xc,0x35,1);   zone_adj_set(0xd,0xc,0x36,1);
            zone_adj_set(0xd,0xc,0x37,1);   zone_adj_set(0xd,0xc,0x38,1);   zone_adj_set(0xd,0xc,0x39,1);
            zone_adj_set(0xd,0xc,0x3a,1);   zone_adj_set(0xd,0xc,0x3b,1);   zone_adj_set(0xd,0xc,0x3c,1);
            zone_adj_set(0xd,0xc,0x3d,1);   zone_adj_set(0xd,0xc,0x3e,1);   zone_adj_set(0xd,0xc,0x3f,1);
            zone_adj_set(0xd,0xc,0x40,1);   zone_adj_set(0xd,0xc,0x41,1);
            zone_adj_set(0xd,0xe,0x46,1);   zone_adj_set(0xd,0xe,0x54,1);   zone_adj_set(0xd,0xe,0x53,1);
            zone_adj_set(0xd,0xe,0x52,1);   zone_adj_set(0xd,0xe,0x51,1);   zone_adj_set(0xd,0xe,0x50,1);
            zone_adj_set(0xd,0xe,0x4f,1);   zone_adj_set(0xd,0xe,0x4e,1);   zone_adj_set(0xd,0xe,0x4d,1);
            zone_adj_set(0xd,0xe,0x4c,1);   zone_adj_set(0xd,0xe,0x4b,1);   zone_adj_set(0xd,0xe,0x4a,1);
            zone_adj_set(0xd,0xe,0x49,1);   zone_adj_set(0xd,0xe,0x47,1);   zone_adj_set(0xd,0xe,0x48,1);
            zone_adj_set(0xd,0xe,0x55,1);
            zone_adj_set(0xc,0xb,0x30,1);   zone_adj_set(0xc,0xd,0x43,1);   zone_adj_set(0xc,0xd,0x44,1);
            zone_adj_set(0xc,0xd,0x45,1);
            zone_adj_set(0xb,10,0x27,1);    zone_adj_set(0xb,10,0x28,1);    zone_adj_set(0xb,10,0x29,1);
            zone_adj_set(0xb,10,0x2a,1);    zone_adj_set(0xb,10,0x2b,1);    zone_adj_set(0xb,10,0x2c,1);
            zone_adj_set(0xb,10,0x2d,1);    zone_adj_set(0xb,10,0x2e,1);    zone_adj_set(0xb,10,0x2f,1);
            zone_adj_set(0xb,0xc,0x31,1);   zone_adj_set(0xb,0xc,0x32,1);   zone_adj_set(0xb,0xc,0x33,1);
            zone_adj_set(0xb,0xc,0x34,1);   zone_adj_set(0xb,0xc,0x35,1);   zone_adj_set(0xb,0xc,0x36,1);
            zone_adj_set(0xb,0xc,0x38,1);   zone_adj_set(0xb,0xc,0x39,1);   zone_adj_set(0xb,0xc,0x3a,1);
            zone_adj_set(0xb,0xc,0x3b,1);   zone_adj_set(0xb,0xc,0x3c,1);   zone_adj_set(0xb,0xc,0x3d,1);
            zone_adj_set(0xb,0xc,0x3e,1);   zone_adj_set(0xb,0xc,0x3f,1);   zone_adj_set(0xb,0xc,0x40,1);
            zone_adj_set(0xb,0xc,0x41,1);
            zone_adj_set(10,9,0x26,1);      zone_adj_set(10,0xb,0x30,1);
            zone_adj_set(9,10,0x27,1);      zone_adj_set(9,10,0x28,1);      zone_adj_set(9,10,0x29,1);
            zone_adj_set(9,10,0x2a,1);      zone_adj_set(9,10,0x2b,1);      zone_adj_set(9,10,0x2c,1);
            zone_adj_set(9,10,0x2d,1);      zone_adj_set(9,10,0x2e,1);      zone_adj_set(9,10,0x2f,1);
            zone_adj_set(9,8,0x25,1);
            zone_adj_set(8,9,0x26,1);       zone_adj_set(8,7,0x24,1);
            zone_adj_set(7,8,0x25,1);       zone_adj_set(7,6,0x23,1);
            zone_adj_set(7,5,0x11,1);       zone_adj_set(7,5,0x12,1);       zone_adj_set(7,5,0x13,1);
            zone_adj_set(7,5,0x14,1);       zone_adj_set(7,5,0x15,1);       zone_adj_set(7,5,0x16,1);
            zone_adj_set(7,5,0x17,1);       zone_adj_set(7,5,0x18,1);       zone_adj_set(7,5,0x19,1);
            zone_adj_set(7,5,0x1a,1);       zone_adj_set(7,5,0x1b,1);       zone_adj_set(7,5,0x1c,1);
            zone_adj_set(7,5,0x1d,1);       zone_adj_set(7,5,0x1e,1);       zone_adj_set(7,5,0x1f,1);
            zone_adj_set(7,5,0x20,1);       zone_adj_set(7,5,0x21,1);       zone_adj_set(7,5,0x22,1);
            zone_adj_set(6,7,0x24,1);
            zone_adj_set(6,5,0x11,1);       zone_adj_set(6,5,0x12,1);       zone_adj_set(6,5,0x13,1);
            zone_adj_set(6,5,0x14,1);       zone_adj_set(6,5,0x15,1);       zone_adj_set(6,5,0x16,1);
            zone_adj_set(6,5,0x17,1);       zone_adj_set(6,5,0x18,1);       zone_adj_set(6,5,0x19,1);
            zone_adj_set(6,5,0x1a,1);       zone_adj_set(6,5,0x1b,1);       zone_adj_set(6,5,0x1c,1);
            zone_adj_set(6,5,0x1d,1);       zone_adj_set(6,5,0x1e,1);       zone_adj_set(6,5,0x1f,1);
            zone_adj_set(6,5,0x20,1);       zone_adj_set(6,5,0x21,1);       zone_adj_set(6,5,0x22,1);
            zone_adj_set(5,4,8,1);          zone_adj_set(5,4,9,1);          zone_adj_set(5,4,10,1);
            zone_adj_set(5,4,0xb,1);        zone_adj_set(5,4,0xc,1);        zone_adj_set(5,4,0xd,1);
            zone_adj_set(5,4,0xe,1);        zone_adj_set(5,4,0xf,1);        zone_adj_set(5,4,0x10,1);
            zone_adj_set(5,6,0x23,1);       zone_adj_set(5,7,0x24,1);
            zone_adj_set(4,5,0x11,1);       zone_adj_set(4,5,0x12,1);       zone_adj_set(4,5,0x13,1);
            zone_adj_set(4,5,0x14,1);       zone_adj_set(4,5,0x15,1);       zone_adj_set(4,5,0x16,1);
            zone_adj_set(4,5,0x17,1);       zone_adj_set(4,5,0x18,1);       zone_adj_set(4,5,0x19,1);
            zone_adj_set(4,5,0x1a,1);       zone_adj_set(4,5,0x1b,1);       zone_adj_set(4,5,0x1c,1);
            zone_adj_set(4,5,0x1d,1);       zone_adj_set(4,5,0x1e,1);       zone_adj_set(4,5,0x1f,1);
            zone_adj_set(4,5,0x20,1);       zone_adj_set(4,5,0x21,1);       zone_adj_set(4,5,0x22,1);
            zone_adj_set(4,3,4,1);          zone_adj_set(4,3,5,1);          zone_adj_set(4,3,6,1);
            zone_adj_set(4,3,7,1);
            zone_adj_set(3,4,8,1);          zone_adj_set(3,4,9,1);          zone_adj_set(3,4,10,1);
            zone_adj_set(3,4,0xb,1);        zone_adj_set(3,4,0xc,1);        zone_adj_set(3,4,0xd,1);
            zone_adj_set(3,4,0xe,1);        zone_adj_set(3,4,0xf,1);        zone_adj_set(3,4,0x10,1);
            zone_adj_set(3,2,3,1);          zone_adj_set(3,1,2,1);
            zone_adj_set(2,3,4,1);          zone_adj_set(2,3,5,1);          zone_adj_set(2,3,6,1);
            zone_adj_set(2,3,7,1);          zone_adj_set(2,1,2,1);
            zone_adj_set(1,2,3,1);          zone_adj_set(1,0,1,1);          zone_adj_set(1,3,4,1);
            zone_adj_set(1,3,5,1);          zone_adj_set(1,3,6,1);          zone_adj_set(1,3,7,1);
            zone_adj_set(0,1,2,1);
            DAT_80612628 = 1;
        }
        if (DAT_80612614 == 0x12) {
            zone_adj_set(0,1,0x1a,3);
            zone_adj_set(0,2,0x1b,1); zone_adj_set(0,2,0x1c,1); zone_adj_set(0,2,0x1d,1);
            zone_adj_set(0,2,0x1e,1); zone_adj_set(0,2,0x1f,1); zone_adj_set(0,2,0x20,1);
            zone_adj_set(0,2,0x21,1); zone_adj_set(0,2,0x22,1);
            zone_adj_set(0,3,0x23,1); zone_adj_set(0,3,0x24,1); zone_adj_set(0,3,0x25,1);
            zone_adj_set(0,3,0x26,1); zone_adj_set(0,3,0x27,1); zone_adj_set(0,3,0x28,1);
            zone_adj_set(1,0,1,1);  zone_adj_set(1,0,2,1);  zone_adj_set(1,0,3,1);
            zone_adj_set(1,0,4,1);  zone_adj_set(1,0,5,1);  zone_adj_set(1,0,6,1);
            zone_adj_set(1,0,7,1);  zone_adj_set(1,0,8,1);  zone_adj_set(1,0,9,1);
            zone_adj_set(1,0,10,1); zone_adj_set(1,0,0xb,1);zone_adj_set(1,0,0xc,1);
            zone_adj_set(1,0,0xd,1);zone_adj_set(1,0,0xe,1);zone_adj_set(1,0,0xf,1);
            zone_adj_set(1,0,0x10,1);zone_adj_set(1,0,0x11,1);zone_adj_set(1,0,0x12,1);
            zone_adj_set(1,0,0x13,1);zone_adj_set(1,0,0x14,1);zone_adj_set(1,0,0x15,1);
            zone_adj_set(1,0,0x16,1);
            zone_adj_set(1,2,0x1b,1);zone_adj_set(1,2,0x1c,1);zone_adj_set(1,2,0x1d,1);
            zone_adj_set(1,2,0x1e,1);zone_adj_set(1,2,0x1f,1);zone_adj_set(1,2,0x20,1);
            zone_adj_set(1,2,0x21,1);zone_adj_set(1,2,0x22,1);
            zone_adj_set(1,3,0x23,1);zone_adj_set(1,3,0x24,1);zone_adj_set(1,3,0x25,1);
            zone_adj_set(1,3,0x26,1);zone_adj_set(1,3,0x27,1);zone_adj_set(1,3,0x28,1);
            zone_adj_set(1,1,0x1a,5);
            zone_adj_set(3,0,1,1);  zone_adj_set(3,0,2,1);  zone_adj_set(3,0,3,1);
            zone_adj_set(3,0,4,1);  zone_adj_set(3,0,5,1);  zone_adj_set(3,0,6,1);
            zone_adj_set(3,0,7,1);  zone_adj_set(3,0,8,1);  zone_adj_set(3,0,9,1);
            zone_adj_set(3,0,10,1); zone_adj_set(3,0,0xb,1);zone_adj_set(3,0,0xc,1);
            zone_adj_set(3,0,0xd,1);zone_adj_set(3,0,0xe,1);zone_adj_set(3,0,0xf,1);
            zone_adj_set(3,0,0x10,1);zone_adj_set(3,0,0x11,1);zone_adj_set(3,0,0x12,1);
            zone_adj_set(3,0,0x13,1);zone_adj_set(3,0,0x14,1);zone_adj_set(3,0,0x15,1);
            zone_adj_set(3,0,0x16,1);
            zone_adj_set(3,1,0x1a,3);
            zone_adj_set(3,2,0x1b,1);zone_adj_set(3,2,0x1c,1);zone_adj_set(3,2,0x1d,1);
            zone_adj_set(3,2,0x1e,1);zone_adj_set(3,2,0x1f,1);zone_adj_set(3,2,0x20,1);
            zone_adj_set(3,2,0x21,1);zone_adj_set(3,2,0x22,1);
            zone_adj_set(4,0,1,1);  zone_adj_set(4,0,2,1);  zone_adj_set(4,0,3,1);
            zone_adj_set(4,0,4,1);  zone_adj_set(4,0,5,1);  zone_adj_set(4,0,6,1);
            zone_adj_set(4,0,7,1);  zone_adj_set(4,0,8,1);  zone_adj_set(4,0,9,1);
            zone_adj_set(4,0,10,1); zone_adj_set(4,0,0xb,1);zone_adj_set(4,0,0xc,1);
            zone_adj_set(4,0,0xd,1);zone_adj_set(4,0,0xe,1);zone_adj_set(4,0,0xf,1);
            zone_adj_set(4,0,0x10,1);zone_adj_set(4,0,0x11,1);zone_adj_set(4,0,0x12,1);
            zone_adj_set(4,0,0x13,1);zone_adj_set(4,0,0x14,1);zone_adj_set(4,0,0x15,1);
            zone_adj_set(4,0,0x16,1);
            zone_adj_set(4,1,0x1a,3);
            DAT_80612628 = 1;
        }
        if (DAT_80612614 == 9) {
            DAT_80612628 = 1;
        }
    }
    FUN_80382730();  // spill-frame close (dead PPC ABI plumbing)
    return;
}

// 0x801c5390 - per-frame apply gate: only apply zone visibility when the local
// player context reports it is in a valid room (*DAT_80611d74 == 0).
void zone_apply_visibility_gate(void)
{
    if (*DAT_80611d74 == 0) {
        zone_apply_visibility();
    }
    return;
}

// 0x801c53c0 - reset all secondary rooms' visibility mask to "show all" (0xff).
// Room 0 is skipped; used as a pre-pass before the per-zone apply.
void zone_reset_room_visibility(void)
{
    int *roomPtr;
    int off;
    int r;

    if (DAT_80612628 == 0) {
        return;
    }
    off = 4;
    for (r = 1; r < DAT_80611b14; r = r + 1) {
        roomPtr = (int *)(DAT_80611b04 + off);
        off = off + 4;
        *(int *)(*(int *)(*roomPtr + 4) + 0x30) = 0xff;
    }
    return;
}

// 0x801c5404 - per-frame: resolve the local player's current zone, cache it,
// and write every room's visibility mask (+0x30) from that zone's adjacency
// row. 0xff means "show all rooms". Modes 4/9/0xb/0x12 layer extra portal
// blocking and per-map special cases (mode 0xb flight sequence, mode 9 sub
// hangar, etc.) on top of the base adjacency application.
void zone_apply_visibility(void)
{
    bool isFlightZone;
    int obj;
    int listIdx;
    unsigned int *entry;
    int flagObj;
    int *rec;
    unsigned int slot;
    int zone;
    int flwlObjs[8];

    if (DAT_80612628 == 0) {
        return;
    }

    if (0 < DAT_806108e4) {
        DAT_80610d10 = *(unsigned int *)(DAT_80611ad8 + *(int *)(DAT_80611d54 + 0x2a0) * 4);
    }
    if (DAT_80611d74 != (int *)0x0) {
        DAT_80611d74[0x4f8] = (int)DAT_80610d10;
    }
    *(unsigned int *)(DAT_80611d54 + 0x2c0) = DAT_80610d10;

    if (DAT_80610d10 == 0xff) {
        // "show all": mark every room in every zone visible.
        obj = 0;
        for (zone = 0; zone < DAT_80611ae4; zone = zone + 1) {
            listIdx = 0;
            for (slot = 0; slot < *(unsigned int *)(obj + DAT_8061262c); slot = slot + 1) {
                rec = (int *)(((unsigned int *)(obj + DAT_8061262c))[1] + listIdx);
                listIdx = listIdx + 8;
                *(int *)(*(int *)(*(int *)(DAT_80611b04 + *rec * 4) + 4) + 0x30) = 0xff;
            }
            obj = obj + 8;
        }
        if (DAT_80612614 == 4) {
            *(int *)(*(int *)(*(int *)(DAT_80611b04 + 0x148) + 4) + 0x30) = 3;
        }
    } else if (-1 < (int)DAT_80610d10) {
        for (zone = 0; zone < DAT_80611ae4; zone = zone + 1) {
            listIdx = 0;
            int rowBase = (int)DAT_80610d10 * DAT_80611ae4;
            for (slot = 0; entry = (unsigned int *)((zone + rowBase) * 8 + DAT_8061262c),
                           slot < *entry; slot = slot + 1) {
                rec = (int *)(entry[1] + listIdx);
                listIdx = listIdx + 8;
                *(int *)(*(int *)(*(int *)(DAT_80611b04 + *rec * 4) + 4) + 0x30) = rec[1];
            }
        }

        // mode 0x12: OR-in extra visibility for the three door triggers when open.
        if ((DAT_80612614 == 0x12) &&
            ((zone_trigger_force_zone(0x4454534c,1,3) != 0 ||
              zone_trigger_force_zone(0x44545348,0,3) != 0) ||
             zone_trigger_force_zone(0x47424444,0,4) != 0)) {
            for (zone = 0; zone < DAT_80611ae4; zone = zone + 1) {
                listIdx = 0;
                int rowBase = (int)DAT_80610d10 * DAT_80611ae4;
                for (slot = 0; entry = (unsigned int *)((zone + rowBase) * 8 + DAT_8061262c),
                               slot < *entry; slot = slot + 1) {
                    rec = (int *)(entry[1] + listIdx);
                    listIdx = listIdx + 8;
                    flagObj = *(int *)(*(int *)(DAT_80611b04 + *rec * 4) + 4);
                    *(unsigned int *)(flagObj + 0x30) =
                        *(unsigned int *)(flagObj + 0x30) | (unsigned int)rec[1];
                }
            }
        }

        if (DAT_80612614 == 4) {
            if (((DAT_80610d10 < 2) || (DAT_80610d10 == 0xc)) ||
                ((DAT_80610d10 - 6 < 2) || (DAT_80610d10 == 9))) {
                scene_portal_block(0x52); scene_portal_block(0xa7); scene_portal_block(0xa6);
                scene_portal_block(0xa9); scene_portal_block(0x53); scene_portal_block(0x3e);
            } else {
                scene_portal_unblock(0x52); scene_portal_unblock(0xa7); scene_portal_unblock(0xa6);
                scene_portal_unblock(0xa9); scene_portal_unblock(0x53); scene_portal_unblock(0x3e);
            }
        }
        if (DAT_80612614 == 9) {
            if (DAT_80611d74[0x37b] == 0x36) {
                scene_portal_block(0x4f); scene_portal_block(0x5d);
                scene_portal_block(0x5f); scene_portal_block(0x50);
            } else {
                scene_portal_unblock(0x4f); scene_portal_unblock(0x5d);
                scene_portal_unblock(0x5f); scene_portal_unblock(0x50);
            }
            if ((DAT_80610d10 == 2) || (DAT_80610d10 == 10)) {
                scene_portal_block(0x4c);
            } else {
                scene_portal_unblock(0x4c);
            }
        }
        if (DAT_80612614 == 0xb) {
            // mode 0xb (flight level): the "PTLC" control object gates a set of
            // flight portals by camera height / the per-player last zone.
            obj = FUN_8023c32c(0x4c505452);
            if ((DAT_80610d10 == 0xffffffff) ||
                ((DAT_80610d10 != 0x14) &&
                 (FLOAT_80616ec8 < ABS(*(float *)(DAT_80611d54 + 0x284))))) {
                *(int *)(*(int *)(obj + 4) + 0x30) = ~(1 << (&DAT_80612620)[*DAT_80611d74]);
            } else if ((DAT_80610d10 != 0xff) && (DAT_80611d74 != (int *)0x0)) {
                int flwlCount = FUN_8023c59c(0x464c574c, (int)flwlObjs, 6);
                int *pctx = DAT_80611d74;
                isFlightZone = (DAT_80610d10 == 0x14);
                (&DAT_80612620)[*DAT_80611d74] = (int)DAT_80610d10;
                if (isFlightZone) {
                    int flwgObj = FUN_8023c32c(0x464c5747);
                    *(int *)(*(int *)(obj + 4) + 0x30) = 0x1ee0;
                    if (flwgObj != 0) {
                        if (DAT_80611d74[0x37b] == 0x61) {
                            int i = 0;
                            *(unsigned int *)(flwgObj + 0x28) =
                                *(unsigned int *)(flwgObj + 0x28) | 1;
                            *(int *)(flwgObj + 0x34) = 0xd5;
                            for (; 0 < flwlCount; flwlCount = flwlCount + -1) {
                                *(float *)(*(int *)((int)flwlObjs + i) + 100) =
                                    *(float *)(obj + 100) +
                                    *(float *)(*(int *)((int)flwlObjs + i) + 0xa8);
                                *(unsigned int *)(*(int *)((int)flwlObjs + i) + 0x28) =
                                    *(unsigned int *)(*(int *)((int)flwlObjs + i) + 0x28) | 0x40000001;
                                pctx = (int *)((int)flwlObjs + i);
                                i = i + 4;
                                *(int *)(*pctx + 0x34) = 0xd5;
                            }
                        } else {
                            int i = 0;
                            for (; 0 < flwlCount; flwlCount = flwlCount + -1) {
                                pctx = (int *)((int)flwlObjs + i);
                                i = i + 4;
                                *(unsigned int *)(*pctx + 0x28) =
                                    *(unsigned int *)(*pctx + 0x28) & 0xfffffffe;
                            }
                        }
                    }
                } else {
                    if (*pctx == 0) {
                        *(int *)(*(int *)(obj + 4) + 0x30) = 0xffffffff;
                    }
                    int i = 0;
                    for (; 0 < flwlCount; flwlCount = flwlCount + -1) {
                        pctx = (int *)((int)flwlObjs + i);
                        i = i + 4;
                        *(unsigned int *)(*pctx + 0x28) =
                            *(unsigned int *)(*pctx + 0x28) & 0xfffffffe;
                    }
                    flagObj = *(int *)(obj + 4);
                    *(unsigned int *)(flagObj + 0x30) =
                        *(unsigned int *)(flagObj + 0x30) & ~(1 << DAT_80610d10);
                    if (DAT_80610d10 != 0x13) {
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & ~(1 << (DAT_80610d10 + 1));
                    }
                    if (DAT_80610d10 != 0) {
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & ~(1 << (DAT_80610d10 - 1));
                    }
                    if (DAT_80610d10 == 1) {
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & 0xfffffff7;
                    }
                    if (DAT_80610d10 == 5) {
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & 0xffffff7f;
                    }
                    if (DAT_80610d10 == 7) {
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & 0xfffffdff;
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & 0xffffffdf;
                    }
                    if (DAT_80610d10 == 3) {
                        *(unsigned int *)(flagObj + 0x30) =
                            *(unsigned int *)(flagObj + 0x30) & 0xfffffffd;
                    }
                }
            }
        }
    }
    return;
}

// 0x801c5a44 - trigger-gated zone override. Resolves the trigger entity by
// fourcc; if it exists and is not fully open, swaps the current zone id
// (DAT_80610d10) between `zoneA` and `zoneB` and returns 1. Returns 0 when the
// trigger is open/absent (no override). Used by mode 0x12 above.
int zone_trigger_force_zone(int triggerTag, int zoneA, int zoneB)
{
    int trig;
    unsigned int open;
    int result;

    trig = (int)trigger_slot_resolve_entity(triggerTag);
    if ((trig == 0) || (open = (unsigned int)trigger_is_fully_open(trig), open != 0)) {
        result = 0;
    } else {
        int newZone = zoneB;
        if (((int)DAT_80610d10 != zoneA) &&
            (newZone = (int)DAT_80610d10, (int)DAT_80610d10 == zoneB)) {
            newZone = zoneA;
        }
        DAT_80610d10 = newZone;
        result = 1;
    }
    return result;
}
