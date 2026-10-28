// =============================================================================
// HUD Button Prompt System
// TimeSplitters: Future Perfect (GameCube)
// Decompiled: 2026-08-06
// =============================================================================
// Handles the contextual button prompts that appear at the bottom of menus
// (e.g. "A: Select  B: Back  X: Delete"). Supports up to 4 simultaneous
// prompts with auto-layout across multiple lines.
// =============================================================================

#include "common.h"
#include "hud.h"
#include "text.h"

// External references
extern float FLOAT_80613bc8;   // 1.0f
extern float FLOAT_80613c0c;   // alpha scale factor
extern float FLOAT_80613c50;   // prompt Y base offset scale
extern float FLOAT_80613c54;   // prompt Y multi-line offset
extern float FLOAT_80613c58;   // prompt Y single-line offset
extern float FLOAT_80613c5c;   // prompt row height base
extern float FLOAT_80613c60;   // prompt row height multiplier
extern double DOUBLE_80613bc0; // double conversion constant
extern u32 DAT_806109c4;       // screen width in pixels
extern u32 DAT_80611748[];     // button prompt layout config table

// Layout config table (DAT_80611748) offsets:
//   [0] = X start position
//   [1] = text area width
//   [3] = frame counter for conversion
//   [5] = background color/param
//   [6] = Y extent
//   [8] = row height config

// Element button prompt structure (at element + 0x124, stride 0x10 per slot):
//   +0x00: prompt ID (-1 = inactive)
//   +0x04: icon/label string ID
//   +0x08: state (0=appearing, 1=active, 2=dismissed)
//   +0x0C: reserved

// -----------------------------------------------------------------------------
// Address: 0x800448d8 | hud_button_prompt_is_active
// Returns TRUE if the button prompt at slot index [slot] in the element's
// prompt array is active (has valid ID and is not in dismissed state).
//
// Parameters:
//   element - base pointer to HUD element
//   slot    - prompt slot index (0-3)
//
// Returns: 1 if active, 0 if inactive or dismissed
// -----------------------------------------------------------------------------
int hud_button_prompt_is_active(int element, int slot) {
    int offset = element + slot * 0x10;
    int prompt_id = *(int*)(offset + 0x124);
    int state = *(int*)(offset + 0x12C);

    if (prompt_id != -1 && state != 2) {
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x80044908 | hud_button_prompt_update_state
// Updates button prompt visibility state for the current element.
// Checks element focus/interaction state and processes prompt state transitions.
//
// Logic:
//   - If element is focused and interactive (or has specific flags):
//     Process the prompt at the current slot index
//   - State transitions based on focus ownership and parent hierarchy
//   - Uses countLeadingZeros as state dispatch (GC-specific encoding)
// -----------------------------------------------------------------------------
void hud_button_prompt_update_state(void) {
    // Get element context via paired-single register protocol
    u64 context = FUN_803826f0();
    int element = (int)(context >> 32);
    int slot_index = (int)context;

    int focused_item = *(int*)(element + 0x194);

    int is_interactive = FUN_80046f6c(element);
    if (!is_interactive &&
        (*(u32*)(element + 0x40) & 0x200000) == 0 &&
        (*(int*)(element + 0x16C) == 0 ||
         (*(u32*)(*(int*)(element + 0x16C) + 0x44) & 0x10) == 0)) {
        // Not interactive - skip
        FUN_8038273c();
        return;
    }

    int prompt_offset = slot_index * 0x10;
    int prompt_state = *(int*)(element + prompt_offset + 0x124);

    // Check conditions for state update
    if (prompt_state != 0 || focused_item == 0 ||
        (*(u32*)(focused_item + 0x28) & 1) == 0 &&
        (*(int*)(focused_item + 0x38) != 0 ||
         *(int*)(element + 0x1B8) != 0 ||
         (*(u32*)(element + 0x40) & 0x10000) == 0)) {

        if (prompt_state == 3) {
            int result = FUN_801a5040(element);
            if (result != -1) {
                FUN_8038273c();
                return;
            }
        }

        // Process state via CLZ dispatch
        // countLeadingZeros(*(u32*)(element + prompt_offset + 0x12C));
    }

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x800449f8 | hud_button_prompt_render
// Renders the button prompt bar at the bottom of a menu element.
// Supports up to 4 prompts with automatic multi-line layout when text
// would exceed screen width.
//
// Layout algorithm:
//   1. Measure each active prompt's text width
//   2. If accumulated width exceeds screen, wrap to next line
//   3. Store per-prompt X position and line number
//   4. Compute Y position: single-line uses one offset, multi-line another
//   5. Draw background bar spanning full width
//   6. Draw separator line below background
//   7. For each active prompt: draw text with color based on focus state
//
// Colors:
//   Focused prompt:  0x64646400 | alpha (bright)
//   Unfocused prompt: 0x28282800 | alpha (dim)
//   Alpha derived from element opacity at +0xF0
//
// Spacing: 0x14 pixels between prompts horizontally
// Font: table entry 0x15 (button prompt font)
// -----------------------------------------------------------------------------
void hud_button_prompt_render(void) {
    int element = FUN_803826d8();  // get render context
    char label_buffers[4][255];     // text buffer per prompt slot
    int prompt_x[4];                // X position per prompt
    int prompt_line[4];             // line number per prompt (0-based)

    // Compute base X from layout config
    int base_x = (int)(FLOAT_80613c50 *
                 (float)((double)CONCAT44(0x43300000,
                 DAT_80611748[3] ^ 0x80000000) - DOUBLE_80613bc0));

    // Set font for prompts
    text_set_font_table(0x15);

    // Draw background region
    FUN_8016f0e4(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);

    // Layout pass: measure and assign positions
    int num_lines = 1;
    int x_cursor = base_x;
    int spacing = 0x14;

    for (int i = 0; i < 4; i++) {
        int prompt_id = *(int*)(element + i * 0x10 + 0x124);
        if (prompt_id == -1) continue;

        // Build label text: icon + action name
        int label_string_id = *(int*)(element + i * 0x10 + 0x128);
        text_prepend_label(label_buffers[i], label_string_id, prompt_id);

        // Measure width
        double width = text_get_char_width(label_buffers[i]);
        int w = (int)width;

        // Check for line wrap
        if (DAT_806109c4 - 10 < x_cursor + w) {
            num_lines++;
            x_cursor = base_x;
        }

        prompt_x[i] = x_cursor;
        prompt_line[i] = num_lines - 1;
        x_cursor = w + x_cursor + spacing;
    }

    // Compute Y base position (varies by line count)
    float y_offset = (num_lines > 1) ? FLOAT_80613c54 : FLOAT_80613c58;
    u32 y_base = (u32)((FLOAT_80613bc8 - y_offset) *
                 (float)((double)CONCAT44(0x43300000,
                 DAT_80611748[8] ^ 0x80000000) - DOUBLE_80613bc0));

    // Draw background bar
    FUN_8016f00c(1);
    int bg_sprite = FUN_8004d850(0);
    FUN_8016e77c(bg_sprite);

    // Compute bar rect and draw
    FUN_801708b4(
        (double)(float)DAT_80611748[0],   // x
        (double)(float)y_base,             // y
        (double)(float)DAT_80611748[1],    // width
        (double)(float)DAT_80611748[6],    // height
        0, 0, 0x40, 0x40
    );

    // Draw separator line
    int sep_y = (int)(2.0f + (float)y_base);
    FUN_8016fee0(DAT_80611748[0], sep_y, DAT_80611748[1], sep_y, 0x7F7F7F7F);

    // Enable text rendering
    FUN_8016f00c(1);
    FUN_8016e8ac(0);

    // Render each prompt
    for (int i = 0; i < 4; i++) {
        if (!hud_button_prompt_is_active(element, i)) continue;

        // Compute Y for this prompt's line
        float row_height = (float)((double)CONCAT44(0x43300000,
                           DAT_80611748[8] ^ 0x80000000) - DOUBLE_80613bc0);
        u32 prompt_y = (u32)(FLOAT_80613c5c * row_height +
                       FLOAT_80613c60 * row_height * (float)prompt_line[i] +
                       (float)y_base);

        // Determine color: focused prompts are bright, others dim
        int is_focused = hud_button_prompt_update_state();  // checks current focus
        u32 alpha = FUN_803825c0((double)(FLOAT_80613c0c * *(float*)(element + 0xF0)));
        u32 color;
        if (is_focused) {
            color = (alpha & 0xFF) | 0x64646400;  // bright gray
        } else {
            color = (alpha & 0xFF) | 0x28282800;  // dim gray
        }

        // Draw the label
        text_draw_char((double)(float)prompt_x[i],
                       (double)(float)prompt_y,
                       label_buffers[i], color);
    }

    FUN_80382724();  // restore context
}
