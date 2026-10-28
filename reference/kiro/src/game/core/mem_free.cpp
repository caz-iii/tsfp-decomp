// Address: 0x80006ee4 | Name: mem_free_top | Date: 2026-07-16T00:00:00Z
// Description: Frees a top-allocated memory block (48 xrefs). Validates the LOCL/MEML
//              magic header at (ptr - 0x10). Asserts that the block is at the current
//              top-of-heap position (i.e., only the most recent top-alloc can be freed).
//              Restores the heap top pointer and free space counter.

#include "types.h"
#include "sdk_stubs.h"
#include "forward_decls.h"

extern u32* DAT_806115d0;
extern s32 DAT_806115cc;

extern "C" void FUN_80181a70(void);

void mem_free_top(s32 param_1) {
    // Validate magic: 'LOCL' at offset -0x10, 'MEML' at offset -0x0C
    if (*(s32 *)(param_1 + -0x10) != 0x41434f4c ||
        *(s32 *)(param_1 + -0xc) != 0x4d454d4c) {
        FUN_80181a70(); // panic: invalid header
    }
    // Validate self-pointer: must match current heap top
    if (*(s32 *)(param_1 + -4) != (s32)DAT_806115d0) {
        FUN_80181a70(); // panic: not the top-most allocation
    }
    // Restore heap top and free space
    DAT_806115d0 = (u32*)((s32)DAT_806115d0 + *(s32 *)(param_1 + -8) + 0x10);
    DAT_806115cc = DAT_806115cc + *(s32 *)(param_1 + -8) + 0x10;
    return;
}
