// TSFP GameCube VI (Video Interface) retrace layer (Dolphin SDK VI, from main.dol).
//
// Address: 0x803a5c54 | Name: vi_retrace_handler          | Date: 2026-09-15
// Address: 0x803a5ec8 | Name: vi_set_pre_retrace_callback | Date: 2026-09-15
//
// vi_retrace_handler is the VI display-interrupt handler installed by VIInit.
// The VI has four display-interrupt registers (DI0..DI3) at 0xcc002030..3c; the
// top bit of each is the interrupt-active flag which the handler clears. DI0/DI1
// mark the normal per-field retrace; DI2/DI3 are the pre-retrace "position"
// interrupts. On a normal retrace the handler swaps in a scratch OS context,
// runs the registered pre-retrace callback, flushes any register writes staged
// in the shadow ("SR") register set into the live VI registers (0xcc002000...),
// runs the post-retrace callback, and wakes threads waiting on retrace. On a
// position interrupt it instead runs the position callback.
//
// vi_set_pre_retrace_callback installs a new pre-retrace callback under an
// interrupt lock and returns the previous one (VISetPreRetraceCallback).
//
// NON-MATCHING: the VI register file, the shadow-register staging buffers, and
// the VI driver state block are reached through per-file externs; control flow
// is preserved verbatim.

#include "common.h"

extern "C" {

// ---- VI hardware registers (0xcc002000 block) ----
extern volatile unsigned short DAT_cc002000;   // VI register file base (shadow flush dest)
extern volatile unsigned short DAT_cc002030;   // display interrupt reg 0 (DI0)
extern volatile unsigned short DAT_cc002034;   // display interrupt reg 1 (DI1)
extern volatile unsigned short DAT_cc002038;   // display interrupt reg 2 (DI2)
extern volatile unsigned short DAT_cc00203c;   // display interrupt reg 3 (DI3)

// ---- VI driver state block (0x806131xx) ----
extern int          DAT_806131ec;   // retrace count
extern int          DAT_806131f0;   // "shadow registers dirty" flag
extern void       (*DAT_806131fc)(int retraceCount);  // pre-retrace callback
extern void       (*DAT_80613200)(int retraceCount);  // post-retrace callback
extern void       (*DAT_80613204)(int x, int y);      // position callback
extern int          DAT_806131f4;   // retrace wait thread queue
extern int          DAT_80613220;   // "full register set needs reprogramming" flag
extern unsigned int DAT_80613228;   // shadow-register dirty bitmask (low)
extern unsigned int DAT_8061322c;   // shadow-register dirty bitmask (high)
extern int          DAT_80613230;   // staged reg copy
extern int          DAT_80613234;   // staged reg copy
extern int          DAT_80613238;   // staged reg copy source
extern int          DAT_8061323c;   // staged reg copy dest

// ---- shadow register source table (0x805f37xx) ----
extern short DAT_805f3700;   // shadow register value table
extern int   DAT_805f37a0;   // staged framebuffer/reg value
extern int   DAT_805f37cc;   // staged reg value

// ---- helpers (resolve via generated decls) ----
extern void          os_clear_context(int ctx);
extern int           os_set_current_context(unsigned int ctx);
extern void          os_wakeup_thread(int *queue);
extern void          os_disable_interrupts(void);
extern int           os_get_interrupts_enabled(void);
extern int           __shr2u(unsigned int hi, unsigned int lo, int shift);
extern long long     __shl2i(int hi, int lo, int shift);
extern int           vi_reprogram_all(void);         // FUN_803a796c
extern void          vi_read_beam_position(short *x, short *y);  // FUN_803a7dc8
extern void          vi_flush_shadow_regs(int r3, int r4, int r5, int r6,
                                          int r7, int r8, int r9, int r10,
                                          int a, int b, void *c, unsigned int d,
                                          unsigned int e, int f, int g,
                                          int h);   // FUN_803bc304

// ============================================================================
// vi_retrace_handler (0x803a5c54): VI display-interrupt handler. Acks DI0..DI3,
// then services a normal retrace or a position interrupt.
// ============================================================================
void vi_retrace_handler(int r3, int r4, int r5, int r6, int r7, int r8, int r9,
                        int r10, int r11, unsigned int savedCtx)
{
    bool di0Active;
    unsigned short uVar2;
    unsigned int uVar3;
    int iVar4;
    int uVar5;
    void *puVar6;
    unsigned int uVar7;
    unsigned int diMask;
    long long shiftHi;
    unsigned short posX;
    short posY;
    unsigned char scratchCtx[716];

    puVar6 = (void *)&DAT_cc002000;
    uVar2 = DAT_cc002030;
    uVar7 = (unsigned int)uVar2;
    di0Active = (uVar2 & 0x8000) != 0;
    if (di0Active) {
        DAT_cc002030 = uVar2 & 0x7fff;
    }
    diMask = (unsigned int)di0Active;
    uVar2 = DAT_cc002034;
    if ((uVar2 & 0x8000) != 0) {
        DAT_cc002034 = uVar2 & 0x7fff;
        diMask = diMask | 2;
    }
    uVar2 = DAT_cc002038;
    if ((uVar2 & 0x8000) != 0) {
        DAT_cc002038 = uVar2 & 0x7fff;
        diMask = diMask | 4;
    }
    uVar2 = DAT_cc00203c;
    if ((uVar2 & 0x8000) != 0) {
        DAT_cc00203c = uVar2 & 0x7fff;
        diMask = diMask | 8;
    }

    if (((diMask & 4) == 0) && ((diMask & 8) == 0)) {
        // Normal per-field retrace.
        DAT_806131ec = DAT_806131ec + 1;
        os_clear_context((int)scratchCtx);
        os_set_current_context((unsigned int)scratchCtx);
        if (DAT_806131fc != (void (*)(int))0x0) {
            (*DAT_806131fc)(DAT_806131ec);
        }
        if (DAT_806131f0 != 0) {
            if ((DAT_80613220 == 1) && (iVar4 = vi_reprogram_all(), iVar4 == 0)) {
                iVar4 = 0;
                uVar5 = 0;
            }
            else {
                for (; uVar3 = DAT_8061322c,
                       DAT_8061322c != 0 || DAT_80613228 != 0;
                     DAT_8061322c = DAT_8061322c & ~(unsigned int)shiftHi) {
                    shiftHi = (long long)__shr2u(DAT_80613228, DAT_8061322c, 0x20);
                    iVar4 = countLeadingZeros((int)shiftHi);
                    if (0x1f < iVar4) {
                        iVar4 = countLeadingZeros(uVar3);
                        iVar4 = iVar4 + 0x20;
                    }
                    uVar7 = iVar4 * 2;
                    (&DAT_cc002000)[iVar4] =
                        *(unsigned short *)(&DAT_805f3700 + uVar7);
                    shiftHi = __shl2i(0, 1, 0x3f - iVar4);
                    puVar6 = (void *)~(unsigned int)((unsigned long long)shiftHi >> 0x20);
                    DAT_80613228 = DAT_80613228 & (unsigned int)puVar6;
                }
                DAT_80613220 = 0;
                iVar4 = 1;
                DAT_80613230 = DAT_805f37cc;
                DAT_80613234 = DAT_805f37a0;
                DAT_8061323c = DAT_80613238;
                uVar5 = DAT_805f37a0;
            }
            if (iVar4 != 0) {
                DAT_806131f0 = 0;
                vi_flush_shadow_regs(r3, r4, r5, r6, r7, r8, r9, r10, uVar5,
                                     iVar4, puVar6, uVar7, diMask, r8, r9, r10);
            }
        }
        if (DAT_80613200 != (void (*)(int))0x0) {
            os_clear_context((int)scratchCtx);
            (*DAT_80613200)(DAT_806131ec);
        }
        os_wakeup_thread(&DAT_806131f4);
        os_clear_context((int)scratchCtx);
        os_set_current_context(savedCtx);
    }
    else {
        // Position ("pre-retrace") interrupt.
        os_clear_context((int)scratchCtx);
        os_set_current_context((unsigned int)scratchCtx);
        if (DAT_80613204 != (void (*)(int, int))0x0) {
            vi_read_beam_position(&posY, (short *)&posX);
            (*DAT_80613204)((int)posY, (int)(short)posX);
        }
        os_clear_context((int)scratchCtx);
        os_set_current_context(savedCtx);
    }
}

// ============================================================================
// vi_set_pre_retrace_callback (0x803a5ec8): install a new pre-retrace callback
// under an interrupt lock and return the previous one (VISetPreRetraceCallback).
// ============================================================================
void *vi_set_pre_retrace_callback(void *cb)
{
    void *prev;

    prev = (void *)DAT_806131fc;
    os_disable_interrupts();
    DAT_806131fc = (void (*)(int))cb;
    os_get_interrupts_enabled();
    return prev;
}

} // extern "C"

// ============================================================================
// batch_1125: the rest of the VI driver - timing selection/programming, init,
// render-mode configure, framebuffer & geometry staging, retrace wait, and the
// shadow-register flush. Appended to video/vi.cpp.
//
// Address: 0x803a5f0c | Name: vi_set_post_retrace_callback | Date: 2026-09-15
// Address: 0x803a5f50 | Name: vi_get_timing                | Date: 2026-09-15
// Address: 0x803a5ff8 | Name: vi_program_timing            | Date: 2026-09-15
// Address: 0x803a61fc | Name: VIInit                       | Date: 2026-09-15
// Address: 0x803a66ac | Name: VIWaitForRetrace             | Date: 2026-09-15
// Address: 0x803a6700 | Name: vi_calc_framebuffers         | Date: 2026-09-15
// Address: 0x803a69d4 | Name: vi_calc_horizontal_regs      | Date: 2026-09-15
// Address: 0x803a6b74 | Name: VIConfigure                  | Date: 2026-09-15
// Address: 0x803a737c | Name: vi_set_display_geometry      | Date: 2026-09-15
// Address: 0x803a7710 | Name: vi_flush                     | Date: 2026-09-15
//
// The VI driver keeps a large "shadow" register set in RAM (the 0x805f36xx /
// 0x805f37xx block) that the game stages writes into; vi_flush marks them dirty
// and the retrace handler (above) copies them into the live VI register file
// (0xcc0020xx) at the next field. VIConfigure / vi_set_display_geometry
// recompute the whole shadow set from a render-mode object; vi_program_timing
// writes the VI timing registers directly (used before the shadow machinery is
// up). Timing parameter blocks are selected by TV-format index via vi_get_timing.
//
// NON-MATCHING: VIInit/VIConfigure/VIWaitForRetrace/VIFlush keep their clean SDK
// signatures (the r3..r10 args are variadic/ABI artifacts, dropped); the VI
// register file, shadow-register block, driver state, and timing tables are
// reached through per-file externs; control flow is preserved verbatim.

extern "C" {

// OSReport is declared in sdk_stubs.h; OSPanic is not.
extern void OSPanic(const char *file, int line, const char *fmt, ...);

// ---- low-memory global ----
extern unsigned int DAT_800000cc;   // current TV format (bus-clock derived)

// ---- VI hardware register file (0xcc0020xx), 16-bit each ----
extern volatile unsigned short DAT_cc002002;
extern volatile unsigned short DAT_cc002004;
extern volatile unsigned short DAT_cc002006;
extern volatile unsigned short DAT_cc002008;
extern volatile unsigned short DAT_cc00200a;
extern volatile unsigned short DAT_cc00200c;
extern volatile unsigned short DAT_cc00200e;
extern volatile unsigned short DAT_cc002010;
extern volatile unsigned short DAT_cc002012;
extern volatile unsigned short DAT_cc002014;
extern volatile unsigned short DAT_cc002016;
extern volatile unsigned short DAT_cc002018;
extern volatile unsigned short DAT_cc00201a;
extern volatile unsigned short DAT_cc002032;
extern volatile unsigned short DAT_cc002036;
extern volatile unsigned short DAT_cc002048;
extern volatile unsigned short DAT_cc00204c;
extern volatile unsigned short DAT_cc00204e;
extern volatile unsigned short DAT_cc002050;
extern volatile unsigned short DAT_cc002052;
extern volatile unsigned short DAT_cc002054;
extern volatile unsigned short DAT_cc002056;
extern volatile unsigned short DAT_cc002058;
extern volatile unsigned short DAT_cc00205a;
extern volatile unsigned short DAT_cc00205c;
extern volatile unsigned short DAT_cc00205e;
extern volatile unsigned short DAT_cc002060;
extern volatile unsigned short DAT_cc002062;
extern volatile unsigned short DAT_cc002064;
extern volatile unsigned short DAT_cc002066;
extern volatile unsigned short DAT_cc00206c;
extern volatile unsigned short DAT_cc002070;
// DAT_cc002000/2030/2034 are already declared at the top of this file.

// ---- VI driver state block (0x806131xx / 0x806132xx) ----
extern int          DAT_806131e8;   // "VIInit already ran" latch
extern int          DAT_80613208;   // "black-and-mode-valid" flag
extern int          DAT_8061320c;   // SRAM display offset (H)
extern short        DAT_8061320e;   // SRAM display offset (V)
extern int          DAT_80613210;   // pending "black" change flag
extern int          DAT_80613218;   // pending framebuffer-register change bitmask
extern int          DAT_8061321c;   // pending timing-register change bitmask
extern unsigned int DAT_80613228;   // retrace-handler FB dirty mask
extern unsigned int DAT_8061322c;   // retrace-handler timing dirty mask
extern int          DAT_80613230;   // staged timing block copy
extern int          DAT_80613234;   // staged mode copy
extern int          DAT_80613238;   // staged extra value
extern int          DAT_80613240;   // "framebuffer set" flag
extern int          DAT_80613244;   // custom timing block pointer (formats 0x1c..)
extern int          DAT_80613248;   // "PAL debug warning shown" latch
// DAT_806131ec (retrace count), DAT_806131f0 (shadow dirty), DAT_806131f4
// (retrace wait queue), DAT_806131fc (pre cb), DAT_80613200 (post cb),
// DAT_80613220 (reprogram-all flag) are declared at the top of this file.

// ---- VI shadow register / geometry state (0x805f36xx / 0x805f37xx) ----
extern short          DAT_805f3688;  // display-config shadow (VICLK/config)
extern short          DAT_805f368a;  // display-config working copy
extern unsigned short DAT_805f368c;
extern unsigned short DAT_805f368e;
extern short          DAT_805f3690;
extern short          DAT_805f3692;
extern short          DAT_805f3694;
extern short          DAT_805f3696;
extern short          DAT_805f3698;
extern short          DAT_805f369a;
extern unsigned short DAT_805f369c;
extern unsigned short DAT_805f369e;
extern unsigned short DAT_805f36a0;
extern unsigned short DAT_805f36a2;
extern short          DAT_805f36a4;
extern short          DAT_805f36a6;
extern short          DAT_805f36a8;
extern short          DAT_805f36aa;
extern short          DAT_805f36ac;
extern short          DAT_805f36ae;
extern short          DAT_805f36b0;
extern short          DAT_805f36b2;
extern short          DAT_805f36b8;
extern short          DAT_805f36ba;
extern unsigned short DAT_805f36d0;
extern unsigned short DAT_805f36d2;
extern unsigned short DAT_805f36f4;  // field-parity / progressive flags
extern unsigned short DAT_805f36f8;
extern short          DAT_805f377a;
extern short          DAT_805f377c;
extern short          DAT_805f377e;
extern short          DAT_805f3778;
extern short          DAT_805f3780;
extern unsigned short DAT_805f3782;
extern short          DAT_805f3784;
extern short          DAT_805f3786;
extern short          DAT_805f3788;
extern short          DAT_805f378a;
extern short          DAT_805f378c;
extern short          DAT_805f378e;
extern short          DAT_805f3790;
extern short          DAT_805f3792;
extern short          DAT_805f3794;
extern int            DAT_805f3798;  // "2fb / 3D" flag
extern unsigned int   DAT_805f379c;  // interlace/progressive selector
extern int            DAT_805f37a4;  // filter tap
extern unsigned char  DAT_805f37a5;
extern unsigned char  DAT_805f37a6;
extern int            DAT_805f37a8;  // staged extra value source
extern int            DAT_805f37b4;
extern int            DAT_805f37b8;  // adjust flag
extern int            DAT_805f37bc;  // "3D" flag
extern int            DAT_805f37cc;  // current timing block pointer
// DAT_805f37a0 (current format), DAT_805f3700 (shadow reg mirror table) are
// declared at the top of this file.

// FB-calc output shadow words (contiguous with the block above).
extern int            DAT_805f37ac;
extern int            DAT_805f37b0;
extern int            DAT_805f37c4;
extern int            DAT_805f37c8;

// ---- filter-coefficient / timing source tables (0x8049a7xx) ----
extern unsigned short DAT_8049a760, DAT_8049a762, DAT_8049a764, DAT_8049a766;
extern unsigned short DAT_8049a768, DAT_8049a76a, DAT_8049a76c, DAT_8049a76e;
extern unsigned short DAT_8049a770, DAT_8049a772, DAT_8049a774, DAT_8049a776;
extern unsigned short DAT_8049a778, DAT_8049a77a, DAT_8049a77c, DAT_8049a77e;
extern unsigned short DAT_8049a780, DAT_8049a782, DAT_8049a784, DAT_8049a786;
extern unsigned short DAT_8049a788, DAT_8049a78a, DAT_8049a78c, DAT_8049a78e;
extern unsigned short DAT_8049a790;

// ---- timing parameter blocks (byte-addressed) ----
extern unsigned char DAT_8049a5e4, DAT_8049a60a, DAT_8049a630, DAT_8049a656;
extern unsigned char DAT_8049a67c, DAT_8049a6a2, DAT_8049a6c8, DAT_8049a6ee;
extern unsigned char DAT_8049a714, DAT_8049a73a;

// ---- version string + panic/report literals ----
extern char DAT_80611440;   // VIInit version string
extern char DAT_80611444;   // VIConfigure panic file
extern char s__________________________________8049a810;
extern char s_______C_A_U_T_I_O_N_______8049a83c;
extern char s_This_TV_format__DEBUG_PAL__is_on_8049a868;
extern char s_temporary_solution_until_PAL_DAC_8049a894;
extern char s_is_available__Please_do_NOT_use_t_8049a8c0;
extern char s_mode_in_real_games____8049a8ec;
extern char s_VIConfigure____Tried_to_change_m_8049a918;

// ---- helpers (resolve via generated decls) ----
extern void            os_register_version(char *fmt);
extern void            os_sleep_thread(int *queue);
extern unsigned short *os_sram_lock(void);
extern void            os_sram_commit(int recompute);
extern int             os_init_thread_queue(int *queue);
extern void            os_set_interrupt_handler(int irq, void *handler);
extern void            os_unmask_interrupts(unsigned int mask);

// Forward decls for the intra-module callers below.
unsigned char *vi_get_timing(unsigned int format);
void vi_program_timing(unsigned int format);
void vi_calc_framebuffers(int rmode, unsigned int *tf, unsigned int *bf,
                          unsigned int *tf3d, unsigned int *bf3d);
void vi_calc_horizontal_regs(unsigned short width, unsigned short prevWidth,
                             unsigned short cfg, short p4, short p5, short p6,
                             short p7, short p8, int adjust);

// ============================================================================
// vi_set_post_retrace_callback (0x803a5f0c): install a new post-retrace
// callback under an interrupt lock and return the previous one.
// ============================================================================
void *vi_set_post_retrace_callback(void *cb)
{
    void *prev = (void *)DAT_80613200;
    os_disable_interrupts();
    DAT_80613200 = (void (*)(int))cb;
    os_get_interrupts_enabled();
    return prev;
}

// ============================================================================
// vi_get_timing (0x803a5f50): return the VI timing parameter block for a
// TV-format index, the custom block for 0x1c..0x1e, or NULL for unknown.
// ============================================================================
unsigned char *vi_get_timing(unsigned int format)
{
    switch (format) {
    case 0:    return &DAT_8049a5e4;
    case 1:    return &DAT_8049a60a;
    case 2:    return &DAT_8049a6c8;
    case 3:    return &DAT_8049a6ee;
    case 4:    return &DAT_8049a630;
    case 5:    return &DAT_8049a656;
    default:   return (unsigned char *)0x0;
    case 8:    return &DAT_8049a67c;
    case 9:    return &DAT_8049a6a2;
    case 0x10: return &DAT_8049a630;
    case 0x11: return &DAT_8049a656;
    case 0x14: return &DAT_8049a5e4;
    case 0x15: return &DAT_8049a60a;
    case 0x18: return &DAT_8049a714;
    case 0x1a: return &DAT_8049a73a;
    case 0x1c:
    case 0x1d:
    case 0x1e: return (unsigned char *)DAT_80613244;
    }
}

// ============================================================================
// vi_program_timing (0x803a5ff8): write the VI timing registers directly from a
// timing block for the given format, resetting the display interface first.
// ============================================================================
void vi_program_timing(unsigned int format)
{
    unsigned char *t;
    unsigned int fmt;
    unsigned int i;

    fmt = format >> 2;
    DAT_800000cc = fmt;
    t = vi_get_timing(format);
    DAT_cc002002 = 2;
    for (i = 0; i < 1000; i = i + 8) {
    }
    DAT_cc002002 = 0;
    DAT_cc002006 = *(unsigned short *)(t + 0x1a);
    DAT_cc002004 = *(unsigned short *)(t + 0x1d);
    DAT_cc00200a = (unsigned short)(t[0x1c] | (t[0x1f] << 7));
    DAT_cc002008 = *(short *)(t + 0x20) << 1;
    DAT_cc002000 = (unsigned short)*t;
    DAT_cc00200e = *(short *)(t + 4) + *(short *)(t + 2) * 2 + -2;
    DAT_cc00200c = *(short *)(t + 8) + 2;
    DAT_cc002012 = *(short *)(t + 6) + *(short *)(t + 2) * 2 + -2;
    DAT_cc002010 = *(short *)(t + 10) + 2;
    DAT_cc002016 = (unsigned short)(t[0xc] | (*(short *)(t + 0x10) << 5));
    DAT_cc002014 = (unsigned short)(t[0xe] | (*(short *)(t + 0x14) << 5));
    DAT_cc00201a = (unsigned short)(t[0xd] | (*(short *)(t + 0x12) << 5));
    DAT_cc002018 = (unsigned short)(t[0xf] | (*(short *)(t + 0x16) << 5));
    DAT_cc002048 = 0x2828;
    DAT_cc002036 = 1;
    DAT_cc002034 = 0x1001;
    DAT_cc002032 = *(short *)(t + 0x1a) + 1;
    DAT_cc002030 = (unsigned short)(((short)((int)(unsigned int)*(unsigned short *)(t + 0x18) >> 1) + 1) | 0x1000);
    if ((3 < fmt) || (fmt == 0)) {
        fmt = 0;
    }
    if ((format & 3) < 2) {
        DAT_cc002002 = (unsigned short)((fmt << 8) | ((format & 3) << 2 & 4) | 1);
        DAT_cc00206c = 0;
    }
    else {
        DAT_cc002002 = (unsigned short)((fmt << 8) | 5);
        DAT_cc00206c = 1;
    }
}

// ============================================================================
// VIInit (0x803a61fc): one-time VI init - see plate comment.
// ============================================================================
void VIInit(void)
{
    short *psVar1;
    unsigned short uVar2;
    unsigned short *puVar3;
    unsigned int uVar4;
    int iVar5;
    int iVar6;
    int iVar7;
    short sVar9;
    int iVar8;
    short sVar10;

    if (DAT_806131e8 == 0) {
        os_register_version(&DAT_80611440);
        DAT_806131e8 = 1;
        DAT_80613208 = 1;
        uVar2 = DAT_cc002002;
        if ((uVar2 & 1) == 0) {
            vi_program_timing(0);
        }
        DAT_806131ec = 0;
        DAT_8061321c = 0;
        DAT_80613218 = 0;
        DAT_8061322c = 0;
        DAT_80613228 = 0;
        DAT_80613210 = 0;
        DAT_80613220 = 0;
        DAT_806131f0 = 0;
        DAT_cc00204e = (unsigned short)(DAT_8049a760 | (DAT_8049a762 << 10));
        DAT_cc00204c = (unsigned short)((int)(unsigned int)DAT_8049a762 >> 6) | (unsigned short)(DAT_8049a764 << 4);
        DAT_cc002052 = (unsigned short)(DAT_8049a766 | (DAT_8049a768 << 10));
        DAT_cc002050 = (unsigned short)((int)(unsigned int)DAT_8049a768 >> 6) | (unsigned short)(DAT_8049a76a << 4);
        DAT_cc002056 = (unsigned short)(DAT_8049a76c | (DAT_8049a76e << 10));
        DAT_cc002054 = (unsigned short)((int)(unsigned int)DAT_8049a76e >> 6) | (unsigned short)(DAT_8049a770 << 4);
        DAT_cc00205a = (unsigned short)(DAT_8049a772 | (DAT_8049a774 << 8));
        DAT_cc002058 = (unsigned short)(DAT_8049a776 | (DAT_8049a778 << 8));
        DAT_cc00205e = (unsigned short)(DAT_8049a77a | (DAT_8049a77c << 8));
        DAT_cc00205c = (unsigned short)(DAT_8049a77e | (DAT_8049a780 << 8));
        DAT_cc002062 = (unsigned short)(DAT_8049a782 | (DAT_8049a784 << 8));
        DAT_cc002060 = (unsigned short)(DAT_8049a786 | (DAT_8049a788 << 8));
        DAT_cc002066 = (unsigned short)(DAT_8049a78a | (DAT_8049a78c << 8));
        DAT_cc002064 = (unsigned short)(DAT_8049a78e | (DAT_8049a790 << 8));
        DAT_cc002070 = 0x280;
        puVar3 = os_sram_lock();
        DAT_8061320e = 0;
        DAT_8061320c = (short)*(char *)(puVar3 + 8);
        os_sram_commit(0);
        uVar2 = DAT_cc002002;
        DAT_805f379c = (unsigned int)(uVar2 >> 2 & 1);
        DAT_805f37a0 = (int)(uVar2 >> 8 & 3);
        if ((DAT_800000cc == 1) && ((uVar2 >> 8 & 3) == 0)) {
            DAT_805f37a0 = 5;
        }
        uVar4 = (unsigned int)DAT_805f37a0;
        if (DAT_805f37a0 == 3) {
            uVar4 = 0;
        }
        DAT_805f37cc = (int)vi_get_timing(uVar4 * 4 + DAT_805f379c);
        DAT_80613234 = DAT_805f37a0;
        DAT_805f377c = 0x280;
        psVar1 = (short *)(DAT_805f37cc + 2);
        DAT_805f377e = *psVar1 * 2;
        DAT_805f3778 = 0x28;
        DAT_805f377a = 0;
        DAT_805f3780 = 0x50;
        iVar5 = DAT_8061320c + 0x28;
        if (iVar5 < 0x51) {
            if (iVar5 < 0) {
                iVar5 = 0;
            }
            DAT_805f3780 = (short)iVar5;
        }
        if (DAT_805f3798 == 0) {
            iVar5 = 2;
        }
        else {
            iVar5 = 1;
        }
        iVar6 = (int)DAT_8061320e;
        DAT_805f3782 = 0;
        if (0 < iVar6) {
            DAT_805f3782 = (unsigned short)DAT_8061320e;
        }
        iVar7 = (int)*psVar1;
        if (DAT_805f377e + iVar6 + iVar7 * -2 < 1) {
            sVar10 = 0;
        }
        else {
            sVar10 = (short)(DAT_805f377e + iVar6) + *psVar1 * -2;
        }
        sVar9 = DAT_8061320e;
        if (-1 < iVar6) {
            sVar9 = 0;
        }
        DAT_805f3784 = (DAT_805f377e + sVar9) - sVar10;
        iVar8 = iVar6;
        if (-1 < iVar6) {
            iVar8 = 0;
        }
        DAT_805f3786 = DAT_805f3790 - (short)(iVar8 / iVar5);
        if (DAT_805f377e + iVar6 + iVar7 * -2 < 1) {
            iVar7 = 0;
        }
        else {
            iVar7 = DAT_805f377e + iVar6 + iVar7 * -2;
        }
        if (-1 < iVar6) {
            iVar6 = 0;
        }
        DAT_805f3788 = (DAT_805f3794 + (short)(iVar6 / iVar5)) - (short)(iVar7 / iVar5);
        DAT_805f378a = 0x280;
        DAT_805f378c = *psVar1 << 1;
        DAT_805f378e = 0;
        DAT_805f3790 = 0;
        DAT_805f3792 = 0x280;
        DAT_805f3794 = *psVar1 << 1;
        DAT_805f3798 = 0;
        DAT_805f37a4 = 0x28;
        DAT_805f37a5 = 0x28;
        DAT_805f37a6 = 0x28;
        DAT_805f37b4 = 0;
        DAT_805f37b8 = 1;
        DAT_805f37bc = 0;
        DAT_805f368a = uVar2;
        DAT_80613230 = DAT_805f37cc;
        os_init_thread_queue(&DAT_806131f4);
        uVar2 = DAT_cc002030;
        DAT_cc002030 = uVar2 & 0x7fff;
        uVar2 = DAT_cc002034;
        DAT_cc002034 = uVar2 & 0x7fff;
        DAT_806131fc = 0;
        DAT_80613200 = 0;
        os_set_interrupt_handler(0x18, (void *)vi_retrace_handler);
        os_unmask_interrupts(0x80);
    }
}

// ============================================================================
// VIWaitForRetrace (0x803a66ac): block until the retrace counter advances.
// ============================================================================
void VIWaitForRetrace(void)
{
    int start;

    os_disable_interrupts();
    start = DAT_806131ec;
    do {
        os_sleep_thread(&DAT_806131f4);
    } while (start == DAT_806131ec);
    os_get_interrupts_enabled();
}

// ============================================================================
// vi_calc_framebuffers (0x803a6700): compute top/bottom (and 3D) framebuffer
// bases from the render config and stage them into the shadow FB registers.
// (Internal helper; the public VISetNextFrameBuffer stub is elsewhere.)
// ============================================================================
void vi_calc_framebuffers(int rmode, unsigned int *tf, unsigned int *bf,
                          unsigned int *tf3d, unsigned int *bf3d)
{
    unsigned short uVar1;
    int iVar2;
    unsigned int uVar3;
    short sVar4;
    int iVar5;

    iVar2 = (unsigned int)*(unsigned char *)(rmode + 0x2c) * 0x20;
    iVar5 = *(int *)(rmode + 0x20);
    uVar1 = *(unsigned short *)(rmode + 10);
    *tf = *(int *)(rmode + 0x30) +
          (*(unsigned short *)(rmode + 0x16) & 0x7ffffff0) * 2 +
          iVar2 * (unsigned int)*(unsigned short *)(rmode + 0xe);
    if (iVar5 == 0) {
        uVar3 = *tf;
    }
    else {
        uVar3 = *tf + iVar2;
    }
    *bf = uVar3;
    if ((unsigned int)uVar1 + ((int)(unsigned int)uVar1 >> 1) * -2 == 1) {
        uVar3 = *tf;
        *tf = *bf;
        *bf = uVar3;
    }
    *tf = *tf & 0x3fffffff;
    *bf = *bf & 0x3fffffff;
    if (*(int *)(rmode + 0x44) != 0) {
        iVar2 = (unsigned int)*(unsigned char *)(rmode + 0x2c) * 0x20;
        iVar5 = *(int *)(rmode + 0x20);
        uVar1 = *(unsigned short *)(rmode + 10);
        *tf3d = *(int *)(rmode + 0x48) +
                (*(unsigned short *)(rmode + 0x16) & 0x7ffffff0) * 2 +
                iVar2 * (unsigned int)*(unsigned short *)(rmode + 0xe);
        if (iVar5 == 0) {
            uVar3 = *tf3d;
        }
        else {
            uVar3 = *tf3d + iVar2;
        }
        *bf3d = uVar3;
        if ((unsigned int)uVar1 + ((int)(unsigned int)uVar1 >> 1) * -2 == 1) {
            uVar3 = *tf3d;
            *tf3d = *bf3d;
            *bf3d = uVar3;
        }
        *tf3d = *tf3d & 0x3fffffff;
        *bf3d = *bf3d & 0x3fffffff;
    }
    if ((((*tf < 0x1000000) && (*bf < 0x1000000)) && (*tf3d < 0x1000000)) &&
        (*bf3d < 0x1000000)) {
        sVar4 = 0;
    }
    else {
        sVar4 = 1;
    }
    if (sVar4 != 0) {
        *tf = *tf >> 5;
        *bf = *bf >> 5;
        *tf3d = *tf3d >> 5;
        *bf3d = *bf3d >> 5;
    }
    DAT_805f36a6 = (short)*tf;
    DAT_805f36a4 = (short)((sVar4 << 0xc) | (unsigned short)(*tf >> 0x10) |
                          (unsigned short)(*(unsigned char *)(rmode + 0x3c) << 8));
    DAT_805f36ae = (short)*bf;
    DAT_805f36ac = (short)(*bf >> 0x10);
    uVar3 = DAT_80613218 | 0x33000;
    if (*(int *)(rmode + 0x44) != 0) {
        DAT_805f36aa = (short)*tf3d;
        DAT_805f36a8 = (short)(*tf3d >> 0x10);
        DAT_805f36b2 = (short)*bf3d;
        DAT_805f36b0 = (short)(*bf3d >> 0x10);
        uVar3 = DAT_80613218 | 0x3fc00;
    }
    DAT_80613218 = uVar3;
}

// ============================================================================
// vi_calc_horizontal_regs (0x803a69d4): compute horizontal blank/burst shadow
// registers from the display width and field parity.
// ============================================================================
void vi_calc_horizontal_regs(unsigned short width, unsigned short prevWidth,
                             unsigned short cfg, short p4, short p5, short p6,
                             short p7, short p8, int adjust)
{
    short sVar1;
    unsigned int uVar2;
    short sVar3;

    if ((DAT_805f36f4 & 1) == 0) {
        uVar2 = 2;
        sVar3 = 1;
    }
    else {
        uVar2 = 1;
        sVar3 = 2;
    }
    if ((unsigned int)width == (unsigned int)(((int)(unsigned int)width >> 1) * 2)) {
        sVar1 = sVar3 * (((short)uVar2 * p4 - prevWidth) - width);
        DAT_805f3696 = p5 + sVar3 * width;
        DAT_805f3694 = p7 + sVar1;
        DAT_805f369a = p6 + sVar3 * width;
        DAT_805f3698 = p8 + sVar1;
    }
    else {
        sVar1 = sVar3 * (((short)uVar2 * p4 - prevWidth) - width);
        DAT_805f3696 = p6 + sVar3 * width;
        DAT_805f3694 = p8 + sVar1;
        DAT_805f369a = p5 + sVar3 * width;
        DAT_805f3698 = p7 + sVar1;
    }
    uVar2 = prevWidth / uVar2;
    if (adjust != 0) {
        sVar3 = (short)uVar2 * 2 + -2;
        DAT_805f3696 = DAT_805f3696 + sVar3;
        DAT_805f369a = DAT_805f369a + sVar3;
        uVar2 = 0;
        DAT_805f3694 = DAT_805f3694 + 2;
        DAT_805f3698 = DAT_805f3698 + 2;
    }
    DAT_805f3688 = (short)((cfg & 0xff) | (unsigned short)(uVar2 << 4));
    DAT_80613218 = DAT_80613218 | 0x83c00000;
}

// ============================================================================
// VIConfigure (0x803a6b74): apply a GXRenderModeObj - see plate comment.
// (Clean SDK signature; the render-mode object is taken as void*.)
// ============================================================================
void VIConfigure(GXRenderModeObj rmode)
{
    unsigned int *param = (unsigned int *)rmode;
    unsigned int uVar1;
    int iVar2;
    unsigned char *pbVar3;
    unsigned int uVar4;
    unsigned int uVar5;
    int iVar7;
    unsigned int uVar8;
    int iVar9;
    unsigned short uVar10;
    short sVar11;
    int iVar12;
    short sVar13;

    os_disable_interrupts();
    uVar4 = DAT_800000cc;
    uVar1 = *param & 3;
    if (DAT_805f379c != uVar1) {
        DAT_80613210 = 1;
        DAT_805f379c = uVar1;
    }
    uVar5 = *param >> 2;
    if ((uVar5 == 4) && (DAT_80613248 == 0)) {
        DAT_80613248 = 1;
        OSReport(&s__________________________________8049a810);
        OSReport(&s_______C_A_U_T_I_O_N_______8049a83c);
        OSReport(&s_This_TV_format__DEBUG_PAL__is_on_8049a868);
        OSReport(&s_temporary_solution_until_PAL_DAC_8049a894);
        OSReport(&s_is_available__Please_do_NOT_use_t_8049a8c0);
        OSReport(&s_mode_in_real_games____8049a8ec);
        OSReport(&s__________________________________8049a810);
    }
    if (uVar4 == 2) {
LAB_803a6c78:
        if (((uVar5 == 0) || (uVar5 == 2)) || (uVar5 - 6 < 2)) goto LAB_803a6cc4;
    }
    else if ((int)uVar4 < 2) {
        if (uVar4 == 0) goto LAB_803a6c78;
        if (-1 < (int)uVar4) goto LAB_803a6c98;
    }
    else {
        if (uVar4 != 5) {
            if (((int)uVar4 < 5) || (7 < (int)uVar4)) goto LAB_803a6ca8;
            goto LAB_803a6c78;
        }
LAB_803a6c98:
        if ((uVar5 == 1) || (uVar5 == 5)) goto LAB_803a6cc4;
    }
LAB_803a6ca8:
    OSPanic(&DAT_80611444, 0x7bb, &s_VIConfigure____Tried_to_change_m_8049a918, uVar4, uVar5);
LAB_803a6cc4:
    if ((uVar5 == 0) || (DAT_805f37a0 = uVar5, uVar5 == 2)) {
        DAT_805f37a0 = uVar4;
    }
    DAT_805f3778 = *(short *)((int)param + 10);
    if (DAT_805f379c == 1) {
        DAT_805f377a = *(short *)(param + 3) << 1;
    }
    else {
        DAT_805f377a = *(unsigned short *)(param + 3);
    }
    DAT_805f377c = *(unsigned short *)((int)param + 0xe);
    DAT_805f378a = *(unsigned short *)(param + 1);
    DAT_805f378c = *(short *)(param + 2);
    DAT_805f3798 = param[5];
    DAT_805f378e = 0;
    DAT_805f3790 = 0;
    DAT_805f377e = DAT_805f378c;
    if (((DAT_805f379c != 2) && (DAT_805f379c != 3)) && (DAT_805f3798 == 0)) {
        DAT_805f377e = DAT_805f378c << 1;
    }
    DAT_805f37bc = (int)(DAT_805f379c == 3);
    DAT_805f3792 = DAT_805f378a;
    DAT_805f3794 = DAT_805f378c;
    pbVar3 = vi_get_timing(DAT_805f37a0 * 4 + DAT_805f379c);
    uVar1 = 0x2d0 - DAT_805f377c;
    uVar4 = (int)DAT_805f3778 + (int)DAT_8061320c;
    if (((int)uVar4 <= (int)uVar1) && (uVar1 = uVar4, (int)uVar4 < 0)) {
        uVar1 = 0;
    }
    DAT_805f3780 = (short)uVar1;
    if (DAT_805f3798 == 0) {
        iVar2 = 2;
    }
    else {
        iVar2 = 1;
    }
    iVar7 = (int)DAT_8061320e;
    uVar5 = DAT_805f377a & 1;
    uVar4 = uVar5;
    if ((int)uVar5 < (short)DAT_805f377a + iVar7) {
        uVar4 = (short)DAT_805f377a + iVar7;
    }
    DAT_805f3782 = (unsigned short)uVar4;
    iVar9 = *(short *)(pbVar3 + 2) * 2 - uVar5;
    iVar12 = (int)(short)DAT_805f377a + DAT_805f377e + iVar7;
    if (iVar12 - iVar9 < 1) {
        sVar13 = 0;
    }
    else {
        sVar13 = (short)iVar12 - (short)iVar9;
    }
    iVar12 = (short)DAT_805f377a + iVar7;
    if ((int)(iVar12 - uVar5) < 0) {
        sVar11 = (short)iVar12 - (short)uVar5;
    }
    else {
        sVar11 = 0;
    }
    DAT_805f3784 = (DAT_805f377e + sVar11) - sVar13;
    if ((int)(((short)DAT_805f377a + iVar7) - uVar5) < 0) {
        iVar12 = ((short)DAT_805f377a + iVar7) - uVar5;
    }
    else {
        iVar12 = 0;
    }
    DAT_805f3786 = DAT_805f3790 - (short)(iVar12 / iVar2);
    iVar12 = (int)(short)DAT_805f377a + DAT_805f377e + iVar7;
    if (iVar12 - iVar9 < 1) {
        iVar12 = 0;
    }
    else {
        iVar12 = iVar12 - iVar9;
    }
    iVar7 = (short)DAT_805f377a + iVar7;
    if ((int)(iVar7 - uVar5) < 0) {
        iVar7 = iVar7 - uVar5;
    }
    else {
        iVar7 = 0;
    }
    DAT_805f3788 = (DAT_805f3794 + (short)(iVar7 / iVar2)) - (short)(iVar12 / iVar2);
    if (DAT_80613208 == 0) {
        DAT_805f37a0 = 3;
    }
    iVar2 = (int)(unsigned int)*(unsigned short *)(pbVar3 + 0x18) >> 1;
    if ((((unsigned int)*(unsigned short *)(pbVar3 + 0x18) + iVar2 * -2) & 0xffff) == 0) {
        sVar13 = 0;
    }
    else {
        sVar13 = *(short *)(pbVar3 + 0x1a);
    }
    DAT_805f36ba = sVar13 + 1;
    DAT_805f36b8 = (short)(((int)iVar2 + 1U) | 0x1000);
    if ((DAT_805f379c == 2) || (DAT_805f379c == 3)) {
        uVar10 = (unsigned short)((DAT_805f368a & 0xfffb) | 4);
        DAT_805f36f4 = (unsigned short)((DAT_805f36f4 & 0xfffe) | 1);
    }
    else {
        uVar10 = (unsigned short)((DAT_805f379c & 1) << 2) | (unsigned short)(DAT_805f368a & 0xfffb);
        DAT_805f36f4 = (unsigned short)(DAT_805f36f4 & 0xfffe);
    }
    uVar10 = (uVar10 & 0xfff7) | (unsigned short)(DAT_805f37bc << 3);
    if ((DAT_805f37a0 == 1) || (DAT_805f37a0 - 2 < 2)) {
        DAT_805f368a = (short)((uVar10 & 0xfcff) | (unsigned short)(DAT_805f37a0 << 8));
    }
    else {
        DAT_805f368a = (short)(uVar10 & 0xfcff);
    }
    uVar5 = (unsigned int)(unsigned short)DAT_805f377c;
    uVar8 = (unsigned int)(unsigned short)DAT_805f3792;
    uVar4 = uVar8;
    if (DAT_805f37bc != 0) {
        uVar4 = uVar8 << 1;
    }
    if ((uVar4 & 0xffff) < uVar5) {
        DAT_805f36d2 = (unsigned short)((uVar5 + (uVar4 & 0xffff) * 0x100 + -1) / uVar5) | 0x1000;
        DAT_805f36f8 = (unsigned short)uVar4;
        DAT_8061321c = DAT_8061321c | 0x4000280;
    }
    else {
        DAT_805f36d2 = 0x100;
        DAT_8061321c = DAT_8061321c | 0x4000200;
    }
    DAT_805f368e = *(unsigned short *)(pbVar3 + 0x1a);
    DAT_805f368c = *(unsigned short *)(pbVar3 + 0x1d);
    uVar4 = ((unsigned int)pbVar3[0x1f] + (uVar1 & 0xffff)) - 0x28;
    DAT_805f3692 = (short)((unsigned short)pbVar3[0x1c] | (short)(uVar4 * 0x80));
    DAT_805f3690 = (short)((unsigned short)(uVar4 >> 9) |
                   (short)((((unsigned int)*(unsigned short *)(pbVar3 + 0x20) + (uVar1 & 0xffff) + 0x28) -
                          (0x2d0 - uVar5)) * 2));
    DAT_805f369e = (unsigned short)(pbVar3[0xc] | (*(short *)(pbVar3 + 0x10) << 5));
    DAT_805f369c = (unsigned short)(pbVar3[0xe] | (*(short *)(pbVar3 + 0x14) << 5));
    DAT_805f36a2 = (unsigned short)(pbVar3[0xd] | (*(short *)(pbVar3 + 0x12) << 5));
    DAT_805f36a0 = (unsigned short)(pbVar3[0xf] | (*(short *)(pbVar3 + 0x16) << 5));
    DAT_80613218 = DAT_80613218 | 0x7c3c00c0;
    DAT_805f37a4 = (int)((int)(DAT_805f378a + 0xf) >> 4);
    DAT_805f37a5 = (unsigned char)DAT_805f37a4;
    if (DAT_805f3798 != 0) {
        DAT_805f37a5 = (unsigned char)(DAT_805f37a4 << 1);
    }
    DAT_805f37b4 = (char)DAT_805f378e + (char)((int)(unsigned int)DAT_805f378e >> 4) * -0x10;
    DAT_805f37a6 = (unsigned char)((int)((unsigned int)DAT_805f37b4 + uVar8 + 0xf) >> 4);
    DAT_805f36d0 = (unsigned short)(((unsigned int)DAT_805f37a6 << 8) | DAT_805f37a5);
    DAT_8061321c = DAT_8061321c | 0x8000000;
    DAT_805f37cc = (int)pbVar3;
    if (DAT_80613240 != 0) {
        vi_calc_framebuffers(-0x7fa0c888, (unsigned int *)&DAT_805f37ac,
                             (unsigned int *)&DAT_805f37b0,
                             (unsigned int *)&DAT_805f37c4,
                             (unsigned int *)&DAT_805f37c8);
    }
    vi_calc_horizontal_regs(DAT_805f3782, DAT_805f3784, (unsigned short)*pbVar3,
                            *(short *)(pbVar3 + 2), *(short *)(pbVar3 + 4),
                            *(short *)(pbVar3 + 6), *(short *)(pbVar3 + 8),
                            *(short *)(pbVar3 + 10), DAT_805f37b8);
    os_get_interrupts_enabled();
}

// ============================================================================
// vi_set_display_geometry (0x803a737c): reconfigure just display position/size
// (VISetDisplayXY family) - restage H/V timing, scaling, and stride shadow regs
// from explicit args without changing the TV format.
// ============================================================================
void vi_set_display_geometry(unsigned short p1, short p2, unsigned short p3,
                             unsigned short p4)
{
    unsigned char *t;
    int iVar2;
    unsigned int uVar3;
    int iVar4;
    unsigned int uVar5;
    unsigned int uVar6;
    int iVar7;
    int iVar8;
    short sVar9;
    short sVar10;

    os_disable_interrupts();
    t = (unsigned char *)DAT_805f37cc;
    DAT_805f377e = p4;
    if (((DAT_805f379c != 2) && (DAT_805f379c != 3)) && (DAT_805f3798 == 0)) {
        DAT_805f377e = p4 << 1;
    }
    iVar2 = 0x2d0 - (unsigned int)(unsigned short)DAT_805f377c;
    iVar4 = (int)DAT_805f3778 + (int)DAT_8061320c;
    if ((iVar4 <= iVar2) && (iVar2 = iVar4, iVar4 < 0)) {
        iVar2 = 0;
    }
    DAT_805f3780 = (short)iVar2;
    if (DAT_805f3798 == 0) {
        iVar2 = 2;
    }
    else {
        iVar2 = 1;
    }
    iVar4 = (int)DAT_8061320e;
    uVar5 = DAT_805f377a & 1;
    uVar3 = uVar5;
    if ((int)uVar5 < (short)DAT_805f377a + iVar4) {
        uVar3 = (short)DAT_805f377a + iVar4;
    }
    DAT_805f3782 = (unsigned short)uVar3;
    iVar7 = *(short *)(DAT_805f37cc + 2) * 2 - uVar5;
    iVar8 = (int)(short)DAT_805f377a + (short)DAT_805f377e + iVar4;
    if (iVar8 - iVar7 < 1) {
        sVar9 = 0;
    }
    else {
        sVar9 = (short)iVar8 - (short)iVar7;
    }
    iVar8 = (short)DAT_805f377a + iVar4;
    if ((int)(iVar8 - uVar5) < 0) {
        sVar10 = (short)iVar8 - (short)uVar5;
    }
    else {
        sVar10 = 0;
    }
    DAT_805f3784 = (DAT_805f377e + sVar10) - sVar9;
    if ((int)(((short)DAT_805f377a + iVar4) - uVar5) < 0) {
        iVar8 = ((short)DAT_805f377a + iVar4) - uVar5;
    }
    else {
        iVar8 = 0;
    }
    DAT_805f3786 = p2 - (short)(iVar8 / iVar2);
    iVar8 = (int)(short)DAT_805f377a + (short)DAT_805f377e + iVar4;
    if (iVar8 - iVar7 < 1) {
        iVar8 = 0;
    }
    else {
        iVar8 = iVar8 - iVar7;
    }
    iVar4 = (short)DAT_805f377a + iVar4;
    if ((int)(iVar4 - uVar5) < 0) {
        iVar4 = iVar4 - uVar5;
    }
    else {
        iVar4 = 0;
    }
    DAT_805f3788 = (p4 + (short)(iVar4 / iVar2)) - (short)(iVar8 / iVar2);
    uVar6 = (unsigned int)(unsigned short)DAT_805f377c;
    uVar5 = (unsigned int)p3;
    uVar3 = uVar5;
    if (DAT_805f37bc != 0) {
        uVar3 = uVar5 << 1;
    }
    if ((uVar3 & 0xffff) < uVar6) {
        DAT_805f36d2 = (unsigned short)((uVar6 + (uVar3 & 0xffff) * 0x100 + -1) / uVar6) | 0x1000;
        DAT_805f36f8 = (unsigned short)uVar3;
        DAT_8061321c = DAT_8061321c | 0x4000080;
    }
    else {
        DAT_805f36d2 = 0x100;
        DAT_8061321c = DAT_8061321c | 0x4000000;
    }
    DAT_805f37a4 = (int)((int)(DAT_805f378a + 0xf) >> 4);
    DAT_805f37a5 = (unsigned char)DAT_805f37a4;
    if (DAT_805f3798 != 0) {
        DAT_805f37a5 = (unsigned char)(DAT_805f37a4 << 1);
    }
    DAT_805f37b4 = (char)p1 + (char)((int)(unsigned int)p1 >> 4) * -0x10;
    DAT_805f37a6 = (unsigned char)((int)((unsigned int)DAT_805f37b4 + uVar5 + 0xf) >> 4);
    DAT_805f36d0 = (unsigned short)(((unsigned int)DAT_805f37a6 << 8) | DAT_805f37a5);
    DAT_8061321c = DAT_8061321c | 0x8000000;
    DAT_805f378e = p1;
    DAT_805f3790 = p2;
    DAT_805f3792 = p3;
    DAT_805f3794 = p4;
    if (DAT_80613240 != 0) {
        vi_calc_framebuffers(-0x7fa0c888, (unsigned int *)&DAT_805f37ac,
                             (unsigned int *)&DAT_805f37b0,
                             (unsigned int *)&DAT_805f37c4,
                             (unsigned int *)&DAT_805f37c8);
    }
    vi_calc_horizontal_regs(DAT_805f3782, DAT_805f377e, (unsigned short)*t,
                            *(short *)(t + 2), *(short *)(t + 4),
                            *(short *)(t + 6), *(short *)(t + 8),
                            *(short *)(t + 10), DAT_805f37b8);
    os_get_interrupts_enabled();
}

// ============================================================================
// vi_flush (0x803a7710 / VIFlush): commit the staged shadow VI registers - see
// plate comment.
// ============================================================================
void vi_flush(void)
{
    unsigned int uVar1;
    int iVar2;
    long long uVar3;

    os_disable_interrupts();
    DAT_80613220 = DAT_80613220 | DAT_80613210;
    DAT_80613210 = 0;
    DAT_80613228 = DAT_80613228 | (unsigned int)DAT_80613218;
    DAT_8061322c = DAT_8061322c | (unsigned int)DAT_8061321c;
    for (; uVar1 = (unsigned int)DAT_8061321c,
           DAT_8061321c != 0 || DAT_80613218 != 0;
         DAT_8061321c = DAT_8061321c & ~(unsigned int)uVar3) {
        uVar3 = (long long)__shr2u((unsigned int)DAT_80613218, DAT_8061321c, 0x20);
        iVar2 = countLeadingZeros((int)uVar3);
        if (0x1f < iVar2) {
            iVar2 = countLeadingZeros(uVar1);
            iVar2 = iVar2 + 0x20;
        }
        *(unsigned short *)(&DAT_805f3700 + iVar2 * 2) = (&DAT_805f3688)[iVar2];
        uVar3 = __shl2i(0, 1, 0x3f - iVar2);
        DAT_80613218 = DAT_80613218 & ~(unsigned int)((unsigned long long)uVar3 >> 0x20);
    }
    DAT_806131f0 = 1;
    DAT_80613238 = DAT_805f37a8;
    os_get_interrupts_enabled();
}

} // extern "C"

// ============================================================================
// batch_1126: VI query/getter layer and the public framebuffer/field setters -
// beam-position reads, retrace count, TV-format and field queries, DTV status,
// the public VISetNextFrameBuffer, and the field-adjust setter. Appended to
// video/vi.cpp.
//
// Address: 0x803a7840 | Name: VISetNextFrameBuffer      | Date: 2026-09-15
// Address: 0x803a78ac | Name: vi_set_field_adjust       | Date: 2026-09-15
// Address: 0x803a7928 | Name: vi_get_retrace_count      | Date: 2026-09-15
// Address: 0x803a7930 | Name: vi_read_beam_position_raw | Date: 2026-09-15
// Address: 0x803a796c | Name: vi_reprogram_all          | Date: 2026-09-15
// Address: 0x803a79d4 | Name: vi_get_current_field      | Date: 2026-09-15
// Address: 0x803a7a70 | Name: vi_get_current_line       | Date: 2026-09-15
// Address: 0x803a7b08 | Name: vi_get_tv_format          | Date: 2026-09-15
// Address: 0x803a7b70 | Name: vi_get_dtv_status         | Date: 2026-09-15
// Address: 0x803a7bac | Name: vi_line_to_beam_pos       | Date: 2026-09-15
//
// These sit on top of the VI register file and the shadow-register state
// established earlier in this file. The beam-position reads (VI regs
// 0xcc00202c current halfline / 0xcc00202e current field) all use a re-read
// loop to sample a value that is not mid-update. vi_reprogram_all is the
// "is the beam in the active region" predicate the retrace handler consults
// before doing a full register reprogram.
//
// NON-MATCHING: VISetNextFrameBuffer keeps its clean SDK signature (void*);
// the VI registers, shadow state, and driver state are reached through per-file
// externs; control flow is preserved verbatim.

extern "C" {

// ---- VI hardware registers used by this batch ----
extern volatile unsigned short DAT_cc00202c;  // current beam halfline
extern volatile unsigned short DAT_cc00202e;  // current beam field/line
extern volatile unsigned short DAT_cc00206e;  // DTV / component-cable status

// ---- VI driver state / shadow (re-declared; identical types to above) ----
extern int          DAT_806131ec;  // retrace count
extern int          DAT_80613230;  // staged timing block pointer
extern int          DAT_80613234;  // staged mode index
extern int          DAT_80613240;  // "framebuffer set" flag
extern unsigned int DAT_805f379c;  // interlace/progressive selector
extern unsigned short DAT_805f3782; // active-region start line
extern short        DAT_805f377e;   // active-region height
extern int          DAT_805f37a8;   // latched framebuffer base
extern int          DAT_805f37b8;   // field-adjust flag
extern int          DAT_805f37cc;   // current timing block pointer
extern int          DAT_805f37ac;   // FB-calc output shadow words
extern int          DAT_805f37b0;
extern int          DAT_805f37c4;
extern int          DAT_805f37c8;

// ---- helpers ----
extern void vi_calc_framebuffers(int rmode, unsigned int *tf, unsigned int *bf,
                                 unsigned int *tf3d, unsigned int *bf3d);
extern void vi_calc_horizontal_regs(unsigned short width,
                                    unsigned short prevWidth,
                                    unsigned short cfg, short p4, short p5,
                                    short p6, short p7, short p8, int adjust);
extern void vi_read_beam_position_raw(unsigned int *line, unsigned int *field);

// ============================================================================
// VISetNextFrameBuffer (0x803a7840): public framebuffer setter.
// ============================================================================
void VISetNextFrameBuffer(void *fb)
{
    os_disable_interrupts();
    DAT_80613240 = 1;
    DAT_805f37a8 = (int)(intptr_t)fb;
    vi_calc_framebuffers(-0x7fa0c888, (unsigned int *)&DAT_805f37ac,
                         (unsigned int *)&DAT_805f37b0,
                         (unsigned int *)&DAT_805f37c4,
                         (unsigned int *)&DAT_805f37c8);
    os_get_interrupts_enabled();
}

// ============================================================================
// vi_set_field_adjust (0x803a78ac): set the field-adjust flag and recompute the
// horizontal blank/burst shadow registers.
// ============================================================================
void vi_set_field_adjust(int adjust)
{
    os_disable_interrupts();
    DAT_805f37b8 = adjust;
    vi_calc_horizontal_regs(DAT_805f3782, DAT_805f377e,
                            (unsigned short)*(unsigned char *)DAT_805f37cc,
                            *(short *)(DAT_805f37cc + 2),
                            *(short *)(DAT_805f37cc + 4),
                            *(short *)(DAT_805f37cc + 6),
                            *(short *)(DAT_805f37cc + 8),
                            *(short *)(DAT_805f37cc + 10), adjust);
    os_get_interrupts_enabled();
}

// ============================================================================
// vi_get_retrace_count (0x803a7928 / VIGetRetraceCount): return the retrace
// counter.
// ============================================================================
int vi_get_retrace_count(void)
{
    return DAT_806131ec;
}

// ============================================================================
// vi_read_beam_position_raw (0x803a7930): consistent read of the beam position.
// ============================================================================
void vi_read_beam_position_raw(unsigned int *line, unsigned int *field)
{
    bool changed;
    unsigned short uVar2;
    unsigned short uVar3;
    unsigned int uVar4;
    unsigned int uVar5;

    uVar2 = DAT_cc00202c;
    uVar4 = uVar2 & 0x7ff;
    do {
        uVar2 = DAT_cc00202c;
        uVar3 = DAT_cc00202e;
        uVar5 = uVar2 & 0x7ff;
        changed = uVar4 != uVar5;
        uVar4 = uVar5;
    } while (changed);
    *line = uVar3 & 0x7ff;
    *field = uVar5;
}

// ============================================================================
// vi_reprogram_all (0x803a796c): return whether the beam is inside the active
// display region (safe to reprogram the full register set).
// ============================================================================
int vi_reprogram_all(void)
{
    bool changed;
    unsigned short uVar2;
    unsigned short uVar3;
    unsigned int uVar4;
    unsigned int uVar5;

    uVar2 = DAT_cc00202c;
    uVar4 = uVar2 & 0x7ff;
    do {
        uVar2 = DAT_cc00202c;
        uVar3 = DAT_cc00202e;
        uVar5 = uVar2 & 0x7ff;
        changed = uVar4 != uVar5;
        uVar4 = uVar5;
    } while (changed);
    if ((uVar5 - 1) * 2 + ((uVar3 & 0x7ff) - 1) / (unsigned int)*(unsigned short *)(DAT_80613230 + 0x1a) <
        (unsigned int)*(unsigned short *)(DAT_80613230 + 0x18)) {
        return 1;
    }
    return 0;
}

// ============================================================================
// vi_get_current_field (0x803a79d4): return the current output field (0/1).
// ============================================================================
unsigned short vi_get_current_field(void)
{
    unsigned short uVar1;
    unsigned short uVar2;
    unsigned int line;
    unsigned int field[3];

    os_disable_interrupts();
    vi_read_beam_position_raw(&line, field);
    uVar1 = *(unsigned short *)(DAT_80613230 + 0x1a);
    uVar2 = *(unsigned short *)(DAT_80613230 + 0x18);
    os_get_interrupts_enabled();
    return (unsigned short)(((field[0] - 1) * 2 + (line - 1) / (unsigned int)uVar1 < (unsigned int)uVar2)
                            ^ 1 ^ (DAT_805f3782 & 1));
}

// ============================================================================
// vi_get_current_line (0x803a7a70): return the current active display line.
// ============================================================================
unsigned int vi_get_current_line(void)
{
    bool changed;
    unsigned short uVar2;
    unsigned short uVar3;
    int iVar4;
    unsigned int uVar5;
    unsigned int uVar6;

    iVar4 = DAT_80613230;
    os_disable_interrupts();
    uVar2 = DAT_cc00202c;
    uVar6 = uVar2 & 0x7ff;
    do {
        uVar2 = DAT_cc00202c;
        uVar3 = DAT_cc00202e;
        uVar5 = uVar2 & 0x7ff;
        changed = uVar6 != uVar5;
        uVar6 = uVar5;
    } while (changed);
    uVar6 = (uVar5 - 1) * 2 + ((uVar3 & 0x7ff) - 1) / (unsigned int)*(unsigned short *)(DAT_80613230 + 0x1a);
    os_get_interrupts_enabled();
    if (*(unsigned short *)(iVar4 + 0x18) <= uVar6) {
        uVar6 = uVar6 - *(unsigned short *)(iVar4 + 0x18);
    }
    return uVar6 >> 1;
}

// ============================================================================
// vi_get_tv_format (0x803a7b08 / VIGetTvFormat): map the staged mode index to
// the TV-format class.
// ============================================================================
int vi_get_tv_format(void)
{
    int result = 0;

    os_disable_interrupts();
    switch (DAT_80613234) {
    case 0:
    case 3:
    case 6:
    case 7:
        result = 0;
        break;
    case 1:
    case 4:
        result = 1;
        break;
    case 2:
    case 5:
        result = DAT_80613234;
        break;
    }
    os_get_interrupts_enabled();
    return result;
}

// ============================================================================
// vi_get_dtv_status (0x803a7b70): return the DTV / component-cable status bit.
// ============================================================================
unsigned short vi_get_dtv_status(void)
{
    unsigned short uVar1;

    os_disable_interrupts();
    uVar1 = DAT_cc00206e;
    os_get_interrupts_enabled();
    return uVar1 & 1;
}

// ============================================================================
// vi_line_to_beam_pos (0x803a7bac): convert a (halfline,field) sample into an
// active scanline index and the raw VI position value, per interlace mode.
// ============================================================================
void vi_line_to_beam_pos(int halfline, int field, short *outLine,
                         unsigned short *outPos)
{
    unsigned int uVar1;
    unsigned int uVar2;

    uVar1 = (field + -1) * 2 + (halfline - 1U) / (unsigned int)*(unsigned short *)(DAT_80613230 + 0x1a);
    if (DAT_805f379c == 0) {
        uVar2 = (unsigned int)*(unsigned short *)(DAT_80613230 + 0x18);
        if (uVar1 < uVar2) {
            if (uVar1 < (unsigned int)*(unsigned short *)(DAT_80613230 + 4) + (unsigned int)*(unsigned char *)DAT_80613230 * 3) {
                *outPos = 0xffff;
            }
            else if (uVar1 < uVar2 - *(unsigned short *)(DAT_80613230 + 8)) {
                *outPos = (unsigned short)(((short)uVar1 + (unsigned short)*(unsigned char *)DAT_80613230 * -3) - *(unsigned short *)(DAT_80613230 + 4)) & 0xfffe;
            }
            else {
                *outPos = 0xffff;
            }
        }
        else {
            uVar1 = uVar1 - uVar2;
            if (uVar1 < (unsigned int)*(unsigned short *)(DAT_80613230 + 6) + (unsigned int)*(unsigned char *)DAT_80613230 * 3) {
                *outPos = 0xffff;
            }
            else if (uVar1 < uVar2 - *(unsigned short *)(DAT_80613230 + 10)) {
                *outPos = ((unsigned short)(((short)uVar1 + (unsigned short)*(unsigned char *)DAT_80613230 * -3) - *(unsigned short *)(DAT_80613230 + 6)) & 0xfffe) + 1;
            }
            else {
                *outPos = 0xffff;
            }
        }
    }
    else if (DAT_805f379c == 1) {
        uVar2 = (unsigned int)*(unsigned short *)(DAT_80613230 + 0x18);
        if (uVar2 <= uVar1) {
            uVar1 = uVar1 - uVar2;
        }
        if (uVar1 < (unsigned int)*(unsigned short *)(DAT_80613230 + 4) + (unsigned int)*(unsigned char *)DAT_80613230 * 3) {
            *outPos = 0xffff;
        }
        else if (uVar1 < uVar2 - *(unsigned short *)(DAT_80613230 + 8)) {
            *outPos = (unsigned short)(((short)uVar1 + (unsigned short)*(unsigned char *)DAT_80613230 * -3) - *(unsigned short *)(DAT_80613230 + 4)) & 0xfffe;
        }
        else {
            *outPos = 0xffff;
        }
    }
    else if (DAT_805f379c == 2) {
        uVar2 = (unsigned int)*(unsigned short *)(DAT_80613230 + 0x18);
        if (uVar1 < uVar2) {
            if (uVar1 < (unsigned int)*(unsigned short *)(DAT_80613230 + 4) + (unsigned int)*(unsigned char *)DAT_80613230 * 3) {
                *outPos = 0xffff;
            }
            else if (uVar1 < uVar2 - *(unsigned short *)(DAT_80613230 + 8)) {
                *outPos = (unsigned short)(((short)uVar1 + (unsigned short)*(unsigned char *)DAT_80613230 * -3) - *(unsigned short *)(DAT_80613230 + 4));
            }
            else {
                *outPos = 0xffff;
            }
        }
        else {
            uVar1 = uVar1 - uVar2;
            if (uVar1 < (unsigned int)*(unsigned short *)(DAT_80613230 + 6) + (unsigned int)*(unsigned char *)DAT_80613230 * 3) {
                *outPos = 0xffff;
            }
            else if (uVar1 < uVar2 - *(unsigned short *)(DAT_80613230 + 10)) {
                *outPos = (unsigned short)(((short)uVar1 + (unsigned short)*(unsigned char *)DAT_80613230 * -3) - *(unsigned short *)(DAT_80613230 + 6)) & 0xfffe;
            }
            else {
                *outPos = 0xffff;
            }
        }
    }
    *outLine = (short)halfline + -1;
}

} // extern "C"

// ============================================================================
// batch_1127: VI beam-position query used by the retrace position-interrupt.
//
// Address: 0x803a7dc8 | Name: vi_get_beam_position | Date: 2026-09-15
//
// vi_get_beam_position reads the current beam position with a consistency
// re-read loop and converts the (halfline,field) sample to an active scanline
// and raw VI position via vi_line_to_beam_pos. Called by vi_retrace_handler on
// the position-interrupt path.
//
// NON-MATCHING: VI registers reached through per-file externs; control flow
// preserved verbatim.

extern "C" {

extern volatile unsigned short DAT_cc00202c;  // current beam halfline
extern volatile unsigned short DAT_cc00202e;  // current beam field/line

extern void vi_line_to_beam_pos(int halfline, int field, short *outLine,
                                unsigned short *outPos);

// ============================================================================
// vi_get_beam_position (0x803a7dc8): consistent beam-position read + convert.
// ============================================================================
void vi_get_beam_position(short *outLine, unsigned short *outPos)
{
    bool changed;
    unsigned short uVar2;
    unsigned short uVar3;
    unsigned int uVar4;
    unsigned int uVar5;

    uVar2 = DAT_cc00202c;
    uVar4 = uVar2 & 0x7ff;
    do {
        uVar2 = DAT_cc00202c;
        uVar3 = DAT_cc00202e;
        uVar5 = uVar2 & 0x7ff;
        changed = uVar4 != uVar5;
        uVar4 = uVar5;
    } while (changed);
    vi_line_to_beam_pos(uVar3 & 0x7ff, uVar5, outLine, outPos);
}

} // extern "C"
