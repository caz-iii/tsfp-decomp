// Address: 0x80003130 | Name: leaf_3130 | Date: 2025-07-13T12:00:00Z
// Description: Optimized memset implementation. Fills memory with a byte value,
//              using word-aligned writes for blocks >= 32 bytes.

#include "types.h"
#include "sdk_stubs.h"

void leaf_3130(s32 param_1, u8 param_2, u32 param_3) {
    u32 uVar1;
    u32* puVar2;
    u8* pbVar3;
    u32 uVar4;

    uVar4 = (u32)param_2;
    pbVar3 = (u8*)(param_1 + -1);
    if (0x1f < param_3) {
        uVar1 = ~(u32)pbVar3 & 3;
        if (uVar1 != 0) {
            param_3 = param_3 - uVar1;
            do {
                uVar1 = uVar1 - 1;
                pbVar3 = pbVar3 + 1;
                *pbVar3 = param_2;
            } while (uVar1 != 0);
        }
        if (param_2 != 0) {
            uVar4 = uVar4 | uVar4 << 8 | (u32)param_2 << 0x18 | uVar4 << 0x10;
        }
        puVar2 = (u32*)(pbVar3 + -3);
        for (uVar1 = param_3 >> 5; uVar1 != 0; uVar1 = uVar1 - 1) {
            puVar2[1] = uVar4;
            puVar2[2] = uVar4;
            puVar2[3] = uVar4;
            puVar2[4] = uVar4;
            puVar2[5] = uVar4;
            puVar2[6] = uVar4;
            puVar2[7] = uVar4;
            puVar2 = puVar2 + 8;
            *puVar2 = uVar4;
        }
        for (uVar1 = param_3 >> 2 & 7; uVar1 != 0; uVar1 = uVar1 - 1) {
            puVar2 = puVar2 + 1;
            *puVar2 = uVar4;
        }
        pbVar3 = (u8*)((s32)puVar2 + 3);
        param_3 = param_3 & 3;
    }
    if (param_3 != 0) {
        do {
            param_3 = param_3 - 1;
            pbVar3 = pbVar3 + 1;
            *pbVar3 = (u8)uVar4;
        } while (param_3 != 0);
        return;
    }
    return;
}
