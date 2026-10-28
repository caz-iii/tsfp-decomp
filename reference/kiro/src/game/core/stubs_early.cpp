// Address: 0x80006028 | Name: stub_6028 | Date: 2026-07-23
// Address: 0x80006050 | Name: stub_6050 | Date: 2026-07-23
// Address: 0x80006184 | Name: stub_6184 | Date: 2026-07-23
// Address: 0x800061ac | Name: stub_61ac | Date: 2026-07-23
// Address: 0x800061b0 | Name: stub_61b0 | Date: 2026-07-23
// Address: 0x800061b4 | Name: stub_61b4 | Date: 2026-07-23
// Address: 0x800061b8 | Name: stub_61b8 | Date: 2026-07-23
// Description: Empty stub functions. These are stripped debug callbacks / unused hooks.
// Some may be populated in debug builds. init_pair_61b0_61b4 calls stub_61b0 + stub_61b4.

#include "common.h"

void stub_6028(void) { return; }
void stub_6050(void) { return; }
void stub_6184(void) { return; }
void stub_61ac(void) { return; }
void stub_61b0(void) { return; }
void stub_61b4(void) { return; }
void stub_61b8(void) { return; }

// Address: 0x80006188 | Name: init_pair_61b0_61b4 | Date: 2026-07-23
// Description: Calls stub_61b0 and stub_61b4 - likely pre/post frame buffer swap hooks.
void init_pair_61b0_61b4(void) {
    stub_61b0();
    stub_61b4();
}
