// Address: 0x8004d788 | Name: hud_style_spacing_set_transition | Date: 2026-08-10
// Address: 0x8004d7bc | Name: hud_style_spacing_set_scroll | Date: 2026-08-10
// Address: 0x8004d7d8 | Name: hud_style_spacing_set_alignment | Date: 2026-08-10
//
// HUD Style Descriptor Builder - Spacing Descriptor Setters (continued)
// These functions configure the active spacing descriptor pointed to by g_current_spacing (DAT_80611a28).
// Part of the style definition API used by hud_theme_init_default and menu theme builders.

#include "common.h"
#include "hud_style.h"

// Global pointer to the currently-being-configured spacing descriptor
// Set by hud_style_create_spacing, used by all spacing setters
extern u8* g_current_spacing; // DAT_80611a28

// -----------------------------------------------------------------------------
// Address: 0x8004d788
// Sets transition animation parameters for the current spacing descriptor.
// Fields stored at offsets +0x60 through +0x80 in the spacing struct.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_transition(f32 speed, u32 enter_mode, u32 exit_mode,
                                      u32 enter_time, u32 exit_time, u32 delay)
{
    *(u32*)(g_current_spacing + 0x60) = enter_mode;
    *(u32*)(g_current_spacing + 0x64) = exit_mode;
    *(u32*)(g_current_spacing + 0x68) = enter_time;
    *(u32*)(g_current_spacing + 0x6C) = exit_time;
    *(u32*)(g_current_spacing + 0x70) = delay;
    *(f32*)(g_current_spacing + 0x80) = speed;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d7bc
// Sets scroll parameters for the current spacing descriptor.
// Fields stored at offsets +0x84, +0x88, +0x8C.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_scroll(f32 scroll_speed, u32 scroll_mode, u32 overflow_mode)
{
    *(u32*)(g_current_spacing + 0x84) = scroll_mode;
    *(u32*)(g_current_spacing + 0x88) = overflow_mode;
    *(f32*)(g_current_spacing + 0x8C) = scroll_speed;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d7d8
// Sets alignment/wrap properties for the current spacing descriptor.
// Fields stored at offsets +0x74, +0x78, +0x7C.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_alignment(u32 h_align, u32 v_align, u32 wrap_mode)
{
    *(u32*)(g_current_spacing + 0x74) = h_align;
    *(u32*)(g_current_spacing + 0x78) = v_align;
    *(u32*)(g_current_spacing + 0x7C) = wrap_mode;
}
