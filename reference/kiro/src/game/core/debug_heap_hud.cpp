// Address: 0x80006fc4 | Name: debug_heap_hud | Date: 2026-07-23
// Description: Debug HUD overlay showing heap allocation stats.
// Displays: "Period Alloc Free Used Max" header, then per-heap stats in KB:
//   "Level: %dk %dk %dk" (total, free, used)
//   "Eternal: %dk %dk %dk" (total=0x1A7=423KB, free, used)
// Uses text_draw_char and text_set_font_table for rendering.
// Only visible in debug builds.

#include "common.h"

// See raw Ghidra decompilation for full implementation.
// Key data:
//   DAT_806115cc = level heap free bytes
//   DAT_806115e4 - 0x69C00 = level heap total bytes
//   DAT_806115bc - DAT_806115b8 = eternal heap used
