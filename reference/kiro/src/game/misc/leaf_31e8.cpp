// Address: 0x800031e8 | Name: leaf_31e8 | Date: 2025-07-13T12:00:00Z
// Description: Memmove implementation. Copies memory with overlap handling.
//              Copies forward if dest <= src, backward otherwise.

#include "types.h"
#include "sdk_stubs.h"

void leaf_31e8(u32 param_1, u32 param_2, s32 param_3) {
    u8* puVar1;
    s32 iVar2;
    u8* puVar3;

    if (param_1 <= param_2) {
        puVar1 = (u8*)(param_2 - 1);
        puVar3 = (u8*)(param_1 - 1);
        iVar2 = param_3 + 1;
        while (TRUE) {
            iVar2 = iVar2 + -1;
            if (iVar2 == 0) break;
            puVar1 = puVar1 + 1;
            puVar3 = puVar3 + 1;
            *puVar3 = *puVar1;
        }
        return;
    }
    puVar1 = (u8*)(param_2 + param_3);
    puVar3 = (u8*)(param_1 + param_3);
    iVar2 = param_3 + 1;
    while (TRUE) {
        iVar2 = iVar2 + -1;
        if (iVar2 == 0) break;
        puVar1 = puVar1 + -1;
        puVar3 = puVar3 + -1;
        *puVar3 = *puVar1;
    }
    return;
}
