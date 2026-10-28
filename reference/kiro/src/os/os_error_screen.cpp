// os_error_screen.cpp - Dolphin OS low-level fatal-error display screen
//
// Address: 0x8039b860 | Name: os_error_vi_configure  | Date: 2026-09-14
// Address: 0x8039b95c | Name: os_error_screen_init    | Date: 2026-09-14
// Address: 0x8039bb34 | Name: os_error_screen_render  | Date: 2026-09-14
//
// When the OS hits an unrecoverable condition before (or instead of) the game
// renderer, it brings the video hardware up on its own and paints a full-screen
// error message. os_error_screen_init tears down audio/DSP, re-inits VI, waits a
// couple of frames, quiesces the DVD/EXI, installs the decrementer handler, seeds
// the OS arena, and switch-stacks into os_error_screen_render on a fresh stack.
// os_error_screen_render allocates a framebuffer, fills it with a background
// colour (RGB -> YCbCr via the SDK colour matrix at FLOAT_8061bf**), initialises
// the built-in font (os_font_init), draws the message (os_console_draw_text),
// flushes the framebuffer, OSReports the message, and halts (no return).
// os_error_vi_configure picks the VI render mode from the detected TV format.
//
// NON-MATCHING: the PPC int->float magic-bias idiom
// ((double)CONCAT44(0x43300000, x) - bias) and the YCbCr colour-matrix
// evaluation are transcribed verbatim; the varargs FP-spill around the final
// OSReport is dropped to match the real OSReport(const char*, ...) prototype.

#include "common.h"

extern "C" {
extern ushort DAT_cc00206c;   // VI status register (field/interlace bit)
extern uint   DAT_cc00680c;   // AI/DMA busy status
extern int    DAT_800000f8;   // bus clock speed (for the spin timeout)
extern int    DAT_80000038;   // arena-hi seed (top of usable RAM)
extern undefined1 DAT_805f0000;
extern undefined1 DAT_80398658;   // decrementer exception handler body
extern int    DAT_805f2758;   // error-screen background RGB
extern int    DAT_805f275c;   // error-screen foreground RGB
extern char  *DAT_805f2760;   // error message string
extern int    DAT_806113e0;   // OSReport format string ("%s")

// YCbCr colour-matrix + clamp constants for the background fill.
extern float  FLOAT_8061bfa0;
extern float  FLOAT_8061bfa4;
extern float  FLOAT_8061bfa8;
extern float  FLOAT_8061bfac;
extern float  FLOAT_8061bfb0;
extern float  FLOAT_8061bfb4;
extern float  FLOAT_8061bfb8;
extern float  FLOAT_8061bfbc;
extern float  FLOAT_8061bfc0;
extern float  FLOAT_8061bfc4;
extern float  FLOAT_8061bfc8;
extern float  FLOAT_8061bfcc;
extern float  FLOAT_8061bfd0;
extern double DOUBLE_8061bfd8;

// Context slot + callback storage for the error path.
extern int    DAT_805f2768;   // error-screen thread context block
extern int    DAT_805f2758_ctx;
extern int    DAT_805f275c_ctx;
extern int    DAT_805f2760_ctx;

// VIConfigure(rmode) / VIInit(void) are declared in sdk_stubs.h; the original
// call sites carry the PPC varargs FP-spill which is dropped here.

// Named OS helpers (real signatures per generated_decls.h; declared per-file
// because the os/ tree does not route through a module shim header).
extern void os_clear_context(u32 ctx);
extern void os_set_current_context(u32 ctx);
extern void os_dsp_halt(void);
extern void *os_set_exception_handler(unsigned int index, void *handler);
extern void os_dsp_heap_init(u32 base);
extern void os_set_arena_hi(unsigned int hi);
extern unsigned int os_get_arena_hi(void);
extern unsigned int os_get_arena_lo(void);
extern void os_switch_stack_call(void (*func)(void), u32 stack_top);
extern void os_console_draw_text(int fb, uint w, uint h, byte *colour, uint x, int y, int adv,
                                 char *text);
extern void os_halt(void);
extern void OSReport(const char *fmt, ...);   // also declared in sdk_stubs.h

// Permissive raw helpers (resolve as int(...) via generated_externs.h in the
// full build; declared here for the per-file syntax compile).
extern int  os_get_frame_count(void);
extern int  os_dsp_heap_alloc(int size, int align);
extern void os_dc_flush_range(uint addr, int len);
extern int  msl_strlen(int s);
extern uint msl_memmove(uint dst, uint src, uint len);

// DVD / VI / audio bring-up + teardown plumbing (raw entry points).
extern int  FUN_803a7b08(void);        // detect TV format (NTSC/PAL/...)
extern void FUN_803a737c(int, int, int, int);
extern void FUN_8039cca0(void);        // OSDisableInterrupts
extern void FUN_8039f35c(void);
extern void FUN_803a9c14(int);
extern u64  FUN_803a9c40(int);
extern void FUN_8039d0f0(int);
extern void FUN_803a78ac(int);
extern u64  FUN_803a7710(void);
extern void FUN_803a5ec8(int);
extern void FUN_803a5f0c(int);
extern void FUN_8039ccb4(void);
extern u64  FUN_803a0c30(void);        // read time-base
extern int  FUN_8039db2c(int);
extern void FUN_803b930c(int, int);
extern int  FUN_803b9fb8(int, int, int);
extern void FUN_803b9078(int);
extern void FUN_803b9924(int);
extern void FUN_803ba0ac(int);
extern void FUN_803c6d78(void);
extern u64  FUN_803a7840(void *fb);

// Font subsystem (os_font.cpp).
extern int  os_font_init(int sheetBuf, char *romBuf);
}

// The error-screen render entry that runs on the freshly-allocated OS stack.
void os_error_screen_render(void);

// VIConfigure wrapper: choose a render-mode config block from the detected TV
// format then configure VI and clear to black.
void os_error_vi_configure(undefined8 param_1, double param_2, double param_3, double param_4,
                           double param_5, double param_6, double param_7, double param_8,
                           undefined2 param_9, undefined4 param_10, undefined4 param_11,
                           undefined4 param_12, undefined4 param_13, undefined4 param_14,
                           undefined4 param_15, undefined4 param_16)
{
    ushort viStat;
    int tvFormat;
    uint local_3c;
    undefined2 local_38;
    undefined2 local_36;
    undefined2 local_34;
    undefined2 local_32;
    undefined2 local_30;
    undefined2 local_2e;
    undefined2 local_2c;
    undefined4 local_28;

    local_36 = 0x1e0;
    local_34 = (undefined2)param_10;
    local_32 = 0x28;
    local_2e = 0x280;
    local_38 = param_9;
    local_2c = local_34;
    tvFormat = FUN_803a7b08();
    if (tvFormat != 2) {
        if (1 < tvFormat) {
            if (tvFormat == 5) {
                local_3c = 0x14;
                local_30 = 0;
                local_28 = 1;
            }
            goto configure;
        }
        if (tvFormat != 0) {
            if (-1 < tvFormat) {
                local_3c = 4;
                local_30 = 0x2f;
                local_28 = 1;
            }
            goto configure;
        }
    }
    viStat = DAT_cc00206c;
    if ((viStat & 1) == 0) {
        local_3c = 0;
        local_30 = 0;
        local_28 = 1;
    } else {
        local_3c = 2;
        local_30 = 0;
        local_28 = 0;
    }
configure:
    VIConfigure(&local_3c);
    FUN_803a737c(0, 0, 0x280, 0x1e0);
}

// OSFatal-style error-screen setup: quiesce audio/DSP, re-init VI, wait for the
// display to settle, reset DVD/EXI, install the decrementer handler, seed the OS
// arena and font heap, stash the message + colours, then switch onto a fresh
// stack and run os_error_screen_render (which never returns).
void os_error_screen_init(undefined8 param_1, undefined8 param_2, undefined8 param_3,
                          undefined8 param_4, undefined8 param_5, undefined8 param_6,
                          undefined8 param_7, undefined8 param_8, undefined4 *bgColor,
                          undefined4 *fgColor, undefined4 msg, undefined4 param_12,
                          undefined4 param_13, undefined4 param_14, undefined4 param_15,
                          undefined4 param_16)
{
    uint aiStat;
    int f0;
    int f1;
    bool done;
    undefined *lowMem;
    u64 vi;
    u64 t0;
    u64 t1;
    uint timeoutTicks;

    lowMem = &DAT_805f0000;
    FUN_8039cca0();
    FUN_8039f35c();
    os_clear_context(0x805f2768);
    os_set_current_context(0x805f2768);
    os_dsp_halt();
    FUN_803a9c14(0);
    vi = FUN_803a9c40(0);
    (void)vi; (void)lowMem; (void)msg; (void)param_13; (void)param_14; (void)param_15; (void)param_16;
    VIInit();
    FUN_8039d0f0(0x80);
    FUN_803a78ac(1);
    FUN_803a7710();
    FUN_803a5ec8(0);
    FUN_803a5f0c(0);
    FUN_8039ccb4();
    f0 = os_get_frame_count();
    do {
        f1 = os_get_frame_count();
    } while (f1 - f0 < 1);

    t0 = FUN_803a0c30();
    timeoutTicks = (uint)DAT_800000f8 / 4000;
    do {
        done = FUN_8039db2c(0) != 0;
        if (done)
            break;
        t1 = FUN_803a0c30();
    } while (((int)((u64)t1 >> 0x20)
              - ((uint)((uint)t1 < (uint)t0) + (int)((u64)t0 >> 0x20)) ^ 0x80000000)
             < ((uint)((uint)t1 - (uint)t0 < timeoutTicks * 1000) + 0x80000000));

    FUN_8039cca0();
    FUN_8039db2c(1);
    FUN_803b930c(0, 0);
    FUN_803b930c(2, 0);
    while (FUN_803b9fb8(0, 1, 0) == 0) {
        FUN_803b9078(0);
        FUN_803b9924(0);
        FUN_803ba0ac(0);
    }
    FUN_803ba0ac(0);
    do {
        aiStat = DAT_cc00680c;
    } while ((aiStat & 1) == 1);

    os_set_exception_handler(8, &DAT_80398658);
    FUN_803c6d78();
    os_dsp_heap_init(0x81400000);
    os_set_arena_hi(DAT_80000038);
    DAT_805f2758 = *bgColor;
    DAT_805f275c = *fgColor;
    DAT_805f2760 = (char *)(uintptr_t)msg;
    f0 = os_get_arena_hi();
    os_switch_stack_call(os_error_screen_render, (u32)f0);
}

// The error-screen renderer: relocate the message, build a framebuffer, fill it
// with the background colour (RGB->YCbCr via the SDK colour matrix), init the
// built-in font, draw the message, flush, OSReport it, and halt. No return.
void os_error_screen_render(void)
{
    unsigned char yBg;
    unsigned char cbBg;
    unsigned char crBg;
    int len;
    uint sheetBuf;
    char *msg;
    undefined1 *fb;
    undefined1 *row;
    uint glyphW;
    int r;
    int c;
    u64 fbHandle;
    double dR, dG, dB, dY, dCb, dCr, dClampHi, dClampLo, dTmp;
    uint rgb;
    uint compR;
    uint compG;
    uint compB;
    byte colour[4];

    FUN_8039ccb4();
    len = msl_strlen((int)DAT_805f2760);
    sheetBuf = os_dsp_heap_alloc(len + 1, 0x20);
    DAT_805f2760 = (char *)(uintptr_t)msl_memmove(sheetBuf, (uint)(uintptr_t)DAT_805f2760, len + 1);

    sheetBuf = os_dsp_heap_alloc(0xa1004, 0x20);
    msg = (char *)(uintptr_t)os_get_arena_lo();
    os_font_init(sheetBuf, msg);

    fb = (undefined1 *)(uintptr_t)os_dsp_heap_alloc(0x96000, 0x20);

    // --- Background fill: convert DAT_805f275c (RGB) to a YCbCr pixel pair. ---
    rgb = DAT_805f275c;
    compB = DAT_805f275c >> 0x18;
    compG = DAT_805f275c >> 0x10 & 0xff;
    compR = DAT_805f275c >> 8 & 0xff;
    dTmp = (double)FLOAT_8061bfa4;
    dCr = (double)(FLOAT_8061bfb8 * (float)((double)CONCAT44(0x43300000, compG) - DOUBLE_8061bfd8));
    dCb = (double)(FLOAT_8061bfc4 * (float)((double)CONCAT44(0x43300000, compB) - DOUBLE_8061bfd8));
    dClampHi = (double)FLOAT_8061bfb4;
    dR = (double)(FLOAT_8061bfb8 * (float)((double)CONCAT44(0x43300000, compR) - DOUBLE_8061bfd8)
                  + (FLOAT_8061bfbc * (float)((double)CONCAT44(0x43300000, compG) - DOUBLE_8061bfd8)
                     - FLOAT_8061bfc0 * (float)((double)CONCAT44(0x43300000, compB) - DOUBLE_8061bfd8)));
    dY = (double)(FLOAT_8061bfa0
                  + (float)(dTmp + (double)(FLOAT_8061bfa8 * (float)((double)CONCAT44(0x43300000, compR) - DOUBLE_8061bfd8)
                                            + FLOAT_8061bfac * (float)((double)CONCAT44(0x43300000, compG) - DOUBLE_8061bfd8)
                                            + FLOAT_8061bfb0 * (float)((double)CONCAT44(0x43300000, compB) - DOUBLE_8061bfd8))));
    dClampLo = (double)FLOAT_8061bfcc;
    dG = (double)(FLOAT_8061bfa0 + (float)(dClampHi + dR));
    dB = (double)(FLOAT_8061bfa0
                  + (float)(dClampHi + (double)((float)(dCr - dCb)
                                                - FLOAT_8061bfc8 * (float)((double)CONCAT44(0x43300000, compR) - DOUBLE_8061bfd8))));
    if ((dY <= dClampLo) && (dClampLo = dY, dY < dTmp)) {
        dClampLo = dTmp;
    }
    dTmp = (double)FLOAT_8061bfd0;
    yBg = (unsigned char)(int)dClampLo;
    if ((dG <= dTmp) && (dTmp = dG, dG < (double)FLOAT_8061bfa4)) {
        dTmp = (double)FLOAT_8061bfa4;
    }
    cbBg = (unsigned char)(int)dTmp;
    dY = (double)FLOAT_8061bfd0;
    if ((dB <= dY) && (dY = dB, dB < (double)FLOAT_8061bfa4)) {
        dY = (double)FLOAT_8061bfa4;
    }
    crBg = (unsigned char)(uint)dY;

    // Fill 0x1e0 rows of 0x28 8-pixel YUYV cells with the background pair.
    r = 0;
    row = fb;
    do {
        int cell = 0x28;
        do {
            for (int k = 0; k < 0x20; k += 4) {
                row[k]     = yBg;
                row[k + 1] = cbBg;
                row[k + 2] = yBg;
                row[k + 3] = crBg;
            }
            row = row + 0x20;
            cell = cell + -1;
        } while (cell != 0);
        r = r + 1;
    } while (r < 0x1e0);

    fbHandle = FUN_803a7840(fb);
    os_error_vi_configure(fbHandle, dB, dG, dCb, dR, dCr, dClampHi, dTmp, 0x280, 0x1e0,
                          (uint)cbBg, (uint)crBg, 0, 0, 0, 0);
    FUN_803a7710();
    r = os_get_frame_count();
    do {
        c = os_get_frame_count();
    } while (c - r < 2);

    // Foreground text colour word (DAT_805f2758) -> the {luma,chroma,chroma} triple.
    colour[0] = yBg;      // reuse computed luma for the message
    colour[1] = cbBg;
    colour[2] = crBg;
    colour[3] = 0;
    glyphW = (uint)*(ushort *)(sheetBuf + 0xe);
    os_console_draw_text((int)(uintptr_t)fb, 0x280, 0x1e0, colour, 0x30, 100, glyphW, DAT_805f2760);
    os_dc_flush_range((uint)(uintptr_t)fb, 0x96000);
    FUN_803a78ac(0);
    FUN_803a7710();
    r = os_get_frame_count();
    do {
        c = os_get_frame_count();
    } while (c - r < 1);
    FUN_8039cca0();
    OSReport((const char *)&DAT_806113e0, DAT_805f2760);
    // WARNING: Subroutine does not return
    os_halt();
}
