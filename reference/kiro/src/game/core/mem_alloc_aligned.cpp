// Address: 0x80006da4 | Name: mem_alloc_aligned | Date: 2026-07-16T00:00:00Z
// Description: Aligned memory allocation wrapper (46 xrefs). Adjusts the heap pointer
//              to satisfy alignment (param_9) before calling the core mem_alloc.
//              Handles alignment for all allocation types (0, 1, 2, 4).

#include "types.h"
#include "sdk_stubs.h"
#include "forward_decls.h"

extern s32 DAT_806115b8;
extern s32 DAT_806115c0;
extern s32 DAT_806115c4;
extern s32 DAT_806115cc;
extern u32* DAT_806115d0;
extern s32 DAT_806115e0;

extern "C" s32 mem_alloc(s32 size, s32 alloc_type);

void mem_alloc_aligned(s32 alignment, s32 size, s32 alloc_type) {
    s32 iVar2;

    if (alloc_type == 2) {
        iVar2 = (~((u32)(alignment - 1)) & (u32)(alignment + DAT_806115b8) - 1) - DAT_806115b8;
        DAT_806115c0 = DAT_806115c0 - iVar2;
        DAT_806115b8 = DAT_806115b8 + iVar2;
    }
    else if (alloc_type < 2) {
        if (alloc_type == 0) {
            u32 uVar3 = (u32)(alignment - 1);
            u32 uVar5 = ~uVar3;
            u32 uVar4 = uVar5 & (u32)((s32)DAT_806115d0 + uVar3);
            s32 iVar6 = alignment - (s32)(uVar4 - (u32)(s32)DAT_806115d0);
            DAT_806115d0 = (u32*)((s32)DAT_806115d0 - iVar6);
            DAT_806115cc = DAT_806115cc - iVar6;
            mem_alloc(size, 0);
            *(s32 *)((s32)DAT_806115d0 + 8) = *(s32 *)((s32)DAT_806115d0 + 8) + iVar6;
            return;
        }
        if (alloc_type >= 0) {
            iVar2 = (~((u32)(alignment - 1)) & (u32)(alignment + DAT_806115c4) - 1) - DAT_806115c4;
            DAT_806115cc = DAT_806115cc - iVar2;
            DAT_806115c4 = DAT_806115c4 + iVar2;
        }
    }
    else if (alloc_type == 4) {
        DAT_806115e0 = (s32)(~((u32)(alignment - 1)) & (u32)(alignment + DAT_806115e0) - 1);
    }
    mem_alloc(size, alloc_type);
    return;
}
