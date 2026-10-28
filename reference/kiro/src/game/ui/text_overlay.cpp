// Address: 0x8000ce84 | Module: text_overlay | Date: 2026-08-05
// HUD text overlay: formatted string drawing for on-screen messages
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern u32 DAT_806116b8;      // Active overlay button mask
extern u32 DAT_8060f9d8;      // Overlay trigger flag A
extern s32 iRam8060f9dc;      // Overlay trigger flag B
extern u32 DAT_806109c4;      // Screen width
extern u32 DAT_806109c8;      // Screen height
extern f32 FLOAT_80611674;    // Text Y offset
extern u32 DAT_80611654;      // Font rendering state flag
extern u32 DAT_8060fa10;      // Formatted string param table
extern u32 DAT_80612614;      // Game state / level mode

// External function declarations
extern void  FUN_803826e4(void);     // OSDisableInterrupts (variant)
extern void  FUN_80382730(void);     // OSRestoreInterrupts (variant)
extern int   FUN_802e0a9c(void);     // Check if overlay should be shown
extern int   input_can_show_overlay(void);
extern int   FUN_8012e924(u32 mask); // Count trailing zeros / button index
extern int   text_set_font_table(int font_id);
extern int   FUN_8016f00c(int param);
extern int   FUN_8016e8ac(int param);
extern void* FUN_8016f4bc(int x, int y, u32 w, u32 h, int color);
extern char* get_string_by_id(int string_id);
extern void  format_string_varargs(void* ctx, ...);
extern f64   text_get_char_height(u8* str);
extern void  text_draw_at_int_coords(u32* x, u32* y, u8* str, u32 color);

// 0x8000ce84 | text_draw_formatted_string
// Draws a centered, formatted text overlay on screen (e.g. button prompts during gameplay)
// Uses string ID 0x1941 as format template, centers vertically and horizontally
void text_draw_formatted_string(void) {
    u8 formatted_buf[256];

    FUN_803826e4();  // Disable interrupts

    int should_show = FUN_802e0a9c();
    if (should_show == 0) {
        DAT_806116b8 = 0;
    } else if (((DAT_806116b8 != 0) || (DAT_8060f9d8 != 0) || (iRam8060f9dc != 0)) &&
               (input_can_show_overlay() != 0)) {

        // Determine which button triggered the overlay
        int button_index = FUN_8012e924((DAT_806116b8 & ~DAT_806116b8) + 1);

        // Calculate screen center
        u32 center_x = DAT_806109c4 / 2;
        u32 center_y = DAT_806109c8 / 2;

        // Set up font and rendering state
        int prev_font = text_set_font_table(0x15);
        int y_offset = (int)FLOAT_80611674;
        int prev_mode = FUN_8016f00c(1);
        int prev_align = FUN_8016e8ac(0);
        u32 prev_flag = DAT_80611654;

        // Create rendering context covering full screen
        void* ctx = FUN_8016f4bc(0, 0, DAT_806109c4, DAT_806109c8, 0x5f);

        // Get format string and render
        char* fmt_string = get_string_by_id(0x1941);
        format_string_varargs(ctx, formatted_buf, fmt_string, &DAT_8060fa10, button_index + 1);

        DAT_80611654 = 1;

        // Center text vertically
        f64 char_height = text_get_char_height(formatted_buf);
        center_y -= (u32)((int)char_height + y_offset) / 2;

        // Draw the text
        text_draw_at_int_coords(&center_x, &center_y, formatted_buf, 0x7f7f7f7f);

        // Restore previous state
        FUN_8016e8ac(prev_align);
        FUN_8016f00c(prev_mode);
        text_set_font_table(prev_font);
        DAT_80611654 = prev_flag;
    }

    FUN_80382730();  // Restore interrupts
}
