// Address: 0x80003100 | Name: func_text_3100 | Date: 2026-07-23
// Description: Memory fill/clear wrapper. Calls leaf_3130 (memset equivalent).
// Returns the destination pointer (param_1).

#include "common.h"

extern void leaf_3130(s32 dst, u8 value, u32 size);

s32 func_text_3100(s32 dst, u8 value, u32 size) {
    leaf_3130(dst, value, size);
    return dst;
}
