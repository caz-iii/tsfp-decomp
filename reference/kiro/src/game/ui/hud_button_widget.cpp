// HUD Toggle / Icon Button Widgets + Progress Split Panel
// Batch 1043 | Date: 2026-09-12
//
// A small family of clickable HUD widgets driven by the property-panel
// framework. Two widget flavours share the same pattern:
//
//   toggle_button : two visual states (off/on) each with its own string id,
//                   sprite id and click callback. Layout (word offsets):
//                     [0..3]   live bounds (x0,y0,x1,y1)
//                     [8..0xb] base rect (copied source, used to re-derive bounds)
//                     [0x10]   off string id      [0x11] on string id
//                     [0x12]   on  sprite id      [0x13] off sprite id
//                     [0x14]   off click callback [0x15] on  click callback
//                     [0x16]   toggle flag (0=off, !=0=on)
//                     [0x17]   enabled flag
//
//   icon_button   : single-state push button. Layout (word offsets):
//                     [0..3]   live bounds     [4..7] base rect
//                     [8]      click callback  [9] sprite texture id
//                     [10]     secondary sprite / callback slot
//                     [0xb]    enabled flag
//
// Hit-testing is invoked from the panel event pump with a style code; style 3
// is a click. The bevel/progress helper renders a two-tone filled bar.
//
// Named cross-module helpers (prop_panel_draw_bevel*, text_*, sprite_*, gx_*,
// get_string_by_id) resolve through the shared generated headers; only the raw
// FUN_/DAT_/FLOAT_/DOUBLE_ symbols get per-file externs below.

#include "common.h"
#include "hud.h"

// --- per-file externs for raw symbols ---------------------------------------
// Register-window accessors: return the active widget record pointer in the
// high word of the paired return; the low word carries a companion pointer.
extern unsigned long long FUN_803826e8(void);   // progress-split: (x0_hi, y0_lo)
extern unsigned long long FUN_803826f0(void);    // icon_button: (dst_hi, src_lo)
extern int  FUN_803826dc(void);                  // toggle_button_draw: widget record
extern void FUN_80382734(void);                  // progress-split end hook
extern void FUN_80382728(void);                  // toggle_button_draw end hook
extern void FUN_8038273c(void);                  // icon_button_init end hook

extern int    DAT_80610a20;        // sprite table base pointer
extern float  FLOAT_8061b454;      // 0.5 (centering half-factor)
extern double DOUBLE_8061b480;     // 4503599627370496.0 (int<->double bias)

// ============================================================================
// Address: 0x803742a4 | Name: button_draw_progress_split
// Two-tone beveled progress panel. Draws the outer bevel across the full
// interior, then an inner colored bevel filling ratio*(width) from the left.
// ============================================================================
void button_draw_progress_split(int /*panel*/, int /*unused*/, unsigned int x1,
                                unsigned int y1, unsigned int fill_col) {
    unsigned long long rec = FUN_803826e8();
    int x0 = (int)(rec >> 0x20);
    int y0 = (int)rec;
    double ratio = (double)*(float*)&y0;  // fill ratio latched in companion register

    prop_panel_draw_bevel(x0, y0, x1, y1);

    int span = (int)x1 - x0;
    double bias = (double)(span ^ 0x80000000) - DOUBLE_8061b480;
    int inner_x1 = x0 + (int)(ratio * (double)(float)bias);
    prop_panel_draw_bevel_col(x0 + 1, y0 + 1, (unsigned)inner_x1, y1 - 1, fill_col);

    FUN_80382734();
}

// ============================================================================
// Address: 0x80374344 | Name: toggle_button_init
// Copy the source bounds/base-rect blocks into a fresh toggle-button record and
// latch the off/on string ids, sprite ids and click callbacks.
// ============================================================================
void toggle_button_init(unsigned int* dst, unsigned int* src, unsigned int off_str,
                        unsigned int on_str, unsigned int on_sprite, unsigned int off_sprite,
                        unsigned int off_cb, unsigned int on_cb) {
    dst[0] = src[0];
    dst[2] = src[2];
    dst[1] = src[1];
    dst[3] = src[3];
    // Mirror bounds into the base rect [8..0xb].
    int off = 0;
    int n = 4;
    do {
        unsigned int* p = (unsigned int*)((char*)dst + off);
        off += 4;
        p[8] = *p;
        n--;
    } while (n != 0);

    dst[0x10] = off_str;
    dst[0x11] = on_str;
    dst[0x14] = on_sprite;
    dst[0x15] = off_sprite;
    dst[0x12] = off_cb;
    dst[0x13] = on_cb;
}

// ============================================================================
// Address: 0x803743a0 | Name: toggle_button_set_bounds
// Offset the live bounds [0..3] from the base rect [8..0xb] by (dx,dy).
// ============================================================================
void toggle_button_set_bounds(double dx, double dy, float* w) {
    w[0] = (float)((double)w[8] + dx);
    w[2] = (float)((double)w[10] + dx);
    w[1] = (float)((double)w[9] + dy);
    w[3] = (float)((double)w[0xb] + dy);
}

// ============================================================================
// Address: 0x803743d4 | Name: toggle_button_set_callbacks
// Store the off/on click callbacks at word offsets 0x58/0x5c.
// ============================================================================
void toggle_button_set_callbacks(int w, unsigned int off_cb, unsigned int on_cb) {
    *(unsigned int*)(w + 0x58) = off_cb;
    *(unsigned int*)(w + 0x5c) = on_cb;
}

// ============================================================================
// Address: 0x803743e0 | Name: toggle_button_hit_test
// On a style-3 click with the button enabled ([0x17]) and the pointer
// (entity +0x38/+0x3c) inside the widget bounds, fire the off callback [0x14]
// when the toggle flag [0x16] is clear, else the on callback [0x15].
// ============================================================================
void toggle_button_hit_test(float* w, int ent, int style) {
    typedef void (*click_fn)(int);
    if (*(int*)(ent + 0x70) == 0 && style == 3 && w[0x17] != 0.0f &&
        w[0] <= *(float*)(ent + 0x38) && w[1] <= *(float*)(ent + 0x3c) &&
        *(float*)(ent + 0x38) <= w[2] && *(float*)(ent + 0x3c) <= w[3]) {
        if (w[0x16] == 0.0f && (click_fn)(unsigned int)w[0x14] != (click_fn)0) {
            ((click_fn)(unsigned int)w[0x14])(ent);
        } else if (w[0x16] != 0.0f && (click_fn)(unsigned int)w[0x15] != (click_fn)0) {
            ((click_fn)(unsigned int)w[0x15])(ent);
        }
    }
}

// ============================================================================
// Address: 0x803744b0 | Name: toggle_button_draw
// Render the toggle button. With no sprite bound, draw just the current-state
// label centered (dimmed). Otherwise bind the state sprite material, draw the
// sprite quad and overlay the centered label. State selects off/on string and
// sprite; brightness follows the enabled flag [0x17].
// ============================================================================
void toggle_button_draw(void) {
    float* w = (float*)FUN_803826dc();

    if (w[0x12] == 0.0f || w[0x13] == 0.0f) {
        // Label-only button.
        u8* s = (u8*)get_string_by_id((w[0x16] == 0.0f) ? (int)w[0x11] : (int)w[0x10]);
        double cw = (double)text_get_char_width(s);
        float x1 = w[2];
        float x0 = w[0];
        double ch = (double)text_get_char_height(s);
        double bx = (double)((int)(x1 - x0) - (int)cw ^ 0x80000000);
        double by = (double)((int)(w[3] - w[1]) - (int)ch ^ 0x80000000);
        text_draw_char((double)(w[0] + (float)(bx - DOUBLE_8061b480) * FLOAT_8061b454),
                       (double)(w[1] + (float)(by - DOUBLE_8061b480) * FLOAT_8061b454),
                       s, 0x7f7f7f80);
    } else {
        u8* s = (u8*)get_string_by_id((w[0x16] == 0.0f) ? (int)w[0x11] : (int)w[0x10]);
        int tex = (int)((w[0x16] == 0.0f) ? sprite_get_texture_id((int)w[0x13])
                                          : sprite_get_texture_id((int)w[0x12]));
        unsigned int col = (w[0x17] != 0.0f) ? 0x7f7f7f80u : 0x3c3c3c3cu;

        double cw = (double)text_get_char_width(s);
        float x1 = w[2];
        float x0 = w[0];
        double ch = (double)text_get_char_height(s);
        float y1 = w[3];
        float y0 = w[1];

        gx_set_active_material(tex);
        int sprite = *(int*)&DAT_80610a20 + tex * 0x58;
        gx_draw_sprite_quad((double)w[0], (double)w[1], (double)w[2], (double)w[3],
                            (unsigned int)*(unsigned short*)(sprite + 0x52) << 4,
                            (unsigned int)*(unsigned short*)(sprite + 0x4e) << 4, 0, 0);

        double bx = (double)((int)(x1 - x0) - (int)cw ^ 0x80000000);
        double by = (double)((int)(y1 - y0) - (int)ch ^ 0x80000000);
        text_draw_char((double)(w[0] + (float)(bx - DOUBLE_8061b480) * FLOAT_8061b454),
                       (double)(w[1] + (float)(by - DOUBLE_8061b480) * FLOAT_8061b454),
                       s, col);
    }
    FUN_80382728();
}

// ============================================================================
// Address: 0x80374740 | Name: icon_button_init
// Initialize an icon (push) button. Pulls the record via FUN_803826f0, copies
// the source bounds into the base rect, resolves the sprite texture id, and
// stores the click callback, secondary sprite and enable flag.
// ============================================================================
void icon_button_init(unsigned int click_cb, unsigned int /*unused*/, unsigned int click_cb2,
                      unsigned int sprite2, int sprite_id) {
    unsigned long long rec = FUN_803826f0();
    unsigned int* dst = (unsigned int*)(unsigned int)(rec >> 0x20);
    unsigned int* src = (unsigned int*)(unsigned int)rec;

    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];

    dst[9] = (unsigned int)sprite_get_texture_id(sprite_id);

    // Mirror bounds [0..3] into base rect [4..7].
    int off = 0;
    int n = 4;
    do {
        unsigned int* p = (unsigned int*)((char*)dst + off);
        off += 4;
        p[4] = *p;
        n--;
    } while (n != 0);

    dst[8] = click_cb2;
    dst[10] = sprite2;
    dst[0xb] = 1;
    (void)click_cb;
    FUN_8038273c();
}

// ============================================================================
// Address: 0x803747d4 | Name: icon_button_set_bounds
// Offset the live bounds [0..3] from the base rect [4..7] by (dx,dy).
// ============================================================================
void icon_button_set_bounds(double dx, double dy, float* w) {
    w[0] = (float)((double)w[4] + dx);
    w[1] = (float)((double)w[5] + dy);
    w[2] = (float)((double)w[6] + dx);
    w[3] = (float)((double)w[7] + dy);
}

// ============================================================================
// Address: 0x80374808 | Name: icon_button_set_callback
// Store the click callback at word offset 0x2c.
// ============================================================================
void icon_button_set_callback(int w, unsigned int cb) {
    *(unsigned int*)(w + 0x2c) = cb;
}

// ============================================================================
// Address: 0x80374810 | Name: icon_button_hit_test
// On a style-3 click with the button enabled ([0xb]) and the pointer inside
// the widget bounds, invoke the click callback at [10].
// ============================================================================
void icon_button_hit_test(float* w, int ent, int style) {
    typedef void (*click_fn)(int);
    if (*(int*)(ent + 0x70) == 0 && style == 3 && w[0xb] != 0.0f &&
        w[0] <= *(float*)(ent + 0x38) && w[1] <= *(float*)(ent + 0x3c) &&
        *(float*)(ent + 0x38) <= w[2] && *(float*)(ent + 0x3c) <= w[3] &&
        (click_fn)(unsigned int)w[10] != (click_fn)0) {
        ((click_fn)(unsigned int)w[10])(ent);
    }
}
