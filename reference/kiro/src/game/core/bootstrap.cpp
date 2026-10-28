// Address: 0x80005240 | Name: dispatch_5240 | Date: 2026-07-23
// Description: Early system bootstrap. Called after CRT init. Sets up DOL loading,
// relocates modules, calls OS init functions, and jumps to game entry.
// References: leaf_539c, func_text_54ec, func_text_542c, func_text_51ec, leaf_5238

#include "common.h"

extern s32 DAT_80000044;
extern void* DAT_800000f4;
extern u32 DAT_800030e8;
extern s32 DAT_80000034;

extern u64 leaf_539c(void);
extern void func_text_54ec(void);
extern void func_text_542c(void);
extern void func_text_51ec(void);
extern s8 leaf_5238(void);
extern void leaf_522c(void);
extern void FUN_80395850(/* args */);
extern void FUN_80397b8c(void);
extern void FUN_80397e04(/* args */);
extern void FUN_803958e8(/* args */);
extern void FUN_803a1054(void);
extern void FUN_8016bf88(/* args */);
extern void FUN_80382d54(void);

void dispatch_5240(void) {
    leaf_539c();
    func_text_54ec();
    func_text_542c();

    DAT_80000044 = 0;

    u32 mode;
    if (DAT_800000f4 == NULL) {
        mode = DAT_800030e8;
        if (DAT_80000034 == 0) {
            // skip
        } else {
            goto has_mode;
        }
    } else {
        mode = ((u32*)DAT_800000f4)[3];
has_mode:
        if (mode == 2 || mode == 3) {
            FUN_80395850(/* relocation args */);
        } else if (mode == 4) {
            leaf_522c();
        }
    }

    // Relocate module pointers if present
    if (DAT_800000f4 != NULL) {
        // ... relocation logic ...
    }

    FUN_80397b8c();
    FUN_80397e04(/* args */);

    // Conditional debug print
    func_text_51ec();

    // Check leaf_5238 flag
    if (leaf_5238() == 1) {
        FUN_803958e8(/* args */);
    }

    FUN_803a1054();
    FUN_8016bf88(/* args */);
    FUN_80382d54();
}
