// Address: 0x800055e0 | Name: func_text_55e0 | Date: 2026-07-23
// Description: Heap/arena lookup. Finds which arena a pointer belongs to by comparing
// against a table of arena descriptors at DAT_8061224c. Sets DAT_80611544 to the index.

#include "common.h"

extern s32 DAT_80612248;   // arena count
extern s32 DAT_8061224c;   // arena descriptor table base
extern s32 DAT_80611544;   // current arena index result

extern void* FUN_803826f0(void);
extern s32 FUN_8038b5c8(s32 ptr);
extern s32 FUN_8038b378(void* ptr, u32* arena);
extern void FUN_8038273c(void);

void func_text_55e0(void) {
    u32* ptr = (u32*)FUN_803826f0();
    s32 found = FUN_8038b5c8((s32)ptr);

    if (found != 0) {
        s32 offset = 4;
        for (s32 i = 1; i < DAT_80612248; i++) {
            s32 result = FUN_8038b378(ptr, *(u32**)(DAT_8061224c + offset));
            if (result == 0) {
                goto done;
            }
            offset += 4;
        }
    }

    DAT_80611544 = 0;
    found = DAT_80611544;

done:
    DAT_80611544 = found;
    FUN_8038273c();
}
