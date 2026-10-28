// Address: 0x80005e5c | Name: leaf_5e5c | Date: 2026-07-23
// Description: Toggles a boolean flag (double-buffer swap indicator).

#include "common.h"

extern s32 DAT_80611590;

void leaf_5e5c(void) {
    DAT_80611590 = 1 - DAT_80611590;
}
