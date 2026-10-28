// Address: 0x80005d5c | Name: leaf_5d5c | Date: 2026-07-23
// Description: Array lookup - returns a u32 value from a table at DAT_804b00e0 indexed by param.

#include "common.h"

extern u32 DAT_804b00e0[];

u32 leaf_5d5c(s32 index) {
    return DAT_804b00e0[index];
}
