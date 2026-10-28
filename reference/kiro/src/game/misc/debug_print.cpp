// Address: 0x800051c0 | Name: func_text_51c0 | Date: 2026-07-23
// Address: 0x800051ec | Name: func_text_51ec | Date: 2026-07-23
// Description: Debug/OS print wrappers. func_text_51c0 always calls the print function.
// func_text_51ec only calls if DAT_800030e4 has specific flags set (0xEEF mask check).
// These are likely OSReport/printf wrappers gated by debug level.

#include "common.h"

extern u32 DAT_800030e4;
extern void FUN_8039dcf4(/* variadic args */);

// Unconditional debug print
void func_text_51c0(void) {
    FUN_8039dcf4(/* forwarded args */);
}

// Conditional debug print - only if debug flags 0xEEF all set
void func_text_51ec(void) {
    if ((DAT_800030e4 & 0xEEF) == 0xEEF) {
        FUN_8039dcf4(/* forwarded args */);
    }
}
