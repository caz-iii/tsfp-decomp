// =============================================================================
// anim_state_query.cpp - Animation-state slot queries + bone-matrix ring cursor
// Address range: 0x80025830 - 0x80025950
// Batch: 392 | Date: 2026-09-05
//
// Small read-only accessors over a model's animation-state block and the
// per-frame bone-matrix ring buffer, continuing the anim pipeline
// (batches 385-391). The state block holds two blend slots: the primary clip at
// +0xcc and the secondary/blend clip at +0x124 (weight at +400/0x190).
//
//   * anim_bone_matrix_ring_pop    (0x80025830) step the ring cursor back one slot (FUN_80025830)
//   * anim_bone_matrix_current     (0x80025848) pointer to the current ring matrix (FUN_80025848)
//   * anim_active_clip_slot         (0x80025868) address of the active blend slot (FUN_80025868)
//   * anim_is_clip_active           (0x8002588c) is an anim id loaded in either slot? (FUN_8002588c)
//   * anim_get_clip_end_time_of     (0x800258b8) clip end-time via the primary descriptor (FUN_800258b8)
//   * anim_slot_is_primary_clip     (0x800258d4) does the anim id match the primary slot? (FUN_800258d4)
//   * anim_slot_is_secondary_clip   (0x800258ec) does the anim id match the secondary slot? (FUN_800258ec)
//   * anim_primary_clip_userflags   (0x80025904) primary slot user flags (+0x11c) if it matches (FUN_80025904)
//   * anim_active_slot_userflags    (0x80025924) user flags of whichever slot is active (FUN_80025924)
//   * anim_clip_has_flags           (0x80025950) do either slot's flag tables have the masks? (FUN_80025950)
//
// Compile-clean non-matching reconstruction. countLeadingZeros(x)>>5 is the
// original branchless (x == 0) equality test, preserved as-is.
// =============================================================================
#include "common.h"
#include "generated_decls.h"

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern int    DAT_806117d0;    // bone-matrix ring write cursor
extern int    DAT_806117d4;    // bone-matrix ring half-flag
extern uint   DAT_804d34d8;    // per-anim flag table (word 0, idx*8)
extern uint   DAT_804d34dc;    // per-anim flag table (word 1, idx*8)

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0

// bone-matrix ring buffer base (0x804e52a0, seen as -0x7fb1ad60 signed)
#define BONE_MTX_RING_BASE ((int)0x804e52a0)

// ---------------------------------------------------------------------------
// anim_bone_matrix_ring_pop (0x80025830)  [FUN_80025830]
// Step the bone-matrix ring cursor back by one slot (undo the last push) and
// clear the half-flag.
// ---------------------------------------------------------------------------
extern "C" void FUN_80025830(void)
{
    DAT_806117d4 = 0;
    DAT_806117d0 = DAT_806117d0 - 1;
}

// ---------------------------------------------------------------------------
// anim_bone_matrix_current (0x80025848)  [FUN_80025848]
// Pointer to the matrix at the current ring position (cursor + half-flag).
// ---------------------------------------------------------------------------
extern "C" int FUN_80025848(void)
{
    return (DAT_806117d0 + DAT_806117d4) * 0x40 + BONE_MTX_RING_BASE;
}

// ---------------------------------------------------------------------------
// anim_active_clip_slot (0x80025868)  [FUN_80025868]
// Address of the currently active blend slot in a model's anim-state block:
// the secondary slot (+0x124) when its blend weight (+400/0x190) is above zero,
// otherwise the primary slot (+0xcc).
// ---------------------------------------------------------------------------
extern "C" int FUN_80025868(int rec)
{
    int st = *(int*)(rec + 4);
    if (FLOAT_80613930 < *(float*)(st + 400))
        return st + 0x124;
    return st + 0xcc;
}

// ---------------------------------------------------------------------------
// anim_is_clip_active (0x8002588c)  [FUN_8002588c]
// True when the given anim id is currently loaded in either the primary (+0xcc)
// or secondary (+0x124) blend slot.
// ---------------------------------------------------------------------------
extern "C" int FUN_8002588c(int rec, int animId)
{
    int st = *(int*)(rec + 4);
    if (*(int*)(st + 0xcc) != animId && *(int*)(st + 0x124) != animId)
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// anim_get_clip_end_time_of (0x800258b8)  [FUN_800258b8]
// End-time (channel header +0x18) of the clip referenced by the primary slot's
// descriptor index (+0xd0).
// ---------------------------------------------------------------------------
extern "C" double FUN_800258b8(int rec)
{
    int st = *(int*)(rec + 4);
    int tbl = *(int*)(DAT_80611824 + *(int*)(st + 0xd0) * 4);
    return (double)*(float*)(tbl + 0x18);
}

// ---------------------------------------------------------------------------
// anim_slot_is_primary_clip (0x800258d4)  [FUN_800258d4]
// True (1) when animId equals the primary slot's clip id. Uses the original
// countLeadingZeros(diff)>>5 branchless equality test.
// ---------------------------------------------------------------------------
extern "C" unsigned int FUN_800258d4(int rec, int animId)
{
    int st = *(int*)(rec + 4);
    return countLeadingZeros(animId - *(int*)(st + 0xcc)) >> 5;
}

// ---------------------------------------------------------------------------
// anim_slot_is_secondary_clip (0x800258ec)  [FUN_800258ec]
// True (1) when animId equals the secondary slot's clip id.
// ---------------------------------------------------------------------------
extern "C" unsigned int FUN_800258ec(int rec, int animId)
{
    int st = *(int*)(rec + 4);
    return countLeadingZeros(animId - *(int*)(st + 0x124)) >> 5;
}

// ---------------------------------------------------------------------------
// anim_primary_clip_userflags (0x80025904)  [FUN_80025904]
// User flags (+0x11c) of the primary slot when its clip id matches animId,
// else 0.
// ---------------------------------------------------------------------------
extern "C" unsigned int FUN_80025904(int rec, int animId)
{
    int st = *(int*)(rec + 4);
    if (*(int*)(st + 0xcc) == animId)
        return *(unsigned int*)(st + 0x11c);
    return 0;
}

// ---------------------------------------------------------------------------
// anim_active_slot_userflags (0x80025924)  [FUN_80025924]
// User flags of whichever blend slot is active on the given state block: the
// primary slot flags (+0x11c) when no secondary clip is loaded, otherwise the
// secondary slot flags (+0x174).
// ---------------------------------------------------------------------------
extern "C" unsigned int FUN_80025924(int st)
{
    if (st == 0)
        return 0;
    if (*(int*)(st + 0x124) == 0)
        return *(unsigned int*)(st + 0x11c);
    return *(unsigned int*)(st + 0x174);
}

// ---------------------------------------------------------------------------
// anim_clip_has_flags (0x80025950)  [FUN_80025950]
// Test whether either blend slot's clip has any of the requested flag bits.
// Checks the primary slot's descriptor (+0xd0) flag words (DAT_804d34d8/dc)
// against maskLo/maskHi; if neither matches, falls through to the secondary
// slot (+0x128). Returns 1 if any match, else 0.
// ---------------------------------------------------------------------------
extern "C" unsigned int FUN_80025950(int rec, int unused, unsigned int maskLo, unsigned int maskHi)
{
    (void)unused;
    int st = *(int*)(rec + 4);
    int idx = *(int*)(st + 0xd0);
    if ((*(int*)(DAT_80611824 + idx * 4) == 0) ||
        ((maskHi & *(unsigned int*)(&DAT_804d34dc + idx * 8)) == 0 &&
         (maskLo & *(unsigned int*)(&DAT_804d34d8 + idx * 8)) == 0)) {
        idx = *(int*)(st + 0x128);
        if (*(int*)(DAT_80611824 + idx * 4) == 0)
            return 0;
        if ((maskHi & *(unsigned int*)(&DAT_804d34dc + idx * 8)) == 0 &&
            (maskLo & *(unsigned int*)(&DAT_804d34d8 + idx * 8)) == 0)
            return 0;
    }
    return 1;
}
