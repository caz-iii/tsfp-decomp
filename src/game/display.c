#include "types.h"

// Defined in a neighbouring (not yet split) unit's .sdata.
extern u32 g_dwDisplayAspectX;
extern u32 g_dwDisplayAspectY;
extern f32 g_flDisplayAspectScale;

void set_display_mode_4x3(void) {
    g_dwDisplayAspectX = 4;
    g_dwDisplayAspectY = 3;
    g_flDisplayAspectScale = 1.0f;
}

void set_display_mode_16x9(void) {
    g_dwDisplayAspectX = 16;
    g_dwDisplayAspectY = 9;
    g_flDisplayAspectScale = 0.75f;
}

u32 get_display_width(void) {
    return g_dwDisplayAspectX;
}

u32 get_display_height(void) {
    return g_dwDisplayAspectY;
}

u8 is_widescreen(void) {
    return g_dwDisplayAspectY == 9 && g_dwDisplayAspectX == 16;
}
