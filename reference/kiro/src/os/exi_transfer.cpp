// EXI (External Interface) channel transfer engine.
//
// The GameCube EXI bus drives the three serial device channels (memory-card
// slots A/B and the serial port / IPL ROM). This file is the low-level
// transfer layer that the SI-device / memory-card driver (input/si_device.cpp)
// sits on top of: immediate (register) transfers, DMA transfers, the transfer
// completion poll ("sync"), channel select, and the card-present probe /
// attach path.
//
// Per-channel hardware registers live at 0xCC006800 with a 0x14 (5-word)
// stride: +0x00 channel parameter/status (CPR), +0x04 DMA memory address,
// +0x08 DMA length, +0x0C transfer control (TSTART / DMA / RW / len), +0x10
// immediate data. Per-channel software state lives at 0x8060E940 with a 0x40
// stride: +0x04 exi-callback, +0x08 detach callback, +0x0C flags (bit0=DMA
// in-flight, bit1=imm in-flight, bit2=attached, bit3=locked), +0x10 imm
// length, +0x14 imm buffer ptr.
//
// NON-MATCHING: the flat &DAT_cc0068xx / &DAT_8060e9xx byte-offset arithmetic,
// the 4-byte big-endian pack/unpack unrolled by 8, and the HW-revision / IPL
// errata guard in exi_sync are kept verbatim from the decompiler output.
//
// Batch: batch_1151
// Source module: os/exi_transfer.cpp
#include "common.h"

// Ghidra function-pointer placeholder type used by the interrupt handlers.
typedef void code;

// ---------------------------------------------------------------------------
// Per-file externs for raw symbols referenced by this translation unit.
// (This TU includes only common.h; the interrupt helpers are declared with the
//  same shapes used across ai.cpp / arq.cpp.)
// ---------------------------------------------------------------------------
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void os_unmask_interrupts(unsigned int mask);
extern unsigned int       os_get_hw_revision(void);
extern int                os_get_console_type(void);
extern unsigned long long os_read_time_base(void);
extern u64                __div2i(u32 loDividend, u32 hiDividend, u32 hiDivisor, u32 loDivisor);

extern void os_mask_interrupts(unsigned int mask);
extern void os_clear_context(int ctx);
extern void os_set_current_context(unsigned int ctx);
extern void os_set_interrupt_handler(int idx, void (*handler)(short, uint));
// The kernel version-registration entry is called here with a large forwarded
// argument list; declared variadic to match the tail-forwarded call site.
extern void os_register_version(int fmt, ...);
extern int  msl_memmove(unsigned int dst, unsigned int src, int n);

// TRK hardware interrupt-mask policy hook (defined in os/trk_hw_dispatch.cpp).
extern void trk_hw_set_int_mask(int channel, int *stateWord);

// Forward declarations for the intra-file device-id helpers (defined below).
undefined4 exi_get_device_id(int channel, int arg, byte *out);
undefined4 exi_read_device_id_locked(int channel, int arg, byte *out);
void       exi_recognize_device(int channel, int arg);

// Forward declarations for the intra-file helpers referenced before their
// definitions.
uint        exi_select(int channel, int csEnable, int clkBit, int extBit);
undefined4  exi_sync(int channel);
undefined4  exi_probe(int channel);

// EXI channel hardware register block (0xCC006800, per-channel stride 0x14).
extern uint  DAT_cc006800;   // +0x00 ch0 channel parameter/status (CPR)
extern uint  DAT_cc006804;   // +0x04 ch0 DMA memory address
extern uint  DAT_cc006808;   // +0x08 ch0 DMA length
extern uint  DAT_cc00680c;   // +0x0C ch0 transfer control (TSTART/DMA/RW/len)
extern uint  DAT_cc006810;   // +0x10 ch0 immediate data
extern uint  DAT_cc006814;   // +0x14 ch1 CPR
extern uint  DAT_cc006820;   // +0x20 ch1 transfer control
extern uint  DAT_cc006828;   // +0x28 ch2 CPR
extern uint  DAT_cc006834;   // +0x34 ch2 transfer control

// Per-channel software state block (0x8060E940, per-channel stride 0x40).
extern uint  DAT_8060e940;   // +0x00 saved interrupt-handler slot
extern uint  DAT_8060e944;   // +0x04 exi (transfer-complete) callback
extern uint  DAT_8060e948;   // +0x08 detach / ext callback
extern uint  DAT_8060e94c;   // +0x0C flags
extern uint  DAT_8060e950;   // +0x10 immediate transfer length
extern uint  DAT_8060e954;   // +0x14 immediate transfer buffer ptr
extern uint  DAT_8060e958;   // +0x18 locked device id
extern uint  DAT_8060e95c;   // +0x1C cached device-id read
extern uint  DAT_8060e960;   // +0x20 attached-device flag
extern uint  DAT_8060e964;   // +0x24 ext-callback queue count
extern uint  DAT_8060e968;   // +0x28 ext-callback queue base (id/cb pairs)
extern uint  DAT_8060e96c;   // +0x2C ext-callback queue first cb
extern uint  DAT_8060e970;   // +0x30 ext-callback queue second slot
extern uint  DAT_8060e9a0;   // +0x60 (probe scratch)

// IPL / probe debounce timestamp (indexed per channel).
extern int   DAT_800030c0;
extern int   DAT_800030c4;
// Timer-tick base (bus clock / 4) and IPL revision guard.
extern uint  DAT_800000f8;
// Boot / IPL detection words and device-id scratch used by exi_init.
extern uint  DAT_80613418;   // boot device-id read buffer
extern int   DAT_80613068;   // "skip auto-detect" flag
extern uint  DAT_806114b8;   // kernel version blob passed to os_register_version

// Debug-console-over-EXI configuration (set by exi_recognize_device when a
// recognized debug device is found; consumed by exi_console_write).
extern uint  DAT_80613420;   // console EXI channel
extern uint  DAT_80613424;   // console EXI device index
extern uint  DAT_80613428;   // console-present magic (0xa5ff005a)
extern uint  DAT_8061342c;   // console-recognized magic (0xa5ff005a)
extern short DAT_800030e6;

// ---------------------------------------------------------------------------
// exi_imm_transfer_4 - kick a single immediate (register) transfer of up to
// four bytes on a channel. Requires the channel attached (bit2) and idle
// (bits0/1 clear). For a write, the source bytes are packed big-endian into
// the immediate data register; for a read the length is stashed for the sync
// step to unpack. Returns 1 if the transfer was started, 0 if busy/detached.
//   rw: 0 = read, 1 = write, 2 = read+write
// ---------------------------------------------------------------------------
undefined4 exi_imm_transfer_4(int channel, byte *buf, int len, int rw,
                              undefined4 completeCb)
{
    int    iVar1;
    uint   uVar2;
    undefined4 uVar3;
    byte  *pbVar4;
    int    iVar5;
    int    iVar6;
    uint   uVar7;

    iVar1 = channel * 0x40;
    os_disable_interrupts();
    if (((*(uint *)(&DAT_8060e94c + iVar1) & 3) == 0) &&
        ((*(uint *)(&DAT_8060e94c + iVar1) & 4) != 0)) {
        *(undefined4 *)(&DAT_8060e944 + iVar1) = completeCb;
        if (*(int *)(&DAT_8060e944 + iVar1) != 0) {
            exi_select(channel, 0, 1, 0);
            os_unmask_interrupts(0x200000 >> channel * 3);
        }
        *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) | 2;
        if (rw != 0) {
            uVar2 = 0;
            iVar6 = 0;
            if (0 < len) {
                if ((8 < len) && (uVar7 = len - 1U >> 3, pbVar4 = buf, 0 < len + -8)) {
                    do {
                        uVar2 = uVar2 | (uint)*pbVar4 << (3 - iVar6) * 8 |
                                (uint)pbVar4[1] << (3 - (iVar6 + 1)) * 8 |
                                (uint)pbVar4[2] << (3 - (iVar6 + 2)) * 8 |
                                (uint)pbVar4[3] << iVar6 * -8 |
                                (uint)pbVar4[4] << (3 - (iVar6 + 4)) * 8 |
                                (uint)pbVar4[5] << (3 - (iVar6 + 5)) * 8 |
                                (uint)pbVar4[6] << (3 - (iVar6 + 6)) * 8 |
                                (uint)pbVar4[7] << (3 - (iVar6 + 7)) * 8;
                        pbVar4 = pbVar4 + 8;
                        iVar6 = iVar6 + 8;
                        uVar7 = uVar7 - 1;
                    } while (uVar7 != 0);
                }
                pbVar4 = buf + iVar6;
                iVar5 = len - iVar6;
                if (iVar6 < len) {
                    do {
                        uVar2 = uVar2 | (uint)*pbVar4 << (3 - iVar6) * 8;
                        pbVar4 = pbVar4 + 1;
                        iVar6 = iVar6 + 1;
                        iVar5 = iVar5 + -1;
                    } while (iVar5 != 0);
                }
            }
            *(uint *)(&DAT_cc006810 + channel * 0x14) = uVar2;
        }
        *(byte **)(&DAT_8060e954 + iVar1) = buf;
        iVar6 = len;
        if (rw == 1) {
            iVar6 = 0;
        }
        *(int *)(&DAT_8060e950 + iVar1) = iVar6;
        (&DAT_cc00680c)[channel * 5] = rw << 2 | 1U | (len + -1) * 0x10;
        os_get_interrupts_enabled();
        uVar3 = 1;
    }
    else {
        os_get_interrupts_enabled();
        uVar3 = 0;
    }
    return uVar3;
}

// ---------------------------------------------------------------------------
// exi_imm_transfer - immediate transfer of an arbitrary-length buffer, driven
// in <=4-byte chunks with a blocking sync between each. Returns 1 on success,
// 0 if any chunk failed to start or the sync failed.
// ---------------------------------------------------------------------------
undefined4 exi_imm_transfer(int channel, byte *buf, int len, int rw)
{
    int iVar1;
    int iVar2;

    while (true) {
        if (len == 0) {
            return 1;
        }
        iVar2 = len;
        if (3 < len) {
            iVar2 = 4;
        }
        iVar1 = exi_imm_transfer_4(channel, buf, iVar2, rw, 0);
        if (iVar1 == 0) break;
        iVar1 = exi_sync(channel);
        if (iVar1 == 0) {
            return 0;
        }
        buf = buf + iVar2;
        len = len - iVar2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// exi_dma_transfer - kick a DMA transfer on a channel. Programs the DMA memory
// address (32-byte aligned) and length registers and sets the control word
// with the DMA bit. Requires attached + idle; returns 1 started / 0 busy.
//   rw: 0 = read, 1 = write
// ---------------------------------------------------------------------------
undefined4 exi_dma_transfer(int channel, uint dmaAddr, undefined4 dmaLen,
                            int rw, undefined4 completeCb)
{
    int iVar1;
    undefined4 uVar2;

    iVar1 = channel * 0x40;
    os_disable_interrupts();
    if (((*(uint *)(&DAT_8060e94c + iVar1) & 3) == 0) &&
        ((*(uint *)(&DAT_8060e94c + iVar1) & 4) != 0)) {
        *(undefined4 *)(&DAT_8060e944 + iVar1) = completeCb;
        if (*(int *)(&DAT_8060e944 + iVar1) != 0) {
            exi_select(channel, 0, 1, 0);
            os_unmask_interrupts(0x200000 >> channel * 3);
        }
        *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) | 1;
        *(uint *)(&DAT_cc006804 + channel * 0x14) = dmaAddr & 0x3ffffe0;
        *(undefined4 *)(&DAT_cc006808 + channel * 0x14) = dmaLen;
        (&DAT_cc00680c)[channel * 5] = rw << 2 | 3;
        os_get_interrupts_enabled();
        uVar2 = 1;
    }
    else {
        os_get_interrupts_enabled();
        uVar2 = 0;
    }
    return uVar2;
}

// ---------------------------------------------------------------------------
// exi_sync - block until the channel's active transfer completes (TSTART bit
// clears), unpack the read-back immediate data into the caller's buffer, and
// clear the busy flags. Returns 1 on success. The final guard reproduces the
// IPL/HW-revision errata: on early hardware certain 4-byte reads of specific
// device-id words are treated as failures unless the debug IPL guard is set.
// ---------------------------------------------------------------------------
undefined4 exi_sync(int channel)
{
    int         *piVar1;
    int          iVar2;
    uint         uVar3;
    int          iVar4;
    int          iVar5;
    undefined1  *puVar6;
    int          iVar7;
    undefined4   uVar8;
    uint         uVar9;

    iVar2 = channel * 0x40;
    uVar8 = 0;
    do {
        if ((*(uint *)(&DAT_8060e94c + iVar2) & 4) == 0) {
            return 0;
        }
    } while (((&DAT_cc00680c)[channel * 5] & 1) != 0);
    os_disable_interrupts();
    if ((*(uint *)(&DAT_8060e94c + iVar2) & 4) != 0) {
        if ((*(uint *)(&DAT_8060e94c + iVar2) & 3) != 0) {
            if ((*(uint *)(&DAT_8060e94c + iVar2) & 2) != 0) {
                iVar4 = *(int *)(&DAT_8060e950 + iVar2);
                if (iVar4 != 0) {
                    puVar6 = *(undefined1 **)(&DAT_8060e954 + iVar2);
                    uVar3 = *(uint *)(&DAT_cc006810 + channel * 0x14);
                    iVar5 = 0;
                    if (0 < iVar4) {
                        if ((8 < iVar4) && (uVar9 = iVar4 - 1U >> 3, 0 < iVar4 + -8)) {
                            do {
                                *puVar6 = (char)(uVar3 >> (3 - iVar5) * 8);
                                puVar6[1] = (char)(uVar3 >> (3 - (iVar5 + 1)) * 8);
                                puVar6[2] = (char)(uVar3 >> (3 - (iVar5 + 2)) * 8);
                                puVar6[3] = (char)(uVar3 >> iVar5 * -8);
                                puVar6[4] = (char)(uVar3 >> (3 - (iVar5 + 4)) * 8);
                                puVar6[5] = (char)(uVar3 >> (3 - (iVar5 + 5)) * 8);
                                puVar6[6] = (char)(uVar3 >> (3 - (iVar5 + 6)) * 8);
                                puVar6[7] = (char)(uVar3 >> (3 - (iVar5 + 7)) * 8);
                                puVar6 = puVar6 + 8;
                                iVar5 = iVar5 + 8;
                                uVar9 = uVar9 - 1;
                            } while (uVar9 != 0);
                        }
                        iVar7 = iVar4 - iVar5;
                        if (iVar5 < iVar4) {
                            do {
                                *puVar6 = (char)(uVar3 >> (3 - iVar5) * 8);
                                puVar6 = puVar6 + 1;
                                iVar5 = iVar5 + 1;
                                iVar7 = iVar7 + -1;
                            } while (iVar7 != 0);
                        }
                    }
                }
            }
            *(uint *)(&DAT_8060e94c + iVar2) = *(uint *)(&DAT_8060e94c + iVar2) & 0xfffffffc;
        }
        uVar3 = os_get_hw_revision();
        if (((((uVar3 != 0xff) || (uVar3 = os_get_console_type(), (uVar3 & 0xf0000000) == 0x20000000))
             || (*(int *)(&DAT_8060e950 + iVar2) != 4)) ||
            ((((&DAT_cc006800)[channel * 5] & 0x70) != 0 ||
             (((piVar1 = (int *)(&DAT_cc006810 + channel * 0x14), *piVar1 != 0x1010000 &&
               (*piVar1 != 0x5070000)) && (*piVar1 != 0x4220001)))))) || (DAT_800030e6 == -0x7e00)) {
            uVar8 = 1;
        }
    }
    os_get_interrupts_enabled();
    return uVar8;
}

// ---------------------------------------------------------------------------
// exi_select - set the channel parameter register (CPR): pick the device
// enable, clock rate, and interrupt bits. Preserves the reserved/mask bits and
// returns the previous CPR value.
//   csEnable -> bit1, clkBit -> bit3, extBit -> bit0x800
// ---------------------------------------------------------------------------
uint exi_select(int channel, int csEnable, int clkBit, int extBit)
{
    uint uVar1;
    uint uVar2;

    uVar1 = (&DAT_cc006800)[channel * 5];
    uVar2 = uVar1 & 0x7f5;
    if (csEnable != 0) {
        uVar2 = uVar2 | 2;
    }
    if (clkBit != 0) {
        uVar2 = uVar2 | 8;
    }
    if (extBit != 0) {
        uVar2 = uVar2 | 0x800;
    }
    (&DAT_cc006800)[channel * 5] = uVar2;
    return uVar1;
}

// ---------------------------------------------------------------------------
// exi_set_handler - install a per-channel interrupt handler slot and refresh
// the TRK hardware interrupt mask for that channel. Channel 2 (serial/EXI-2)
// shares the mask policy of channel 0's state word. Returns the prior slot.
// ---------------------------------------------------------------------------
int exi_set_handler(int channel, int handler)
{
    int  iVar1;
    int *piVar2;

    piVar2 = (int *)(&DAT_8060e940 + channel * 0x40);
    os_disable_interrupts();
    iVar1 = *piVar2;
    *piVar2 = handler;
    if (channel == 2) {
        trk_hw_set_int_mask(0, (int *)&DAT_8060e940);
    }
    else {
        trk_hw_set_int_mask(channel, piVar2);
    }
    os_get_interrupts_enabled();
    return iVar1;
}

// ---------------------------------------------------------------------------
// exi_probe - card-present probe with a debounce window. Reads the CPR
// insertion bits and, on a fresh insertion, records a timebase-derived
// timestamp; the device is only reported present after the debounce interval
// (>=3 ticks) has elapsed. Channel 2 (serial) is always considered present.
// Returns 1 present / 0 absent-or-settling.
// ---------------------------------------------------------------------------
undefined4 exi_probe(int channel)
{
    undefined4 uVar1;
    uint      *puVar2;
    int       *piVar3;
    int        iVar4;
    uint       uVar5;
    undefined8 uVar6;

    if (channel == 2) {
        uVar1 = 1;
    }
    else {
        uVar1 = 1;
        os_disable_interrupts();
        puVar2 = &DAT_cc006800 + channel * 5;
        uVar5 = *puVar2;
        if ((*(uint *)(&DAT_8060e94c + channel * 0x40) & 8) == 0) {
            if ((uVar5 & 0x800) != 0) {
                *puVar2 = *puVar2 & 0x7f5 | 0x800;
                (&DAT_8060e960)[channel * 0x10] = 0;
                (&DAT_800030c0)[channel] = 0;
            }
            if ((uVar5 & 0x1000) == 0) {
                (&DAT_8060e960)[channel * 0x10] = 0;
                (&DAT_800030c0)[channel] = 0;
                uVar1 = 0;
            }
            else {
                uVar5 = DAT_800000f8 / 4000;
                uVar6 = os_read_time_base();
                uVar6 = __div2i((uint)((ulonglong)uVar6 >> 0x20), (uint)uVar6, 0, uVar5);
                uVar6 = __div2i((uint)((ulonglong)uVar6 >> 0x20), (uint)uVar6, 0, 100);
                iVar4 = (int)uVar6 + 1;
                piVar3 = &DAT_800030c0 + channel;
                if (*piVar3 == 0) {
                    *piVar3 = iVar4;
                }
                if (iVar4 - *piVar3 < 3) {
                    uVar1 = 0;
                }
            }
        }
        else if (((uVar5 & 0x1000) == 0) || ((uVar5 & 0x800) != 0)) {
            (&DAT_8060e960)[channel * 0x10] = 0;
            (&DAT_800030c0)[channel] = 0;
            uVar1 = 0;
        }
        os_get_interrupts_enabled();
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// exi_probe_reset - probe the channel and, if newly present and not yet
// attached, run the low-level device reset/probe (exi_get_device_id). Returns 1 if
// the device is present and ready, 0 otherwise.
// ---------------------------------------------------------------------------
int exi_probe_reset(int channel)
{
    int  iVar1;
    byte abStack_c[4];

    iVar1 = exi_probe(channel);
    if ((iVar1 != 0) && ((&DAT_8060e960)[channel * 0x10] == 0)) {
        iVar1 = exi_get_device_id(channel, 0, abStack_c);
        if (iVar1 == 0) {
            iVar1 = 0;
        }
        else {
            iVar1 = 1;
        }
    }
    return iVar1;
}

// ---------------------------------------------------------------------------
// exi_probe_ex - like exi_probe_reset but returns a tri-state: 1 present,
// 0 settling (probe pending), -1 absent (no timestamp recorded).
// ---------------------------------------------------------------------------
undefined4 exi_probe_ex(int channel)
{
    int        iVar1;
    undefined4 uVar2;
    byte       abStack_c[4];

    iVar1 = exi_probe(channel);
    if ((iVar1 != 0) && ((&DAT_8060e960)[channel * 0x10] == 0)) {
        iVar1 = exi_get_device_id(channel, 0, abStack_c);
        if (iVar1 == 0) {
            iVar1 = 0;
        }
        else {
            iVar1 = 1;
        }
    }
    if (iVar1 == 0) {
        if ((&DAT_800030c0)[channel] == 0) {
            uVar2 = 0xffffffff;
        }
        else {
            uVar2 = 0;
        }
    }
    else {
        uVar2 = 1;
    }
    return uVar2;
}

// ---------------------------------------------------------------------------
// exi_attach - probe, reset, then lock/attach the device on a channel and
// register the caller's detach/ext callback. Selects the device, records the
// callback at +0x08, unmasks the channel's ext interrupt, and sets the
// attached flag (bit3). Returns 1 attached / 0 absent-or-busy.
// ---------------------------------------------------------------------------
undefined4 exi_attach(int channel, undefined4 extCb)
{
    int        iVar1;
    int        iVar2;
    undefined4 uVar3;
    byte       abStack_24[16];

    iVar1 = channel * 0x40;
    iVar2 = exi_probe(channel);
    if ((iVar2 != 0) && ((&DAT_8060e960)[channel * 0x10] == 0)) {
        exi_get_device_id(channel, 0, abStack_24);
    }
    os_disable_interrupts();
    if ((&DAT_8060e960)[channel * 0x10] == 0) {
        os_get_interrupts_enabled();
        uVar3 = 0;
    }
    else {
        os_disable_interrupts();
        if (((*(uint *)(&DAT_8060e94c + iVar1) & 8) == 0) &&
            (iVar2 = exi_probe(channel), iVar2 != 0)) {
            exi_select(channel, 1, 0, 0);
            *(undefined4 *)(&DAT_8060e948 + iVar1) = extCb;
            os_unmask_interrupts(0x100000 >> channel * 3);
            *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) | 8;
            os_get_interrupts_enabled();
            uVar3 = 1;
        }
        else {
            os_get_interrupts_enabled();
            uVar3 = 0;
        }
        os_get_interrupts_enabled();
    }
    return uVar3;
}

// ===========================================================================
// batch_1152: lock / select / interrupt-handler / init layer.
// ===========================================================================

// ---------------------------------------------------------------------------
// exi_unlock - release a channel lock (flag bit3). If the lock was taken with
// the "keep selected" bit (0x10) and no queued locker remains, the lock bit is
// cleared and the channel's ext/insert interrupts are masked. Returns 1 if the
// channel was released (or was not locked), 0 if a queued locker still holds it.
// ---------------------------------------------------------------------------
undefined4 exi_unlock(int channel)
{
    int        iVar1;
    undefined4 uVar2;

    iVar1 = channel * 0x40;
    os_disable_interrupts();
    if ((*(uint *)(&DAT_8060e94c + iVar1) & 8) == 0) {
        os_get_interrupts_enabled();
        uVar2 = 1;
    }
    else if (((*(uint *)(&DAT_8060e94c + iVar1) & 0x10) == 0) ||
             (*(int *)(&DAT_8060e958 + iVar1) != 0)) {
        *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) & 0xfffffff7;
        os_mask_interrupts(0x500000 >> channel * 3);
        os_get_interrupts_enabled();
        uVar2 = 1;
    }
    else {
        os_get_interrupts_enabled();
        uVar2 = 0;
    }
    return uVar2;
}

// ---------------------------------------------------------------------------
// exi_select_device - select a device on a locked channel: set the CS/enable
// bit for device index (param_2) and the clock rate (param_3) in the CPR while
// preserving the reserved bits, marking the channel busy (bit2). Channel 2 is
// always selectable; other channels require either being attached, the device
// present (probe), or the caller matching the locked device id. Returns 1 on
// success, 0 if the channel is already selected or the guard fails.
// ---------------------------------------------------------------------------
undefined4 exi_select_device(int channel, int device, int clockRate)
{
    int        iVar1;
    int        iVar2;
    undefined4 uVar3;

    iVar1 = channel * 0x40;
    os_disable_interrupts();
    if (((*(uint *)(&DAT_8060e94c + iVar1) & 4) == 0) &&
        ((channel == 2 ||
         ((((device != 0 || ((*(uint *)(&DAT_8060e94c + iVar1) & 8) != 0)) ||
           (iVar2 = exi_probe(channel), iVar2 != 0)) &&
          (((*(uint *)(&DAT_8060e94c + iVar1) & 0x10) != 0 &&
           (*(int *)(&DAT_8060e958 + iVar1) == device)))))))) {
        *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) | 4;
        (&DAT_cc006800)[channel * 5] =
             (&DAT_cc006800)[channel * 5] & 0x405 | (1 << device) << 7 | clockRate << 4;
        if ((*(uint *)(&DAT_8060e94c + iVar1) & 8) != 0) {
            if (channel == 1) {
                os_mask_interrupts(0x20000);
            }
            else if ((channel < 1) && (-1 < channel)) {
                os_mask_interrupts(0x100000);
            }
        }
        os_get_interrupts_enabled();
        uVar3 = 1;
    }
    else {
        os_get_interrupts_enabled();
        uVar3 = 0;
    }
    return uVar3;
}

// ---------------------------------------------------------------------------
// exi_deselect_device - clear the device select on a channel (busy bit2), drop
// the CS/clock bits from the CPR (keeping reserved 0x405), and re-enable the
// insert/ext interrupts if attached. Returns 1 on success / still-present, 0 if
// the channel was not selected or the device is no longer present.
// ---------------------------------------------------------------------------
undefined4 exi_deselect_device(int channel)
{
    undefined4 uVar1;
    int        iVar2;
    uint       uVar3;

    iVar2 = channel * 0x40;
    os_disable_interrupts();
    if ((*(uint *)(&DAT_8060e94c + iVar2) & 4) == 0) {
        os_get_interrupts_enabled();
        uVar1 = 0;
    }
    else {
        *(uint *)(&DAT_8060e94c + iVar2) = *(uint *)(&DAT_8060e94c + iVar2) & 0xfffffffb;
        uVar3 = (&DAT_cc006800)[channel * 5];
        (&DAT_cc006800)[channel * 5] = uVar3 & 0x405;
        if ((*(uint *)(&DAT_8060e94c + iVar2) & 8) != 0) {
            if (channel == 1) {
                os_unmask_interrupts(0x20000);
            }
            else if ((channel < 1) && (-1 < channel)) {
                os_unmask_interrupts(0x100000);
            }
        }
        os_get_interrupts_enabled();
        if ((channel == 2) || ((uVar3 & 0x80) == 0)) {
            uVar1 = 1;
        }
        else {
            iVar2 = exi_probe(channel);
            if (iVar2 == 0) {
                uVar1 = 0;
            }
            else {
                uVar1 = 1;
            }
        }
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// exi_isr_ext - EXI EXT (device insert / debug-cable) interrupt handler,
// installed at IRQ vectors 9/0xC/0xF. Recovers the channel from the vector
// index, acks the EXT interrupt in the CPR, and dispatches the per-channel
// saved handler slot (+0x00) inside a fresh OS context.
//   vector: PowerPC external-interrupt cause index; channel = (vector-9)/3
// ---------------------------------------------------------------------------
void exi_isr_ext(short vector, uint ctx)
{
    int   channel;
    code *pcVar2;
    undefined1 auStack_2e0[724];

    channel = (vector + -9) / 3;
    (&DAT_cc006800)[channel * 5] = (&DAT_cc006800)[channel * 5] & 0x7f5 | 2;
    pcVar2 = *(code **)(&DAT_8060e940 + channel * 0x40);
    if (pcVar2 != (code *)0x0) {
        os_clear_context((int)auStack_2e0);
        os_set_current_context((uint)auStack_2e0);
        (*(void (*)(int, uint))pcVar2)(channel, ctx);
        os_clear_context((int)auStack_2e0);
        os_set_current_context(ctx);
    }
    return;
}

// ---------------------------------------------------------------------------
// exi_isr_transfer - EXI transfer-complete (TC) interrupt handler, installed at
// IRQ vectors 0xA/0xD/0x10. Acks the TC interrupt, and if a transfer-complete
// callback (+0x04) is pending: for an immediate read it unpacks the read-back
// data into the caller buffer (big-endian, unrolled by 8), clears the busy
// flags, then fires the callback in a fresh OS context.
//   vector: cause index; channel = (vector-10)/3
// ---------------------------------------------------------------------------
void exi_isr_transfer(short vector, uint ctx)
{
    int   channel;
    int   iVar1;
    code *pcVar3;
    uint  uVar4;
    int   iVar5;
    int   iVar6;
    undefined1 *puVar7;
    int   iVar8;
    uint  uVar9;
    undefined1 auStack_2d8[712];

    channel = (vector + -10) / 3;
    iVar1 = channel * 0x40;
    os_mask_interrupts(0x80000000 >> (int)vector);
    (&DAT_cc006800)[channel * 5] = (&DAT_cc006800)[channel * 5] & 0x7f5 | 8;
    pcVar3 = *(code **)(&DAT_8060e944 + iVar1);
    if (pcVar3 != (code *)0x0) {
        iVar6 = 0;
        *(undefined4 *)(&DAT_8060e944 + iVar1) = 0;
        if ((*(uint *)(&DAT_8060e94c + iVar1) & 3) != 0) {
            if ((*(uint *)(&DAT_8060e94c + iVar1) & 2) != 0) {
                iVar5 = *(int *)(&DAT_8060e950 + iVar1);
                if (iVar5 != 0) {
                    puVar7 = *(undefined1 **)(&DAT_8060e954 + iVar1);
                    uVar4 = *(uint *)(&DAT_cc006810 + channel * 0x14);
                    if (0 < iVar5) {
                        if ((8 < iVar5) && (uVar9 = iVar5 - 1U >> 3, 0 < iVar5 + -8)) {
                            do {
                                *puVar7 = (char)(uVar4 >> (3 - iVar6) * 8);
                                puVar7[1] = (char)(uVar4 >> (3 - (iVar6 + 1)) * 8);
                                puVar7[2] = (char)(uVar4 >> (3 - (iVar6 + 2)) * 8);
                                puVar7[3] = (char)(uVar4 >> iVar6 * -8);
                                puVar7[4] = (char)(uVar4 >> (3 - (iVar6 + 4)) * 8);
                                puVar7[5] = (char)(uVar4 >> (3 - (iVar6 + 5)) * 8);
                                puVar7[6] = (char)(uVar4 >> (3 - (iVar6 + 6)) * 8);
                                puVar7[7] = (char)(uVar4 >> (3 - (iVar6 + 7)) * 8);
                                puVar7 = puVar7 + 8;
                                iVar6 = iVar6 + 8;
                                uVar9 = uVar9 - 1;
                            } while (uVar9 != 0);
                        }
                        iVar8 = iVar5 - iVar6;
                        if (iVar6 < iVar5) {
                            do {
                                *puVar7 = (char)(uVar4 >> (3 - iVar6) * 8);
                                puVar7 = puVar7 + 1;
                                iVar6 = iVar6 + 1;
                                iVar8 = iVar8 + -1;
                            } while (iVar8 != 0);
                        }
                    }
                }
            }
            *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) & 0xfffffffc;
        }
        os_clear_context((int)auStack_2d8);
        os_set_current_context((uint)auStack_2d8);
        (*(void (*)(int, uint))pcVar3)(channel, ctx);
        os_clear_context((int)auStack_2d8);
        os_set_current_context(ctx);
    }
    return;
}

// ---------------------------------------------------------------------------
// exi_isr_unlock - EXI unlock/detach interrupt handler, installed at IRQ
// vectors 0xB/0xE. Masks the channel's ext/insert interrupts, clears the lock
// bit (bit3), and fires the pending detach callback (+0x08) in a fresh context.
//   vector: cause index; channel = (vector-0xB)/3
// ---------------------------------------------------------------------------
void exi_isr_unlock(short vector, uint ctx)
{
    int   channel;
    int   iVar1;
    code *pcVar3;
    undefined1 auStack_2d8[712];

    channel = (vector + -0xb) / 3;
    os_mask_interrupts(0x500000 >> channel * 3);
    iVar1 = channel * 0x40;
    pcVar3 = *(code **)(&DAT_8060e948 + iVar1);
    *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) & 0xfffffff7;
    if (pcVar3 != (code *)0x0) {
        os_clear_context((int)auStack_2d8);
        os_set_current_context((uint)auStack_2d8);
        *(undefined4 *)(&DAT_8060e948 + iVar1) = 0;
        (*(void (*)(int, uint))pcVar3)(channel, ctx);
        os_clear_context((int)auStack_2d8);
        os_set_current_context(ctx);
    }
    return;
}

// ---------------------------------------------------------------------------
// exi_init - one-time EXI subsystem bring-up. Waits for all three channels to
// go idle, masks the EXI interrupt block, clears the channel-0/1/2 CPRs, sets
// the ROM-descramble enable bit, installs the eight EXI interrupt handlers
// (EXT / TC / unlock across the three channels), then auto-detects the boot
// device: reads the boot id into DAT_80613418, and unless auto-detect is
// disabled (DAT_80613068) probes channel 0 then channel 1 for the 0x07010000
// (IPL/serial) id and brings the matching channel up via FUN_803ba704.
// Finally forwards the discovered configuration to os_register_version.
// ---------------------------------------------------------------------------
void exi_init(void)
{
    uint  uVar1;
    int   iVar2;
    undefined4 versionFlag;
    byte *pbVar4;
    undefined8 bootCfg;
    int   local_18[2];

    bootCfg = 0;
    do {
        do {
            uVar1 = DAT_cc00680c;
        } while ((uVar1 & 1) == 1);
        uVar1 = DAT_cc006820;
    } while (((uVar1 & 1) == 1) || (uVar1 = DAT_cc006834, (uVar1 & 1) == 1));
    os_mask_interrupts(0x7f8000);
    DAT_cc006800 = 0;
    DAT_cc006814 = 0;
    DAT_cc006828 = 0;
    DAT_cc006800 = 0x2000;
    os_set_interrupt_handler(9, exi_isr_ext);
    os_set_interrupt_handler(10, exi_isr_transfer);
    os_set_interrupt_handler(0xb, exi_isr_unlock);
    os_set_interrupt_handler(0xc, exi_isr_ext);
    os_set_interrupt_handler(0xd, exi_isr_transfer);
    os_set_interrupt_handler(0xe, exi_isr_unlock);
    os_set_interrupt_handler(0xf, exi_isr_ext);
    os_set_interrupt_handler(0x10, exi_isr_transfer);
    pbVar4 = (byte *)&DAT_80613418;
    exi_get_device_id(0, 2, (byte *)&DAT_80613418);
    if (DAT_80613068 == 0) {
        pbVar4 = (byte *)local_18;
        iVar2 = exi_get_device_id(0, 0, pbVar4);
        if ((iVar2 == 0) || (local_18[0] != 0x7010000)) {
            versionFlag = 0;
            pbVar4 = (byte *)local_18;
            iVar2 = exi_get_device_id(1, 0, pbVar4);
            if ((iVar2 != 0) && (local_18[0] == 0x7010000)) {
                exi_recognize_device(0, 2);
                bootCfg = 0;
            }
        }
        else {
            exi_recognize_device(1, 0);
            bootCfg = 0;
            versionFlag = 0;
        }
    }
    else {
        versionFlag = 0x80000000;
        DAT_800030c4 = 0;
        DAT_800030c0 = 0;
        DAT_8060e9a0 = 0;
        DAT_8060e960 = 0;
        exi_probe(0);
        exi_probe(1);
    }
    os_register_version((int)bootCfg, DAT_806114b8, versionFlag, pbVar4);
    return;
}

// ---------------------------------------------------------------------------
// exi_register_ext_callback - register an insert/ext-event callback for a
// device id on a channel. If the channel is not yet listening (flag bit4), it
// starts listening, records the watched device id (+0x18) and refreshes the
// TRK mask; otherwise it enqueues the (id, callback) pair into the per-channel
// callback queue (dedup by id). Returns 1 if listening was just enabled, 0 if
// it was enqueued or the id is already queued.
// ---------------------------------------------------------------------------
undefined4 exi_register_ext_callback(int channel, int deviceId, int callback)
{
    int  iVar1;
    int *piVar2;
    undefined4 uVar3;
    int  iVar4;
    int *piVar5;
    int  iVar6;

    iVar1 = channel * 0x40;
    piVar5 = (int *)(&DAT_8060e940 + iVar1);
    os_disable_interrupts();
    if ((*(uint *)(&DAT_8060e94c + iVar1) & 0x10) == 0) {
        *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) | 0x10;
        *(int *)(&DAT_8060e958 + iVar1) = deviceId;
        trk_hw_set_int_mask(channel, piVar5);
        os_get_interrupts_enabled();
        uVar3 = 1;
    }
    else {
        if (callback != 0) {
            iVar4 = *(int *)(&DAT_8060e964 + iVar1);
            piVar2 = piVar5;
            iVar6 = iVar4;
            if (0 < iVar4) {
                do {
                    if (piVar2[10] == deviceId) {
                        os_get_interrupts_enabled();
                        return 0;
                    }
                    piVar2 = piVar2 + 2;
                    iVar6 = iVar6 + -1;
                } while (iVar6 != 0);
            }
            piVar5[iVar4 * 2 + 0xb] = callback;
            piVar5[*(int *)(&DAT_8060e964 + iVar1) * 2 + 10] = deviceId;
            *(int *)(&DAT_8060e964 + iVar1) = *(int *)(&DAT_8060e964 + iVar1) + 1;
        }
        os_get_interrupts_enabled();
        uVar3 = 0;
    }
    return uVar3;
}

// ---------------------------------------------------------------------------
// exi_remove_ext_callback - stop listening for ext events on a channel: clear
// the listening bit (bit4), refresh the TRK mask, and if the callback queue is
// non-empty pop the head, shift the remainder down, and fire the popped
// callback. Returns 1 if listening was disabled, 0 if it was not listening.
// ---------------------------------------------------------------------------
undefined4 exi_remove_ext_callback(int channel)
{
    int   iVar1;
    int   iVar2;
    undefined4 uVar3;
    code *pcVar4;

    iVar1 = channel * 0x40;
    os_disable_interrupts();
    if ((*(uint *)(&DAT_8060e94c + iVar1) & 0x10) == 0) {
        os_get_interrupts_enabled();
        uVar3 = 0;
    }
    else {
        *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) & 0xffffffef;
        trk_hw_set_int_mask(channel, (int *)(&DAT_8060e940 + iVar1));
        if (0 < *(int *)(&DAT_8060e964 + iVar1)) {
            pcVar4 = *(code **)(&DAT_8060e96c + iVar1);
            iVar2 = *(int *)(&DAT_8060e964 + iVar1) + -1;
            *(int *)(&DAT_8060e964 + iVar1) = iVar2;
            if (0 < iVar2) {
                msl_memmove((uint)(&DAT_8060e968 + iVar1), (uint)(&DAT_8060e970 + iVar1),
                            *(int *)(&DAT_8060e964 + iVar1) << 3);
            }
            (*(void (*)(int, int))pcVar4)(channel, 0);
        }
        os_get_interrupts_enabled();
        uVar3 = 1;
    }
    return uVar3;
}

// ---------------------------------------------------------------------------
// exi_get_state - return the raw per-channel flags word (+0x0C).
// ---------------------------------------------------------------------------
undefined4 exi_get_state(int channel)
{
    return *(undefined4 *)(&DAT_8060e94c + channel * 0x40);
}

// ===========================================================================
// batch_1153: EXI device-identification + command-transaction + boot-device
// recognition + debug-console-over-EXI output.
// ===========================================================================

// ---------------------------------------------------------------------------
// exi_get_device_id_cb - trivial ext-event callback used while identifying a
// device: it just re-reads the id (drops the result) to service the event.
// ---------------------------------------------------------------------------
void exi_get_device_id_cb(int channel)
{
    byte abStack_8[8];

    exi_get_device_id(channel, 0, abStack_8);
    return;
}

// ---------------------------------------------------------------------------
// exi_get_device_id - read a device's 32-bit EXI id word on a channel. For the
// cached boot channel (0, cmd 2) it returns the pre-read boot id. For the
// memory-card channels (0/1, cmd 0) it probes, caches per generation, and
// attaches the channel if needed. Otherwise it runs the id command
// transaction: register ext callback, select, write the 2-byte id command,
// read back 4 bytes, deselect, remove the callback; then for card channels it
// unlocks and caches the id against the current probe generation.
// Returns the id (card channels) or 1/0 success (other channels).
// ---------------------------------------------------------------------------
undefined4 exi_get_device_id(int channel, int cmd, byte *out)
{
    int   iVar1;
    bool  bVar2;
    uint  uVar3;
    uint  uVar4;
    uint  uVar5;
    uint  uVar6;
    uint  uVar7;
    uint  uVar8;
    int   iVar9;
    undefined4 uVar10;
    code *pcVar11;
    int   generation;
    byte  local_2c[8];

    iVar1 = channel * 0x40;
    generation = 0;
    if (((channel == 0) && (cmd == 2)) && (DAT_80613418 != 0)) {
        *(int *)out = DAT_80613418;
        return 1;
    }
    if ((channel < 2) && (cmd == 0)) {
        iVar9 = exi_probe(channel);
        if (iVar9 == 0) {
            return 0;
        }
        if ((&DAT_8060e960)[channel * 0x10] == (&DAT_800030c0)[channel]) {
            *(undefined4 *)out = *(undefined4 *)(&DAT_8060e95c + iVar1);
            return (&DAT_8060e960)[channel * 0x10];
        }
        os_disable_interrupts();
        if (((*(uint *)(&DAT_8060e94c + iVar1) & 8) == 0) && (iVar9 = exi_probe(channel), iVar9 != 0)) {
            exi_select(channel, 1, 0, 0);
            *(undefined4 *)(&DAT_8060e948 + iVar1) = 0;
            os_unmask_interrupts(0x100000 >> channel * 3);
            *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) | 8;
            os_get_interrupts_enabled();
            bVar2 = true;
        }
        else {
            os_get_interrupts_enabled();
            bVar2 = false;
        }
        if (!bVar2) {
            return 0;
        }
        generation = (&DAT_800030c0)[channel];
    }
    os_disable_interrupts();
    bVar2 = false;
    if ((channel < 2) && (cmd == 0)) {
        bVar2 = true;
    }
    if (bVar2) {
        pcVar11 = (code *)exi_get_device_id_cb;
    }
    else {
        pcVar11 = (code *)0x0;
    }
    uVar10 = exi_register_ext_callback(channel, cmd, (int)pcVar11);
    uVar3 = countLeadingZeros(uVar10);
    uVar3 = uVar3 >> 5;
    if (uVar3 == 0) {
        uVar10 = exi_select_device(channel, cmd, 0);
        uVar4 = countLeadingZeros(uVar10);
        uVar3 = uVar4 >> 5;
        if (uVar3 == 0) {
            local_2c[0] = 0;
            local_2c[1] = 0;
            local_2c[2] = 0;
            local_2c[3] = 0;
            uVar10 = exi_imm_transfer_4(channel, local_2c, 2, 1, 0);
            uVar3 = countLeadingZeros(uVar10);
            uVar10 = exi_sync(channel);
            uVar5 = countLeadingZeros(uVar10);
            uVar10 = exi_imm_transfer_4(channel, out, 4, 0, 0);
            uVar6 = countLeadingZeros(uVar10);
            uVar10 = exi_sync(channel);
            uVar7 = countLeadingZeros(uVar10);
            uVar10 = exi_deselect_device(channel);
            uVar8 = countLeadingZeros(uVar10);
            uVar3 = (uVar4 | uVar3 | uVar5 | uVar6 | uVar7 | uVar8) >> 5;
        }
        os_disable_interrupts();
        if ((*(uint *)(&DAT_8060e94c + iVar1) & 0x10) == 0) {
            os_get_interrupts_enabled();
        }
        else {
            *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) & 0xffffffef;
            trk_hw_set_int_mask(channel, (int *)(&DAT_8060e940 + iVar1));
            if (0 < *(int *)(&DAT_8060e964 + iVar1)) {
                pcVar11 = *(code **)(&DAT_8060e96c + iVar1);
                iVar9 = *(int *)(&DAT_8060e964 + iVar1) + -1;
                *(int *)(&DAT_8060e964 + iVar1) = iVar9;
                if (0 < iVar9) {
                    msl_memmove((uint)(&DAT_8060e968 + iVar1), (uint)(&DAT_8060e970 + iVar1),
                                *(int *)(&DAT_8060e964 + iVar1) << 3);
                }
                (*(void (*)(int, int))pcVar11)(channel, 0);
            }
            os_get_interrupts_enabled();
        }
    }
    os_get_interrupts_enabled();
    if ((channel < 2) && (cmd == 0)) {
        os_disable_interrupts();
        if ((*(uint *)(&DAT_8060e94c + iVar1) & 8) == 0) {
            os_get_interrupts_enabled();
        }
        else if (((*(uint *)(&DAT_8060e94c + iVar1) & 0x10) == 0) ||
                 (*(int *)(&DAT_8060e958 + iVar1) != 0)) {
            *(uint *)(&DAT_8060e94c + iVar1) = *(uint *)(&DAT_8060e94c + iVar1) & 0xfffffff7;
            os_mask_interrupts(0x500000 >> channel * 3);
            os_get_interrupts_enabled();
        }
        else {
            os_get_interrupts_enabled();
        }
        os_disable_interrupts();
        iVar9 = (&DAT_800030c0)[channel];
        if (uVar3 == 0 && iVar9 == generation) {
            *(undefined4 *)(&DAT_8060e95c + iVar1) = *(undefined4 *)out;
            (&DAT_8060e960)[channel * 0x10] = generation;
        }
        os_get_interrupts_enabled();
        if (uVar3 == 0 && iVar9 == generation) {
            uVar10 = (&DAT_8060e960)[channel * 0x10];
        }
        else {
            uVar10 = 0;
        }
    }
    else if (uVar3 == 0) {
        uVar10 = 1;
    }
    else {
        uVar10 = 0;
    }
    return uVar10;
}

// ---------------------------------------------------------------------------
// exi_read_device_id_locked - variant of the id command transaction that first
// locks the channel via exi_attach, issues a 0x00130120 id command, reads back
// 4 bytes, then unlocks. Returns 1 on a valid id (not 0xffffffff), 0 otherwise.
// ---------------------------------------------------------------------------
undefined4 exi_read_device_id_locked(int channel, int cmd, byte *out)
{
    uint uVar1;
    uint uVar2;
    uint uVar3;
    uint uVar4;
    uint uVar5;
    int  iVar6;
    undefined4 uVar7;
    byte local_14[4];

    if (((channel != 2) && (cmd == 0)) && (iVar6 = exi_attach(channel, 0), iVar6 == 0)) {
        return 0;
    }
    uVar7 = exi_register_ext_callback(channel, cmd, 0);
    uVar1 = countLeadingZeros(uVar7);
    uVar1 = uVar1 >> 5;
    if (uVar1 == 0) {
        uVar7 = exi_select_device(channel, cmd, 0);
        uVar1 = countLeadingZeros(uVar7);
        uVar1 = uVar1 >> 5;
        if (uVar1 == 0) {
            local_14[0] = 0x20;
            local_14[1] = 1;
            local_14[2] = 0x13;
            local_14[3] = 0;
            uVar7 = exi_imm_transfer_4(channel, local_14, 4, 1, 0);
            uVar1 = countLeadingZeros(uVar7);
            uVar7 = exi_sync(channel);
            uVar2 = countLeadingZeros(uVar7);
            uVar7 = exi_imm_transfer_4(channel, out, 4, 0, 0);
            uVar3 = countLeadingZeros(uVar7);
            uVar7 = exi_sync(channel);
            uVar4 = countLeadingZeros(uVar7);
            uVar7 = exi_deselect_device(channel);
            uVar5 = countLeadingZeros(uVar7);
            uVar1 = (uVar1 | uVar2 | uVar3 | uVar4 | uVar5) >> 5;
        }
        exi_remove_ext_callback(channel);
    }
    if ((channel != 2) && (cmd == 0)) {
        exi_unlock(channel);
    }
    if (uVar1 == 0) {
        if (*(int *)out == -1) {
            uVar7 = 0;
        }
        else {
            uVar7 = 1;
        }
    }
    else {
        uVar7 = 0;
    }
    return uVar7;
}

// ---------------------------------------------------------------------------
// exi_recognize_device - identify the device on a channel and, if its id is not
// one of the known non-console EXI device ids (a large switch of known
// memory-card / peripheral ids), fall through to exi_read_device_id_locked and,
// on success, latch the debug-console-over-EXI configuration (channel, device,
// and the two 0xa5ff005a present/recognized magics).
// ---------------------------------------------------------------------------
void exi_recognize_device(int channel, int cmd)
{
    int iVar2;
    int id;
    int local_10[2];

    iVar2 = exi_get_device_id(channel, cmd, (byte *)local_10);
    if (iVar2 == 0) {
        return;
    }
    id = local_10[0];
    // Known device ids: nothing to bring up as a debug console.
    if (id == 0x1020000 || id == 4 || id == (int)0x80000010 || id == (int)0x80000008 ||
        id == (int)0x80000004 || id == -1 || id == (int)0x80000020 || id == 0x20 || id == 0x10 ||
        id == 8 || id == 0x1010000 || id == 0x4040404 || id == 0x4020100 || id == 0x3010000 ||
        id == 0x2020000 || id == 0x4020300 || id == 0x4020200 || id == 0x4130000 ||
        id == 0x4120000 || id == 0x4060000 || id == 0x4220000) {
        return;
    }
    iVar2 = exi_read_device_id_locked(channel, cmd, (byte *)local_10);
    if (iVar2 != 0) {
        DAT_8061342c = 0xa5ff005a;
        DAT_80613428 = 0xa5ff005a;
        DAT_80613420 = channel;
        DAT_80613424 = cmd;
    }
    return;
}

// ---------------------------------------------------------------------------
// exi_console_enable - after recognition, decide whether the debug console is
// usable: if the console-recognized magic is not set, and this is a devkit
// (console-type bit 0x10000000), latch present and select channel 0 device 1;
// returns 0 usable, 2 unavailable.
// ---------------------------------------------------------------------------
undefined4 exi_console_enable(void)
{
    undefined4 uVar1;
    uint uVar2;

    if (DAT_8061342c == 0xa5ff005a) {
        uVar1 = 0;
    }
    else {
        uVar2 = os_get_console_type();
        if ((uVar2 & 0x10000000) == 0) {
            DAT_80613428 = 0;
            uVar1 = 2;
        }
        else {
            DAT_80613428 = 0xa5ff005a;
            DAT_80613420 = 0;
            uVar1 = 0;
            DAT_80613424 = 1;
        }
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// exi_console_write - write a text buffer to the recognized debug console over
// EXI. Converts LF->CR in place, then, in a loop, polls the console's free
// space (0x20010000 status command, 0x10 - byte3), and when there is room
// (or the whole buffer fits) issues the 0xa0010000 write command followed by
// the data in <=4-byte immediate chunks. Returns 0 on success, 2 if the
// console is not present, 3 on a transfer error.
// ---------------------------------------------------------------------------
undefined4 exi_console_write(byte *buf, uint len)
{
    uint  uVar1;
    undefined4 uVar2;
    int   iVar3;
    byte *pbVar4;
    uint  uVar5;
    uint  local_20;
    byte  local_1c[4];

    if (DAT_80613428 == 0xa5ff005a) {
        iVar3 = exi_register_ext_callback(DAT_80613420, DAT_80613424, 0);
        pbVar4 = buf;
        if (iVar3 == 0) {
            uVar2 = 0;
        }
        else {
            for (; (uint)((int)pbVar4 - (int)buf) < len; pbVar4 = pbVar4 + 1) {
                if (*pbVar4 == 10) {
                    *pbVar4 = 0xd;
                }
            }
            local_1c[0] = 0xa0;
            local_1c[1] = 1;
            local_1c[2] = 0;
            local_1c[3] = 0;
            uVar2 = 0;
            while (len != 0) {
                iVar3 = exi_select_device(DAT_80613420, DAT_80613424, 3);
                if (iVar3 == 0) {
                    uVar1 = 0xffffffff;
                }
                else {
                    local_20 = 0x20010000;
                    exi_imm_transfer_4(DAT_80613420, (byte *)&local_20, 4, 1, 0);
                    exi_sync(DAT_80613420);
                    exi_imm_transfer_4(DAT_80613420, (byte *)&local_20, 1, 0, 0);
                    exi_sync(DAT_80613420);
                    exi_deselect_device(DAT_80613420);
                    uVar1 = 0x10 - (local_20 >> 0x18);
                }
                if ((int)uVar1 < 0) {
                    uVar2 = 3;
                    break;
                }
                if ((0xb < (int)uVar1) || (len <= uVar1)) {
                    iVar3 = exi_select_device(DAT_80613420, DAT_80613424, 3);
                    if (iVar3 == 0) {
                        uVar2 = 3;
                        break;
                    }
                    exi_imm_transfer_4(DAT_80613420, local_1c, 4, 1, 0);
                    exi_sync(DAT_80613420);
                    for (; ((uVar1 != 0 && (len != 0)) && ((3 < (int)uVar1 || (len <= uVar1))));
                        len = len - uVar5) {
                        uVar5 = len;
                        if (3 < len) {
                            uVar5 = 4;
                        }
                        exi_imm_transfer_4(DAT_80613420, buf, uVar5, 1, 0);
                        buf = buf + uVar5;
                        uVar1 = uVar1 - uVar5;
                        exi_sync(DAT_80613420);
                    }
                    exi_deselect_device(DAT_80613420);
                }
            }
            exi_remove_ext_callback(DAT_80613420);
        }
    }
    else {
        uVar2 = 2;
    }
    return uVar2;
}
