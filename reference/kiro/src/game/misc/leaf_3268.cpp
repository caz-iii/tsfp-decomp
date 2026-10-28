// Address: 0x80003268 | Name: leaf_3268 | Date: 2025-07-13T12:00:00Z
// Description: Simple memcpy implementation. Copies bytes from src to dest
//              one byte at a time (no overlap handling).

#include "types.h"
#include "sdk_stubs.h"

void leaf_3268(s32 param_1, s32 param_2, s32 param_3) {
    u8* puVar1;
    s32 iVar2;
    u8* puVar3;

    puVar1 = (u8*)(param_2 + -1);
    puVar3 = (u8*)(param_1 + -1);
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
