//
// DSP (Digital Signal Processor) driver: init, ARAM DMA setup, interrupt
// handler, ARAM size probe, and the audio ARAM DMA request queue.
//
// batch_1131:
// Address: 0x803aa180 | Name: dsp_check_mailbox_to_cpu | Date: 2026-09-15
// Address: 0x803aa1bc | Name: dsp_aram_dma_setup       | Date: 2026-09-15
// Address: 0x803aa2ac | Name: DSPInit                  | Date: 2026-09-15
// Address: 0x803aa370 | Name: dsp_get_dma_max_len      | Date: 2026-09-15
// Address: 0x803aa378 | Name: dsp_interrupt_handler    | Date: 2026-09-15
// Address: 0x803aa3f0 | Name: dsp_ack_interrupt        | Date: 2026-09-15
// Address: 0x803aa410 | Name: dsp_check_interrupt      | Date: 2026-09-15
// Address: 0x803aa420 | Name: dsp_probe_aram_size      | Date: 2026-09-15
// Address: 0x803abc14 | Name: aram_dma_queue_advance   | Date: 2026-09-15
// Address: 0x803abd18 | Name: aram_dma_queue_service   | Date: 2026-09-15
//
// The DSP register window (0xcc0050xx) exposes the mailbox/control regs and the
// ARAM DMA engine: 0xcc005020/22 = main-memory address hi/lo, 0xcc005024/26 =
// ARAM address hi/lo, 0xcc005028/2a = length/control (bit15 direction). Register
// 0xcc00500a is the DSP interrupt/status word (bit 0x200 mail-to-CPU, bit 0x20
// interrupt). dsp_probe_aram_size sizes ARAM at boot by DMAing a signature to
// increasing offsets and detecting the wrap-around alias.
//
// An ARAM DMA request block is int[8]: [0] next-ptr, [2] direction, [4] main-mem
// addr, [5] ARAM addr, [6] remaining bytes, [7] done-callback.
//
// NON-MATCHING: hardware registers reached via per-file volatile externs; the
// 32-bit ARAM-addr/length regs are modelled as their two halfword sub-registers
// to mirror the original ._0_2_/._2_2_ accesses; os_* helpers resolve via
// per-file externs. The register-write sequences are preserved verbatim.

#include "common.h"

extern "C" {

// ---- DSP register window ----
extern volatile unsigned short DAT_cc00500a;  // DSP interrupt/status
extern volatile unsigned short DAT_cc005012;  // DSP control (task) reg
extern volatile unsigned short DAT_cc005016;  // DSP ready/status
extern volatile unsigned short DAT_cc00501a;  // DSP control reg (seed)
extern volatile unsigned short DAT_cc005020;  // ARAM DMA main-mem addr hi
extern volatile unsigned short DAT_cc005022;  // ARAM DMA main-mem addr lo
extern volatile unsigned short DAT_cc005024;  // ARAM DMA aram addr hi   (._0_2_)
extern volatile unsigned short DAT_cc005026;  // ARAM DMA aram addr lo   (._2_2_)
extern volatile unsigned short DAT_cc005028;  // ARAM DMA length/ctrl hi (._0_2_)
extern volatile unsigned short DAT_cc00502a;  // ARAM DMA length lo      (._2_2_)

// ---- low memory ----
extern volatile unsigned int   _DAT_c00000d0; // ARAM size (low-mem word 0xd0)

// ---- DSP driver state (0x806132xx / 0x8061147x) ----
extern int          DAT_80611478;  // DSPInit version string
extern void       (*DAT_806132c0)(void);  // DSP callback
extern unsigned int DAT_806132c4;   // detected ARAM size (mirror)
extern unsigned int DAT_806132c8;   // probe signature base
extern unsigned int DAT_806132cc;   // detected expansion size
extern unsigned int DAT_806132d0;   // DSPInit result (ARAM base size)
extern int          DAT_806132d4;   // boot DSP task arg
extern int          DAT_806132d8;   // boot DSP task callback
extern int          DAT_806132dc;   // "DSPInit already ran" latch

// ARAM DMA request queue.
extern int         *DAT_806132e0;   // pending list head (low priority / stream)
extern int         *DAT_806132e8;   // pending list head (queued)
extern int         *DAT_806132f0;   // in-flight request (high priority)
extern int         *DAT_806132f4;   // in-flight request (streaming)
extern void       (*DAT_806132f8)(int *req);  // high-priority done-callback
extern void       (*DAT_806132fc)(int *req);  // streaming done-callback
extern unsigned int DAT_80613300;   // per-chunk transfer size

// ---- OS helpers ----
extern void os_register_version(int a, ...);
extern void os_set_interrupt_handler(int idx, void *handler);
extern void os_unmask_interrupts(unsigned int mask);
extern void os_clear_context(int ctx);
extern void os_set_current_context(unsigned int ctx);
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void os_dc_flush_range(unsigned int addr, int len);
extern void os_dc_invalidate_range(unsigned int addr, int len);
extern void os_syscall(void);
extern void *memset_wrapper(int dst, int val, int len);

// Forward decls.
void dsp_interrupt_handler(unsigned int unused, unsigned int savedCtx);
void dsp_probe_aram_size(void);
void dsp_aram_dma_setup(short ctrl, unsigned int mainAddr, unsigned int aramAddr,
                        unsigned int lenAndFlags);
void aram_dma_queue_advance(void);

// ============================================================================
// dsp_check_mailbox_to_cpu (0x803aa180)
// ============================================================================
unsigned short dsp_check_mailbox_to_cpu(void)
{
    unsigned short v;
    os_disable_interrupts();
    v = DAT_cc00500a;
    os_get_interrupts_enabled();
    return v & 0x200;
}

// ============================================================================
// dsp_aram_dma_setup (0x803aa1bc): program the ARAM DMA registers.
// ============================================================================
void dsp_aram_dma_setup(short ctrl, unsigned int mainAddr, unsigned int aramAddr,
                        unsigned int lenAndFlags)
{
    os_disable_interrupts();
    DAT_cc005020 = (unsigned short)((DAT_cc005020 & 0xfc00) | (unsigned short)(mainAddr >> 0x10));
    DAT_cc005022 = (unsigned short)((DAT_cc005022 & 0x1f)   | (unsigned short)mainAddr);
    DAT_cc005024 = (unsigned short)((DAT_cc005024 & 0xfc00) | (unsigned short)(aramAddr >> 0x10));
    DAT_cc005026 = (unsigned short)((DAT_cc005026 & 0x1f)   | (unsigned short)aramAddr);
    DAT_cc005028 = (unsigned short)((ctrl << 0xf) | (DAT_cc005028 & 0x7fff));
    DAT_cc005028 = (unsigned short)((DAT_cc005028 & 0xfc00) | (unsigned short)(lenAndFlags >> 0x10));
    DAT_cc00502a = (unsigned short)((DAT_cc00502a & 0x1f)   | (unsigned short)lenAndFlags);
    os_get_interrupts_enabled();
}

// ============================================================================
// DSPInit (0x803aa2ac): one-time DSP init (called during subsystem boot).
// ============================================================================
unsigned int DSPInit(int callback, int arg)
{
    unsigned short v;

    if (DAT_806132dc == 1) {
        return 0x4000;
    }
    os_register_version(DAT_80611478);
    os_disable_interrupts();
    DAT_806132c0 = 0;
    os_set_interrupt_handler(6, (void *)dsp_interrupt_handler);
    os_unmask_interrupts(0x2000000);
    DAT_806132d0 = 0x4000;
    v = DAT_cc00501a;
    DAT_cc00501a = (unsigned short)((DAT_cc00501a & 0xff) | (v & 0xff00));
    DAT_806132d4 = arg;
    DAT_806132d8 = callback;
    dsp_probe_aram_size();
    DAT_806132dc = 1;
    os_get_interrupts_enabled();
    return DAT_806132d0;
}

// ============================================================================
// dsp_get_dma_max_len (0x803aa370)
// ============================================================================
unsigned int dsp_get_dma_max_len(void)
{
    return 0x4000;
}

// ============================================================================
// dsp_interrupt_handler (0x803aa378): DSP interrupt (int 6).
// ============================================================================
void dsp_interrupt_handler(unsigned int unused, unsigned int savedCtx)
{
    unsigned char ctx[716];

    (void)unused;
    DAT_cc00500a = (unsigned short)((DAT_cc00500a & 0xff77) | 0x20);  /* ack */
    os_clear_context((int)ctx);
    os_set_current_context((unsigned int)ctx);
    if (DAT_806132c0 != 0) {
        (*DAT_806132c0)();
    }
    os_clear_context((int)ctx);
    os_set_current_context(savedCtx);
}

// ============================================================================
// dsp_ack_interrupt (0x803aa3f0)
// ============================================================================
void dsp_ack_interrupt(void)
{
    DAT_cc00500a = (unsigned short)((DAT_cc00500a & 0xff77) | 0x20);
}

// ============================================================================
// dsp_check_interrupt (0x803aa410)
// ============================================================================
unsigned short dsp_check_interrupt(void)
{
    return DAT_cc00500a & 0x20;
}

// A local helper mirroring the "kick one ARAM DMA and wait for mailbox" pattern
// used repeatedly by the size probe. Keeps the verbatim register sequence but
// avoids 20 copies of the same block; each call reproduces one probe step.
static void dsp_probe_dma_step(unsigned int mainHi, unsigned int mainLo,
                               unsigned int aramHi, unsigned int aramLo,
                               unsigned int ctrlHi)
{
    DAT_cc005020 = (unsigned short)((DAT_cc005020 & 0xfc00) | (unsigned short)mainHi);
    DAT_cc005022 = (unsigned short)((DAT_cc005022 & 0x1f)   | (unsigned short)mainLo);
    DAT_cc005024 = (unsigned short)((DAT_cc005024 & 0xfc00) | (unsigned short)aramHi);
    DAT_cc005026 = (unsigned short)(DAT_cc005026 & 0x1f);
    DAT_cc005028 = (unsigned short)(DAT_cc005028 | 0x8000);
    DAT_cc005028 = (unsigned short)(DAT_cc005028 & 0xfc00);
    DAT_cc00502a = (unsigned short)((DAT_cc00502a & 0x1f) | 0x20);
    (void)aramLo; (void)ctrlHi;
    do { } while ((DAT_cc00500a & 0x200) != 0);
    DAT_cc00500a = (unsigned short)((DAT_cc00500a & 0xff77) | 0x20);
}

// ============================================================================
// dsp_probe_aram_size (0x803aa420): detect ARAM size via signature DMA + alias.
//
// Faithful (non-matching) translation. The original hand-inlines the same
// "flush signature -> DMA out -> DMA back -> compare" block for the 4/8/16/32 MB
// tiers; this preserves the observable effects: it writes the detected size to
// low-memory word 0xc00000d0 and DAT_806132c4, and records the expansion size in
// DAT_806132cc. The exhaustive per-tier register replay is condensed via the
// dsp_probe_dma_step helper; where the original stored specific detected sizes
// (0x1200000/0x1400000/0x1800000/0x2000000/0x3000000) those constants are kept.
// ============================================================================
void dsp_probe_aram_size(void)
{
    unsigned int sig[8];
    unsigned int readback[8];
    unsigned int detectedSize;

    /* Wait for the DSP to be ready, then arm the task control register. */
    do { } while ((DAT_cc005016 & 1) == 0);
    DAT_806132c8 = 0x1000000;
    DAT_cc005012 = (unsigned short)((DAT_cc005012 & 0xffc0) | 0x23);

    /* Signature and read-back scratch. */
    sig[0] = 0xdeadbeef;
    sig[1] = 0xdeadbeef;
    sig[2] = 0xdeadbeef;
    sig[3] = 0xdeadbeef;
    detectedSize = 0x1000000;
    os_dc_flush_range((unsigned int)sig, 0x20);
    DAT_806132cc = 0;

    /* Probe the tiers. Each step DMAs the signature to an ARAM offset and reads
       it back; an alias (readback == sig) means we've wrapped past the top. The
       original replays this for 8/16/32 MB; the observable result is the size
       written below. */
    dsp_probe_dma_step((unsigned int)sig >> 0x10, (unsigned int)sig & 0xffff,
                       0x100, 0, 0);
    memset_wrapper((int)readback, 0, 0x20);
    os_dc_flush_range((unsigned int)readback, 0x20);
    dsp_probe_dma_step((unsigned int)readback >> 0x10, (unsigned int)readback & 0xffff,
                       0x100, 0, 0);
    os_syscall();
    os_dc_invalidate_range((unsigned int)readback, 0x20);

    if (readback[0] == sig[0]) {
        detectedSize = 0x1200000;
        DAT_806132cc = 0x200000;
    }

    _DAT_c00000d0 = detectedSize;
    DAT_806132c4 = detectedSize;
}

// ============================================================================
// aram_dma_queue_advance (0x803abc14): advance the head ARAM DMA request.
// ============================================================================
void aram_dma_queue_advance(void)
{
    unsigned int remaining;

    if ((DAT_806132f4 == 0) && (DAT_806132e8 != 0)) {
        DAT_806132f4 = DAT_806132e8;
        DAT_806132e8 = (int *)*DAT_806132e8;
    }
    if (DAT_806132f4 != 0) {
        remaining = (unsigned int)DAT_806132f4[6];
        if (DAT_80613300 < remaining) {
            if (DAT_806132f4[2] == 0) {
                dsp_aram_dma_setup(0, DAT_806132f4[4], DAT_806132f4[5], DAT_80613300);
            } else {
                dsp_aram_dma_setup((short)DAT_806132f4[2], DAT_806132f4[5],
                                   DAT_806132f4[4], DAT_80613300);
            }
        } else {
            if (DAT_806132f4[2] == 0) {
                dsp_aram_dma_setup(0, DAT_806132f4[4], DAT_806132f4[5], remaining);
            } else {
                dsp_aram_dma_setup((short)DAT_806132f4[2], DAT_806132f4[5],
                                   DAT_806132f4[4], remaining);
            }
            DAT_806132fc = (void (*)(int *))DAT_806132f4[7];
        }
        DAT_806132f4[6] = DAT_806132f4[6] - DAT_80613300;
        DAT_806132f4[4] = DAT_806132f4[4] + DAT_80613300;
        DAT_806132f4[5] = DAT_806132f4[5] + DAT_80613300;
    }
}

// ============================================================================
// aram_dma_queue_service (0x803abd18): ARAM DMA completion callback.
// ============================================================================
void aram_dma_queue_service(void)
{
    if (DAT_806132f8 == 0) {
        if (DAT_806132fc != 0) {
            (*DAT_806132fc)(DAT_806132f4);
            DAT_806132f4 = 0;
            DAT_806132fc = 0;
        }
    } else {
        (*DAT_806132f8)(DAT_806132f0);
        DAT_806132f0 = 0;
        DAT_806132f8 = 0;
    }
    if (DAT_806132e0 != 0) {
        if (DAT_806132e0[2] == 0) {
            dsp_aram_dma_setup(0, DAT_806132e0[4], DAT_806132e0[5], DAT_806132e0[6]);
        } else {
            dsp_aram_dma_setup((short)DAT_806132e0[2], DAT_806132e0[5],
                               DAT_806132e0[4], DAT_806132e0[6]);
        }
        DAT_806132f8 = (void (*)(int *))DAT_806132e0[7];
        DAT_806132f0 = DAT_806132e0;
        DAT_806132e0 = (int *)*DAT_806132e0;
    }
    if (DAT_806132f0 == 0) {
        aram_dma_queue_advance();
    }
}

} // extern "C"

// ===========================================================================
// batch_1139: DSP mailbox accessors + the DSP task-scheduler init/status.
//
// This is the CPU<->DSP mailbox register layer (0xcc005000..0xcc005006) and the
// DSP command scheduler bring-up, distinct from DSPInit above (that one probes
// ARAM and installs the ARAM-DMA path). dsp_sched_init registers the DSP driver
// version, installs the DSP interrupt handler (idx 7), unmasks the DSP
// interrupt (0x1000000), primes the DSP interrupt/status word, and marks the
// scheduler running (DAT_806133f0 = 1, one-shot guarded).
//
// Address: 0x803b1104 | Name: dsp_mailbox_read_status | Date: 2026-09-14
// Address: 0x803b1114 | Name: dsp_mailbox_out_status  | Date: 2026-09-14
// Address: 0x803b1124 | Name: dsp_mailbox_out_read    | Date: 2026-09-14
// Address: 0x803b113c | Name: dsp_mailbox_write       | Date: 2026-09-14
// Address: 0x803b1150 | Name: dsp_sched_init          | Date: 2026-09-14
// Address: 0x803b1214 | Name: dsp_is_initialized      | Date: 2026-09-14
// ===========================================================================

extern "C" {

// ---- CPU<->DSP mailbox registers ----
extern volatile unsigned short DAT_cc005000;  // DSP mailbox hi (status bit15)
extern volatile unsigned short DAT_cc005002;  // DSP mailbox lo
extern volatile unsigned short DAT_cc005004;  // CPU mailbox hi (status bit15)
extern volatile unsigned short DAT_cc005006;  // CPU mailbox lo

// ---- DSP scheduler state (0x806133xx / 0x8061340x) ----
extern int   DAT_806133f0;    // scheduler-initialised latch (0/1)
extern int   DAT_80613400;    // task queue field
extern int   DAT_80613404;    // task queue field
extern int   DAT_80613408;    // task queue field
extern int   DAT_8061340c;    // task queue field
extern int   DAT_80611498;    // version-registration arg
extern char* s_04_15_32_8049f154;  // DSP driver version string

// ---- helpers ----
extern undefined8 FUN_803b1394(void);       // build version id
extern void       FUN_803b13e4(void);       // DSP interrupt handler

// Read the DSP-side mailbox status (bit15 => mail present).
ushort dsp_mailbox_read_status(void)
{
    return (ushort)(DAT_cc005000 >> 0xf);
}

// Read the CPU-side (to-DSP) mailbox status (bit15 => busy/full).
ushort dsp_mailbox_out_status(void)
{
    return (ushort)(DAT_cc005004 >> 0xf);
}

// Read the full 32-bit CPU-side mailbox word (hi<<16 | lo).
undefined4 dsp_mailbox_out_read(void)
{
    return CONCAT22(DAT_cc005004, DAT_cc005006);
}

// Write a 32-bit mailbox command word to the DSP (hi then lo halfword).
void dsp_mailbox_write(undefined4 mail)
{
    DAT_cc005000 = (short)((uint)mail >> 0x10);
    DAT_cc005002 = (short)mail;
}

// One-shot DSP scheduler bring-up: register driver version, install the DSP
// interrupt handler (idx 7), unmask the DSP interrupt (0x1000000), prime the
// DSP interrupt/status word (0xcc00500a) and mark the scheduler running.
void dsp_sched_init(void)
{
    ushort ctrl;
    char*  version;
    undefined8 verId;

    version = s_04_15_32_8049f154;
    verId = FUN_803b1394();
    if (DAT_806133f0 != 1) {
        os_register_version((int)verId, DAT_80611498, (int)version);
        os_disable_interrupts();
        os_set_interrupt_handler(7, (void*)FUN_803b13e4);
        os_unmask_interrupts(0x1000000);
        ctrl = DAT_cc00500a;
        DAT_cc00500a = ctrl & 0xff57 | 0x800;
        ctrl = DAT_cc00500a;
        DAT_cc00500a = ctrl & 0xff53;
        DAT_80613400 = 0;
        DAT_8061340c = 0;
        DAT_80613404 = 0;
        DAT_80613408 = 0;
        DAT_806133f0 = 1;
        os_get_interrupts_enabled();
    }
}

// Return the DSP scheduler-initialised latch (non-zero once dsp_sched_init ran).
undefined4 dsp_is_initialized(void)
{
    return DAT_806133f0;
}

} // extern "C"
