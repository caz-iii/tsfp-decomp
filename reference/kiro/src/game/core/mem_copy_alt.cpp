// Address: 0x80003238 | Name: func_text_3238 | Date: 2026-07-23
// Description: Memory copy wrapper. Calls FUN_80393874 (memcpy equivalent).
// Returns the destination pointer (param_1).

#include "common.h"

extern void FUN_80393874(s32 dst, u8 value, u32 size);

s32 func_text_3238(s32 dst, u8 value, u32 size) {
    FUN_80393874(dst, value, size);
    return dst;
}
