// batch_1086 (part 2 -> new file): the MetroTRK register/memory access core - the
// register-group read/write handlers, the FPR and SPR access paths, the exception
// notify/stop event body builders, and the pump-side step-continuation logic.
//
// The register accessors work by assembling tiny PPC trampolines on the stack (a
// mfspr/mtspr or lfd/stfd instruction followed by blr), flushing them to the
// instruction cache, and calling them - the classic MetroTRK self-modifying-code
// technique for touching registers that have no memory address. The DAT_803e1b*
// words are the pre-baked instruction templates; the high opcode bits are OR'd with
// the register number shifted into place (param << 0x15).
//
// Non-matching decomp: the trampoline dispatch is modelled with a function-pointer
// typedef; TRK message/cache/notify helpers resolve as cross-module externs.
//
// Address range: 0x803940dc - 0x8039513f

#include "common.h"

// --- cross-module TRK helpers ----------------------------------------------
extern void FUN_80396f44(void);                                  // step hook (no-op)
extern void trk_flush_cache_range(unsigned int addr, unsigned int count);
extern void trk_nop_392c(void);
extern void trk_nop_3934(void);
extern void trk_nop_3ce4(void);
extern void trk_read_fpscr(unsigned long long *out);
extern "C" void memset_wrapper(int dst, int val, unsigned int n);
extern int  trk_msg_write_bytes(int msg, unsigned char *in, int count);
extern void trk_msg_read_u64(int msg, unsigned char *out);
extern int  trk_msg_write_u64(int msg, unsigned int hdr, unsigned int lo, unsigned int hi);
extern unsigned int trk_msg_read_u32_array(int msg, unsigned char *out, int count);
extern unsigned int trk_msg_write_u32_array(int msg, unsigned int *in, int count);
extern int  trk_host_notify(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                            int opcode, void *body, unsigned int *out, int a12, int a13,
                            int a14, int a15, int a16);
extern void msg_request_init(void *req, int type);
extern void msg_queue_post(int req);

// FUN_803950c8: the general guarded scalar access (defined in trk_mem_access.cpp).
extern int  trk_access_scalar(int addr, unsigned int val, int *io_size, int a4, int is_read);

// --- TRK register/state globals --------------------------------------------
extern int          DAT_805f144c;      // run/stop state
extern unsigned int DAT_805f1450;      // suspend-notify guard
extern unsigned int DAT_805f14d8;      // cumulative host-transfer counter / current PC
extern unsigned int DAT_805f1650;      // scheduler/resume flags
extern unsigned int DAT_805f1750;      // exception cause word
extern unsigned char DAT_805f13b4;     // notify body scratch
extern unsigned int DAT_805f1458;      // context-register save array base
extern unsigned int DAT_805f1600;      // register cache array base
extern unsigned int DAT_805f1644;      // register cache sub-range marker
extern unsigned int DAT_805f16d0;      // register cache sub-range marker
extern unsigned int DAT_805f191c;      // trampoline scratch operand

// Step-control block.
extern unsigned int DAT_80498014;
extern int          DAT_80498018;
extern int          DAT_8049801c;
extern unsigned int DAT_80498020;
extern unsigned int DAT_80498024;
// Saved FP/exception context (low byte of DAT_80498010 is the fault flag).
extern unsigned int DAT_80498004;
extern unsigned int DAT_80498008;
extern unsigned int DAT_8049800c;
extern unsigned int DAT_80498010;
// Register-cache dirty flags.
extern unsigned char DAT_80497ff8;
extern unsigned char DAT_80497ff9;

// PPC instruction templates for the register-access trampolines.
extern unsigned int DAT_803e1b38, DAT_803e1b3c, DAT_803e1b40, DAT_803e1b44;
extern unsigned int DAT_803e1b48, DAT_803e1b4c, DAT_803e1b50, DAT_803e1b5c;
extern unsigned int DAT_803e1b60, DAT_803e1b64, DAT_803e1b68, DAT_803e1b6c;
extern unsigned int DAT_803e1b70, DAT_803e1b74, DAT_803e1b78;
extern unsigned int DAT_803e1b80, DAT_803e1b84, DAT_803e1b88, DAT_803e1b8c;
extern unsigned int DAT_803e1b90, DAT_803e1b94, DAT_803e1b98, DAT_803e1b9c;
extern unsigned int DAT_803e1ba0, DAT_803e1ba4;

// A trampoline call site: (assembled code)(operand_ptr, scratch).
typedef void (*trk_tramp_fn)(void *arg0, void *arg1);

// ---------------------------------------------------------------------------
// trk_read_fpscr forward already declared. FUN_8039507c: read a 4-byte scalar from
// target memory via the general guarded access; maps a short read to error 0x700.
int trk_read_u32(int addr, unsigned int val)
{
    int size = 4;
    int rc = trk_access_scalar(addr, val, &size, 0, 1);
    if (rc == 0 && size != 4) {
        rc = 0x700;
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_step_range (0x803940dc) - arm a range/branch step (from the register command),
// like trk_step but seeding step mode 0 with an instruction-count budget instead of
// a PC window. Runs the step hook, requests resume via scheduler flag 0x400, adjusts
// the nesting count for a step/trace cause, clears the stopped state, returns 0; a
// non-zero mode returns 0x703 (unsupported).
unsigned int trk_step_range(int count, int mode)
{
    unsigned int status;
    if (mode == 0) {
        DAT_80498018 = 0;
        DAT_80498014 = 1;
        DAT_8049801c = count;
        FUN_80396f44();
        DAT_805f1650 = DAT_805f1650 | 0x400;
        if (DAT_80498018 == 0 || DAT_80498018 == 0x10) {
            DAT_8049801c = DAT_8049801c - 1;
        }
        status = 0;
        DAT_805f144c = 0;
    } else {
        status = 0x703;
    }
    return status;
}

// ---------------------------------------------------------------------------
// trk_send_exception_event (0x80394188) - build a 0x91 (exception) notify body: a
// 0x40-byte message tagged 0x91 carrying the saved DSISR/DAR context word and the
// faulting-address scalar, then write it to the message.
void trk_send_exception_event(int msg)
{
    unsigned int body[16];
    unsigned char *hdr = (unsigned char *)body;
    memset_wrapper((int)body, 0, 0x40);
    body[0] = 0x40;                 // length
    hdr[4] = 0x91;                  // event tag
    body[2] = DAT_80498004;         // context word
    trk_read_u32((int)&body[1], DAT_80498004);
    body[4] = (unsigned int)(unsigned short)(DAT_8049800c & 0xffff);
    trk_msg_write_bytes(msg, hdr, 0x40);
}

// ---------------------------------------------------------------------------
// trk_send_stop_event (0x8039420c) - build a 0x90 (stop) notify body: a 0x40-byte
// message tagged 0x90 carrying the current PC/transfer count and the low half of the
// exception cause, then write it to the message.
void trk_send_stop_event(int msg)
{
    unsigned int body[16];
    unsigned char *hdr = (unsigned char *)body;
    memset_wrapper((int)body, 0, 0x40);
    body[0] = 0x40;                 // length
    hdr[4] = 0x90;                  // event tag
    body[2] = DAT_805f14d8;         // current PC / count
    trk_read_u32((int)&body[1], DAT_805f14d8);
    body[4] = DAT_805f1750 & 0xffff;
    trk_msg_write_bytes(msg, hdr, 0x40);
}

// ---------------------------------------------------------------------------
// trk_pump_check_step (0x80394298) - message-pump handler run for a stop/step event
// (message types 3 and 4). If a step is pending it decides whether the step target
// has been reached: for a step-count cause 0xd00 it compares the current count
// against the [DAT_80498020, DAT_80498024] window (single-step) or checks the
// remaining-count for a branch step. If the step should continue it re-arms the step
// hook and requests resume; otherwise it marks the target stopped and notifies the
// host with a 0x90 event.
int trk_pump_check_step(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                        int *msg_type, int a10, unsigned int *reached, int a12,
                        int a13, int a14, int a15, int a16)
{
    int result = 0;
    if (*msg_type < 5 && 2 < *msg_type) {
        if (DAT_80498014 != 0) {
            reached = (unsigned int *)1;
            DAT_805f1650 = DAT_805f1650 & 0xfffffbff;   // clear resume flag
            if ((DAT_805f1750 & 0xffff) == 0xd00) {
                if (DAT_80498018 == 1) {
                    if (DAT_80498020 <= DAT_805f14d8 && DAT_805f14d8 <= DAT_80498024) {
                        reached = (unsigned int *)0;
                    }
                } else if (DAT_80498018 < 1 && -1 < DAT_80498018 && DAT_8049801c != 0) {
                    reached = (unsigned int *)0;
                }
            }
            if (reached == (unsigned int *)0) {
                reached = &DAT_80498014;
                DAT_80498014 = 1;
                FUN_80396f44();
                DAT_805f1650 = DAT_805f1650 | 0x400;
                if (DAT_80498018 == 0 || DAT_80498018 == 0x10) {
                    DAT_8049801c = DAT_8049801c - 1;
                }
                DAT_805f144c = 0;
            } else {
                DAT_80498014 = 0;
            }
        }
        if (DAT_80498014 == 0) {
            DAT_805f144c = 1;
            result = trk_host_notify(a1, a2, a3, a4, a5, a6, a7, a8, 0x90,
                                     &DAT_805f13b4, reached, a12, a13, a14, a15, a16);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// trk_post_stop_message (0x80394428) - post the appropriate pump message for a stop:
// unless a suspend-notify is already in flight (DAT_805f1450), pick the message type
// from the exception cause - a step/trace cause (0xd00/0x700) with the sentinel PC
// 0xfe00000 posts type 5 (program exit) else type 3 (breakpoint), any other cause
// posts type 4 - then enqueue it.
void trk_post_stop_message(void)
{
    unsigned int req[5];
    int pc;

    if (DAT_805f1450 == 0) {
        unsigned int cause = DAT_805f1750 & 0xffff;
        int type;
        if (cause == 0xd00 || (cause < 0xd00 && cause == 0x700)) {
            trk_read_u32((int)&pc, DAT_805f14d8);
            if (pc == 0xfe00000) {
                type = 5;
            } else {
                type = 3;
            }
        } else {
            type = 4;
        }
        msg_request_init(req, type);
        msg_queue_post((int)req);
    } else {
        DAT_805f1450 = 0;
    }
}

// ---------------------------------------------------------------------------
// trk_access_gpr_group (0x803944d4) - read or write the general register group by
// assembling read/write trampolines on the stack. It rejects a group above 0x1f
// (0x701), saves the FP/exception context, primes three helper trampolines, then per
// register assembles either a "read GPR" (0xe0030000) or "write GPR" (0xf0030000)
// instruction with the register number folded in and executes it, moving 8 bytes per
// register. Returns 0x702 with zero count on a fault, else the running status.
int trk_access_gpr_group(unsigned int first, unsigned int last, int msg, int *out_count, int is_write)
{
    int rc;
    unsigned int reg_shift;
    unsigned int scratch;
    unsigned int u64buf[2];
    unsigned int tramp[10];

    if (last < 0x20) {
        unsigned int saved0 = DAT_80498004;
        unsigned int saved1 = DAT_80498008;
        unsigned int saved2 = DAT_8049800c;
        unsigned int saved3 = DAT_80498010;
        DAT_80498010 = DAT_80498010 & 0xffffff00;      // clear fault flag

        // prime the three setup trampolines (mtmsr/save helpers)
        tramp[0] = DAT_803e1b38; tramp[1] = DAT_803e1b3c; tramp[2] = DAT_803e1b40;
        tramp[3] = DAT_803e1b44; tramp[4] = DAT_803e1b48; tramp[5] = DAT_803e1b4c;
        tramp[6] = DAT_803e1b50;
        tramp[7] = 0x7c98e2a6; tramp[8] = 0x90830000; tramp[9] = 0x4e800020;
        trk_flush_cache_range((unsigned int)tramp, 0x28);
        (reinterpret_cast<trk_tramp_fn>((void*)tramp))(&scratch, &DAT_805f191c);
        scratch = scratch | 0xa0000000;

        tramp[0] = DAT_803e1b38; tramp[1] = DAT_803e1b3c; tramp[2] = DAT_803e1b40;
        tramp[3] = DAT_803e1b44; tramp[4] = DAT_803e1b48; tramp[5] = DAT_803e1b4c;
        tramp[6] = DAT_803e1b50;
        tramp[7] = 0x80830000; tramp[8] = 0x7c98e3a6; tramp[9] = 0x4e800020;
        trk_flush_cache_range((unsigned int)tramp, 0x28);
        (reinterpret_cast<trk_tramp_fn>((void*)tramp))(&scratch, &DAT_805f191c);
        scratch = 0;

        tramp[0] = DAT_803e1b38; tramp[1] = DAT_803e1b3c; tramp[2] = DAT_803e1b40;
        tramp[3] = DAT_803e1b44; tramp[4] = DAT_803e1b48; tramp[5] = DAT_803e1b4c;
        tramp[6] = DAT_803e1b50;
        tramp[7] = 0x80830000; tramp[8] = 0x7c90e3a6; tramp[9] = 0x4e800020;
        trk_flush_cache_range((unsigned int)tramp, 0x28);
        (reinterpret_cast<trk_tramp_fn>((void*)tramp))(&scratch, &DAT_805f191c);

        reg_shift = first << 0x15;
        *out_count = 0;
        rc = 0;
        while (first <= last && rc == 0) {
            if (is_write == 0) {
                trk_msg_read_u64(msg, (unsigned char *)u64buf);
                unsigned int t[10];
                t[0] = reg_shift | 0xe0030000;
                t[1] = DAT_803e1b5c; t[2] = DAT_803e1b60; t[3] = DAT_803e1b64;
                t[4] = DAT_803e1b68; t[5] = DAT_803e1b6c; t[6] = DAT_803e1b70;
                t[7] = DAT_803e1b74; t[8] = DAT_803e1b78; t[9] = 0x4e800020;
                trk_flush_cache_range((unsigned int)t, 0x28);
                (reinterpret_cast<trk_tramp_fn>((void*)t))(u64buf, &DAT_805f191c);
                rc = 0;
            } else {
                unsigned int t[10];
                t[1] = DAT_803e1b5c; t[2] = DAT_803e1b60; t[3] = DAT_803e1b64;
                t[4] = DAT_803e1b68; t[5] = DAT_803e1b6c; t[6] = DAT_803e1b70;
                t[7] = DAT_803e1b74; t[8] = DAT_803e1b78;
                t[0] = reg_shift | 0xf0030000;
                t[9] = 0x4e800020;
                trk_flush_cache_range((unsigned int)t, 0x28);
                (reinterpret_cast<trk_tramp_fn>((void*)t))(u64buf, &DAT_805f191c);
                rc = trk_msg_write_u64(msg, 0, u64buf[0], u64buf[1]);
            }
            reg_shift = reg_shift + 0x200000;
            first = first + 1;
            *out_count = *out_count + 8;
        }
        if ((DAT_80498010 & 0xff) != 0) {
            rc = 0x702;
            *out_count = 0;
        }
        DAT_80498004 = saved0;
        DAT_80498008 = saved1;
        DAT_8049800c = saved2;
        DAT_80498010 = saved3;
    } else {
        rc = 0x701;
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_access_reg_cache (0x8039490c) - read or write the cached register file that
// lives in target memory (base DAT_805f1600). Rejects a group above 0x60 (0x701),
// transfers the inclusive [first,last] word range to/from the message, and on a
// read marks the two dirty sub-ranges (0x805f1644, 0x805f16d0) when the write
// overlaps them. Returns 0x702 on fault.
unsigned int trk_access_reg_cache(unsigned int first, unsigned int last, int msg, int *out_count, int is_write)
{
    unsigned int rc = first;
    unsigned int saved0 = DAT_80498004;
    unsigned int saved1 = DAT_80498008;
    unsigned int saved2 = DAT_8049800c;
    unsigned int saved3 = DAT_80498010;

    if (last < 0x61) {
        DAT_80498010 = DAT_80498010 & 0xffffff00;
        *out_count = 0;
        if (first <= last) {
            int span = (int)(last - first);
            int nwords = span + 1;
            *out_count = *out_count + nwords * 4;
            unsigned int *ptr = &DAT_805f1600 + first;
            if (is_write == 0) {
                if (ptr < (unsigned int *)((int)&DAT_805f1644 + 1) &&
                    (unsigned int *)0x805f163f < ptr + span) {
                    DAT_80497ff8 = 1;
                }
                if (ptr < (unsigned int *)((int)&DAT_805f16d0 + 1) &&
                    (unsigned int *)0x805f16cf < ptr + span) {
                    DAT_80497ff9 = 1;
                }
                rc = trk_msg_read_u32_array(msg, (unsigned char *)ptr, nwords);
            } else {
                rc = trk_msg_write_u32_array(msg, ptr, nwords);
            }
        }
        if ((DAT_80498010 & 0xff) != 0) {
            rc = 0x702;
            *out_count = 0;
        }
    } else {
        rc = 0x701;
    }
    DAT_80498004 = saved0;
    DAT_80498008 = saved1;
    DAT_8049800c = saved2;
    DAT_80498010 = saved3;
    return rc;
}

// ---------------------------------------------------------------------------
// trk_access_fpr_group (0x80394a7c) - read or write the floating-point register
// group (0..0x1f FPRs, 0x20 FPSCR, 0x21 the paired-single companion) via lfd/stfd
// trampolines (0xc8030000/0xd8030000) with the register number folded in. Group 0x20
// routes through the FPSCR snapshot on write; group 0x21 uses a merge helper. Returns
// 0x701 above 0x21, 0x702 on fault.
int trk_access_fpr_group(unsigned int first, unsigned int last, int msg, int *out_count, int is_write)
{
    int rc;
    unsigned int reg_shift;
    unsigned int hdr;
    unsigned int u64buf[2];

    if (last < 0x22) {
        unsigned int saved0 = DAT_80498004;
        unsigned int saved1 = DAT_80498008;
        unsigned int saved2 = DAT_8049800c;
        unsigned int saved3 = DAT_80498010;
        DAT_80498010 = DAT_80498010 & 0xffffff00;
        trk_nop_392c();
        trk_nop_3934();
        *out_count = 0;
        reg_shift = first << 0x15;
        rc = 0;
        while (first <= last && rc == 0) {
            unsigned int t[11];
            if (is_write == 0) {
                trk_msg_read_u64(msg, (unsigned char *)u64buf);
                t[1] = DAT_803e1b80; t[2] = DAT_803e1b84; t[3] = DAT_803e1b88;
                t[4] = DAT_803e1b8c; t[5] = DAT_803e1b90; t[6] = DAT_803e1b94;
                t[7] = DAT_803e1b98; t[8] = DAT_803e1b9c; t[9] = DAT_803e1ba0;
                t[10] = DAT_803e1ba4;
                if (first < 0x20) {
                    t[0] = reg_shift | 0xc8030000;   // lfd fN
                    t[10] = 0x4e800020;
                    trk_flush_cache_range((unsigned int)t, 0x28);
                    (reinterpret_cast<trk_tramp_fn>((void*)t))(u64buf, &DAT_805f191c);
                } else if (first == 0x20) {
                    trk_nop_3ce4();
                    u64buf[0] = 0;
                } else if (first == 0x21) {
                    u64buf[0] = u64buf[1];
                    unsigned int m[9];
                    m[3] = DAT_803e1b38; m[4] = DAT_803e1b3c; m[5] = DAT_803e1b40;
                    m[6] = DAT_803e1b44; m[7] = DAT_803e1b48; m[8] = DAT_803e1b4c;
                    m[2] = DAT_803e1b50;
                    m[1] = 0x7c9efba6; m[0] = 0x80830000;
                    trk_flush_cache_range((unsigned int)m, 0x28);
                    (reinterpret_cast<trk_tramp_fn>((void*)m))(u64buf, &DAT_805f191c);
                }
                rc = 0;
            } else {
                unsigned int uhdr;
                t[1] = DAT_803e1b80; t[2] = DAT_803e1b84; t[3] = DAT_803e1b88;
                t[4] = DAT_803e1b8c; t[5] = DAT_803e1b90; t[6] = DAT_803e1b94;
                t[7] = DAT_803e1b98; t[8] = DAT_803e1b9c; t[9] = DAT_803e1ba0;
                t[10] = DAT_803e1ba4;
                if (first < 0x20) {
                    t[0] = reg_shift | 0xc8030000;
                    if (is_write != 0) {
                        t[0] = reg_shift | 0xd8030000;  // stfd fN
                    }
                    t[10] = 0x4e800020;
                    trk_flush_cache_range((unsigned int)t, 0x28);
                    (reinterpret_cast<trk_tramp_fn>((void*)t))(u64buf, &DAT_805f191c);
                    uhdr = 0;
                } else if (first == 0x20) {
                    if (is_write == 0) {
                        trk_nop_3ce4();
                    } else {
                        trk_read_fpscr((unsigned long long *)u64buf);
                    }
                    uhdr = u64buf[0];
                    u64buf[0] = 0;
                } else {
                    uhdr = DAT_803e1b9c;
                    if (first == 0x21) {
                        if (is_write == 0) {
                            u64buf[0] = u64buf[1];
                        }
                        unsigned int m[9];
                        m[3] = DAT_803e1b38; m[4] = DAT_803e1b3c; m[5] = DAT_803e1b40;
                        m[6] = DAT_803e1b44; m[7] = DAT_803e1b48; m[8] = DAT_803e1b4c;
                        m[2] = DAT_803e1b50;
                        if (is_write == 0) {
                            m[0] = 0x80830000; m[1] = 0x7c9efba6;
                        } else {
                            m[1] = 0x90830000; m[0] = 0x7c9efaa6;
                        }
                        trk_flush_cache_range((unsigned int)m, 0x28);
                        (reinterpret_cast<trk_tramp_fn>((void*)m))(u64buf, &DAT_805f191c);
                        uhdr = 0;
                        if (is_write != 0) {
                            u64buf[1] = u64buf[0];
                            u64buf[0] = 0;
                        }
                    }
                }
                hdr = uhdr;
                rc = trk_msg_write_u64(msg, hdr, u64buf[0], u64buf[1]);
            }
            reg_shift = reg_shift + 0x200000;
            first = first + 1;
            *out_count = *out_count + 8;
        }
        if ((DAT_80498010 & 0xff) != 0) {
            rc = 0x702;
            *out_count = 0;
        }
        DAT_80498004 = saved0;
        DAT_80498008 = saved1;
        DAT_8049800c = saved2;
        DAT_80498010 = saved3;
    } else {
        rc = 0x701;
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_access_ctx_regs (0x80394f88) - read or write the saved context-register array
// (base DAT_805f1458), the group used for the special-purpose registers captured on
// entry. Rejects a group above 0x24 (0x701), transfers the inclusive word range
// to/from the message, returns 0x702 on fault.
unsigned int trk_access_ctx_regs(int first, unsigned int last, int msg, int *out_count, int is_write)
{
    unsigned int saved0 = DAT_80498004;
    unsigned int saved1 = DAT_80498008;
    unsigned int saved2 = DAT_8049800c;
    unsigned int saved3 = DAT_80498010;
    unsigned int rc;

    if (last < 0x25) {
        int nwords = ((int)last - first) + 1;
        DAT_80498010 = DAT_80498010 & 0xffffff00;
        *out_count = nwords * 4;
        if (is_write == 0) {
            rc = trk_msg_read_u32_array(msg, (unsigned char *)(&DAT_805f1458 + first), nwords);
        } else {
            rc = trk_msg_write_u32_array(msg, &DAT_805f1458 + first, nwords);
        }
        if ((DAT_80498010 & 0xff) != 0) {
            rc = 0x702;
            *out_count = 0;
        }
    } else {
        rc = 0x701;
    }
    DAT_80498004 = saved0;
    DAT_80498008 = saved1;
    DAT_8049800c = saved2;
    DAT_80498010 = saved3;
    return rc;
}
