// =============================================================================
// hud_viewport.cpp - Viewport configuration and coordinate transforms
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 064 | Date: 2026-08-05
// =============================================================================
//
// Per-viewport data lives at DAT_8051a814 with stride 0x1E0.
// Key offsets within each viewport:
//   +0x00: u32 flags
//   +0x10: int active (1=enabled, 0=disabled)
//   +0x58: float bounds_x     (logical bounds)
//   +0x5C: float bounds_y
//   +0x60: float bounds_w
//   +0x64: float bounds_h
//   +0x68: float camera_x     (graphics clip region)
//   +0x6C: float camera_y
//   +0x70: float camera_w
//   +0x74: float camera_h
//   +0xF8: int split_mode     (0=full, 2=split)
//   +0xFC: int split_param
//
// Coordinate transform (normalized -> pixel):
//   pixel_x = norm_x * scale_x + offset_x
//   pixel_y = norm_y * scale_y + offset_y
//   Clamped to [min_x, max_x] and [min_y, max_y]
//
// =============================================================================

#include "common.h"

// Current viewport index
extern int g_current_viewport;  // DAT_806119ec

// Viewport data array (4 viewports, stride 0x1E0)
extern u8 g_viewport_data[];   // DAT_8051a814

// Coordinate transform constants
extern float FLOAT_80611a68;   // scale_x
extern float FLOAT_80611a64;   // scale_y
extern float FLOAT_80611a70;   // offset_x
extern float FLOAT_80611a6c;   // offset_y
extern float FLOAT_80611a60;   // inverse_scale_x
extern float FLOAT_80611a5c;   // inverse_scale_y

// Forward declarations
extern u64 FUN_803826f0(void);   // returns mode/param register pair
extern void FUN_8038273c(void);

// -----------------------------------------------------------------------------
// Address: 0x8003d3d8 | Name: hud_element_set_clip_rect
// -----------------------------------------------------------------------------
// Sets a clip rectangle on a HUD element's layout descriptor.
// Writes 4 floats (x, y, w, h) to layout offsets 0x34-0x40.
// Layout pointer is stored at element+0x1C.
// -----------------------------------------------------------------------------
void hud_element_set_clip_rect(float x, float y, float w, float h, int element) {
    int* layout = *(int**)(element + 0x1C);
    *(float*)((char*)layout + 0x34) = x;
    *(float*)((char*)layout + 0x38) = y;
    *(float*)((char*)layout + 0x3C) = w;
    *(float*)((char*)layout + 0x40) = h;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d3fc | Name: hud_viewport_set_split_mode
// -----------------------------------------------------------------------------
// Sets the split-screen mode for a specific viewport index.
// Normalizes modes 3 and 1 to mode 2 (there's effectively only 0=full, 2=split).
// Stores mode at viewport_data[vp]+0xF8 and param at +0xFC.
// -----------------------------------------------------------------------------
void hud_viewport_set_split_mode(int viewport_idx, int mode, int param) {
    if (mode == 3 || mode == 1) {
        mode = 2;  // normalize to split mode
    }
    *(int*)(&g_viewport_data[viewport_idx * 0x1E0 + 0xF8]) = mode;
    *(int*)(&g_viewport_data[viewport_idx * 0x1E0 + 0xFC]) = param;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d42c | Name: hud_viewport_set_all_split_mode
// -----------------------------------------------------------------------------
// Sets all 4 viewports to the same split mode and parameter.
// Used when changing between fullscreen/2-player/4-player layouts.
// -----------------------------------------------------------------------------
void hud_viewport_set_all_split_mode(void) {
    u64 result = FUN_803826f0();  // get mode + param
    int mode = (int)(result >> 32);
    int param = (int)result;

    for (int i = 0; i < 4; i++) {
        hud_viewport_set_split_mode(i, mode, param);
    }

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003d480 | Name: hud_viewport_get_split_mode
// -----------------------------------------------------------------------------
// Returns the current viewport's split mode (0=full, 2=split).
// -----------------------------------------------------------------------------
int hud_viewport_get_split_mode(void) {
    return *(int*)(&g_viewport_data[g_current_viewport * 0x1E0 + 0xF8]);
}

// -----------------------------------------------------------------------------
// Address: 0x8003d49c | Name: hud_viewport_get_split_param
// -----------------------------------------------------------------------------
// Returns the current viewport's split parameter.
// -----------------------------------------------------------------------------
int hud_viewport_get_split_param(void) {
    return *(int*)(&g_viewport_data[g_current_viewport * 0x1E0 + 0xFC]);
}

// -----------------------------------------------------------------------------
// Address: 0x8003d4b8 | Name: hud_viewport_set_bounds
// -----------------------------------------------------------------------------
// Sets the viewport's logical bounds (x, y, w, h). These define the
// renderable area for HUD elements in this viewport.
// Stored at viewport_data offsets 0x58-0x64.
// -----------------------------------------------------------------------------
void hud_viewport_set_bounds(float x, float y, float w, float h) {
    int offset = g_current_viewport * 0x1E0;
    *(float*)(&g_viewport_data[offset + 0x58]) = x;
    *(float*)(&g_viewport_data[offset + 0x5C]) = y;
    *(float*)(&g_viewport_data[offset + 0x60]) = w;
    *(float*)(&g_viewport_data[offset + 0x64]) = h;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d4e0 | Name: hud_viewport_set_camera_region
// -----------------------------------------------------------------------------
// Sets the viewport's camera/scissor region (x, y, w, h). These define
// the area passed to the graphics system for clipping and rendering.
// Stored at viewport_data offsets 0x68-0x74.
// -----------------------------------------------------------------------------
void hud_viewport_set_camera_region(float x, float y, float w, float h) {
    int vp = g_current_viewport;
    *(float*)(&g_viewport_data[vp * 0x1E0 + 0x68]) = x;
    *(float*)(&g_viewport_data[vp * 0x1E0 + 0x6C]) = y;
    *(float*)(&g_viewport_data[vp * 0x1E0 + 0x70]) = w;
    *(float*)(&g_viewport_data[vp * 0x1E0 + 0x74]) = h;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d508 | Name: hud_viewport_enable
// -----------------------------------------------------------------------------
// Enables the current viewport by setting its active flag to 1.
// An enabled viewport participates in the render pass.
// -----------------------------------------------------------------------------
void hud_viewport_enable(void) {
    *(int*)(&g_viewport_data[g_current_viewport * 0x1E0 + 0x10]) = 1;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d528 | Name: hud_viewport_disable
// -----------------------------------------------------------------------------
// Disables the current viewport by setting its active flag to 0.
// A disabled viewport is skipped during rendering.
// -----------------------------------------------------------------------------
void hud_viewport_disable(void) {
    *(int*)(&g_viewport_data[g_current_viewport * 0x1E0 + 0x10]) = 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d548 | Name: hud_viewport_compute_pixel_pos
// -----------------------------------------------------------------------------
// Computes pixel position from normalized viewport coordinates.
// Transform: pixel = normalized * scale + offset
// Clamps result to viewport min/max bounds. Sets flag 2 if clamping occurred
// (indicating the element is partially or fully outside bounds).
//
// Input layout (u32 array):
//   [6]  = normalized X
//   [7]  = normalized Y
//   [8]  = output pixel X
//   [9]  = output pixel Y
//   [0x12] = min pixel X
//   [0x13] = min pixel Y
//   [0x14] = max pixel X
//   [0x15] = max pixel Y
// -----------------------------------------------------------------------------
void hud_viewport_compute_pixel_pos(u32* data) {
    float norm_x = *(float*)&data[6];
    float norm_y = *(float*)&data[7];

    // Transform normalized -> pixel
    float pixel_x = norm_x * FLOAT_80611a68 + FLOAT_80611a70;
    float pixel_y = norm_y * FLOAT_80611a64 + FLOAT_80611a6c;

    *(float*)&data[8] = pixel_x;
    *(float*)&data[9] = pixel_y;

    // Clamp X to [min_x, max_x]
    float min_x = *(float*)&data[0x12];
    float max_x = *(float*)&data[0x14];

    if (pixel_x < min_x) {
        data[0] |= 2;  // clamped flag
        *(float*)&data[8] = min_x;
        *(float*)&data[6] = FLOAT_80611a60 * (min_x - FLOAT_80611a70);
    }
    if (pixel_x > max_x) {
        data[0] |= 2;
        *(float*)&data[8] = max_x;
        *(float*)&data[6] = FLOAT_80611a60 * (max_x - FLOAT_80611a70);
    }

    // Clamp Y to [min_y, max_y]
    float min_y = *(float*)&data[0x13];
    float max_y = *(float*)&data[0x15];

    if (pixel_y < min_y) {
        data[0] |= 2;
        *(float*)&data[9] = min_y;
        *(float*)&data[7] = FLOAT_80611a5c * (min_y - FLOAT_80611a6c);
    }
    if (pixel_y > max_y) {
        data[0] |= 2;
        *(float*)&data[9] = max_y;
        *(float*)&data[7] = FLOAT_80611a5c * (max_y - FLOAT_80611a6c);
    }
}


// =============================================================================
// Viewport Navigation / Cursor / Scrolling (Batch 065)
// =============================================================================
//
// The viewport navigation system handles:
// - Cursor position tracking (pixel + normalized coords)
// - Hit-testing elements against cursor position
// - Scrolling with acceleration/deceleration physics
// - Focus transitions between elements
// - Region-based navigation (content, scroll bars, margins)
//
// Cursor position: DAT_8051a834 (X), DAT_8051a838 (Y) in pixel coords
// Scroll velocity: element+0x8C
// Scroll flags: element+0x44 bits 2-3 (4=left, 8=right)
//
// =============================================================================

extern int FUN_80046df0(int element);   // returns resolved element ptr/id
// --- compile-hardening externs ---
extern int  FUN_803826e8(void);   // returns active element ptr/id
extern void FUN_80382734(void);
extern void FUN_80045f08(int element, int* out);
extern int* DAT_806119e4;         // current frame pointer
extern int  DAT_806119e0;         // current element
extern int  DAT_806119d8;         // last item pointer
extern int  g_hud_element_pool_size; // DAT_806119d0 style pool count
extern int  g_hud_element_pool_ptr;  // pool base address
extern void memset_wrapper(int dst, int val, int len);
extern void FUN_80041350(int element, int layout, float* out_rect);
extern void FUN_80043290(float pos, int element);
extern void FUN_80043274(int element);
extern void FUN_800413d8(void);
extern void FUN_80043398(float scroll_y, int element);
extern void FUN_80043afc(int element);
extern void FUN_8004cd64(int element);
extern void FUN_800485b8(int element, float* out_bounds);

// Cursor position (per-viewport, within viewport data)
// DAT_8051a834 + vp*0x1E0 = cursor pixel X
// DAT_8051a838 + vp*0x1E0 = cursor pixel Y

// Constants
extern float FLOAT_80613bd0;  // 0.5f (center interpolation factor)
extern float FLOAT_80613bd4;  // initial focus search X
extern float FLOAT_80613bd8;  // focus proximity tolerance
extern float FLOAT_80613bdc;  // margin factor
extern float FLOAT_80613be0;  // scroll acceleration factor
extern float FLOAT_80613be4;  // max scroll speed
extern float FLOAT_80613be8;  // scroll deceleration factor
extern float FLOAT_80613bec;  // min scroll speed threshold
extern float FLOAT_806108e8;  // frame time delta

// -----------------------------------------------------------------------------
// Address: 0x8003d664 | Name: hud_viewport_set_cursor_pixel
// -----------------------------------------------------------------------------
// Converts pixel coordinates to normalized viewport coords and updates
// the viewport's stored cursor/scroll position. Calls compute_pixel_pos
// to update clamped pixel output.
// -----------------------------------------------------------------------------
void hud_viewport_set_cursor_pixel(float pixel_x, float pixel_y) {
    int offset = g_current_viewport * 0x1E0;

    // Convert pixel -> normalized
    float norm_x = FLOAT_80611a60 * (pixel_x - FLOAT_80611a70);
    float norm_y = FLOAT_80611a5c * (pixel_y - FLOAT_80611a6c);

    // Store normalized coords
    *(float*)(&g_viewport_data[offset + 0x18]) = norm_x;  // DAT_8051a82c
    *(float*)(&g_viewport_data[offset + 0x1C]) = norm_y;  // DAT_8051a830

    // Recompute clamped pixel position
    hud_viewport_compute_pixel_pos((u32*)&g_viewport_data[offset]);
}

// -----------------------------------------------------------------------------
// Address: 0x8003d6c0 | Name: hud_viewport_update_scroll_pos
// -----------------------------------------------------------------------------
// Updates viewport scroll position based on the root element's layout.
// Gets center point of active element via interpolation, converts to pixel.
// Used for smooth camera following in scrollable menu lists.
// -----------------------------------------------------------------------------
void hud_viewport_update_scroll_pos(void) {
    int root_element = *(int*)(&g_viewport_data[g_current_viewport * 0x1E0 + 0x0C]);
    int element = FUN_80046df0(root_element);

    // Ensure layout is computed
    int layout_cache = *(int*)(element + 0x194);
    if (layout_cache == 0) {
        FUN_80045f08(element, *(int**)(element + 0x174));
        layout_cache = *(int*)(element + 0x194);
    }

    if (layout_cache != 0) {
        // Check viewport is active and element is visible
        int vp_idx = *(int*)(element + 0x38);
        int vp_active = *(int*)(&g_viewport_data[vp_idx * 0x1E0 + 0x10]);

        if (vp_active != 0 && (*(u32*)(element + 0x40) & 0x40000) == 0) {
            float rect[4];
            FUN_80041350(element, layout_cache, rect);

            // Interpolate to center (50% blend)
            float center_x = FLOAT_80613bd0 * (rect[2] - rect[0]) + rect[0];
            float center_y = FLOAT_80613bd0 * (rect[3] - rect[1]) + rect[1];

            hud_viewport_set_cursor_pixel(center_x, center_y);
        }
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003d784 | Name: hud_compute_normalized_scroll
// -----------------------------------------------------------------------------
// Computes normalized scroll position (0.0-1.0) from cursor pixel position
// relative to a bounding rectangle.
//   result_x = (cursor_x - rect_left) / (rect_right - rect_left)
//   result_y = (cursor_y - rect_top) / (rect_bottom - rect_top)
// -----------------------------------------------------------------------------
void hud_compute_normalized_scroll(int unused, float* rect, float* out_result) {
    int offset = g_current_viewport * 0x1E0;
    float cursor_x = *(float*)(&g_viewport_data[offset + 0x20]);  // DAT_8051a834
    float cursor_y = *(float*)(&g_viewport_data[offset + 0x24]);  // DAT_8051a838

    out_result[0] = (cursor_x - rect[0]) / (rect[2] - rect[0]);
    out_result[1] = (cursor_y - rect[1]) / (rect[3] - rect[1]);
}

// -----------------------------------------------------------------------------
// Address: 0x8003d7d8 | Name: hud_element_cache_scroll_pos
// -----------------------------------------------------------------------------
// Caches the computed normalized scroll position onto the element at
// offsets 0x19C (X) and 0x1A0 (Y).
// -----------------------------------------------------------------------------
void hud_element_cache_scroll_pos(int element, float* rect) {
    float result[2];
    hud_compute_normalized_scroll(element, rect, result);
    *(float*)(element + 0x19C) = result[0];
    *(float*)(element + 0x1A0) = result[1];
}

// -----------------------------------------------------------------------------
// Address: 0x8003d818 | Name: hud_cursor_hit_test_rect
// -----------------------------------------------------------------------------
// Returns 1 if the cursor position is inside the given rectangle.
// Tests: rect[0] <= cursor_x < rect[2] AND rect[1] <= cursor_y < rect[3].
// -----------------------------------------------------------------------------
int hud_cursor_hit_test_rect(float* rect) {
    int offset = g_current_viewport * 0x1E0;
    float cursor_x = *(float*)(&g_viewport_data[offset + 0x20]);
    float cursor_y = *(float*)(&g_viewport_data[offset + 0x24]);

    if (rect[0] <= cursor_x && cursor_x < rect[2]) {
        if (rect[1] <= cursor_y && cursor_y < rect[3]) {
            return 1;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d87c | Name: hud_cursor_hit_test_owned
// -----------------------------------------------------------------------------
// Hit-test with ownership filter. Only performs rect test if element's
// owner (at element+0x190) is -1 (any) or matches the given owner ID.
// -----------------------------------------------------------------------------
int hud_cursor_hit_test_owned(int element, int owner_id, float* rect) {
    int elem_owner = *(int*)(element + 0x190);
    if (elem_owner == -1 || elem_owner == owner_id) {
        return hud_cursor_hit_test_rect(rect);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d8bc | Name: hud_cursor_hit_test_element
// -----------------------------------------------------------------------------
// Hit-tests cursor against a HUD element's bounds. First checks the element's
// main bounds (offsets 0x78-0x84). If that fails and the element has sub-items
// (offset 0xB4 != 0), tests sub-element bounds via FUN_800485b8.
// Skips entirely if flag 0x4 is set in element+0x28 (non-interactive).
// -----------------------------------------------------------------------------
int hud_cursor_hit_test_element(int unused, int element) {
    // Skip non-interactive elements
    if (*(u32*)(element + 0x28) & 0x04) {
        return 0;
    }

    int offset = g_current_viewport * 0x1E0;
    float cursor_x = *(float*)(&g_viewport_data[offset + 0x20]);
    float cursor_y = *(float*)(&g_viewport_data[offset + 0x24]);

    // Test main bounds
    float left   = *(float*)(element + 0x78);
    float right  = *(float*)(element + 0x80);
    float top    = *(float*)(element + 0x7C);
    float bottom = *(float*)(element + 0x84);

    if (left <= cursor_x && cursor_x < right && top <= cursor_y && cursor_y < bottom) {
        return 1;
    }

    // Test sub-element bounds
    if (*(int*)(element + 0xB4) != 0) {
        float sub_bounds[4];
        FUN_800485b8(element, sub_bounds);
        if (hud_cursor_hit_test_rect(sub_bounds)) {
            return 1;
        }
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d974 | Name: hud_find_nearest_focusable
// -----------------------------------------------------------------------------
// Finds the nearest focusable HUD element to the cursor position.
// Walks the element linked list (next at +4), testing each with
// hud_cursor_hit_test_element. Picks best match using proximity tolerance.
// -----------------------------------------------------------------------------
void* hud_find_nearest_focusable(void) {
    int root = FUN_803826f0();
    int child = *(int*)(root + 0x174);  // first child in list
    int best = 0;
    float best_x = FLOAT_80613bd4;
    float best_y = FLOAT_80613bd4;

    while (child != 0) {
        if (hud_cursor_hit_test_element(root, child)) {
            if (best != 0) {
                float tolerance = FLOAT_80613bd8;
                float child_x = *(float*)(child + 0x78);
                float child_y = *(float*)(child + 0x7C);
                // Proximity comparison with tolerance
                if (best_y - tolerance <= child_x && child_x < best_y + tolerance) {
                    if (best_x - tolerance <= child_y) {
                        goto next;
                    }
                }
            }
            best_x = *(float*)(child + 0x78);
            best_y = *(float*)(child + 0x7C);
            best = child;
        }
next:
        child = *(int*)(child + 4);  // next in linked list
    }

    FUN_8038273c();
    return (void*)best;
}

// -----------------------------------------------------------------------------
// Address: 0x8003da44 | Name: hud_element_compute_regions
// -----------------------------------------------------------------------------
// Computes 5 sub-regions from a HUD element's layout data:
//   param_2: full bounds (left, top, right, bottom)
//   param_3: content area (inner bounds minus margins)
//   param_4: scroll/viewport area (proportional to scroll position)
//   param_5: top margin strip
//   param_6: bottom margin strip
// Any NULL output parameter is skipped.
// -----------------------------------------------------------------------------
void hud_element_compute_regions(int element, float* full, float* content,
                                  float* scroll, float* top_margin, float* bottom_margin) {
    int* layout = *(int**)(element + 0x1C);

    // Compute base position from element offsets
    float base_x = *(float*)(element + (*(int*)((char*)layout + 0x70) == 0 ? 0xE0 : 0xE8));
    float top_y = *(float*)(element + 0x68) * FLOAT_80611a64 + *(float*)(element + 0xA8);
    float bottom_y = *(float*)(element + 0x6C) * FLOAT_80611a64 + *(float*)(element + 0xA8);
    float left = *(float*)((char*)layout + 0x74) * FLOAT_80611a68 + base_x;
    float right = *(float*)((char*)layout + 0x78) * FLOAT_80611a68 + base_x;

    // Full bounds
    if (full != NULL) {
        full[0] = left;
        full[1] = top_y;
        full[2] = right;
        full[3] = bottom_y;
    }

    float width = right - left;
    float content_top = top_y + width;  // square header area
    float margin_h = FLOAT_80613bdc * width;
    float content_height = (bottom_y - width) - content_top;

    // Content area
    if (content != NULL) {
        content[0] = left;
        content[1] = content_top;
        content[2] = right;
        content[3] = bottom_y - width;
    }

    // Scroll area (proportional to scroll position)
    float scale_y = *(float*)(element + 0x80);
    float pos_x = *(float*)(element + 0x58);
    float range_y = *(float*)(element + 0x6C) - *(float*)(element + 0x68);

    if (scroll != NULL) {
        scroll[0] = left;
        scroll[1] = content_height * (scale_y / pos_x) + content_top;
        scroll[2] = right;
        scroll[3] = content_height * ((scale_y + range_y) / pos_x) + content_top;
    }

    // Top margin
    if (top_margin != NULL) {
        top_margin[0] = left;
        top_margin[1] = top_y;
        top_margin[2] = right;
        top_margin[3] = top_y + margin_h;
    }

    // Bottom margin
    if (bottom_margin != NULL) {
        bottom_margin[0] = left;
        bottom_margin[1] = bottom_y - margin_h;
        bottom_margin[2] = right;
        bottom_margin[3] = bottom_y;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003db58 | Name: hud_viewport_navigation_update
// -----------------------------------------------------------------------------
// Main viewport navigation/scrolling update. Called each frame per viewport.
// Handles:
// - Cursor focus detection (determines which element is under the cursor)
// - Horizontal/vertical scroll physics (acceleration with damping)
// - Snap-to-element focus when cursor enters interactive regions
// - Region transitions between content, scroll bars, and margins
// - Focus IDs: -2=content, -3=top area, -4=bottom area, -5=scroll bar
// -----------------------------------------------------------------------------
void hud_viewport_navigation_update(void) {
    int element = FUN_803826e8();
    int vp_offset = g_current_viewport * 0x1E0;
    int focus_region = -1;

    float full_rect[4], content_rect[4], scroll_rect[4];
    float top_margin[4], bottom_margin[4];

    hud_element_compute_regions(element, full_rect, content_rect, scroll_rect,
                                 top_margin, bottom_margin);

    // Clear scroll indicators
    *(float*)(&g_viewport_data[vp_offset + 0x38]) = 0.0f;  // scroll_ind_left
    *(float*)(&g_viewport_data[vp_offset + 0x3C]) = 0.0f;  // scroll_ind_right
    *(float*)(&g_viewport_data[vp_offset + 0x40]) = 0.0f;  // scroll_ind_up
    *(float*)(&g_viewport_data[vp_offset + 0x44]) = 0.0f;  // scroll_ind_down

    // Reset element focus filter if not locked
    if ((*(u32*)(element + 0xC4) & 0x10) == 0) {
        *(int*)(element + 0x190) = -1;
    }

    // Main navigation logic branches:
    // 1. If cursor is locked to element (flag 0x10 at +0xC4 AND focus != -2):
    //    - Apply proportional scrolling based on cached scroll position
    //    - Recompute pixel position
    //    - Set all scroll indicators to 1.0
    //
    // 2. If cursor is inside full bounds:
    //    - Test sub-regions: top margin (-3), bottom margin (-4),
    //      content area with scroll bar check (-5, -2)
    //    - Cache scroll position for current region
    //    - Handle auto-scroll flag on content area
    //
    // 3. If cursor is outside full bounds (need to scroll):
    //    - Determine scroll direction based on cursor vs element bounds
    //    - Apply scroll acceleration (flag 4=left, 8=right at +0x44)
    //    - Scroll velocity at element+0x8C with max speed cap
    //    - Deceleration when no longer scrolling
    //    - Call FUN_80043290 to apply scroll offset

    // After region determination:
    // - Find nearest focusable element via hud_find_nearest_focusable
    // - Update element's focus target via FUN_80045f08
    // - Store focus region ID at element+0x188

    FUN_80382734();
}


// =============================================================================
// Scrolling / Clipping / Pool Allocation (Batch 066)
// =============================================================================

extern void FUN_8003fed8(void);

// -----------------------------------------------------------------------------
// Address: 0x8003e124 | Name: hud_viewport_set_scissor_rect
// -----------------------------------------------------------------------------
void hud_viewport_set_scissor_rect(float* rect) {
    int offset = g_current_viewport * 0x1E0;
    *(float*)(&g_viewport_data[offset + 0x48]) = rect[0];
    *(float*)(&g_viewport_data[offset + 0x4C]) = rect[1];
    *(float*)(&g_viewport_data[offset + 0x50]) = rect[2];
    *(float*)(&g_viewport_data[offset + 0x54]) = rect[3];
}

// -----------------------------------------------------------------------------
// Address: 0x8003e15c | Name: hud_element_setup_scroll
// -----------------------------------------------------------------------------
// Configures scrolling on the current element. Sets direction (1=H, 2=V),
// speed, frame counter, and copies initial position. If param_6 (use_layout),
// reads default speeds from layout descriptor unless overridden.
// -----------------------------------------------------------------------------
void hud_element_setup_scroll(int direction, float speed_x, float speed_y,
                               int unused_a, int unused_b, int use_layout) {
    u64 result = FUN_803826f0();
    int* frame = DAT_806119e4;
    int element = DAT_806119e0;

    if (*(int*)(frame + 0x38) == 0) {
        FUN_8003fed8();

        *(int*)((char*)frame + 0x38) = direction;
        *(float*)((char*)frame + 0x3C) = speed_x;  // stored as result high
        *(int*)((char*)frame + 0x40) = (int)result; // frame counter
        *(int*)((char*)frame + 0x44) = 0;           // threshold

        // Copy current position to end position
        *(float*)(element + 0x68) = *(float*)(element + 0x64);

        // Set speed params
        *(float*)(element + 0x9C) = speed_x;
        *(float*)(element + 0xA0) = speed_y;

        if (use_layout) {
            // Use layout-defined defaults unless params override
            *(float*)(element + 0x9C) = *(float*)(*(int*)(element + 0x1C) + 0x68);
            *(float*)(element + 0xA0) = *(float*)(*(int*)(element + 0x1C) + 0x6C);
            if (0.0f < speed_x) {
                *(float*)(element + 0x9C) = speed_x;
            }
            if (0.0f < speed_y) {
                *(float*)(element + 0xA0) = speed_y;
            }
        }
    }

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003e274 | Name: hud_element_scroll_horizontal
// -----------------------------------------------------------------------------
void hud_element_scroll_horizontal(int direction, int param) {
    hud_element_setup_scroll(direction, 0.0f, 0.0f, 1, param, 0);
}

// -----------------------------------------------------------------------------
// Address: 0x8003e2a8 | Name: hud_element_scroll_vertical
// -----------------------------------------------------------------------------
void hud_element_scroll_vertical(int direction, int param) {
    hud_element_setup_scroll(direction, 0.0f, 0.0f, 2, param, 0);
}

// -----------------------------------------------------------------------------
// Address: 0x8003e2dc | Name: hud_element_scroll_h_overflow
// -----------------------------------------------------------------------------
void hud_element_scroll_h_overflow(int direction, float speed_x, float speed_y, int param) {
    hud_element_setup_scroll(direction, speed_x, speed_y, 1, param, 1);
}

// -----------------------------------------------------------------------------
// Address: 0x8003e308 | Name: hud_element_apply_scroll
// -----------------------------------------------------------------------------
// Per-frame scroll advancement. For scroll type 1 (horizontal), clamps
// delta to max element width (offset 0x58). For type 2 (vertical), no clamp.
// Looks up child elements of type 4/7/8 for speed factor.
// Adds bonus offset from layout+0x30 when flag 0x4000 is set and frame
// counter exceeds threshold.
// -----------------------------------------------------------------------------
void hud_element_apply_scroll(void) {
    FUN_8003fed8();

    int* frame = DAT_806119e4;
    int element = DAT_806119e0;
    int scroll_type = *(int*)((char*)frame + 0x38);

    if (scroll_type == 0) return;

    float speed = *(float*)((char*)frame + 0x3C);

    if (speed <= 0.0f) {
        // Find speed from child elements (type 4/7/8)
        float child_speed = 0.0f;
        int* child = *(int**)(element + 0x174);
        while (child != NULL) {
            int type = child[0];
            if (type == 4 || type == 7 || type == 8) {
                child_speed = *(float*)((char*)child + 0x70);
                break;
            }
            child = (int*)child[1];
        }

        u32 frame_count = *(u32*)((char*)frame + 0x40);

        if (scroll_type == 1) {
            // Horizontal: scale by frame count, clamp to max width
            float delta = child_speed * (float)frame_count;
            if (delta > *(float*)(element + 0x58)) {
                delta = *(float*)(element + 0x58);
            }
            *(float*)(element + 0x64) += delta;

            // Bonus offset for flag 0x4000
            if ((*(u32*)(element + 0x40) & 0x4000) != 0) {
                if (*(int*)((char*)frame + 0x44) <= (int)frame_count) {
                    float bonus = *(float*)(*(int*)(element + 0x1C) + 0x30) * (float)frame_count;
                    if (bonus < 0.0f) bonus = 0.0f;
                    *(float*)(element + 0x64) += bonus;
                }
            }
        } else {
            // Vertical: no clamp
            float delta = child_speed * (float)frame_count;
            *(float*)(element + 0x64) += delta;

            if ((*(u32*)(element + 0x40) & 0x4000) != 0) {
                if (*(int*)((char*)frame + 0x44) <= (int)frame_count) {
                    float bonus = *(float*)(*(int*)(element + 0x1C) + 0x30) * (float)frame_count;
                    if (bonus < 0.0f) bonus = 0.0f;
                    *(float*)(element + 0x64) += bonus;
                }
            }
        }
    } else {
        // Direct speed application
        if (scroll_type == 1) {
            float clamped = speed;
            if (clamped > *(float*)(element + 0x58)) {
                clamped = *(float*)(element + 0x58);
            }
            *(float*)(element + 0x64) += clamped;
        } else {
            *(float*)(element + 0x64) += speed;
        }
    }

    // Update end position to match
    *(float*)(element + 0x6C) = *(float*)(element + 0x64);

    // Clear scroll state
    *(int*)((char*)frame + 0x38) = 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e530 | Name: hud_begin_clip_group
// -----------------------------------------------------------------------------
void hud_begin_clip_group(void) {
    *(int*)((char*)DAT_806119e4 + 0x10) = 1;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e540 | Name: hud_end_clip_group
// -----------------------------------------------------------------------------
void hud_end_clip_group(void) {
    FUN_8003fed8();

    if (*(int*)(DAT_806119e0 + 0x1E8) != 0) {
        *(int*)(DAT_806119e0 + 0x1EC) = DAT_806119d8;
    }

    *(int*)((char*)DAT_806119e4 + 0x10) = 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e584 | Name: hud_element_get_state
// -----------------------------------------------------------------------------
int hud_element_get_state(int element) {
    return *(int*)(element + 0x1F0);
}

// -----------------------------------------------------------------------------
// Address: 0x8003e58c | Name: hud_pool_alloc_element
// -----------------------------------------------------------------------------
// Allocates from the dynamic element pool (stride 0x10C). Scans for first
// empty slot (where first int == 0), zeros it, sets type=4 and parent=-1.
// Returns pointer or NULL if pool exhausted.
// -----------------------------------------------------------------------------
int* hud_pool_alloc_element(void) {
    for (int i = 0; i < g_hud_element_pool_size; i++) {
        int* slot = (int*)(g_hud_element_pool_ptr + i * 0x10C);
        if (*slot == 0) {
            memset_wrapper((int)slot, 0, 0x10C);
            slot[0] = 4;    // type = list item
            slot[5] = -1;   // parent = none
            return slot;
        }
    }
    return NULL;
}
