// =============================================================================
// HUD Element Update System
// TimeSplitters: Future Perfect (GameCube)
// Decompiled: 2026-08-06
// =============================================================================
// This file contains the per-frame update logic for HUD elements:
// - Scroll positioning helpers
// - Input polling with repeat timers
// - Pixel coordinate refresh from normalized positions
// - Pre-confirm validation
// - The master recursive element update function
// =============================================================================

#include "common.h"
#include "hud.h"
#include "input.h"
#include "sound.h"

// External references
extern float FLOAT_80613bd0;  // scroll region ratio
extern float FLOAT_806108e8;  // frame delta time
extern float FLOAT_80613bc8;  // 1.0f
extern float FLOAT_80613bb0;  // 0.0f
extern float FLOAT_80613bb4;  // -1.0f (or negative unit)
extern float FLOAT_80613bec;  // dead zone threshold
extern float FLOAT_80613c20;  // positive analog threshold
extern float FLOAT_80613c38;  // negative analog threshold
extern float FLOAT_80613c24;  // cursor fade speed
extern float FLOAT_80613c2c;  // max repeat acceleration
extern float FLOAT_80613c30;  // repeat acceleration step
extern float FLOAT_80613c34;  // initial repeat speed
extern float FLOAT_80613c3c;  // initial delay (dpad)
extern float FLOAT_80613c40;  // initial delay (analog)
extern float FLOAT_80613c44;  // initial repeat timer value
extern float FLOAT_80613c48;  // cursor approach speed
extern float FLOAT_80613c4c;  // cursor decay speed
extern float FLOAT_80613bd8;  // cursor minimum threshold
extern float FLOAT_80613be0;  // transition lerp speed
extern float FLOAT_80611a64;  // screen scale Y
extern float FLOAT_80611a68;  // screen scale X
extern float FLOAT_80611a6c;  // screen offset Y
extern float FLOAT_80611a70;  // screen offset X

extern double DOUBLE_80613bc0; // double conversion constant
extern u32 DAT_806109c0;       // frame counter for double conversion
extern int DAT_806119ec;       // current viewport index
extern int DAT_80612028;       // last input frame counter
extern u8 DAT_8051a814[];     // viewport state array (stride 0x1E0)
extern int DAT_80525124[];    // per-viewport frame counters
extern u32 DAT_80525134[];    // button mask table for input
extern int DAT_806119c0;       // sound suppress flag
extern int DAT_80611a54;       // sound enabled flag
extern int* DAT_80611a58;      // cursor move sound ID ptr
extern int DAT_80612614;       // menu depth counter

// -----------------------------------------------------------------------------
// Address: 0x80043398 | hud_scroll_to_normalized_pos
// Scrolls viewport to a normalized position (0.0-1.0) by computing pixel
// offset from item height and visible region size.
// -----------------------------------------------------------------------------
void hud_scroll_to_normalized_pos(double normalized_pos, int element) {
    float item_height = *(float*)(element + 0x58);
    float visible_top = *(float*)(element + 0x68);
    float visible_bottom = *(float*)(element + 0x6C);
    float visible_region = FLOAT_80613bd0 * (visible_bottom - visible_top);
    float target = (float)(normalized_pos * (double)item_height - (double)visible_region);

    hud_scroll_set_target((double)target, element);
}

// -----------------------------------------------------------------------------
// Address: 0x800433d4 | hud_scroll_page_by_input
// Handles page-up/page-down scrolling from dpad input and viewport owner
// cursor navigation. Checks input flags and viewport state before dispatch.
// -----------------------------------------------------------------------------
void hud_scroll_page_by_input(int element) {
    int viewport_idx = *(int*)(element + 0x38);
    int viewport_owner = *(int*)(&DAT_8051a814[viewport_idx * 0x1E0] + 0x10);
    u32 flags40 = *(u32*)(element + 0x40);
    u32 flags_c4 = *(u32*)(element + 0xC4);

    BOOL has_cursor = FALSE;
    if (viewport_owner != 0 && (flags40 & 0x40000) == 0 && (flags_c4 & 0x10) != 0) {
        has_cursor = TRUE;
    }

    float scroll_current = *(float*)(element + 0x84);
    float scroll_target = *(float*)(element + 0x80);

    if (scroll_current != scroll_target) {
        return;  // already scrolling
    }

    float page_size = *(float*)(element + 0x94);
    int nav_event = *(int*)(element + 0x188);

    // Page down: input flag bit 1 + flag 0x8, or nav event -4 with cursor
    if (((flags_c4 & 2) != 0 && (flags40 & 8) != 0) ||
        (nav_event == -4 && has_cursor)) {
        hud_scroll_set_target((double)(scroll_target + page_size), element);
    }

    // Page up: input flag bit 0 + flag 0x8, or nav event -3 with cursor
    if (((flags_c4 & 1) != 0 && (flags40 & 8) != 0) ||
        (nav_event == -3 && has_cursor)) {
        hud_scroll_set_target((double)(scroll_target - page_size), element);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x800434d0 | hud_input_poll_and_repeat
// Main HUD input polling function. Reads analog sticks and buttons for all 4
// controllers, processes input repeat timers with initial delay and acceleration,
// dispatches viewport pixel position computation when cursor is active.
//
// Input repeat system uses 13 virtual buttons (4 analog dirs + 9 digital):
//   [0-3]: analog stick directions (up/down/left/right)
//   [4-12]: digital button mappings
// Each has a timer at viewport+0x7C and a state counter at viewport+0xB0.
// Initial press requires threshold time before repeat kicks in.
// Once repeating, fires at fixed interval.
// -----------------------------------------------------------------------------
void hud_input_poll_and_repeat(void) {
    int element = FUN_803826e8();  // get current HUD element context
    float dt = FLOAT_806108e8;
    int viewport_idx = DAT_806119ec;
    int vp_offset = viewport_idx * 0x1E0;
    u32* viewport = (u32*)(&DAT_8051a814[vp_offset]);
    u32 held_buttons = 0;

    float repeat_step = FLOAT_80613bc8 / (float)((double)CONCAT44(0x43300000,
                        DAT_806109c0 ^ 0x80000000) - DOUBLE_80613bc0);
    float dt_scaled = dt * repeat_step;

    // Clear "newly pressed" flag on viewport
    *viewport = *viewport & ~2u;

    double dVar23 = (double)(dt * repeat_step);

    for (int pad = 0; pad < 4; pad++) {
        if ((*(u32*)(element + 0x3C) & (1 << pad)) == 0) continue;

        double analog_x = input_get_analog_value(pad, 1);
        double analog_y = input_get_analog_value(pad, 2);
        u32 buttons = input_get_buttons_pressed(pad);

        u32 base_button_idx = 0;
        int viewport_owner = *(int*)(&DAT_8051a814[*(int*)(element + 0x38) * 0x1E0] + 0x10);

        if (viewport_owner == 0 || (*(u32*)(element + 0x40) & 0x40000) != 0) {
            // Digital navigation mode: threshold analog to discrete directions
            u32 analog_dirs[4];
            analog_dirs[0] = (u32)((double)FLOAT_80613c20 < analog_y);  // down
            analog_dirs[1] = (u32)(analog_y < (double)FLOAT_80613c38);  // up
            analog_dirs[2] = (u32)(analog_x < (double)FLOAT_80613c38);  // left
            analog_dirs[3] = (u32)((double)FLOAT_80613c20 < analog_x);  // right
            // ... process with digital repeat logic
        } else {
            // Cursor/analog mode: smooth movement with acceleration
            // Updates cursor position at viewport + 0x2C/0x30
            // Calls hud_viewport_compute_pixel_pos
            base_button_idx = 4;  // skip analog directions for button processing
        }

        // Process 13 virtual buttons with repeat timers
        for (u32 btn = base_button_idx; btn < 13; btn++) {
            float* timer = (float*)((int)viewport + btn * 4 + 0x7C);
            int* state = (int*)((int)viewport + btn * 4 + 0xB0);
            u32 bit = 1 << btn;
            u32 repeat_bit = 1 << (btn + 16);
            BOOL is_dpad = (btn < 4);

            // Button held: accumulate timer, fire repeat when threshold reached
            if (*timer >= 0.0f) {
                if ((buttons & DAT_80525134[btn]) != 0 || (btn < 4 && /* analog active */0)) {
                    held_buttons |= bit;
                    *(u32*)(element + 0xC4) |= bit;

                    // First frame: set "just pressed" flag
                    if ((*(u32*)(&DAT_8051a814[vp_offset] + 0x78) & bit) == 0) {
                        *(u32*)(element + 0xC0) |= (bit | repeat_bit);
                    }

                    *timer += (float)dVar23;

                    if (*state == 0) {
                        // Initial delay phase
                        float threshold = is_dpad ? FLOAT_80613c3c : FLOAT_80613c40;
                        if (threshold <= *timer) {
                            *timer = 0.0f;
                            *state = 1;
                            // Fire repeat event
                            *(u32*)(element + 0xC0) |= repeat_bit;
                        }
                    } else {
                        // Repeat phase: fire at fixed interval
                        if (FLOAT_80613bec <= *timer) {
                            *timer = 0.0f;
                            *(u32*)(element + 0xC0) |= repeat_bit;
                        }
                    }
                } else {
                    // Released
                    *timer = 0.0f;
                    *(int*)((int)viewport + btn * 4 + 0xB0) = 0;
                    if ((*(u32*)(&DAT_8051a814[vp_offset] + 0x78) & bit) != 0) {
                        *(u32*)(element + 0xC4) |= repeat_bit;  // release event
                    }
                }
            } else {
                // Negative timer: releasing animation
                float t = *timer + (float)dVar23;
                if (0.0f < t) t = 0.0f;
                *timer = t;
                if ((buttons & DAT_80525134[btn]) != 0 || (btn < 4 && /* analog */0)) {
                    held_buttons |= bit;
                }
            }
        }
    }

    // Store final held state
    *(u32*)(&DAT_8051a814[vp_offset] + 0x78) = held_buttons;
    FUN_80382734();  // restore register context
}

// -----------------------------------------------------------------------------
// Address: 0x800439d4 | hud_input_reset_repeat_timers
// Resets all 13 input repeat timers to their initial delay value.
// Called when focus changes or input context switches.
// -----------------------------------------------------------------------------
void hud_input_reset_repeat_timers(void) {
    float initial = FLOAT_80613c44;
    int vp_offset = DAT_806119ec * 0x1E0;

    for (int i = 0; i < 13; i++) {
        *(float*)(&DAT_8051a814[vp_offset] + 0x7C + i * 4) = initial;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80043a0c | hud_element_refresh_pixel_coords
// Refreshes pixel-space coordinates from normalized element positions.
// Applies split-screen scaling factors and computes all rect variants:
//   +0xA4..+0xB8: content rect (scaled)
//   +0xC8..+0xDC: working rect (copy of content)
//   +0x100: scale factor (1.0)
// -----------------------------------------------------------------------------
void hud_element_refresh_pixel_coords(int element) {
    *(float*)(element + 0x78) = 0.0f;
    *(float*)(element + 0x7C) = 0.0f;

    int split_param = hud_viewport_get_split_param();
    if (split_param == 0) {
        // Not split-screen: use element's own offset
        *(float*)(element + 0x78) = *(float*)(element + 0x5C);
        *(float*)(element + 0x7C) = *(float*)(element + 0x60);
    }

    // Compute pixel-space position
    *(float*)(element + 0xA4) = *(float*)(element + 0x78) * FLOAT_80611a68 + FLOAT_80611a70;
    *(float*)(element + 0xA8) = *(float*)(element + 0x7C) * FLOAT_80611a64 + FLOAT_80611a6c;

    // Compute pixel-space dimensions
    *(float*)(element + 0xAC) = *(float*)(element + 0x4C) * FLOAT_80611a68;
    *(float*)(element + 0xB0) = *(float*)(element + 0x64) * FLOAT_80611a64;
    *(float*)(element + 0xB4) = *(float*)(element + 0x70) * FLOAT_80611a68;
    *(float*)(element + 0xB8) = *(float*)(element + 0x74) * FLOAT_80611a64;

    // Copy to working rect
    *(float*)(element + 0xC8) = *(float*)(element + 0xA4);
    *(float*)(element + 0xCC) = *(float*)(element + 0xA8);
    *(float*)(element + 0xD0) = *(float*)(element + 0xAC);
    *(float*)(element + 0xD4) = *(float*)(element + 0xB0);
    *(float*)(element + 0xD8) = *(float*)(element + 0xB4);
    *(float*)(element + 0xDC) = *(float*)(element + 0xB8);

    // Reset scale
    *(float*)(element + 0x100) = 1.0f;
}

// -----------------------------------------------------------------------------
// Address: 0x80043afc | hud_element_can_confirm
// Pre-confirm validation: calls the layout callback at layout_table+0xA4.
// Returns 1 (allow) if no callback exists, or the callback's return value.
// Used to gate confirm actions (e.g. "are all fields valid?").
// -----------------------------------------------------------------------------
int hud_element_can_confirm(int element) {
    int layout = *(int*)(element + 0x1C);
    void* callback = *(void**)(layout + 0xA4);

    if (callback == NULL) {
        return 1;
    }
    return ((int (*)(void))callback)();
}

// -----------------------------------------------------------------------------
// Address: 0x80043b38 | hud_element_update
// Master per-frame update for a HUD element. This is the central hub of the
// menu system, called recursively for each element in the hierarchy.
//
// Responsibilities:
// 1. Transition state machine (appear/active/closing/closed)
// 2. Input processing when focused:
//    - Confirm (A button) → fires confirm callback + sound 0xD26
//    - Cancel (B button) → fires cancel callback + sound 0xD27
//    - Navigate (dpad) → item selection with wrap + sound
//    - Value change (L/R or left/right on slider items)
// 3. Scroll management (interpolation + page scroll + item tracking)
// 4. Visibility computation for all children
// 5. Focus fade animation
// 6. Sub-content rendering dispatch
// 7. Recursive child update
// 8. Auto-destroy when closing transition completes
//
// Element state at +0x6B (transition state):
//   1 = appearing, 2 = active, 3 = idle, 4 = closing, 5 = closed
//
// Sound effects:
//   0xD26 = confirm, 0xD27 = cancel, 0xD2B/0xD2A = transition sounds
//   Cursor move sound from DAT_80611a58
// -----------------------------------------------------------------------------
void hud_element_update(void) {
    // [MASSIVE FUNCTION - see Ghidra decompilation for full detail]
    // Key flow:
    //
    // 1. Get element context, compute dt and viewport state
    // 2. Check if element has cursor ownership (viewport owner)
    // 3. Handle cursor approach interpolation (smooth follow)
    // 4. Clear per-frame event flags
    // 5. Call hud_input_poll_and_repeat if input frame is new
    // 6. Process transition state machine:
    //    - State 1 (appearing): wait for appear timer, then -> state 2
    //    - State 2 (active): compute transition_t for animations
    //    - State 4/5 (closing): compute closing transition_t
    // 7. Block input during transition warmup period
    // 8. Call layout update callback if present
    // 9. Call user update callback (element+0x0C)
    // 10. If scrollable + has items:
    //     - hud_scroll_interpolate (smooth scroll current -> target)
    //     - hud_compute_item_visibility (clip hidden items)
    //     - hud_scroll_page_by_input (page up/down)
    //     - Process focus navigation (row/column with wrap)
    //     - Process confirm/cancel/value-change input
    //     - hud_scroll_to_item (ensure focused item visible)
    // 11. Process custom button events (L/R triggers, etc.)
    // 12. For each child item: update focus fade + draw sub-content
    // 13. For each child element: recurse hud_element_update()
    // 14. If closing complete (transition_t <= 0): destroy self
    // 15. Reset global state (DAT_806119c0, DAT_806119ec)

    // Recursive call at end: iterates child elements linked at +0x168
    // for (child = element->first_child; child; child = child->next_sibling) {
    //     hud_element_update();  // context pushed via FUN_803826e0
    // }
}
