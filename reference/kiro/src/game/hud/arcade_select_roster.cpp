// Batch: batch_864 | Module: arcade-select team/class roster manager
// Address range: 0x803010d4 - 0x803015ac
//
// Small per-team roster held on the active screen context (DAT_80612be4). The
// roster header is the object returned by the hud active-screen accessors
// (FUN_803826e8 / FUN_803826f0, a {ushort* base, int index} pair). Its layout:
//   header[0..2] (ushort)  per-team member counts (team 0 / 1 / 2)
//   header[3]    (ushort)  total member count
//   +8           member records, 0xc bytes each (a 10-byte payload copied by
//                roster_copy_member; the trailing 2 bytes are the team tag @+0xf)
// Per-team caps: team0 = 0x28, team1 = 0x1e, team2 = 0x64.
//
//   roster_can_add          0x803010d4  gate: 1 if roster disabled (ctx+0x2f1c==0),
//                                        else (ctx+0x254 == 1) via branchless clz.
//   roster_copy_member      0x80301100  copy a 10-byte member record (field-by-field
//                                        to preserve the original 2+2+2+1+1+.. shape).
//   roster_clear_quad       0x80301144  zero the 4-short scratch array @ctx+0x3868.
//   roster_remove_current   0x8030116c  remove the current member: decrement its
//                                        team count + total, compact the tail down,
//                                        and re-derive counts if they desynced.
//   roster_remove_and_clear 0x80301324  remove wrapper; clear the selection slot
//                                        (ctx+0x2f78) if it referenced this index.
//   roster_add_member       0x80301370  add member at record[index] if its team is
//                                        under cap: copy the record into the tail,
//                                        bump the team count + total; returns the new
//                                        slot index or -1 (full/invalid team).
//   roster_add_via_screen   0x803014ac  add wrapper over the active screen ctx, then
//                                        run roster_remove_current on success.
//   roster_layout_resolve   0x80301500  layout-0x22 index resolve: 1 for id 0x47,
//                                        0 for id 0x46, else -1.
//   roster_take_pending     0x80301568  read+consume the pending-action bitfield
//                                        (ctx+0x2f4c): return 2 when none/negative,
//                                        else the value and reset it to -1.
//   roster_get_field_2f54   0x803015ac  accessor: ctx+0x2f54 for arg 0, else 0.
//
// Named helpers (avanim_layout_is_0x22) resolve through the shared headers; the raw
// hud active-screen ctx / finalize FUN_ symbols get per-file externs. Function
// definitions are at file scope (C++ linkage) to match generated_decls.h.
//
// NON-MATCHING: countLeadingZeros(1 - x) >> 5 branchless (x == 1) test preserved;
// the {ushort*,int} register-pair returns of FUN_803826e8/f0 are read via the
// ulonglong split exactly as the decompiler produced them.

#include "common.h"
#include "hud.h"

extern "C" {
// hud active-screen context accessors (return a {ushort* base, int index} pair)
u64  FUN_803826e8(void);
u64  FUN_803826f0(void);
void FUN_80382734(void);   // hud event-list finalize
void FUN_8038273c(void);   // hud screen finalize

extern int DAT_80612be4;   // active screen context base
} // extern "C" — definitions below use C++ linkage to match generated_decls.h

// ---------------------------------------------------------------------------
// 0x803010d4  roster_can_add
// ---------------------------------------------------------------------------
unsigned int roster_can_add(void)
{
    if (*(int*)(DAT_80612be4 + 0x2f1c) == 0) {
        return 1;
    }
    return countLeadingZeros(1 - *(int*)(DAT_80612be4 + 0x254)) >> 5;
}

// ---------------------------------------------------------------------------
// 0x80301100  roster_copy_member
// Copy a 10-byte member record field-by-field (2+2+2+1+1+2+1+1 shape).
// ---------------------------------------------------------------------------
void roster_copy_member(unsigned short* pDst, unsigned short* pSrc)
{
    unsigned short u3 = pSrc[1];
    pDst[0] = pSrc[0];
    unsigned short u4 = pSrc[2];
    pDst[1] = u3;
    unsigned char b1 = *(unsigned char*)(pSrc + 3);
    pDst[2] = u4;
    unsigned char b2 = *(unsigned char*)((int)pSrc + 7);
    *(unsigned char*)(pDst + 3) = b1;
    u3 = pSrc[5];
    *(unsigned char*)((int)pDst + 7) = b2;
    b1 = *(unsigned char*)(pSrc + 4);
    pDst[5] = u3;
    b2 = *(unsigned char*)((int)pSrc + 9);
    *(unsigned char*)(pDst + 4) = b1;
    *(unsigned char*)((int)pDst + 9) = b2;
}

// ---------------------------------------------------------------------------
// 0x80301144  roster_clear_quad
// Zero the 4-short scratch array at ctx+0x3868.
// ---------------------------------------------------------------------------
void roster_clear_quad(void)
{
    *(unsigned short*)(DAT_80612be4 + 0x3868) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x386a) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x386c) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x386e) = 0;
}

// ---------------------------------------------------------------------------
// 0x8030116c  roster_remove_current
// Remove the current member: decrement its team count + total, compact the tail,
// and re-derive the per-team counts if the total desynced.
// ---------------------------------------------------------------------------
void roster_remove_current(void)
{
    unsigned short local_28, local_24, local_20;

    u64 uVar7 = FUN_803826e8();
    int iVar3 = (int)((unsigned long long)uVar7 >> 0x20);
    unsigned short* puVar2 = (unsigned short*)(u32)uVar7;
    int iVar6 = iVar3 * 0xc;
    unsigned char bVar1 = *(unsigned char*)((int)puVar2 + iVar6 + 0xf);
    if (bVar1 == 1) {
        puVar2[1] = puVar2[1] - 1;
    } else if (bVar1 == 0) {
        puVar2[0] = puVar2[0] - 1;
    } else {
        if (2 < bVar1) goto done;
        puVar2[2] = puVar2[2] - 1;
    }
    {
        int iVar5 = iVar3 * 0xc;
        puVar2[3] = puVar2[3] - 1;
        for (int iVar4 = iVar3; iVar4 < (int)(unsigned int)puVar2[3]; iVar4 = iVar4 + 1) {
            local_28 = *(unsigned short*)((int)puVar2 + iVar5 + 0x14);
            local_24 = *(unsigned short*)((int)puVar2 + iVar5 + 0x18);
            local_20 = *(unsigned short*)((int)puVar2 + iVar5 + 0x1c);
            roster_copy_member((unsigned short*)((int)puVar2 + iVar5 + 8), &local_28);
            iVar5 = iVar5 + 0xc;
        }
        if ((unsigned int)puVar2[3] !=
            (unsigned int)puVar2[0] + (unsigned int)puVar2[1] + (unsigned int)puVar2[2]) {
            puVar2[3] = 0;
            puVar2[0] = 0;
            puVar2[1] = 0;
            puVar2[2] = 0;
            for (; iVar3 < (int)(unsigned int)puVar2[3]; iVar3 = iVar3 + 1) {
                bVar1 = *(unsigned char*)((int)puVar2 + iVar6 + 0xf);
                if (bVar1 == 1) {
                    puVar2[1] = puVar2[1] + 1;
                    puVar2[3] = puVar2[3] + 1;
                } else if (bVar1 == 0) {
                    puVar2[0] = puVar2[0] + 1;
                    puVar2[3] = puVar2[3] + 1;
                } else {
                    if (2 < bVar1) break;
                    puVar2[2] = puVar2[2] + 1;
                    puVar2[3] = puVar2[3] + 1;
                }
            }
        }
    }
done:
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80301324  roster_remove_and_clear
// Remove wrapper; clear the selection slot (ctx+0x2f78) if it matched this index.
// ---------------------------------------------------------------------------
void roster_remove_and_clear(int index)
{
    roster_remove_current();
    if (*(int*)(DAT_80612be4 + 0x2f78) == index) {
        *(u32*)(DAT_80612be4 + 0x2f78) = 0xffffffff;
    }
}

// ---------------------------------------------------------------------------
// 0x80301370  roster_add_member
// Add member record[index] if its team is under cap: copy into the tail, bump
// the team count + total. Returns the new slot index, or -1 if full/invalid.
// ---------------------------------------------------------------------------
unsigned int roster_add_member(unsigned short* pHeader, int pRecords, int index)
{
    unsigned short local_18, local_14, local_10;

    int iVar6 = pRecords + index * 0xc;
    unsigned char bVar1 = *(unsigned char*)(iVar6 + 0xf);
    unsigned short cur, cap;
    if (bVar1 == 1) {
        cur = pHeader[1];
        cap = 0x1e;
    } else if (bVar1 == 0) {
        cur = pHeader[0];
        cap = 0x28;
    } else {
        if (2 < bVar1) {
            return 0xffffffff;
        }
        cur = pHeader[2];
        cap = 100;
    }
    unsigned int result;
    if (cur < cap) {
        int iVar5 = pRecords + index * 0xc;
        local_18 = *(unsigned short*)(iVar5 + 8);
        local_14 = *(unsigned short*)(iVar5 + 0xc);
        local_10 = *(unsigned short*)(iVar5 + 0x10);
        roster_copy_member(pHeader + (unsigned int)pHeader[3] * 6 + 4, &local_18);
        bVar1 = *(unsigned char*)(iVar6 + 0xf);
        if (bVar1 == 1) {
            pHeader[1] = pHeader[1] + 1;
        } else if (bVar1 == 0) {
            pHeader[0] = pHeader[0] + 1;
        } else {
            if (2 < bVar1) {
                return 0xffffffff;
            }
            pHeader[2] = pHeader[2] + 1;
        }
        result = (unsigned int)pHeader[3];
        pHeader[3] = pHeader[3] + 1;
    } else {
        result = 0xffffffff;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x803014ac  roster_add_via_screen
// Add wrapper over the active screen ctx; on success also run remove_current.
// ---------------------------------------------------------------------------
void roster_add_via_screen(u32 arg1, u32 arg2, int index)
{
    (void)arg1;
    (void)arg2;
    u64 uVar2 = FUN_803826f0();
    unsigned int uVar1 = roster_add_member(
        (unsigned short*)(u32)((unsigned long long)uVar2 >> 0x20), (int)(u32)uVar2, index);
    if (-1 < (int)uVar1) {
        roster_remove_current();
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80301500  roster_layout_resolve
// Layout-0x22 index resolve: 1 for id 0x47, 0 for id 0x46, else -1.
// ---------------------------------------------------------------------------
u32 roster_layout_resolve(int index)
{
    unsigned int uVar1 = avanim_layout_is_0x22(index);
    if (uVar1 == 0) {
        return 0xffffffff;
    }
    if (index == 0x47) {
        return 1;
    }
    if ((index < 0x47) && (0x45 < index)) {
        return 0;
    }
    return 0xffffffff;
}

// ---------------------------------------------------------------------------
// 0x80301568  roster_take_pending
// Read+consume the pending-action bitfield (ctx+0x2f4c): return 2 when none or
// negative, else the value and reset it to -1.
// ---------------------------------------------------------------------------
unsigned int roster_take_pending(void)
{
    unsigned int uVar1 = *(unsigned int*)(DAT_80612be4 + 0x2f4c);
    if (((uVar1 & 2) == 0) && ((uVar1 & 1) == 0) && ((uVar1 & 4) == 0) && ((uVar1 & 8) == 0)) {
        return 2;
    }
    if ((int)uVar1 < 1) {
        return 2;
    }
    *(u32*)(DAT_80612be4 + 0x2f4c) = 0xffffffff;
    return uVar1;
}

// ---------------------------------------------------------------------------
// 0x803015ac  roster_get_field_2f54
// Accessor: ctx+0x2f54 for arg 0, else 0.
// ---------------------------------------------------------------------------
u32 roster_get_field_2f54(int arg)
{
    if (arg == 0) {
        return *(u32*)(DAT_80612be4 + 0x2f54);
    }
    if ((-1 < arg) && (arg < 4)) {
        return 0;
    }
    return 0;
}

// =============================================================================
// batch_865 (0x803015e0 - 0x80301b1c): arcade-select roster add/place/validate +
// hit-test cluster (appended). Continues the roster manager: slot<->layout maps,
// entry load into the working ctx fields, point hit-test over the member list,
// per-team balance validation, and the character-place entry point that shows a
// back-dialog on failure. Function definitions at file scope (C++ linkage).
// =============================================================================

extern "C" {
u64  __save_gpr_28(void);
void __restore_gpr_28(void);

extern short DAT_80477bca;   // layout-index table (0x62 stride), shared w/ arcsel_get_layout_desc

// hud active-screen ctx accessor (returns a {int* base, int index} pair)
u64  FUN_803826d4(void);
void FUN_80382720(void);            // hud screen finalize (place path)

int  FUN_80313af4(double x, double y, double z);   // point pre-test -> hit id / 0
int* FUN_803141f0(int a, short* key, int c);        // record lookup by key
void FUN_80342298(short* out);                      // fill a 6-byte key from cursor
void FUN_803143e0(int* a, int histOut);             // accumulate per-team histogram
unsigned int FUN_8031441c(int teamIdx);             // team label/limit lookup
void FUN_80314040(int a, int rec);                  // detach record
int* FUN_80314148(int a, void* key, unsigned int pRec); // attach record -> new record ptr
unsigned int FUN_8030f830(int size);                // slot alloc gate
void FUN_80348238(int mode);                        // mark ctx dirty (sound/anim)
void FUN_80307ec0(void);                            // roster changed refresh

extern int DAT_80612d68;    // secondary roster ctx (record store)
extern int DAT_80612c14;    // member-list head
extern int DAT_80612bf4;    // dialog owner
extern int DAT_806119ec;    // current player savegame block index
extern int DAT_80611180;    // format scratch buffer
extern int DAT_80477854;    // per-team limit table (0x18 stride, +0 int)
extern int DAT_80477860;    // per-layout callback vtable (0x18 stride)
extern int DAT_8051a834;    // per-player view origin x (0x1e0 stride)
extern int DAT_8051a838;    // per-player view origin y (0x1e0 stride)

extern float FLOAT_80612c54;
extern float FLOAT_80612c58;
extern float FLOAT_80612c74;
extern float FLOAT_80612c78;
extern u32   DAT_80612c84;
extern u32   DAT_80612c88;
extern double DOUBLE_8061a5e0;
} // extern "C" — definitions below at file scope

// ---------------------------------------------------------------------------
// 0x803015e0  roster_slot_to_layout
// Team-slot -> layout id: slot 1 -> 0x47, slot 0 -> 0x46, else -1. (inverse of
// roster_layout_resolve).
// ---------------------------------------------------------------------------
u32 roster_slot_to_layout(int slot)
{
    if (slot == 1) {
        return 0x47;
    }
    if (slot < 1) {
        if (-1 < slot) {
            return 0x46;
        }
    } else if (slot < 3) {
        return 0xffffffff;
    }
    return 0xffffffff;
}

// ---------------------------------------------------------------------------
// 0x80301624  roster_index_valid
// True if 0 <= index < the entry count at ctx+0x306e.
// ---------------------------------------------------------------------------
u32 roster_index_valid(int index)
{
    if ((-1 < index) && (index < (int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e))) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8030164c  roster_load_entry
// Load entry[index] (table @ctx+0x3077, 0xc stride) into the working ctx fields
// (0x2f34 layout / 0x2f38 count / 0x2f3c / 0x2f54 / 0x2f4c), mark dirty, then
// remove the entry from the roster.
// ---------------------------------------------------------------------------
void roster_load_entry(int index)
{
    if (roster_index_valid(index) != 0) {
        int off = index * 0xc;
        u32 layout = roster_slot_to_layout((unsigned int)*(unsigned char*)(DAT_80612be4 + off + 0x3077));
        *(u32*)(DAT_80612be4 + 0x2f34) = layout;
        *(u32*)(DAT_80612be4 + 0x2f3c) = 0;
        *(int*)(DAT_80612be4 + 0x2f38) = (int)*(short*)(DAT_80612be4 + off + 0x307a);
        *(u32*)(DAT_80612be4 + 0x2f54) = (unsigned int)*(unsigned char*)(DAT_80612be4 + off + 0x3078);
        *(u32*)(DAT_80612be4 + 0x2f4c) = (unsigned int)*(unsigned char*)(DAT_80612be4 + off + 0x3079);
        if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x20) == 0) {
            FUN_80348238(1);
            *(u32*)(DAT_80612be4 + 0x238) = *(u32*)(DAT_80612be4 + 0x238) | 0x20;
        }
        roster_remove_and_clear(index);
    }
}

// ---------------------------------------------------------------------------
// 0x80301710  roster_noop
// ---------------------------------------------------------------------------
void roster_noop(void)
{
}

// ---------------------------------------------------------------------------
// 0x80301714  roster_hit_test_point
// Hit-test the member list against a screen point (screenX in the gpr28 slot,
// y=param2). If FUN_80313af4 returns a direct hit, store it; else walk the member
// list (ctx+0x2f7c, next @+0x40) and store the first record whose on-screen bounds
// contain the point at ctx+0x2f80.
// ---------------------------------------------------------------------------
void roster_hit_test_point(u64 gpr, double y, double z)
{
    double screenX = (double)(int)__save_gpr_28();
    int hit = FUN_80313af4(screenX, y, z);
    if (hit == 0) {
        float* pfVar4;
        if (((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x800000) != 0) &&
            (pfVar4 = *(float**)(DAT_80612be4 + 0x2f7c), pfVar4 != (float*)0)) {
            double dVar1 = (double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a5e0;
            double dVar2 = (double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a5e0;
            for (; pfVar4 != (float*)0; pfVar4 = (float*)(u32)pfVar4[0x10]) {
                int rec = (int)pfVar4[(int)pfVar4[3] + 0xb];
                unsigned int sgn = (int)pfVar4[2] >> 0x1f;
                if ((int)((sgn ^ (int)pfVar4[2]) - sgn) < *(int*)(rec + 0x1c)) {
                    double dVar8 = (double)(FLOAT_80612c58 *
                        (*(float*)(&DAT_8051a834 + DAT_806119ec * 0x1e0) - (float)dVar1) +
                        FLOAT_80612c78 + pfVar4[0]);
                    double bx = (double)CONCAT44(0x43300000, *(u32*)(rec + 0x14) ^ 0x80000000) - DOUBLE_8061a5e0;
                    double dVar9 = (double)(FLOAT_80612c54 *
                        (*(float*)(&DAT_8051a838 + DAT_806119ec * 0x1e0) - (float)dVar2) +
                        FLOAT_80612c74 + pfVar4[1]);
                    double by = (double)CONCAT44(0x43300000, *(u32*)(rec + 0x18) ^ 0x80000000) - DOUBLE_8061a5e0;
                    if ((dVar8 < screenX) && (screenX < (double)(float)(dVar8 + (double)(float)bx)) &&
                        (dVar9 < y) && (y < (double)(float)(dVar9 + (double)(float)by))) {
                        *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) | 0x10;
                        *(float**)(DAT_80612be4 + 0x2f80) = pfVar4;
                        roster_noop();
                        goto done;
                    }
                }
            }
        }
        *(u32*)(DAT_80612be4 + 0x2f80) = 0;
    } else {
        *(int*)(DAT_80612be4 + 0x2f80) = hit;
        *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) & 0xffffffef;
        roster_noop();
    }
done:
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x803019f8  roster_resolve_picked
// Resolve the picked member (ctx+0x2f80) to its record via a cursor-key lookup.
// ---------------------------------------------------------------------------
int* roster_resolve_picked(void)
{
    u32 local_18;
    unsigned short local_14;
    u32 local_10;
    unsigned short local_c;

    FUN_80342298((short*)&local_10);
    int* piVar1;
    if (*(int*)(DAT_80612be4 + 0x2f80) == 0) {
        piVar1 = (int*)0;
    } else {
        local_18 = local_10;
        local_14 = local_c;
        piVar1 = FUN_803141f0(*(int*)(DAT_80612be4 + 0x2f80), (short*)&local_18, -1);
    }
    return piVar1;
}

// ---------------------------------------------------------------------------
// 0x80301a50  roster_refresh_thunk
// ---------------------------------------------------------------------------
void roster_refresh_thunk(void)
{
    FUN_80307ec0();
}

// ---------------------------------------------------------------------------
// 0x80301a74  roster_is_type_36_37
// True when type is 0x36 or 0x37 (branchless (type-0x36U) <= 1).
// ---------------------------------------------------------------------------
bool roster_is_type_36_37(int type)
{
    return (bool)(-1 - (1 < (unsigned int)(type - 0x36)));
}

// ---------------------------------------------------------------------------
// 0x80301a9c  roster_layout_slot_available
// For layout id `layoutId` at screen index `screenIdx`: false if the index's
// layout doesn't match or has no callback; otherwise the layout callback
// (DAT_80477860 vtable, 0x18 stride) result decides availability.
// ---------------------------------------------------------------------------
bool roster_layout_slot_available(int layoutId, int screenIdx)
{
    char cVar2;
    if ((screenIdx == -1) || (layoutId != (short)(&DAT_80477bca)[screenIdx * 0x62])) {
        cVar2 = '\x01';
    } else if (*(void**)(&DAT_80477860 + layoutId * 0x18) == (void*)0) {
        cVar2 = '\x01';
    } else {
        int iVar1 = (*(int (**)(void))(&DAT_80477860 + layoutId * 0x18))();
        cVar2 = '\x01' - (iVar1 == 0);
    }
    return (bool)cVar2;
}

// ---------------------------------------------------------------------------
// 0x80301b1c  roster_try_place_character
// Validate and place a character in the roster with per-team balance limits: build
// the current per-team histogram (member-list walk + FUN_803143e0), bias for the
// added/removed member, and reject (back-dialog id 0x95d / 0x95b + move SFX) if any
// team is over its DAT_80477854 limit or the slot alloc fails; otherwise detach the
// old record and attach the new one, refreshing on a type 0x36/0x37 result.
// ---------------------------------------------------------------------------
void roster_try_place_character(u64 p1, double p2, double p3, double p4, double p5,
                                double p6, u64 p7, u64 p8, u32 p9, u32 p10,
                                int* pSel, int p12, u32 p13, u32 p14, u32 p15, u32 p16)
{
    (void)p1; (void)p9; (void)p10; (void)p13;
    int* piVar1;
    bool bVar5 = false;
    int local_c8[50];
    u32 local_d8; unsigned short local_d4;
    u32 local_d0; unsigned short local_cc;
    u32 local_e0; unsigned short local_dc;
    u32 local_e8; unsigned short local_e4;

    u64 uVar11 = FUN_803826d4();
    int iVar4 = (int)((unsigned long long)uVar11 >> 0x20);
    local_d8 = *(u32*)(u32)uVar11;
    local_d4 = *(unsigned short*)((int)(u32)uVar11 + 4);
    int* piVar10 = (int*)0;
    int iVar8 = -1;
    int iVar7 = 0x14;
    local_d0 = local_d8;
    local_cc = local_d4;
    piVar1 = FUN_803141f0(*(int*)(DAT_80612d68 + 0x8c), (short*)&local_d8, -1);
    if (piVar1 != (int*)0) {
        iVar8 = *piVar1;
    }
    memset_wrapper((int)local_c8, 0, 0x94);
    for (int iVar6 = DAT_80612c14; iVar6 != 0; iVar6 = *(int*)(iVar6 + 0x40)) {
        FUN_803143e0(*(int**)(iVar6 + 0x3c), (int)local_c8);
    }
    local_c8[(short)(&DAT_80477bca)[*pSel * 0x62]] += 1;
    if (iVar8 != -1) {
        local_c8[(short)(&DAT_80477bca)[iVar8 * 0x62]] += -1;
    }
    iVar8 = 0;
    int iVar9 = 0;
    int iVar6b = 0;
    do {
        bVar5 = roster_layout_slot_available(iVar8, *pSel);
        if ((!bVar5) || (*(int*)((int)&DAT_80477854 + iVar9) < *(int*)((int)local_c8 + iVar6b))) {
            u32 uVar2 = FUN_8031441c(iVar8);
            iVar7 = get_string_by_id(0x95d);
            format_string_varargs((u64)0, p2, p3, p4, p5, p6, p7, p8,
                (int*)DAT_80612be4, iVar7, &DAT_80611180,
                (void*)(uintptr_t)(&DAT_80477854)[iVar8 * 6], (void*)(uintptr_t)uVar2,
                (void*)(uintptr_t)p14, p15, p16);
            hud_create_back_dialog(DAT_80612bf4, DAT_80612be4, p12);
            avanim_play_move_sound(0x1575);
            iVar7 = -1;
            break;
        }
        iVar8 = iVar8 + 1;
        iVar6b = iVar6b + 4;
        iVar9 = iVar9 + 0x18;
    } while (iVar8 < 0x25);
    if (iVar7 != -1) {
        if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x20) == 0) {
            FUN_80348238(2);
            *(u32*)(DAT_80612be4 + 0x238) = *(u32*)(DAT_80612be4 + 0x238) | 0x20;
        }
        if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x410000) == 0) {
            unsigned int uVar3 = FUN_8030f830(0x1c);
            if (uVar3 != 0) {
                local_e0 = local_d0;
                local_dc = local_cc;
                piVar1 = FUN_803141f0(iVar4, (short*)&local_e0, -1);
                if (piVar1 != (int*)0) {
                    FUN_80314040(iVar4, (int)piVar1);
                }
                local_e8 = local_d0;
                local_e4 = local_cc;
                piVar10 = FUN_80314148(iVar4, &local_e8, (unsigned int)(uintptr_t)pSel);
                if (piVar10 == (int*)0) {
                    int s = get_string_by_id(0x95b);
                    hud_create_back_dialog(DAT_80612bf4, s, 0);
                    avanim_play_move_sound(0x1575);
                }
                goto place_done;
            }
        }
        int s = get_string_by_id(0x95b);
        hud_create_back_dialog(DAT_80612bf4, s, 0);
        avanim_play_move_sound(0x1575);
    }
place_done:
    if ((piVar10 != (int*)0) && (bVar5 = roster_is_type_36_37(*piVar10), bVar5)) {
        roster_refresh_thunk();
    }
    FUN_80382720();
}
