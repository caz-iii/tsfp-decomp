// Batch: batch_920 | Module: arcade map-editor texset/weapon-set slot record management
// Address range: 0x80328498 - 0x80328888
// Date: 2026-09-12
//
// Management of the arcade map-editor's texset/weapon-set "slot" record array. The records live in
// the editor context (DAT_80612be4) at +0x288, each 0xe4 bytes wide, with the live count at +0x280;
// a scratch/clipboard slot sits at +0x11758 and a running "next id" counter at +0x11614. These
// functions index a slot, copy a slot to/from the scratch (the editor's copy/paste), compare a slot
// against the scratch to detect edits, allocate a fresh slot (picking an unused texset id), and scan
// all slots for a type-5 (texset) entry.
//
//   aveditor_texslot_ptr              0x80328498  slot record pointer by index (base + i*0xe4 + 0x288).
//   aveditor_clear_texslot_scratch    0x803284b0  zero the 0xe4-byte scratch slot at +0x11758.
//   aveditor_copy_to_scratch          0x803284e4  copy a record into the scratch, set flag bit 0x1000.
//   aveditor_copy_current_to_scratch  0x80328524  copy the current flagged slot into the scratch.
//   aveditor_clear_scratch_and_flag   0x8032855c  clear flag bit 0x1000 and zero the scratch.
//   aveditor_scratch_differs          0x8032858c  memcmp a record against the scratch (1 = differs).
//   aveditor_current_differs_scratch  0x803285c8  memcmp the current flagged slot against the scratch.
//   aveditor_add_texslot              0x80328600  append a new slot; return its index.
//   aveditor_any_loaded_texset_slot   0x80328838  1 if any type-5 slot has a positive count (+0x58).
//   aveditor_any_texset_slot          0x80328888  1 if any slot is type 5 (+0x2bc / +700).
//
// Editor context (DAT_80612be4): +0x238 status/dirty flags (bit 0x1000 = scratch-held,
// bit 0x20 = confirm-sound latched, bit 0x40 = a slot was added), +0x280 slot count,
// +0x288 slot record array (stride 0xe4), +0x11614 next-id counter, +0x11758 scratch slot.
// Slot record: +0x34 (700) type (5 = texset), +0x58 count, +0x60 (0x2e8-0x288) texset id.
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; named cross-module helpers keep their
// real ABI. Scratch copy uses the leaf memcpy leaf_31e8; slot compare uses the memcmp FUN_80387b68.

#include "common.h"

extern "C" {

extern int DAT_80612be4;   // editor context base

// cross-module / raw helpers
extern int  memset_wrapper(int dst, unsigned char val, unsigned int n);
extern void leaf_31e8(unsigned int dst, unsigned int src, int n);          // memcpy leaf
extern int  FUN_80387b68(int a, int b, int n);                             // memcmp (0 = equal)
extern int  FUN_8038b510(int *dst, int *src);                              // set slot label from string
extern void FUN_80387fb8(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int slot, char *fmt,
                         unsigned int idx, int ctx, unsigned int r7, unsigned int r8,
                         unsigned int r9, unsigned int r10);               // formatted slot label
extern int  get_string_by_id(int id);
extern void hud_screen_mark_hidden(int screen);
extern void FUN_80348238(int screenIndex);                                 // build/register screen
extern unsigned char aveditor_get_wset_flag(void);                         // batch_919
extern void aveditor_find_loaded_texset(int slot);                         // 0x80314a84
extern unsigned int aveditor_texset_is_loaded(int id);                     // 0x80314a58
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);

// ---- 0x80328498 : slot record pointer by index --------------------------------------------------
int aveditor_texslot_ptr(unsigned int index)
{
    return DAT_80612be4 + (index & 0xff) * 0xe4 + 0x288;
}

// ---- 0x803284b0 : zero the scratch slot ---------------------------------------------------------
void aveditor_clear_texslot_scratch(void)
{
    memset_wrapper(DAT_80612be4 + 0x11758, 0, 0xe4);
}

// ---- 0x803284e4 : copy a record into the scratch, set the scratch-held flag ---------------------
void aveditor_copy_to_scratch(unsigned int record)
{
    unsigned int scratch = (unsigned int)(DAT_80612be4 + 0x11758);
    *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x1000;
    leaf_31e8(scratch, record, 0xe4);
}

// ---- 0x80328524 : copy the current flagged slot into the scratch --------------------------------
void aveditor_copy_current_to_scratch(void)
{
    unsigned char flag = aveditor_get_wset_flag();
    aveditor_copy_to_scratch((unsigned int)(DAT_80612be4 + (unsigned int)flag * 0xe4 + 0x288));
}

// ---- 0x8032855c : clear the scratch-held flag and zero the scratch ------------------------------
void aveditor_clear_scratch_and_flag(void)
{
    *(unsigned int *)(DAT_80612be4 + 0x238) &= 0xffffefff;
    aveditor_clear_texslot_scratch();
}

// ---- 0x8032858c : does a record differ from the scratch? (1 = differs) ---------------------------
bool aveditor_scratch_differs(int record)
{
    int eq = FUN_80387b68(DAT_80612be4 + 0x11758, record, 0xe4);
    return (bool)((char)1 - (char)(eq == 0));
}

// ---- 0x803285c8 : does the current flagged slot differ from the scratch? -------------------------
bool aveditor_current_differs_scratch(void)
{
    unsigned char flag = aveditor_get_wset_flag();
    return aveditor_scratch_differs(DAT_80612be4 + (unsigned int)flag * 0xe4 + 0x288);
}

// ---- 0x80328600 : append a new texset slot; return its index -------------------------------------
// r7 (kind) is passed in the high word of the __save_gpr_28 spill; when it is 5 the slot is a texset
// entry (label from string 0xb52, id resolved from the loaded texset set), otherwise it is a
// weapon-set entry (formatted label 0xb51, id = lowest free id not used by any slot and not loaded).
int aveditor_add_texslot(unsigned long long param_1, unsigned long long param_2,
                         unsigned long long param_3, unsigned long long param_4,
                         unsigned long long param_5, unsigned long long param_6,
                         unsigned long long param_7, unsigned long long param_8)
{
    unsigned int   in_r7, in_r8, in_r9, in_r10;
    unsigned long long spill;
    int  kind;
    int  slotOff;
    int  fmtSlotBias;
    bool used;
    int  scanOff;
    int  count;
    int  id;

    in_r7 = 0; in_r8 = 0; in_r9 = 0; in_r10 = 0;
    spill = __save_gpr_28();
    kind  = (int)((unsigned long long)spill >> 0x20);

    slotOff = *(int *)(DAT_80612be4 + 0x280) * 0xe4;
    *(int *)(DAT_80612be4 + 0x11614) = *(int *)(DAT_80612be4 + 0x11614) + 1;
    memset_wrapper(DAT_80612be4 + slotOff + 0x288, 0, 0xe4);

    if (kind == 5) {
        int *label = (int *)get_string_by_id(0xb52);
        FUN_8038b510((int *)(DAT_80612be4 + slotOff + 0x288), label);
    } else {
        char *fmt = (char *)get_string_by_id(0xb51);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     DAT_80612be4 + slotOff + 0x288, fmt,
                     *(unsigned int *)(DAT_80612be4 + 0x11614), DAT_80612be4,
                     in_r7, in_r8, in_r9, in_r10);
    }

    fmtSlotBias = 2;
    *(int *)(DAT_80612be4 + slotOff + 0x2a8) = 1;
    *(int *)(DAT_80612be4 + slotOff + 700)   = kind;   // +0x2bc slot type
    if (kind == 5) {
        fmtSlotBias = 1;
    }
    *(int *)(DAT_80612be4 + slotOff + 0x2cc) = fmtSlotBias;
    *(int *)(DAT_80612be4 + slotOff + 0x2d4) = 2;
    *(int *)(DAT_80612be4 + slotOff + 0x2c8) = -1;
    *(int *)(DAT_80612be4 + slotOff + 0x2e0) = 0;
    *(int *)(DAT_80612be4 + slotOff + 0x2c0) = 1;
    *(int *)(DAT_80612be4 + slotOff + 0x2c4) = 0;
    *(int *)(DAT_80612be4 + slotOff + 0x2b8) = 1;
    *(int *)(DAT_80612be4 + slotOff + 0x2d0) = 0;

    if (kind == 5) {
        aveditor_find_loaded_texset(DAT_80612be4 + slotOff + 0x288);
    } else {
        id = 0;
        do {
            used  = false;
            scanOff = 0;
            count = *(int *)(DAT_80612be4 + 0x280);
            for (; count > 0; --count) {
                if (*(int *)(DAT_80612be4 + scanOff + 0x2e8) == id) {
                    used = true;
                }
                scanOff += 0xe4;
            }
        } while ((used || aveditor_texset_is_loaded(id) != 0) && (++id, id < 0x32));
        *(int *)(DAT_80612be4 + slotOff + 0x2e8) = id;
    }

    *(int *)(DAT_80612be4 + 0x280) = *(int *)(DAT_80612be4 + 0x280) + 1;
    if ((int)spill != 0) {
        hud_screen_mark_hidden((int)spill);
    }
    *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x40;
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0x11);
        *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x20;
    }
    __restore_gpr_28();
    return *(int *)(DAT_80612be4 + 0x280);   // index of the added slot (post-increment view)
}

// ---- 0x80328838 : 1 if any type-5 slot has a positive count -------------------------------------
int aveditor_any_loaded_texset_slot(void)
{
    int off = 0;
    int count = *(int *)(DAT_80612be4 + 0x280);
    for (; count > 0; --count) {
        int slot = DAT_80612be4 + off + 0x288;
        if (*(int *)(slot + 0x34) == 5 && *(int *)(slot + 0x58) > 0) {
            return 1;
        }
        off += 0xe4;
    }
    return 0;
}

// ---- 0x80328888 : 1 if any slot is type 5 -------------------------------------------------------
int aveditor_any_texset_slot(void)
{
    int off = 0;
    int count = *(int *)(DAT_80612be4 + 0x280);
    for (; count > 0; --count) {
        if (*(int *)(DAT_80612be4 + off + 700) == 5) {
            return 1;
        }
        off += 0xe4;
    }
    return 0;
}

} // extern "C"
