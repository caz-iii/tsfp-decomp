// Address: 0x80110ad8 - 0x80111c40 | Batch 333 | Date: 2026-09-04
// HUD radar / minimap system: blip registration and full radar render.
// hud_radar_add_blip converts a world-space entity position into a
// player-relative (angle, distance) blip and appends it to the per-frame blip
// list. hud_radar_render walks all players, objectives, bag-tag / assault /
// escort targets, etc., registers blips for each, then draws the radar ring,
// the rotating sweep, and every queued blip.
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "sprite.h"
#include "gx_draw.h"
#include "math.h"

// ============================================================================
// External data (still-raw globals; documented by offset)
// ============================================================================
// DAT_80611f8c      : radar blip count (reset to 0 each render pass)
// DAT_8057ff98      : blip distance array (float, 0x10 stride)
// DAT_8057ff94      : blip angle array   (float, 0x10 stride)
// DAT_8057ff9c      : blip color array   (u32,   0x10 stride)
// DAT_8057ffa0      : blip texture array (int,   0x10 stride)
// DAT_80611d74      : active HUD context; +0xa8/0xb0 = player world X/Z,
//                     +0x100 = player facing angle
// DAT_80583228      : sine lookup table (0x800 entries, cos = +0x200 phase)
// DAT_805015bd/be/c0/d8 : current game mode / mode flags
// FLOAT_806157xx / FLOAT_80615xxx : radar scale + range constants

// ============================================================================
// 0x80110ad8 | hud_radar_add_blip
// Registers one radar blip. Computes the delta from the player position to the
// target (fetched via FUN_803826f0), converts to a normalized radar distance
// (range constant depends on mode flag DAT_805015d8 == 0xf) and a relative
// angle (target bearing minus player facing). Stores distance/angle/color/
// texture into the parallel blip arrays and increments the blip count.
// ============================================================================
void hud_radar_add_blip(unsigned int target_ref, unsigned int color, int texture_id) {
    // handle = FUN_803826f0(); pos = high32(handle);
    // dx = pos.x - player_x;  dz = -(pos.z - player_z);
    // dist = sqrt(dx*dx + dz*dz) / range_constant;   (range depends on mode)
    // angle = atan2(dz, dx) - player_facing_bias;
    // blip[count] = { dist, angle, color, texture_id };  ++count;
    (void)target_ref; (void)color; (void)texture_id;
}

// ============================================================================
// 0x80110c04 | hud_radar_render
// Main radar/minimap render for the active player. Depending on the current
// game mode (DAT_805015bd) it enumerates the relevant tracked entities and
// registers a blip for each:
//   - all networked players / bots (skipping self), colored by team/visibility
//   - linked/attached objective carriers (via FUN_80151d2c list walk)
//   - mode 0x01: capture points (DAT_805889f0 table)
//   - mode 0x02: zone / flag holders (soundpad zone resolve helpers)
//   - mode 0x06: team-colored targets (DAT_80588888 table)
//   - flag 0x40 sub-modes 0x0a/0x0b/0x14: tagged entity groups via FUN_8023c59c
// It then draws the radar backing disc (sprite 0xfa), the player arrow, a
// rotating sweep needle (sprite 0xfc) whose angle eases toward the nearest
// contact, the center marker (sprite 0xfe), every queued blip (near blips as
// dots, far blips as edge arrows), and an optional damage-direction flash
// (sprite 0xfb) driven by DAT_805786ac.
// ============================================================================
void hud_radar_render(void) {
    // See Ghidra listing at 0x80110c04 for the full body.
    // Outline:
    //   FUN_803826d0();                         // save HUD render state
    //   if (active_player < 0) { restore; return; }
    //   compute radar center + radius from viewport + FLOAT_806157c0 scale;
    //   DAT_80611f8c = 0;                        // clear blip list
    //   for each entity in the mode-specific table: hud_radar_add_blip(...);
    //   draw backing disc, player arrow;
    //   ease sweep angle toward nearest blip bearing (wrap-corrected);
    //   draw sweep needle, center dot;
    //   for each queued blip: draw dot (in-range) or edge arrow (out-of-range);
    //   if (damage_flash_timer > 0) draw directional damage glow;
    //   FUN_8038271c();                          // restore HUD render state
}
