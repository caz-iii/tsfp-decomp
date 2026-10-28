// Address: 0x80111f70 - 0x80112354 | Batch 333 | Date: 2026-09-04
// HUD animated bar/meter easing tick.
// Per active player, eases the on-screen bar fill values (two channels) toward
// their target values, using a spring-like step whose rate depends on the
// current HUD element subtype (9 = split aspect meter, 10/8 = timed/animated
// meters). Handles clamping to [0,1] and choosing between fast/slow easing
// constants (FLOAT_80615874 vs FLOAT_80615878).
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "math.h"

// ============================================================================
// External data (still-raw globals; documented by offset)
// ============================================================================
// DAT_80611d74      : active HUD context; +4 = player slot, +0x14 = entity,
//                     +0xe04/0xe08 = aim delta X/Y, +0xe8c = secondary entity
// DAT_80578680      : per-player bar state block (0x5df stride);
//                     +0x16f8 = current fill[2], +0x1700 = target fill[2]
// DAT_80579d80/84   : split-meter aspect ratios (0x177c stride)
// DAT_806108e4      : animated-meter enable flag
// FLOAT_80615874/78 : fast / slow easing rates
// FLOAT_806108ec    : frame delta scale

// ============================================================================
// 0x80111f70 | hud_bar_anim_tick
// Advances the animated HUD bars for the active player. Selects a behaviour by
// the active element subtype at DAT_80578680[player*0x5df]:
//   9      : two-channel aspect meter; stores mesh aspect ratios then eases
//            each channel's target toward current with fast/slow rate.
//   10     : (guarded by DAT_806108e4) re-seeds target from a random source
//            when settled, then eases with per-channel rate.
//   8      : (guarded by DAT_806108e4) drives target from aim delta X/Y plus a
//            random settle, then eases with per-channel rate.
// Each channel is clamped to [0,1] and the fill approaches the target by at
// most (rate * frame_delta) per tick.
// ============================================================================
void hud_bar_anim_tick(void) {
    // FUN_803826e8();                           // save HUD state
    // if (active_player < 0) { restore; return; }
    // subtype = state->type;
    // if (subtype == 9)      { ... two-channel aspect ease ... }
    // else if (subtype == 10 && anim_enabled) { ... }
    // else if (subtype == 8  && anim_enabled) { ... }
    // FUN_80382734();                           // restore HUD state
}
