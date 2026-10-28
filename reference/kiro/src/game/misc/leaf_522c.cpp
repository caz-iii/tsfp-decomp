// Address: 0x8000522c | Name: leaf_522c | Date: 2025-07-13T12:00:00Z
// Description: Flag setter. Sets the global flag at DAT_80613110 to 1.

#include "types.h"
#include "sdk_stubs.h"

extern u8 DAT_80613110;

void leaf_522c(void) {
    DAT_80613110 = 1;
    return;
}
