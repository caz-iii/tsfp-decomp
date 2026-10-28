// =============================================================================
// HUD Element Rendering System
// TimeSplitters: Future Perfect (GameCube)
// Decompiled: 2026-08-06
// =============================================================================
// This file contains the rendering pipeline for HUD elements:
// - Scroll arrow indicators (up/down)
// - Tab-style label rendering with decorative borders
// - Render bounds computation (clip + content rects)
// - The master recursive element render function
// - Post-render overlay pass (button prompts, cursor)
// =============================================================================

#include "common.h"
#include "hud.h"
#include "text.h"
#include "gfx.h"

// External references
extern float FLOAT_80613bb0;   // 0.0f
extern float FLOAT_80613bc8;   // 1.0f
extern float FLOAT_80613bd0;   // 0.5f
extern float FLOAT_80613c00;   // border margin
extern float FLOAT_80613c0c;   // alpha scale
extern float FLOAT_80613c24;   // arrow margin offset
extern float FLOAT_80613c4c;   // tab text right padding
extern float FLOAT_80613c64;   // arrow X inset
extern float FLOAT_80613c68;   // arrow height (up)
extern float FLOAT_80613c6c;   // arrow height (down)
extern float FLOAT_80613c70;   // border color blend factor
extern float FLOAT_80613c80;   // frame sprite UV scale
extern float FLOAT_80611a64;   // screen scale Y
extern float FLOAT_80611a68;   // screen scale X
extern float FLOAT_80611a6c;   // screen offset Y
extern float FLOAT_80611a70;   // screen offset X
extern double DOUBLE_80613c78; // double subtraction constant for sprite dims
extern int* DAT_80610a20;      // sprite table base pointer
extern u32 DAT_80611748[];     // HUD layout config table
extern int DAT_806119ec;       // current viewport index
extern int DAT_806119f4;       // split-screen sprite ID (mode 1)
extern u8 DAT_8051a800[];     // split-screen sprite ID table (mode 2)
extern u8 DAT_8051a87c[];     // viewport scissor rect array

// -----------------------------------------------------------------------------
// Address: 0x80044dac | hud_draw_scroll_arrows
// Draws scroll indicator arrows (sprite 0x2BF) at the top and/or bottom
// edges of a scrollable element when content overflows in that direction.
//
// Parameters:
//   param_1 - element (unused directly, context from register)
//   param_2 - unused
//   param_3 - nonzero to draw UP arrow (content above)
//   param_4 - nonzero to draw DOWN arrow (content below)
// -----------------------------------------------------------------------------
void hud_draw_scroll_arrows(int param_1, int param_2, int param_3, int param_4) {
    // Get render context (element + clip rect)
    u64 ctx = FUN_803826dc();
    int element = (int)(ctx >> 32);
    float* clip_rect = (float*)ctx;  // [x, y, w, h]

    // Compute alpha from element opacity
    FUN_803825c0((double)(FLOAT_80613c0c * *(float*)(element + 0xF0)));

    // Get arrow sprite (0x2BF)
    int sprite_idx = FUN_8004d850(0x2BF);
    int sprite_data = *DAT_80610a20 + sprite_idx * 0x58;
    u32 sprite_w = (u32)*(u16*)(sprite_data + 0x4E) << 4;
    u32 sprite_h = (u32)*(u16*)(sprite_data + 0x52) << 4;

    // Set clip region
    FUN_8016f0e4((int)*clip_rect, (int)clip_rect[1],
                 (int)(clip_rect[2] - 1.0f), (int)(clip_rect[3] - 1.0f));

    // Compute Y positions from scroll region bounds
    float scroll_top = *(float*)(element + 0x68) * FLOAT_80611a64 + *(float*)(element + 0xA8);
    float scroll_bot = *(float*)(element + 0x6C) * FLOAT_80611a64 + *(float*)(element + 0xA8);

    int arrow_x = (int)((clip_rect[2] - FLOAT_80613c64) - FLOAT_80613c24);

    FUN_8016f00c(1);
    FUN_8016e8ac(0);
    FUN_8016e77c(sprite_idx);

    // Draw UP arrow at top of scroll region
    if (param_3 != 0) {
        int y = (int)(scroll_top + 0.0f);
        FUN_80170410(arrow_x, y, arrow_x + 14,
                     (int)(FLOAT_80613c68 + (float)y),
                     0, 0, sprite_w, sprite_h);
    }

    // Draw DOWN arrow at bottom of scroll region
    if (param_4 != 0) {
        int y = (int)(scroll_bot - (double)FLOAT_80613c24);
        FUN_80170410(arrow_x, y, arrow_x + 14,
                     (int)(scroll_bot - (double)FLOAT_80613c6c),
                     0, 0, sprite_w, sprite_h);
    }

    FUN_80382728();  // restore context
}

// -----------------------------------------------------------------------------
// Address: 0x80044f90 | hud_draw_scroll_indicators
// Checks if scroll indicators should be visible by comparing current scroll
// position and total content height against the visible region. Calls
// hud_draw_scroll_arrows with appropriate up/down flags.
//
// Parameters:
//   param_1 - element pointer
//   param_2 - clip rect (unused, forwarded)
// -----------------------------------------------------------------------------
void hud_draw_scroll_indicators(int param_1, int param_2) {
    // Don't draw if element has flag 0x100 (indicators suppressed)
    if ((*(u32*)(param_1 + 0x40) & 0x100) != 0) return;

    float scroll_pos = *(float*)(param_1 + 0x80);
    float total_height = *(float*)(param_1 + 0x58);
    float visible_top = *(float*)(param_1 + 0x68);
    float visible_bot = *(float*)(param_1 + 0x6C);
    float visible_size = visible_bot - visible_top;
    float overflow_below = (total_height - visible_size) - scroll_pos;

    BOOL show_up = (0.5f < scroll_pos);
    BOOL show_down = (0.5f < overflow_below);

    if (show_up || show_down) {
        hud_draw_scroll_arrows(param_1, param_2, (int)show_up, (int)show_down);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80045008 | hud_item_draw_tab_label
// Draws an item's tab-style label: shadowed text with surrounding decorative
// border lines forming a tab/header visual. Used for category labels in menus.
//
// Parameters:
//   param_1 - element (from register context)
//   param_2 - item (from register context)
//   param_3 - float[8] screen rect: [x0,y0,x1,y1, cx0,cy0,cx1,cy1]
//
// Rendering:
//   - Text drawn with font 0x11, shadow, color animation from hud_element_set_anim_value
//   - Case/scale applied via hud_text_apply_case_scale
//   - Alignment: left-aligned or right-aligned depending on item mode (+0x30)
//   - Border: 10 line segments forming a tab outline around the text
//   - Border uses two colors: primary line color and dimmed inner color
// -----------------------------------------------------------------------------
void hud_item_draw_tab_label(int param_1, int param_2, float* param_3) {
    // [MASSIVE RENDERING FUNCTION]
    // Get context
    u64 ctx = FUN_803826d4();
    int element = (int)(ctx >> 32);
    int item = (int)ctx;

    // Get animation colors
    u32 text_color = hud_element_set_anim_value(element, 0);
    u32 shadow_color = hud_element_set_anim_value(element, 1);

    // Prepare text with case/scale
    char* text = hud_text_apply_case_scale(0.0f, *(char**)(item + 0x20), *(u32*)(item + 0x28));

    // Compute interpolated rect positions
    float x_left = FLOAT_80613bd0 * (param_3[4] - param_3[0]) + param_3[0];
    float y_top = FLOAT_80613bd0 * (param_3[5] - param_3[1]) + param_3[1];
    float x_right = FLOAT_80613bd0 * (param_3[2] - param_3[6]) + param_3[6];
    float y_bottom = FLOAT_80613bd0 * (param_3[3] - param_3[7]) + param_3[7];

    // Set font and measure text
    text_set_font_table(0x11);
    float text_w, text_h;
    text_measure_string(&text_h, &text_w, text, 0);

    // Alignment: mode 1 = right-aligned, else left-aligned
    float text_x;
    if (*(int*)(item + 0x30) == 1) {
        text_x = param_3[6] - text_h;  // right-aligned
    } else {
        text_x = param_3[4];           // left-aligned
    }

    float text_right = FLOAT_80613c4c + text_x + text_h;

    // Draw text with shadow
    text_draw_char_shadowed(text_x, -(text_w * 0.5f - y_top), text, text_color, shadow_color);

    // Compute border colors
    u32 border_color = FUN_80132140((double)FLOAT_80613c70, text_color, 0xC08107F);
    u32 inner_color = FUN_80132348((double)FLOAT_80613c70, text_color);

    // Draw 10 border line segments forming tab outline
    int x0 = (int)(1.0f + x_left);
    int y_bot = (int)(y_bottom - 1.0f);
    int y_t = (int)(1.0f + y_top);
    int x_tl = (int)(text_x - FLOAT_80613c4c);
    int x_tr = (int)text_right;
    int x1 = (int)(x_right - 1.0f);
    int y0 = (int)y_top;
    int y1 = (int)y_bottom;

    // Primary border (outer lines)
    FUN_8016fcd0(x0, y_bot, x0, y_t, shadow_color);       // left vertical
    FUN_8016fcd0(x0, y_t, x_tl, y_t, shadow_color);       // top-left horizontal
    FUN_8016fcd0(x_tr, y_t, x1, y_t, shadow_color);       // top-right horizontal
    FUN_8016fcd0(x1, y0, x1, y1, shadow_color);            // right vertical
    FUN_8016fcd0(x1, y1, x0, y1, shadow_color);            // bottom horizontal

    // Inner highlight lines
    FUN_8016fcd0(x0, y_bot, x0, y0, inner_color);
    FUN_8016fcd0(x0, y0, x_tl, y0, inner_color);
    FUN_8016fcd0(x_tr, y0, x1, y0, inner_color);
    FUN_8016fcd0(x1, y_t, x1, y_bot, inner_color);
    FUN_8016fcd0(x1, y_bot, x0, y_bot, inner_color);

    FUN_80382720();  // restore context
}

// -----------------------------------------------------------------------------
// Address: 0x80045330 | hud_element_compute_render_bounds
// Computes two rect outputs for element rendering:
//   - Clip rect: viewport bounds (full-screen from sprite dims, or split-screen
//     from element +0xE0..+0xEC)
//   - Content rect: element position/size in pixel space
//   Also computes UV scale factors for the viewport sprite.
//
// Parameters:
//   param_1 - element (register context)
//   param_2 - output: clip rect float[4] (x, y, w, h in pixels)
//   param_3 - output: content rect float[4] (x0, y0, x1, y1 in pixels)
//   param_4 - output: UV scale float[4] (u0, v0, u1, v1)
// -----------------------------------------------------------------------------
void hud_element_compute_render_bounds(int param_1, float* clip_out, float* content_out, float* uv_out) {
    u64 ctx = FUN_803826ec();
    int element = (int)(ctx >> 32);
    float* work = (float*)ctx;

    int split_param = hud_viewport_get_split_param();

    if (split_param == 0) {
        // Full-screen mode: clip from sprite dimensions
        int sprite_idx = *(int*)(element + 0x48);
        int sprite_data = *DAT_80610a20 + sprite_idx * 0x58;
        work[0] = 0.0f;
        work[1] = 0.0f;
        work[2] = (float)((double)CONCAT44(0x43300000, (u32)*(u16*)(sprite_data + 0x4E)) - DOUBLE_80613c78);
        work[3] = (float)((double)CONCAT44(0x43300000, (u32)*(u16*)(sprite_data + 0x52)) - DOUBLE_80613c78);
    } else {
        // Split-screen: clip from element working rect
        work[0] = *(float*)(element + 0xE0) - 0.0f;
        work[1] = *(float*)(element + 0xE4) - 0.0f;
        work[2] = *(float*)(element + 0xE8) + 0.0f;
        work[3] = *(float*)(element + 0xEC) + 0.0f;
    }

    // Compute content rect in pixel space
    content_out[0] = *(float*)(element + 0x5C) * FLOAT_80611a68 + FLOAT_80611a70;
    content_out[1] = *(float*)(element + 0x60) * FLOAT_80611a64 + FLOAT_80611a6c;
    content_out[2] = FLOAT_80611a68 * (*(float*)(element + 0x5C) + *(float*)(element + 0x4C)) + FLOAT_80611a70;
    content_out[3] = FLOAT_80611a64 * (*(float*)(element + 0x60) + *(float*)(element + 0x64)) + FLOAT_80611a6c;

    // Compute UV scale from clip dimensions and border margin
    float margin = FLOAT_80613c00;
    uv_out[0] = 0.0f;
    uv_out[1] = 0.0f;
    uv_out[2] = (float)FUN_8001ca5c((double)((work[2] - work[0]) - margin), *(int*)(element + 0x48));
    uv_out[3] = (float)FUN_8001cac0((double)((work[3] - work[1]) - margin), *(int*)(element + 0x48));

    FUN_80382738();  // restore context
}

// -----------------------------------------------------------------------------
// Address: 0x800454c8 | hud_element_render
// Master render function for a HUD element. MASSIVE recursive function.
//
// Pipeline:
//   1. Push render context, set viewport index
//   2. Initialize GX render state
//   3. Skip rendering if element is in wrong split-screen layer
//   4. Set clip region from layout config
//   5. If element has items (flag 0x2) and meets size threshold:
//      a. Determine split-screen sprite ID (modes 1/2/3)
//      b. Compute render bounds (clip + content + UV)
//      c. Set up split-screen scissor if applicable
//      d. Set per-element scissor from working rect (+0xE0..+0xEC)
//      e. Draw background (gradient fill or custom callback at layout+0xC4)
//      f. Call user pre-render callback (element+0x10)
//      g. If items renderable (flag 0x2000000):
//         - For each item in linked list (+0x174):
//           * Set scissor clip
//           * Compute item screen rect
//           * If focused: draw panel frame highlight
//           * Set scissor again (for content)
//           * Type 5 items: draw tab label (or custom callback at item+0x50)
//           * Types 4/7/8: draw item text
//           * Post-item callback (layout+0xC8)
//         - Draw scrollbar (or custom at layout+0xAC, or scroll arrows)
//         - Second item pass: draw value indicators + icons for visible items
//      h. Reset clip to full layout config
//      i. Post-render callback (layout+0xCC)
//      j. If split-screen (mode 1/3): draw viewport frame sprite
//   6. Recurse for each child element (+0x168 linked list)
//   7. Restore GX state and viewport index
// -----------------------------------------------------------------------------
void hud_element_render(int param_1, int param_2, int param_3) {
    // [MASSIVE FUNCTION - see Ghidra decompilation for full 800+ lines]
    // Key structural points documented above.
    // Recursive call at end for child elements.
}

// -----------------------------------------------------------------------------
// Address: 0x80045cc0 | hud_element_render_overlay
// Post-render overlay pass. Called after main content rendering to draw:
// - Button prompts (contextual action labels)
// - Viewport cursor (when cursor mode is active)
// - Custom overlay callbacks
//
// Recursive for child elements via +0x168 linked list.
//
// Logic:
//   1. Push context, call FUN_80046944 (state setup)
//   2. Recurse into children first
//   3. Skip if wrong split-screen layer (same logic as hud_element_render)
//   4. Set clip region
//   5. Call user overlay callback (element+0x14) if present
//   6. Determine if button prompts should show:
//      - Check flag 0x80: cursor ownership mode
//      - Check flag 0x100: prompts from scrollable content
//      - Check focus/interaction state
//   7. If prompts enabled and not suppressed (flag 0x4000000):
//      - Use custom prompt renderer (element+0x164) or default hud_button_prompt_render
//   8. If viewport owner is active (cursor mode):
//      - Walk sibling chain to check if this is the last cursor-owning element
//      - If so, call viewport cursor draw callback
//   9. Restore viewport index
// -----------------------------------------------------------------------------
void hud_element_render_overlay(void) {
    u64 ctx = FUN_803826f0();
    int element = (int)(ctx >> 32);
    int render_pass = (int)ctx;

    // State setup
    FUN_80046944(render_pass);
    DAT_806119ec = *(int*)(element + 0x38);

    // Recurse children first (overlay draws on top)
    for (int child = *(int*)(element + 0x168); child != 0; child = *(int*)(child + 0x30)) {
        hud_element_render_overlay();  // recursive via register context
    }

    // Skip if wrong split-screen layer
    if ((render_pass == 1 || render_pass == 2)) {
        if (render_pass == 1 && (*(u32*)(element + 0x40) & 0xC00) != 0) goto skip;
        if (render_pass == 2 && (*(u32*)(element + 0x40) & 0xC00) == 0) goto skip;
    }

    // Set clip region
    FUN_8016f0e4(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);

    // User overlay callback
    if (*(void**)(element + 0x14) != NULL) {
        (*(void (*)(int))(*(int*)(element + 0x14)))(element);
    }

    // Determine if button prompts should render
    u32 flags = FUN_8004d234(element);
    BOOL show_prompts = FALSE;

    if ((flags & 0x100) != 0 ||
        (*(int*)(element + 0x124) != -1 &&
         (*(u32*)(element + 0x40) & 2) != 0 &&
         (*(u32*)(element + 0x44) & 1) != 0)) {

        u32 check_flags = FUN_8004d234(element);
        if ((check_flags & 0x80) == 0) {
            int interactive = FUN_80046efc();
            if (interactive != 0 || (*(u32*)(element + 0x40) & 0x200000) != 0) {
                show_prompts = TRUE;
            }
        } else {
            // Cursor ownership check
            int owner = FUN_80046df0(*(int*)(&DAT_8051a87c[DAT_806119ec * 0x1E0] + 0xC));
            show_prompts = (owner == element) ? TRUE : FALSE;
        }
    }

    // Suppress if flag set
    if ((*(u32*)(element + 0x40) & 0x4000000) != 0) {
        show_prompts = FALSE;
    }

    // Draw prompts
    if (show_prompts) {
        if (*(void**)(element + 0x164) == NULL) {
            hud_button_prompt_render();
        } else {
            (*(void (*)(int))(*(int*)(element + 0x164)))(element);
        }
    }

skip:
    // Viewport cursor overlay
    int viewport_offset = *(int*)(element + 0x38) * 0x1E0;
    if (*(int*)(&DAT_8051a87c[viewport_offset] + 0x10) != 0 &&
        (*(u32*)(element + 0x40) & 0x40000) == 0) {
        int interactive = FUN_80046f6c(element);
        if (interactive != 0) {
            // Check if this is the last cursor-owning element in sibling chain
            int sibling;
            for (sibling = *(int*)(element + 0x2C); sibling != 0; sibling = *(int*)(sibling + 0x2C)) {
                if (*(int*)(&DAT_8051a87c[*(int*)(sibling + 0x38) * 0x1E0] + 0x10) == 0 ||
                    (*(u32*)(sibling + 0x40) & 0x40000) != 0) {
                    break;
                }
            }
            if (sibling == 0) {
                // Last one: call viewport cursor draw
                void (*cursor_draw)(void) = *(void (**)(void))(&DAT_8051a87c[DAT_806119ec * 0x1E0] + 0xF4);
                if (cursor_draw != NULL) {
                    cursor_draw();
                }
            }
        }
    }

    DAT_806119ec = *(int*)(element + 0x38);
    FUN_8038273c();  // restore context
}
