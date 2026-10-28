// Address: 0x80005238 | Name: leaf_5238 | Date: 2025-07-13T12:00:00Z
// Description: Flag getter. Returns the value of the global flag at DAT_80613110.

#include "types.h"
#include "sdk_stubs.h"

extern u8 DAT_80613110;

u8 leaf_5238(void) {
    return DAT_80613110;
}
