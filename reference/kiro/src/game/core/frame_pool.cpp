// Address: 0x80007214 | Name: frame_pool_init | Date: 2026-07-23
// Address: 0x80007348 | Name: frame_pool_init_wrapper | Date: 2026-07-23
// Address: 0x80007368 | Name: debug_frame_pool_hud | Date: 2026-07-23
// Address: 0x8000749c | Name: frame_pool_swap | Date: 2026-07-23
// Address: 0x800074cc | Name: mem_advance_secondary | Date: 2026-07-23
// Address: 0x800074f0 | Name: mem_advance_secondary_aligned | Date: 2026-07-23
//
// Description: Double-buffered frame allocator (per-frame scratch memory).
// Two buffers alternate each frame. One is filled while the other is consumed.
// Size varies by game mode:
//   mode 0: 0x400 (1KB) - minimal
//   mode 1: 0x100000 (1MB) - normal gameplay
//   mode 2: 0x19000 (100KB) - cutscene
//   mode 3: 0x4B000 (300KB) - varies by platform/player count
// Allocates 2x the buffer size (double-buffered), splits in half.
//
// Globals:
//   DAT_806115f0 = single buffer size
//   DAT_806115f4 = peak usage watermark
//   DAT_806115f8 = buffer A base
//   DAT_806115fc = current offset
//   DAT_80611600 = current write pointer
//   DAT_80611604 = buffer B base
//   DAT_80611608 = current read pointer

#include "common.h"

extern s32 DAT_806115f0;  // buffer size
extern s32 DAT_806115f4;  // peak usage
extern s32 DAT_806115f8;  // buffer A
extern s32 DAT_806115fc;  // current offset
extern s32 DAT_80611600;  // write ptr
extern s32 DAT_80611604;  // buffer B
extern s32 DAT_80611608;  // read ptr
extern s32 DAT_80612614;  // game mode ID

// Swap frame buffers
void frame_pool_swap(void) {
    if (DAT_806115f4 < DAT_80611600 - DAT_80611608) {
        DAT_806115f4 = DAT_80611600 - DAT_80611608;
    }
    s32 tmp = DAT_80611604;
    DAT_80611604 = DAT_80611608;
    DAT_80611608 = tmp;
    DAT_80611600 = tmp;
}

// Advance secondary allocator by 32-byte aligned size (29 xrefs)
void mem_advance_secondary(s32 size) {
    u32 aligned = (u32)(size + 0x1F) & 0xFFFFFFE0;
    DAT_80611600 += aligned;
    DAT_806115fc += aligned;
}
