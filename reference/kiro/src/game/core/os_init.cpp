// Address: 0x800054ec | Name: func_text_54ec | Date: 2026-07-23
// Description: OS subsystem initialization. Calls three SDK init routines:
// FUN_803986b0 = likely OSInit()
// FUN_80397c78 = likely DVDInit() or similar
// FUN_80399ae4 = likely PADInit() or ARInit()

#include "common.h"

extern void FUN_803986b0(void);  // OSInit
extern void FUN_80397c78(void);  // DVDInit or similar
extern void FUN_80399ae4(void);  // PADInit or ARInit

void func_text_54ec(void) {
    FUN_803986b0();
    FUN_80397c78();
    FUN_80399ae4();
}
