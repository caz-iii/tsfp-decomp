// Address: 0x80005838 | Name: leaf_5838 | Date: 2026-07-23
// Address: 0x80005854 | Name: leaf_5854 | Date: 2026-07-23
// Address: 0x80005870 | Name: leaf_5870 | Date: 2026-07-23
// Address: 0x80005878 | Name: leaf_5878 | Date: 2026-07-23
// Address: 0x80005880 | Name: leaf_5880 | Date: 2026-07-23
// Description: Display mode configuration - resolution width/height and aspect ratio.
// DAT_8060f950 = display width setting (4=low, 16=high)
// DAT_8060f954 = display height setting (3=low, 9=high)
// FLOAT_8060f958 = aspect ratio float

#include "common.h"

extern s32 DAT_8060f950;
extern s32 DAT_8060f954;
extern f32 FLOAT_8060f958;
extern f32 FLOAT_806135e0;  // aspect ratio for 4:3
extern f32 FLOAT_806135e4;  // aspect ratio for 16:9

// Set display mode to 4:3 (standard definition)
void leaf_5838(void) {
    DAT_8060f950 = 4;
    DAT_8060f954 = 3;
    FLOAT_8060f958 = FLOAT_806135e0;
}

// Set display mode to 16:9 (widescreen)
void leaf_5854(void) {
    DAT_8060f950 = 16;
    DAT_8060f954 = 9;
    FLOAT_8060f958 = FLOAT_806135e4;
}

// Get display width setting
u32 leaf_5870(void) {
    return DAT_8060f950;
}

// Get display height setting
u32 leaf_5878(void) {
    return DAT_8060f954;
}

// Check if display is widescreen (16:9)
u32 leaf_5880(void) {
    if (DAT_8060f954 == 9 && DAT_8060f950 == 16) {
        return 1;
    }
    return 0;
}
