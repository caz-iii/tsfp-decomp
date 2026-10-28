// Address: 0x803991c8 | Name: os_dsp (Dolphin OS DSP bootstrap self-test) | Date: 2026-09-14T22:10:00Z
//
// DSP bring-up performed during __OSInit (os_kernel_init). The DSP (a fixed-point
// audio DSP behind the 0xCC005000 register block) is reset, a tiny bootstrap
// microprogram is DMA'd in over the ARAM/DMA path, run, and the DSP is left halted
// ready for the audio driver to take over.
//
// DSP register block (0xCC005000):
//   +0x00 DSP_MAILBOX_OUT_HI (Ramcc005020 alias is the ARAM DMA main-mem addr)
//   +0x04 DSP_MAILBOX_OUT_LO / status (bit 0x8000 = mailbox full)
//   +0x06 DSP_MAILBOX_IN_LO
//   +0x0a DSP_CONTROL (bit 0x1 = reset/go, 0x4 = int, 0x20 = halt, 0x200/0x400 = DMA busy,
//                       0x800 = ARAM DMA enable)
//   +0x12 AR_SIZE (0x43)
//   +0x20 AR_DMA_MMADDR, +0x24 AR_DMA_ARADDR, +0x28 AR_DMA_CNT
//
// The bump allocator pair (os_dsp_heap_init / os_dsp_heap_alloc) hands out
// power-of-two-aligned blocks from a single moving cursor kept in DAT_806113c8;
// it is used to carve the small scratch buffers the DSP setup needs (and by a few
// callers in the 0x8039axxx region).

#include "common.h"

// ---- Raw globals / helpers referenced from this translation unit ----
extern u32 DAT_806113c8;    // DSP-setup bump-allocator cursor

// DSP register block (byte offsets from 0xCC005000)
extern volatile u8   DAT_cc005012;  // AR_SIZE
extern volatile u16  DAT_cc005000;  // DSP mailbox out hi
extern volatile u16  DAT_cc005004;  // DSP mailbox out lo / status
extern volatile u16  DAT_cc005006;  // DSP mailbox in lo
extern volatile u16  DAT_cc00500a;  // DSP control
extern volatile u32  Ramcc005020;   // AR_DMA_MMADDR (main-mem address)
extern volatile u32  DAT_cc005024;  // AR_DMA_ARADDR
extern volatile u32  DAT_cc005028;  // AR_DMA_CNT
extern volatile u16  DAT_cc005036;  // AR misc control

extern int  os_get_arena_hi(void);
extern int  os_get_time_base(void);
extern void leaf_31e8(int dst, int src, int len);   // small block copy (memcpy-like)
extern uint os_dc_flush_range(uint addr, int len);  // 0x803994d0 (DCFlushRange)

// os_dsp_heap_init 0x803991c8
// Seeds the DSP-setup bump allocator with a base cursor.
void os_dsp_heap_init(u32 base)
{
    DAT_806113c8 = base;
}

// os_dsp_heap_alloc 0x803991d0
// Aligns the cursor up to `align` (a power of two), returns that aligned address,
// then advances the cursor past `size` bytes (re-aligned). Returns the block base.
uint os_dsp_heap_alloc(int size, int align)
{
    uint mask;
    uint block;

    mask  = (uint)align - 1;
    block = ~mask & (DAT_806113c8 + mask);
    DAT_806113c8 = ~mask & (block + (uint)size + mask);
    return block;
}

// os_dsp_bootstrap 0x803991fc
// The DSP self-test / bootstrap run from os_kernel_init: saves the top 0x80 bytes
// of the arena, copies the bootstrap microprogram there, flushes it, resets the
// DSP and DMAs the program in via the ARAM DMA engine, spins the DSP twice with a
// short time-base delay between runs, halts it, and restores the saved arena bytes.
void os_dsp_bootstrap(void)
{
    ushort ctrl;
    int    t0;
    int    t1;
    int    arena_hi;

    arena_hi = os_get_arena_hi();
    // stash the top 0x80 arena bytes, drop the bootstrap image in at 0x81000000
    leaf_31e8(arena_hi - 0x80, 0x81000000, 0x80);
    leaf_31e8(0x81000000, 0x804982b0, 0x80);
    os_dc_flush_range(0x81000000, 0x80);

    DAT_cc005012 = 0x43;           // AR_SIZE
    DAT_cc00500a = 0x8ac;          // control: reset image
    ctrl = DAT_cc00500a;
    DAT_cc00500a = (ushort)(ctrl | 1);          // go
    do {
        ctrl = DAT_cc00500a;
    } while ((ctrl & 1) != 0);                   // wait reset clear

    DAT_cc005000 = 0;
    do {
        ctrl = DAT_cc005004;
    } while ((ctrl & 0x8000) != 0);              // wait mailbox drained

    // first ARAM DMA of the bootstrap program
    Ramcc005020  = 0x1000000;
    DAT_cc005024 = 0;
    DAT_cc005028 = 0x20;
    ctrl = DAT_cc00500a;
    while ((ctrl & 0x20) == 0) {                 // wait halt/ack
        ctrl = DAT_cc00500a;
    }
    DAT_cc00500a = ctrl;

    // short spin on the time base between the two runs
    t0 = os_get_time_base();
    do {
        t1 = os_get_time_base();
    } while (t1 - t0 < 0x892);

    // second ARAM DMA / run
    Ramcc005020  = 0x1000000;
    DAT_cc005024 = 0;
    DAT_cc005028 = 0x20;
    ctrl = DAT_cc00500a;
    while ((ctrl & 0x20) == 0) {
        ctrl = DAT_cc00500a;
    }
    DAT_cc00500a = ctrl;

    ctrl = DAT_cc00500a;
    DAT_cc00500a = (ushort)(ctrl & 0xf7ff);      // clear ARAM DMA enable
    do {
        ctrl = DAT_cc00500a;
    } while ((ctrl & 0x400) != 0);               // wait DMA idle

    ctrl = DAT_cc00500a;
    DAT_cc00500a = (ushort)(ctrl & 0xfffb);      // clear int bit
    ctrl = DAT_cc005004;
    while ((ctrl & 0x8000) == 0) {               // wait mailbox result
        ctrl = DAT_cc005004;
    }

    ctrl = DAT_cc00500a;
    (void)DAT_cc005006;                          // read mailbox-in result
    DAT_cc00500a = (ushort)(ctrl | 4);           // ack int
    DAT_cc00500a = 0x8ac;                        // reset image
    ctrl = DAT_cc00500a;
    DAT_cc00500a = (ushort)(ctrl | 1);           // go
    do {
        ctrl = DAT_cc00500a;
    } while ((ctrl & 1) != 0);

    arena_hi = os_get_arena_hi();
    // restore the saved arena bytes
    leaf_31e8(0x81000000, arena_hi - 0x80, 0x80);
}

// os_dsp_halt 0x803993b8
// Halts / quiesces the DSP: masks off the AR misc control top bit, waits for the
// DMA-busy bits to clear, drains the mailbox, waits a short time-base window, then
// pulses the go bit and waits for it to clear, leaving the DSP idle.
void os_dsp_halt(void)
{
    ushort ctrl;
    int    t0;
    int    t1;

    DAT_cc00500a = 0x804;                        // control base
    ctrl = DAT_cc005036;
    DAT_cc005036 = (ushort)(ctrl & 0x7fff);      // clear AR misc top bit

    ctrl = DAT_cc00500a;
    while ((ctrl & 0x400) != 0) {                // wait DMA (0x400) idle
        ctrl = DAT_cc00500a;
    }
    ctrl = DAT_cc00500a;
    while ((ctrl & 0x200) != 0) {                // wait DMA (0x200) idle
        ctrl = DAT_cc00500a;
    }

    DAT_cc00500a = 0x8ac;                        // reset image
    DAT_cc005000 = 0;
    do {
        ctrl = DAT_cc005004;
    } while ((ctrl & 0x8000) != 0);              // wait mailbox drained

    t0 = os_get_time_base();
    do {
        t1 = os_get_time_base();
    } while (t1 - t0 < 0x2c);

    ctrl = DAT_cc00500a;
    DAT_cc00500a = (ushort)(ctrl | 1);           // go
    ctrl = DAT_cc00500a;
    while ((ctrl & 1) != 0) {                     // wait go clear
        ctrl = DAT_cc00500a;
    }
}
