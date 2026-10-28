// ============================================================================
// Address range: 0x80181cf0 - 0x80182170 | batch_471 | Date: 2026-09-06
// Subsystem: graphics (GX TEV-stage register shadow cache + dirty-check setters).
// tag gx-tevcache.
//
// The per-TEV-stage register cache the material pipeline funnels through: each setter
// keeps a shadow copy of the last-programmed register values (indexed by stage) and only
// re-issues the underlying GX call (FUN_803ca*/FUN_803c682c) when a value actually
// changes, avoiding redundant FIFO writes.  All are called from gx_material_setup_tev /
// gx_render_state_flush.  The reset functions clear the shadow tables to the 0xffffffff
// sentinel so the next set always re-programs.
//
//   gx_cached_set_tev_color_op   0x80181cf0  3-value stage reg (16 stages, stride 0xc)
//   gx_cached_set_tev_color_env  0x80181d50  5-value colour-env (stride 5 words + byte)
//   gx_cached_set_tev_alpha_env  0x80181dd4  5-value alpha-env
//   gx_cached_set_tev_color_in   0x80181e58  4-value colour-input sel
//   gx_cached_set_tev_alpha_in   0x80181ec8  4-value alpha-input sel
//   gx_cached_set_tev_order      0x80181f38  TEV order (no colour-swap)
//   gx_cached_set_tev_order_ex   0x80181fb4  TEV order (full, with swap tables)
//   gx_tev_cache_reset           0x8018204c  reset the 5 colour/alpha shadow tables (16)
//   gx_tev_order_cache_reset     0x80182124  reset the TEV-order shadow (8 stages)
//   gx_indirect_cache_reset      0x80182170  reset the indirect-flag cache sentinels

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

// ---- raw GX register-write helpers ------------------------------------------
extern void FUN_803ca4ec(unsigned int stage, unsigned int a, unsigned int b, int c);
extern void FUN_803ca0a4(int stage, unsigned int a, unsigned int b, unsigned int c,
                         unsigned int d, unsigned int e);
extern void FUN_803ca10c(int stage, unsigned int a, unsigned int b, unsigned int c,
                         unsigned int d, unsigned int e);
extern void FUN_803ca01c(int stage, unsigned int a, unsigned int b, unsigned int c, unsigned int d);
extern void FUN_803ca060(int stage, unsigned int a, unsigned int b, unsigned int c, unsigned int d);
extern void FUN_803c682c(int stage, int a, int b, unsigned int c, unsigned int d, int e);

// ---- forward decls for the reset helpers called by gx_tev_cache_reset -------
unsigned int gx_tev_order_cache_reset(void);
unsigned int gx_indirect_cache_reset(void);

// ---- shadow-cache tables ----------------------------------------------------
// (declared as element base symbols indexed by stage*stride, matching the ROM layout)
extern unsigned int DAT_805989a0;   // color-op table  (stride 0xc, 3 words)
extern unsigned int DAT_805989a4;
extern unsigned int DAT_805989a8;
extern unsigned int DAT_80598a60;   // color-env table (stride 0x14: 4 words + byte + word)
extern unsigned int DAT_80598a64;
extern unsigned int DAT_80598a68;
extern unsigned char DAT_80598a6c;
extern unsigned int DAT_80598a70;
extern unsigned int DAT_80598ba0;   // alpha-env table
extern unsigned int DAT_80598ba4;
extern unsigned int DAT_80598ba8;
extern unsigned char DAT_80598bac;
extern unsigned int DAT_80598bb0;
extern unsigned int DAT_80598ce0;   // color-in table (stride 0x10, 4 words)
extern unsigned int DAT_80598ce4;
extern unsigned int DAT_80598ce8;
extern unsigned int DAT_80598cec;
extern unsigned int DAT_80598de0;   // alpha-in table
extern unsigned int DAT_80598de4;
extern unsigned int DAT_80598de8;
extern unsigned int DAT_80598dec;
extern int          DAT_80598f3c;   // tev-order table (stride 0x14)
extern int          DAT_80598f40;
extern unsigned int DAT_80598f44;
extern unsigned char DAT_80598f48;   // order-cache valid flag
extern unsigned char DAT_80598f49;   // order swap byte
extern int          DAT_80598f4c;
extern int          DAT_80610a90;    // indirect-flag cache (shared with gx_frame_present)
extern int          DAT_80610a94;

// gx_cached_set_tev_color_op (0x80181cf0)
// Program a 3-value stage register only if it differs from the cached copy at
// stage*3 in the DAT_805989a0.. table (stride 0xc bytes).
void gx_cached_set_tev_color_op(unsigned int stage, unsigned int a, unsigned int b, int c)
{
    if (((&DAT_805989a0)[stage * 3] != a) || ((&DAT_805989a4)[stage * 3] != b) ||
        ((int)(&DAT_805989a8)[stage * 3] != c)) {
        (&DAT_805989a0)[stage * 3] = a;
        (&DAT_805989a4)[stage * 3] = b;
        (&DAT_805989a8)[stage * 3] = (unsigned int)c;
        FUN_803ca4ec(stage, a, b, c);
    }
}

// gx_cached_set_tev_color_env (0x80181d50)
// 5-value colour-environment register (4 words + a byte + a word) with change detection.
void gx_cached_set_tev_color_env(int stage, unsigned int a, unsigned int b, unsigned int c,
                                 unsigned int d, unsigned int e)
{
    if (((&DAT_80598a60)[stage * 5] != a) || ((&DAT_80598a64)[stage * 5] != b) ||
        ((&DAT_80598a68)[stage * 5] != c) ||
        ((unsigned int)(&DAT_80598a6c)[stage * 0x14] != (d & 0xff)) ||
        ((&DAT_80598a70)[stage * 5] != e)) {
        (&DAT_80598a60)[stage * 5] = a;
        (&DAT_80598a64)[stage * 5] = b;
        (&DAT_80598a68)[stage * 5] = c;
        (&DAT_80598a6c)[stage * 0x14] = (unsigned char)d;
        (&DAT_80598a70)[stage * 5] = e;
        FUN_803ca0a4(stage, a, b, c, d, e);
    }
}

// gx_cached_set_tev_alpha_env (0x80181dd4)
void gx_cached_set_tev_alpha_env(int stage, unsigned int a, unsigned int b, unsigned int c,
                                 unsigned int d, unsigned int e)
{
    if (((&DAT_80598ba0)[stage * 5] != a) || ((&DAT_80598ba4)[stage * 5] != b) ||
        ((&DAT_80598ba8)[stage * 5] != c) ||
        ((unsigned int)(&DAT_80598bac)[stage * 0x14] != (d & 0xff)) ||
        ((&DAT_80598bb0)[stage * 5] != e)) {
        (&DAT_80598ba0)[stage * 5] = a;
        (&DAT_80598ba4)[stage * 5] = b;
        (&DAT_80598ba8)[stage * 5] = c;
        (&DAT_80598bac)[stage * 0x14] = (unsigned char)d;
        (&DAT_80598bb0)[stage * 5] = e;
        FUN_803ca10c(stage, a, b, c, d, e);
    }
}

// gx_cached_set_tev_color_in (0x80181e58)
// 4-value colour-input selector register with change detection (stride 0x10).
void gx_cached_set_tev_color_in(int stage, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{
    if (((&DAT_80598ce0)[stage * 4] != a) || ((&DAT_80598ce4)[stage * 4] != b) ||
        ((&DAT_80598ce8)[stage * 4] != c) || ((&DAT_80598cec)[stage * 4] != d)) {
        (&DAT_80598ce0)[stage * 4] = a;
        (&DAT_80598ce4)[stage * 4] = b;
        (&DAT_80598ce8)[stage * 4] = c;
        (&DAT_80598cec)[stage * 4] = d;
        FUN_803ca01c(stage, a, b, c, d);
    }
}

// gx_cached_set_tev_alpha_in (0x80181ec8)
void gx_cached_set_tev_alpha_in(int stage, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{
    if (((&DAT_80598de0)[stage * 4] != a) || ((&DAT_80598de4)[stage * 4] != b) ||
        ((&DAT_80598de8)[stage * 4] != c) || ((&DAT_80598dec)[stage * 4] != d)) {
        (&DAT_80598de0)[stage * 4] = a;
        (&DAT_80598de4)[stage * 4] = b;
        (&DAT_80598de8)[stage * 4] = c;
        (&DAT_80598dec)[stage * 4] = d;
        FUN_803ca060(stage, a, b, c, d);
    }
}

// gx_cached_set_tev_order (0x80181f38)
// Program a TEV order register (texmap/texcoord/channel) with the colour-swap disabled
// (valid flag forced to 0); FUN_803c682c is called with swap byte 0 and table id 0x7d.
void gx_cached_set_tev_order(int stage, int texMap, int texCoord, unsigned int channel)
{
    if (((&DAT_80598f48)[stage * 0x14] != '\0') || ((&DAT_80598f3c)[stage * 5] != texMap) ||
        ((&DAT_80598f40)[stage * 5] != texCoord) || ((&DAT_80598f44)[stage * 5] != channel)) {
        (&DAT_80598f48)[stage * 0x14] = 0;
        (&DAT_80598f3c)[stage * 5] = texMap;
        (&DAT_80598f40)[stage * 5] = texCoord;
        (&DAT_80598f44)[stage * 5] = channel;
        FUN_803c682c(stage, texMap, texCoord, channel, 0, 0x7d);
    }
}

// gx_cached_set_tev_order_ex (0x80181fb4)
// Full TEV order register including the colour-swap byte and swap-table id, with the
// valid flag set; change detection covers all 6 fields.
void gx_cached_set_tev_order_ex(int stage, int texMap, int texCoord, unsigned int channel,
                                unsigned int swap, int swapTable)
{
    int base = stage * 0x14;
    if (((&DAT_80598f48)[base] == '\0') || ((&DAT_80598f3c)[stage * 5] != texMap) ||
        ((&DAT_80598f40)[stage * 5] != texCoord) || ((&DAT_80598f44)[stage * 5] != channel) ||
        ((unsigned int)(&DAT_80598f49)[base] != (swap & 0xff)) ||
        ((&DAT_80598f4c)[stage * 5] != swapTable)) {
        (&DAT_80598f48)[base] = 1;
        (&DAT_80598f3c)[stage * 5] = texMap;
        (&DAT_80598f40)[stage * 5] = texCoord;
        (&DAT_80598f44)[stage * 5] = channel;
        (&DAT_80598f49)[base] = (unsigned char)swap;
        (&DAT_80598f4c)[stage * 5] = swapTable;
        FUN_803c682c(stage, texMap, texCoord, channel, swap, swapTable);
    }
}

// gx_tev_cache_reset (0x8018204c)
// Reset all colour-op / colour-env / alpha-env / colour-in / alpha-in shadow tables for
// 16 TEV stages to the 0xffffffff sentinel (byte fields to 0xff), then reset the TEV
// order and indirect caches.  Called from gx_swap_batch_page.
unsigned int gx_tev_cache_reset(void)
{
    int envOff = 0;   // stride 0x14 tables (color/alpha env)
    int inOff = 0;    // stride 0x10 tables (color/alpha in)
    int opOff = 0;    // stride 0xc  table  (color op)
    int i = 0x10;
    do {
        *(unsigned int *)((int)&DAT_80598a60 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598ba0 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598a64 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598ba4 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598ce0 + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598de0 + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_805989a0 + opOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598a68 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598ba8 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598ce4 + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598de4 + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_805989a4 + opOff) = 0xffffffff;
        (&DAT_80598a6c)[envOff] = 0xff;
        (&DAT_80598bac)[envOff] = 0xff;
        *(unsigned int *)((int)&DAT_80598ce8 + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598de8 + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_805989a8 + opOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598a70 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598bb0 + envOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598cec + inOff) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598dec + inOff) = 0xffffffff;
        i = i - 1;
        inOff = inOff + 0x10;
        envOff = envOff + 0x14;
        opOff = opOff + 0xc;
    } while (i != 0);
    gx_tev_order_cache_reset();
    gx_indirect_cache_reset();
    return 0;
}

// gx_tev_order_cache_reset (0x80182124)
// Reset the 8-entry TEV order shadow table (stride 0x14) to sentinels + clear valid/swap.
unsigned int gx_tev_order_cache_reset(void)
{
    int off = 0;
    int i = 8;
    do {
        *(unsigned int *)((int)&DAT_80598f3c + off) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598f40 + off) = 0xffffffff;
        *(unsigned int *)((int)&DAT_80598f44 + off) = 0xffffffff;
        (&DAT_80598f48)[off] = 0;
        (&DAT_80598f49)[off] = 0;
        *(unsigned int *)((int)&DAT_80598f4c + off) = 0xffffffff;
        i = i - 1;
        off = off + 0x14;
    } while (i != 0);
    return 0;
}

// gx_indirect_cache_reset (0x80182170)
// Reset the indirect-texture flag cache sentinels (shared with gx_indirect_stage_config).
unsigned int gx_indirect_cache_reset(void)
{
    DAT_80610a90 = 0xffffffff;
    DAT_80610a94 = 0xffffffff;
    return 0xffffffff;
}
