// Address: 0x80111c88 - 0x80111f6c | Batch 333 | Date: 2026-09-04
// HUD directional damage indicator system.
// A ring of indicator slots (count = DAT_806109c8) each hold an angular
// position, a decaying age, and an intensity bucket. init seeds them with
// randomized angles/ages, tick advances the ages and folds intensity into the
// per-direction accumulator, and render draws the accumulated intensity as
// directional flash lines around the reticle.
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "gx_draw.h"
#include "math.h"

// ============================================================================
// External data (still-raw globals; documented by offset)
// ============================================================================
// DAT_806109c8      : number of damage-indicator slots
// DAT_80580314      : per-slot angular velocity  (float, 4 stride)
// DAT_80581314      : per-slot current angle/age (float, 4 stride)
// DAT_80580b14      : per-slot direction bucket  (u32,   4 stride) -> % 5
// DAT_80581b14      : per-direction accumulated intensity (u32, indexed 0..4)
// DAT_80611748      : viewport rect ([5]=y0 [6]=y1 used for line span)
// FLOAT_80615754    : angular scale;  DOUBLE_80615738 : int->float bias const

// ============================================================================
// 0x80111c88 | hud_damage_indicator_init
// Seeds each indicator slot: a randomized angular velocity (FUN_801310c0),
// a starting age index, and a direction bucket (FUN_8013109c % 5).
// ============================================================================
void hud_damage_indicator_init(void) {
    // FUN_803826e0();                           // save HUD state
    // for (i = 0; i < slot_count; ++i) {
    //     ang_vel[i]  = angle_scale * rand_float();
    //     age[i]      = (float)i;
    //     bucket[i]   = rand_uint() % 5;
    // }
    // FUN_8038272c();                           // restore HUD state
}

// ============================================================================
// 0x80111d54 | hud_damage_indicator_tick
// Clears the per-direction intensity accumulator, then advances each slot's
// age by its angular velocity, wrapping past the slot count, and adds the
// slot's direction contribution into the matching accumulator bucket.
// ============================================================================
void hud_damage_indicator_tick(void) {
    // for (i = 0; i < slot_count; ++i) accumulator[i] = 0;
    // for (i = 0; i < slot_count; ++i) {
    //     bucket = (int)age[i];
    //     age[i] += ang_vel[i];
    //     accumulator[bucket] += contribution[i];
    //     if (age[i] >= slot_count) age[i] -= slot_count;   // wrap
    // }
}

// ============================================================================
// 0x80111e3c | hud_damage_indicator_render
// Draws the accumulated directional damage flash as a fan of lines spanning
// the viewport, clamping each accumulator bucket to 0x7f alpha and drawing
// the current line plus its two neighbours at reduced alpha (soft edge).
// ============================================================================
void hud_damage_indicator_render(void) {
    // FUN_803826e4();                           // save HUD state
    // for (i = row_start; i < row_end; ++i) {
    //     a  = min(accumulator[i], 0x7f);
    //     a2 = angle_scale * (float)a;           // neighbour alpha
    //     if (i-1 >= row_start) draw_line(..., a2);
    //     draw_line(..., a);
    //     if (i+1 <  row_end)   draw_line(..., a2);
    // }
    // FUN_80382730();                           // restore HUD state
}
