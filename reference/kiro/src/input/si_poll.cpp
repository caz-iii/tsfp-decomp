// SI (Serial Interface) controller-port poll engine + interrupt handler.
//
// The GameCube SI block drives the four controller ports (and other SI
// devices) through a shared command/response buffer. Registers live at
// 0xCC006400: +0x00..+0x7C the per-device command words, +0x34 the comm-control
// / status register (TCINT/TSTART/RDSTINT bits), +0x38 the poll status word,
// +0x80..0xFF the response buffer. Per-device software state lives at
// 0x8060EA00 (stride 0x20: id, response ptr, length, callbacks, deadlines).
//
// This file is the tail of the SI driver: the transfer-completion reader that
// copies the response buffer back to the requesting device, the poll/retrace
// interrupt handler that fires per-device completion + poll callbacks, and two
// small "is-device-busy" predicates.
//
// NON-MATCHING: the flat DAT_cc0064xx / DAT_8060eaxx offset arithmetic, the
// big-endian response unpack unrolled by 8, and the raw SI status-bit masks are
// kept verbatim from the decompiler output.
//
// Batch: batch_1153
// Source module: input/si_poll.cpp
#include "common.h"

// Ghidra function-pointer placeholder type used by the callback slots.
typedef void code;

// ---------------------------------------------------------------------------
// Per-file externs (this TU includes only common.h).
// ---------------------------------------------------------------------------
extern long long os_get_time(void);
extern void      os_cancel_alarm(int *alarm);
extern unsigned int vi_get_current_line(void);
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void os_unmask_interrupts(unsigned int mask);
extern void os_set_interrupt_handler(int idx, void (*handler)(undefined4, undefined4));
// Kernel version-registration entry; called with a large forwarded arg list.
extern undefined8 os_register_version(int fmt, ...);

// OS alarm + SRAM origin-field + VI format helpers (batch_1155).
extern void os_set_alarm(undefined4 *alarm, int fn, uint deadlineHi, uint deadlineLo,
                         void (*handler)(int));
extern ushort os_sram_get_ex_field(uint device);
extern void   os_sram_set_ex_field(uint device, ushort value);
extern int    vi_get_tv_format(void);
extern void   OSReport(undefined8 a, undefined8 b, undefined8 c, undefined8 d, undefined8 e,
                       undefined8 f, undefined8 g, undefined8 h, void *fmt, undefined4 j,
                       undefined4 k, undefined4 l, undefined4 m, undefined4 n, undefined4 o,
                       undefined4 p);

// OSReport format string used by si_set_sampling_rate on an unknown TV format.
extern char s_SISetSamplingRate__unknown_TV_fo_8049f728[];

// Forward declarations for functions defined later in this file but referenced
// earlier (the transfer kick and the SI poll driver layer added in batch_1155).
undefined4 si_transfer(uint device, undefined4 *cmd, int cmdLen, undefined4 respPtr,
                       int respLen, int completeCb);
undefined4 si_queue_poll_transfer(uint device, undefined4 *cmd, int cmdLen, undefined4 respPtr,
                                  int respLen, int completeCb, int minIntervalHi,
                                  uint minIntervalLo);
bool si_poll_response_ready(int device);
int  si_refresh_poll_status(uint device);
void si_poll_alarm_handler(int alarmArg);
uint si_get_status(int device);          // defined in the batch_1154 section below
void si_poll_status_callback(uint device, uint status);
void si_set_sampling_rate(undefined8 param_1, undefined8 param_2, undefined8 param_3,
                          undefined8 param_4, undefined8 param_5, undefined8 param_6,
                          undefined8 param_7, undefined8 param_8, uint rateIndex,
                          undefined4 param_10, undefined4 param_11, undefined4 param_12,
                          undefined4 param_13, undefined4 param_14, undefined4 param_15,
                          undefined4 param_16);

// SI hardware register block (0xCC006400).
extern uint DAT_cc006400;   // command/response buffer base
extern uint DAT_cc006434;   // comm-control / status register
extern uint DAT_cc006438;   // poll status register
extern uint DAT_cc006480;   // response buffer tail word

// SI software state.
extern int       DAT_8049f5f4;   // active device index (-1 = idle)
extern uint      DAT_8049f5f8;   // poll enable/config bits
extern uint      DAT_8049f5fc;   // active transfer response length (bytes)
extern undefined4 *DAT_8049f600;  // active response destination ptr
extern code     *DAT_8049f604;   // active transfer completion callback
extern uint      DAT_8049f608;   // per-device retry/error state (stride 4)
extern uint      DAT_800000f8;   // timer tick base (bus clock / 4)

// Per-device state table (0x8060EA00, stride 0x20).
extern uint DAT_8060ea00;   // +0x00 device id (0xffffffff = empty)
extern uint DAT_8060ea04;   // +0x04 response ptr
extern uint DAT_8060ea08;   // +0x08
extern uint DAT_8060ea0c;   // +0x0C
extern uint DAT_8060ea10;   // +0x10
extern uint DAT_8060ea14;   // +0x14
extern uint DAT_8060ea18;   // +0x18 deadline hi
extern uint DAT_8060ea1c;   // +0x1C deadline lo
extern int  DAT_8060ea80;   // alarm objects (stride 0x28)

// Per-device transfer timestamps.
extern uint DAT_8060eb20;   // response-complete time (stride 8)
extern uint DAT_8060eb40;   // transfer-start time (stride 8)

// Poll/retrace callback + deadline tables.
extern uint DAT_8060eba0;   // retrace callback slot 0 (of 4)
extern uint DAT_8060eba4;   // retrace callback slot 1
extern uint DAT_8060eba8;   // retrace callback slot 2
extern uint DAT_8060ebac;   // retrace callback slot 3
extern uint DAT_8060ebe0;   // per-device retrace deadline lines (4)
extern uint DAT_8060ebe4;
extern uint DAT_8060ebe8;
extern uint DAT_8060ebec;

// Additional SI hardware registers used by the transfer/poll setup.
extern uint DAT_cc006430;   // poll-rate / poll-enable register

// Per-device state-table id words for the other three devices (stride 0x20).
extern uint DAT_8060ea20;
extern uint DAT_8060ea40;
extern uint DAT_8060ea60;

// Kernel version blob passed to os_register_version by si_init.
extern uint DAT_806114c0;

// Console/debug scratch used by the poll re-arm path.
extern uint DAT_80613430;

// ---------------------------------------------------------------------------
// si_is_busy - true if any SI device is currently mid-transfer (active index
// set), used as a global "SI busy" gate.
// ---------------------------------------------------------------------------
undefined4 si_is_busy(void)
{
    if (DAT_8049f5f4 != -1) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// si_device_busy - true if device is either the currently-active transfer or
// has a live (non-empty) state-table entry.
// ---------------------------------------------------------------------------
undefined4 si_device_busy(int device)
{
    undefined4 uVar1;

    uVar1 = 1;
    if (((&DAT_8060ea00)[device * 8] == -1) && (DAT_8049f5f4 != device)) {
        uVar1 = 0;
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// si_read_response - transfer-completion reader. Starts a read-out of the SI
// response buffer (0xCC006480), timestamps the transfer start, copies the
// active response length (word-aligned bulk unrolled by 8, then the tail bytes
// big-endian) into the active destination buffer, and interprets the SI status
// word to produce the completion code (0 timeout / low-nibble error / 4 ok).
// Clears the active device index. Returns the completion/status code.
// ---------------------------------------------------------------------------
uint si_read_response(void)
{
    undefined4 *puVar1;
    int   iVar2;
    uint  uVar3;
    undefined4 *puVar4;
    uint  uVar5;
    uint  uVar6;
    undefined *puVar7;
    uint  uVar8;
    uint  uVar9;
    longlong lVar10;

    puVar7 = (undefined *)&DAT_cc006400;
    uVar5 = DAT_cc006434;
    uVar6 = DAT_cc006438;
    DAT_cc006434 = uVar5 & 0xfffffffe | 0x80000000;
    if (DAT_8049f5f4 != -1) {
        lVar10 = os_get_time();
        uVar3 = 0;
        *(longlong *)(&DAT_8060eb40 + DAT_8049f5f4 * 8) = lVar10;
        uVar5 = DAT_8049f5fc >> 2;
        puVar4 = DAT_8049f600;
        if (uVar5 != 0) {
            if ((8 < uVar5) && (uVar8 = uVar5 - 1 >> 3, uVar5 != 8)) {
                do {
                    uVar3 = uVar3 + 8;
                    *puVar4 = *(undefined4 *)(puVar7 + 0x80);
                    puVar4[1] = *(undefined4 *)(puVar7 + 0x84);
                    puVar4[2] = *(undefined4 *)(puVar7 + 0x88);
                    puVar4[3] = *(undefined4 *)(puVar7 + 0x8c);
                    puVar4[4] = *(undefined4 *)(puVar7 + 0x90);
                    puVar4[5] = *(undefined4 *)(puVar7 + 0x94);
                    puVar4[6] = *(undefined4 *)(puVar7 + 0x98);
                    puVar1 = (undefined4 *)(puVar7 + 0x9c);
                    puVar7 = puVar7 + 0x20;
                    puVar4[7] = *puVar1;
                    puVar4 = puVar4 + 8;
                    uVar8 = uVar8 - 1;
                } while (uVar8 != 0);
            }
            puVar7 = (undefined *)(&DAT_cc006400 + uVar3 * 4);
            iVar2 = uVar5 - uVar3;
            if (uVar3 < uVar5) {
                do {
                    puVar1 = (undefined4 *)(puVar7 + 0x80);
                    puVar7 = puVar7 + 4;
                    uVar3 = uVar3 + 1;
                    *puVar4 = *puVar1;
                    puVar4 = puVar4 + 1;
                    iVar2 = iVar2 + -1;
                } while (iVar2 != 0);
            }
        }
        uVar5 = DAT_8049f5fc & 3;
        if (uVar5 != 0) {
            uVar8 = (&DAT_cc006480)[uVar3];
            uVar3 = 0;
            if (uVar5 != 0) {
                if ((8 < uVar5) && (uVar9 = uVar5 - 1 >> 3, uVar5 != 8)) {
                    do {
                        *(char *)puVar4 = (char)(uVar8 >> (3 - uVar3) * 8);
                        *(char *)((int)puVar4 + 1) = (char)(uVar8 >> (3 - (uVar3 + 1)) * 8);
                        *(char *)((int)puVar4 + 2) = (char)(uVar8 >> (3 - (uVar3 + 2)) * 8);
                        *(char *)((int)puVar4 + 3) = (char)(uVar8 >> uVar3 * -8);
                        *(char *)(puVar4 + 1) = (char)(uVar8 >> (3 - (uVar3 + 4)) * 8);
                        *(char *)((int)puVar4 + 5) = (char)(uVar8 >> (3 - (uVar3 + 5)) * 8);
                        *(char *)((int)puVar4 + 6) = (char)(uVar8 >> (3 - (uVar3 + 6)) * 8);
                        *(char *)((int)puVar4 + 7) = (char)(uVar8 >> (3 - (uVar3 + 7)) * 8);
                        puVar4 = puVar4 + 2;
                        uVar3 = uVar3 + 8;
                        uVar9 = uVar9 - 1;
                    } while (uVar9 != 0);
                }
                iVar2 = uVar5 - uVar3;
                if (uVar3 < uVar5) {
                    do {
                        *(char *)puVar4 = (char)(uVar8 >> (3 - uVar3) * 8);
                        puVar4 = (undefined4 *)((int)puVar4 + 1);
                        uVar3 = uVar3 + 1;
                        iVar2 = iVar2 + -1;
                    } while (iVar2 != 0);
                }
            }
        }
        uVar5 = DAT_cc006434;
        if ((uVar5 & 0x20000000) == 0) {
            lVar10 = os_get_time();
            uVar6 = 0;
            *(longlong *)(&DAT_8060eb20 + DAT_8049f5f4 * 8) = lVar10;
        }
        else {
            uVar5 = uVar6 >> (3 - DAT_8049f5f4) * 8;
            uVar6 = uVar5 & 0xf;
            if (((uVar5 & 8) != 0) && ((*(uint *)(&DAT_8049f608 + DAT_8049f5f4 * 4) & 0x80) == 0)) {
                *(uint *)(&DAT_8049f608 + DAT_8049f5f4 * 4) = 8;
            }
            if (uVar6 == 0) {
                uVar6 = 4;
            }
        }
        DAT_8049f5f4 = -1;
    }
    return uVar6;
}

// ---------------------------------------------------------------------------
// si_interrupt_handler - SI interrupt-service routine. Handles two causes off
// the comm-control register:
//   * transfer-complete (bits 0xC0000000): read the response via
//     si_read_response, walk the four device slots looking for one whose
//     deadline has expired and re-deliver its response (FUN_803bb4d8), cancel
//     its alarm on success, fire the transfer callback, ack the poll bit, and
//     re-arm the poll alarm for a device still flagged 0x80.
//   * poll / VI-retrace (bits 0x18000000): timestamp each active poll slot
//     against the current VI line, and once every enabled slot has reached its
//     retrace deadline, fire the four registered retrace callbacks and reset
//     the deadlines.
// ---------------------------------------------------------------------------
void si_interrupt_handler(undefined4 param_1, undefined4 param_2)
{
    uint  uVar1;
    code *pcVar2;
    uint  uVar3;
    uint  uVar4;
    bool  bVar5;
    undefined4 *puVar6;
    uint *puVar7;
    uint  uVar8;
    int   iVar9;
    longlong lVar10;

    uVar4 = DAT_8049f5f4;
    uVar1 = DAT_cc006434;
    if ((uVar1 & 0xc0000000) == 0xc0000000) {
        uVar3 = si_read_response();
        pcVar2 = DAT_8049f604;
        iVar9 = 0;
        DAT_8049f604 = (code *)0x0;
        uVar8 = uVar4;
        do {
            uVar8 = uVar8 + 1;
            uVar8 = uVar8 + (((int)uVar8 >> 2) + (uint)((int)uVar8 < 0 && (uVar8 & 3) != 0)) * -4;
            puVar7 = &DAT_8060ea00 + uVar8 * 8;
            if ((*puVar7 != 0xffffffff) &&
               (lVar10 = os_get_time(),
               (uint)((uint)lVar10 < (uint)(&DAT_8060ea1c)[uVar8 * 8]) +
               ((&DAT_8060ea18)[uVar8 * 8] ^ 0x80000000) <=
               ((uint)((ulonglong)lVar10 >> 0x20) ^ 0x80000000))) {
                iVar9 = si_transfer(*puVar7, (undefined4 *)(&DAT_8060ea04)[uVar8 * 8],
                                    (&DAT_8060ea08)[uVar8 * 8], (&DAT_8060ea0c)[uVar8 * 8],
                                    (&DAT_8060ea10)[uVar8 * 8], (&DAT_8060ea14)[uVar8 * 8]);
                if (iVar9 != 0) {
                    os_cancel_alarm((int *)(&DAT_8060ea80 + uVar8 * 0x28));
                    *puVar7 = 0xffffffff;
                }
                break;
            }
            iVar9 = iVar9 + 1;
        } while (iVar9 < 4);
        if (pcVar2 != (code *)0x0) {
            (*(void (*)(uint, uint, undefined4))pcVar2)(uVar4, uVar3, param_2);
        }
        uVar8 = DAT_cc006438;
        DAT_cc006438 = uVar8 & 0xf000000 >> ((uVar4 & 7) << 3);
        if (*(int *)(&DAT_8049f608 + uVar4 * 4) == 0x80) {
            bVar5 = true;
            if (((&DAT_8060ea00)[uVar4 * 8] == -1) && (DAT_8049f5f4 != uVar4)) {
                bVar5 = false;
            }
            if (!bVar5) {
                si_queue_poll_transfer(uVar4, (undefined4 *)&DAT_80613430, 1,
                                       (undefined4)(uintptr_t)(&DAT_8049f608 + uVar4 * 4), 3,
                                       (int)(uintptr_t)si_poll_status_callback, 0,
                                       (DAT_800000f8 / 500000) * 0x41 >> 3);
            }
        }
    }
    if ((uVar1 & 0x18000000) == 0x18000000) {
        uVar4 = vi_get_current_line();
        puVar7 = (uint *)&DAT_8060ebe0;
        uVar4 = uVar4 + 1;
        uVar1 = DAT_8049f5f8 >> 0x10;
        iVar9 = 0;
        do {
            bVar5 = si_poll_response_ready(iVar9);
            if (bVar5) {
                *puVar7 = uVar4;
            }
            iVar9 = iVar9 + 1;
            puVar7 = puVar7 + 1;
        } while (iVar9 < 4);
        uVar1 = (uVar1 & 0x3ff) >> 1;
        if ((((((DAT_8049f5f8 & 0x80) == 0) || ((DAT_8060ebe0 != 0 && (uVar4 <= uVar1 + DAT_8060ebe0))))
             && (((DAT_8049f5f8 & 0x40) == 0 || ((DAT_8060ebe4 != 0 && (uVar4 <= uVar1 + DAT_8060ebe4))))))
            && (((DAT_8049f5f8 & 0x20) == 0 || ((DAT_8060ebe8 != 0 && (uVar4 <= uVar1 + DAT_8060ebe8))))))
           && (((DAT_8049f5f8 & 0x10) == 0 || ((DAT_8060ebec != 0 && (uVar4 <= uVar1 + DAT_8060ebec)))))) {
            iVar9 = 0;
            DAT_8060ebe0 = 0;
            puVar6 = (undefined4 *)&DAT_8060eba0;
            DAT_8060ebe4 = 0;
            DAT_8060ebe8 = 0;
            DAT_8060ebec = 0;
            do {
                if ((code *)*puVar6 != (code *)0x0) {
                    (*(void (*)(undefined4, undefined4))*puVar6)(param_1, param_2);
                }
                iVar9 = iVar9 + 1;
                puVar6 = puVar6 + 1;
            } while (iVar9 < 4);
        }
    }
    return;
}

// ===========================================================================
// batch_1154: SI poll-interrupt config, retrace-callback registry, transfer
// kick, register accessors, and subsystem init.
// ===========================================================================

// ---------------------------------------------------------------------------
// si_enable_poll_interrupt - enable (nonzero) or disable (0) the SI poll /
// read-status interrupt (RDSTINT bit 0x08000000 in the comm-control register).
// When enabling, resets the four per-device retrace deadline lines. Returns the
// previous enable state. The write masks off TCINT/TSTART so it does not kick a
// transfer.
// ---------------------------------------------------------------------------
bool si_enable_poll_interrupt(int enable)
{
    uint uVar1;
    uint uVar2;

    os_disable_interrupts();
    uVar1 = DAT_cc006434;
    if (enable == 0) {
        uVar2 = uVar1 & 0xf7ffffff;
    }
    else {
        DAT_8060ebe0 = 0;
        uVar2 = uVar1 | 0x8000000;
        DAT_8060ebe4 = 0;
        DAT_8060ebe8 = 0;
        DAT_8060ebec = 0;
    }
    DAT_cc006434 = uVar2 & 0x7ffffffe;
    os_get_interrupts_enabled();
    return (uVar1 & 0x8000000) != 0;
}

// ---------------------------------------------------------------------------
// si_register_retrace_callback - add a VI-retrace poll callback to the 4-slot
// table. Returns 1 if already present or newly inserted (enabling the poll
// interrupt on first insert), 0 if the table is full.
// ---------------------------------------------------------------------------
undefined4 si_register_retrace_callback(int callback)
{
    int *piVar1;
    int *piVar2;
    int  iVar3;
    int  iVar4;

    os_disable_interrupts();
    piVar2 = (int *)&DAT_8060eba0;
    iVar3 = 4;
    piVar1 = piVar2;
    do {
        if (*piVar1 == callback) {
            os_get_interrupts_enabled();
            return 1;
        }
        piVar1 = piVar1 + 1;
        iVar3 = iVar3 + -1;
    } while (iVar3 != 0);
    iVar4 = 4;
    iVar3 = 0;
    do {
        if (*piVar2 == 0) {
            (&DAT_8060eba0)[iVar3] = callback;
            si_enable_poll_interrupt(1);
            os_get_interrupts_enabled();
            return 1;
        }
        piVar2 = piVar2 + 1;
        iVar3 = iVar3 + 1;
        iVar4 = iVar4 + -1;
    } while (iVar4 != 0);
    os_get_interrupts_enabled();
    return 0;
}

// ---------------------------------------------------------------------------
// si_unregister_retrace_callback - remove a VI-retrace poll callback from the
// 4-slot table; if the table becomes empty, disable the poll interrupt.
// Returns 1 if found+removed, 0 if not present.
// ---------------------------------------------------------------------------
undefined4 si_unregister_retrace_callback(int callback)
{
    int  iVar1;
    int *piVar2;
    int  iVar3;

    os_disable_interrupts();
    piVar2 = (int *)&DAT_8060eba0;
    iVar3 = 4;
    iVar1 = 0;
    do {
        if (*piVar2 == callback) {
            (&DAT_8060eba0)[iVar1] = 0;
            iVar1 = 0;
            if ((((DAT_8060eba0 == 0) && (iVar1 = 1, DAT_8060eba4 == 0)) &&
                 (iVar1 = 2, DAT_8060eba8 == 0)) && (iVar1 = 3, DAT_8060ebac == 0)) {
                iVar1 = 4;
            }
            if (iVar1 == 4) {
                si_enable_poll_interrupt(0);
            }
            os_get_interrupts_enabled();
            return 1;
        }
        piVar2 = piVar2 + 1;
        iVar1 = iVar1 + 1;
        iVar3 = iVar3 + -1;
    } while (iVar3 != 0);
    os_get_interrupts_enabled();
    return 0;
}

// ---------------------------------------------------------------------------
// si_transfer - kick an SI command/response transfer for a device. Only starts
// if the SI bus is idle (active index 0xffffffff). Copies cmdLen bytes of the
// command into the SI command buffer (word bulk unrolled by 8, then tail
// words), records the active device / response destination / length / callback,
// then builds the comm-control word (input/output byte lengths, callback-enable
// bit, device select, TSTART) and writes it to launch the transfer. Returns 1
// if started, 0 if the bus was busy.
// ---------------------------------------------------------------------------
undefined4 si_transfer(uint device, undefined4 *cmd, int cmdLen, undefined4 respPtr,
                       int respLen, int completeCb)
{
    undefined4 *puVar1;
    uint  uVar2;
    int   iVar4;
    undefined4 uVar5;
    uint  uVar6;
    undefined *puVar7;
    undefined4 *puVar8;
    uint  uVar9;
    uint  local_24;

    os_disable_interrupts();
    if (DAT_8049f5f4 == 0xffffffff) {
        uVar2 = DAT_cc006438;
        DAT_cc006438 = uVar2 & 0xf000000 >> ((device & 7) << 3);
        uVar2 = cmdLen + 3U >> 2;
        uVar6 = 0;
        DAT_8049f5f4 = device;
        DAT_8049f5fc = respLen;
        DAT_8049f600 = (undefined4 *)respPtr;
        DAT_8049f604 = (code *)completeCb;
        if (uVar2 != 0) {
            if (8 < uVar2) {
                uVar9 = uVar2 - 1 >> 3;
                puVar7 = (undefined *)&DAT_cc006400;
                puVar8 = cmd;
                if (uVar2 != 8) {
                    do {
                        uVar6 = uVar6 + 8;
                        *(undefined4 *)(puVar7 + 0x80) = *puVar8;
                        *(undefined4 *)(puVar7 + 0x84) = puVar8[1];
                        *(undefined4 *)(puVar7 + 0x88) = puVar8[2];
                        *(undefined4 *)(puVar7 + 0x8c) = puVar8[3];
                        *(undefined4 *)(puVar7 + 0x90) = puVar8[4];
                        *(undefined4 *)(puVar7 + 0x94) = puVar8[5];
                        *(undefined4 *)(puVar7 + 0x98) = puVar8[6];
                        puVar1 = puVar8 + 7;
                        puVar8 = puVar8 + 8;
                        *(undefined4 *)(puVar7 + 0x9c) = *puVar1;
                        puVar7 = puVar7 + 0x20;
                        uVar9 = uVar9 - 1;
                    } while (uVar9 != 0);
                }
            }
            puVar8 = cmd + uVar6;
            puVar7 = (undefined *)(&DAT_cc006400 + uVar6 * 4);
            iVar4 = uVar2 - uVar6;
            if (uVar6 < uVar2) {
                do {
                    uVar5 = *puVar8;
                    puVar8 = puVar8 + 1;
                    *(undefined4 *)(puVar7 + 0x80) = uVar5;
                    puVar7 = puVar7 + 4;
                    iVar4 = iVar4 + -1;
                } while (iVar4 != 0);
            }
        }
        // Assemble the comm-control word from the current register's preserved
        // interrupt-status bits (top bit cleared) plus the transfer descriptor.
        // Ghidra emitted this via CONCAT11/22/31 sub-byte packing; expressed
        // here as explicit big-endian byte fields.
        uVar2 = DAT_cc006434 & 0x7fffffff;
        if (cmdLen == 0x80) {
            cmdLen = 0;
        }
        if (respLen == 0x80) {
            respLen = 0;
        }
        {
            byte b3 = (byte)((completeCb != 0) << 6) | (byte)(uVar2 >> 0x18) & 0xbf | 0x80;
            byte b2 = (byte)cmdLen & 0x7f | (byte)(uVar2 >> 0x10) & 0x80;
            byte b1 = (byte)respLen & 0x7f | (byte)(uVar2 >> 8) & 0x80;
            byte b0 = (byte)(device << 1) & 6 | (byte)uVar2 & 0xf9;
            local_24 = ((uint)b3 << 0x18) | ((uint)b2 << 0x10) | ((uint)b1 << 8) | (uint)b0;
            local_24 = local_24 & 0xfffffffe | 1;
        }
        DAT_cc006434 = local_24;
        os_get_interrupts_enabled();
        uVar5 = 1;
    }
    else {
        os_get_interrupts_enabled();
        uVar5 = 0;
    }
    return uVar5;
}

// ---------------------------------------------------------------------------
// si_get_status - read the poll status nibble for a device (bits shifted out of
// the poll status register), and latch an error state (0x80) for the device if
// the "no-response" bit (0x08) is set and no error is already recorded. Returns
// the raw shifted status word.
// ---------------------------------------------------------------------------
uint si_get_status(int device)
{
    uint uVar1;

    os_disable_interrupts();
    uVar1 = DAT_cc006438;
    uVar1 = uVar1 >> (3 - device) * 8;
    if (((uVar1 & 8) != 0) && ((*(uint *)(&DAT_8049f608 + device * 4) & 0x80) == 0)) {
        *(uint *)(&DAT_8049f608 + device * 4) = 8;
    }
    os_get_interrupts_enabled();
    return uVar1;
}

// ---------------------------------------------------------------------------
// si_set_command - write a device's command word into the SI command register
// bank (0xCC006400 + device*0x0C).
// ---------------------------------------------------------------------------
void si_set_command(int device, undefined4 cmd)
{
    *(undefined4 *)(&DAT_cc006400 + device * 0xc) = cmd;
    return;
}

// ---------------------------------------------------------------------------
// si_clear_status - clear all latched poll status bits (write the RDSTINT-ack
// magic 0x80000000 to the poll status register).
// ---------------------------------------------------------------------------
void si_clear_status(void)
{
    DAT_cc006438 = 0x80000000;
    return;
}

// ---------------------------------------------------------------------------
// si_set_poll_params - program the poll-rate parameters (poll line count and
// per-frame count) into the poll register, preserving the enable bits. Returns
// the new register value.
// ---------------------------------------------------------------------------
uint si_set_poll_params(int lineCount, int perFrame)
{
    uint uVar1;

    os_disable_interrupts();
    uVar1 = DAT_8049f5f8 & 0xfc0000ff | lineCount << 0x10 | perFrame << 8;
    DAT_cc006430 = uVar1;
    DAT_8049f5f8 = uVar1;
    os_get_interrupts_enabled();
    return uVar1;
}

// ---------------------------------------------------------------------------
// si_enable_polling - update the per-device poll-enable bits. The high nibble
// of the argument selects which devices to change and the next byte carries the
// new enable bits; clears the poll status and rewrites the poll register.
// Returns the resulting poll-config value.
// ---------------------------------------------------------------------------
uint si_enable_polling(uint devicesAndBits)
{
    uint uVar1;

    uVar1 = DAT_8049f5f8;
    if (devicesAndBits != 0) {
        os_disable_interrupts();
        uVar1 = DAT_8049f5f8 & ~(devicesAndBits >> 0x1c) |
                devicesAndBits >> 0x18 & (devicesAndBits >> 0x1c | 0x3fffff0);
        DAT_cc006438 = 0x80000000;
        DAT_cc006430 = uVar1;
        DAT_8049f5f8 = uVar1;
        os_get_interrupts_enabled();
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// si_init - one-time SI subsystem bring-up. Registers the kernel version blob,
// initializes the four per-device state-table id words to empty (0xffffffff),
// clears the poll config, initializes the SI type/state table (FUN_803bc220),
// waits for the SI bus to go idle, installs the SI interrupt handler at cause
// vector 0x14, unmasks it, and brings up polling on all four devices.
// ---------------------------------------------------------------------------
void si_init(void)
{
    uint uVar1;

    os_register_version(0, DAT_806114c0, 0x80610000);
    DAT_8060ea60 = 0xffffffff;
    DAT_8060ea40 = 0xffffffff;
    DAT_8060ea20 = 0xffffffff;
    DAT_8060ea00 = 0xffffffff;
    DAT_8049f5f8 = 0;
    si_set_sampling_rate(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    do {
        uVar1 = DAT_cc006434;
    } while ((uVar1 & 1) != 0);
    DAT_cc006434 = 0x80000000;
    os_set_interrupt_handler(0x14, si_interrupt_handler);
    os_unmask_interrupts(0x800);
    si_refresh_poll_status(0);
    si_refresh_poll_status(1);
    si_refresh_poll_status(2);
    si_refresh_poll_status(3);
    return;
}

// ===========================================================================
// batch_1155: SI poll-driver upper layer - deferred/retried poll transfers,
// the poll status-update callback (with SRAM controller-origin sync), the
// status refresh + response accessors, and the controller sampling-rate
// configuration keyed on TV format.
//
// NON-MATCHING: the flat DAT_8060eaxx / DAT_8060ebxx / DAT_cc00xx offset
// arithmetic, the 64-bit deadline comparisons the decompiler rendered with
// CONCAT44 / CARRY4 / sign-flip tricks, and the raw SI status-bit masks are
// kept verbatim. The two SI poll-completion callback constant 0x803BBC88 in the
// original (encoded as the literal -0x7fc44378) is expressed here as the address
// of si_poll_status_callback; the alarm callback constant 0x803C0000 (mid an
// unrelated function) is kept as a raw literal since it is a fixed dispatch
// stub. os_get_interrupts_enabled() stands in for the original OSRestore-
// Interrupts(bool) sequence.
// ===========================================================================

// --- Additional SI software-state globals used by the poll upper layer ------

// Per-device queued-transfer descriptor table (0x8060EA00, stride 0x20):
//   +0x00 id, +0x04 cmd ptr, +0x08 cmdLen, +0x0C respPtr, +0x10 respLen,
//   +0x14 completeCb, +0x18/+0x1C the 64-bit earliest-start deadline.
// (base + several strided members already declared in the batch_1153 header.)

// Per-device latched poll response (two words) + ready flag.
extern uint DAT_8060ebb0;   // +0x00 ready flag (stride 4)
extern uint DAT_8060ebc0;   // +0x00 latched response word 0 (stride 8)
extern uint DAT_8060ebc4;   // +0x04 latched response word 1 (stride 8)

// SI command-bank response mirrors read by the poll-ready path (stride 0xC).
extern uint DAT_cc006404;   // +0x04 device response word 0
extern uint DAT_cc006408;   // +0x08 device response word 1

// Per-device follow-up completion callback slots (0x8060EB60, 4 x stride 4
// inside a 0x10 per-device block).
extern uint DAT_8060eb60;
extern uint DAT_8060eb64;
extern uint DAT_8060eb68;
extern uint DAT_8060eb6c;

// Per-device recalibrate/get-origin command scratch (0x8060EBF0, stride 4).
extern uint DAT_8060ebf0;

// Per-device transfer-start / poll timestamps used by the refresh policy.
extern uint DAT_8060eb24;   // low word of transfer-start time (stride 8)
extern uint DAT_8060eb44;   // low word of last transfer time for interval calc (stride 8)

// SI type/origin bookkeeping.
extern uint DAT_80613438;   // per-device "origin known" bitmask (bit 31>>dev)
extern uint DAT_80613434;   // get-status command scratch word
extern uint DAT_80613440;   // current sampling-rate index

// Progressive-scan detect register + sampling-rate tables.
extern ushort DAT_cc00206c; // VI display config (bit0 = interlaced)
extern undefined2 DAT_8049f6c8;  // NTSC/default sampling table (12 x u16 line, u8 count)

// ---------------------------------------------------------------------------
// si_disable_polling - clear the per-device poll-enable bits selected by the
// argument's high nibble (arg >> 0x18 & 0xf0), writing the poll register and
// cached poll config. Inverse of si_enable_polling. Returns the prior config
// (unchanged when arg == 0).
// ---------------------------------------------------------------------------
uint si_disable_polling(uint devices)
{
    uint uVar1;

    uVar1 = DAT_8049f5f8;
    if (devices != 0) {
        os_disable_interrupts();
        uVar1 = DAT_8049f5f8 & ~(devices >> 0x18 & 0xf0);
        DAT_cc006430 = uVar1;
        DAT_8049f5f8 = uVar1;
        os_get_interrupts_enabled();
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// si_poll_response_ready - poll one device's status nibble out of the poll
// status register. Latches the error state (0x80) into the per-device
// retry/error word if the "no-response" bit (0x08) is set and no error is
// recorded. If the "poll-complete" bit (0x20) is set, captures the two response
// words from that device's command bank into the per-device latch and marks it
// ready. Returns true when a fresh response was latched.
// ---------------------------------------------------------------------------
bool si_poll_response_ready(int device)
{
    bool bVar1;
    uint uVar2;

    os_disable_interrupts();
    uVar2 = DAT_cc006438;
    uVar2 = uVar2 >> (3 - device) * 8;
    if (((uVar2 & 8) != 0) && ((*(uint *)(&DAT_8049f608 + device * 4) & 0x80) == 0)) {
        *(uint *)(&DAT_8049f608 + device * 4) = 8;
    }
    os_get_interrupts_enabled();
    bVar1 = (uVar2 & 0x20) != 0;
    if (bVar1) {
        *(undefined4 *)(&DAT_8060ebc0 + device * 8) = *(undefined4 *)(&DAT_cc006404 + device * 0xc);
        *(undefined4 *)(&DAT_8060ebc4 + device * 8) = *(undefined4 *)(&DAT_cc006408 + device * 0xc);
        *(undefined4 *)(&DAT_8060ebb0 + device * 4) = 1;
    }
    return bVar1;
}

// ---------------------------------------------------------------------------
// si_get_response - refresh a device's poll status (si_get_status), capture the
// latched response words when the poll-complete bit is set, then, if a response
// is now ready, copy the two latched words into the caller's buffer and clear
// the ready flag. Returns the ready flag (nonzero = response delivered).
// ---------------------------------------------------------------------------
int si_get_response(int device, undefined4 *out)
{
    uint uVar1;
    int  iVar2;

    os_disable_interrupts();
    uVar1 = si_get_status(device);
    if ((uVar1 & 0x20) != 0) {
        *(undefined4 *)(&DAT_8060ebc0 + device * 8) = *(undefined4 *)(&DAT_cc006404 + device * 0xc);
        *(undefined4 *)(&DAT_8060ebc4 + device * 8) = *(undefined4 *)(&DAT_cc006408 + device * 0xc);
        *(undefined4 *)(&DAT_8060ebb0 + device * 4) = 1;
    }
    iVar2 = *(int *)(&DAT_8060ebb0 + device * 4);
    *(undefined4 *)(&DAT_8060ebb0 + device * 4) = 0;
    if (iVar2 != 0) {
        *out = *(undefined4 *)(&DAT_8060ebc0 + device * 8);
        out[1] = *(undefined4 *)(&DAT_8060ebc4 + device * 8);
    }
    os_get_interrupts_enabled();
    return iVar2;
}

// ---------------------------------------------------------------------------
// si_poll_alarm_handler - OS alarm callback that replays a deferred poll
// transfer. The alarm object argument encodes the owning device-table slot
// (recovered by the fixed offset/stride arithmetic the decompiler produced);
// if that slot still holds a live descriptor (id != 0xffffffff) it re-issues
// si_transfer with the saved descriptor, clearing the slot on success.
// ---------------------------------------------------------------------------
void si_poll_alarm_handler(int alarmArg)
{
    int  iVar1;
    uint uVar2;
    int  iVar3;

    iVar1 = (alarmArg + 0x7f9f1580) / 0x28 + (alarmArg + 0x7f9f1580 >> 0x1f);
    iVar1 = iVar1 - (iVar1 >> 0x1f);
    uVar2 = (&DAT_8060ea00)[iVar1 * 8];
    if ((uVar2 != 0xffffffff) &&
        (iVar3 = si_transfer(uVar2, (undefined4 *)(&DAT_8060ea04)[iVar1 * 8],
                             (&DAT_8060ea08)[iVar1 * 8], (&DAT_8060ea0c)[iVar1 * 8],
                             (&DAT_8060ea10)[iVar1 * 8], (&DAT_8060ea14)[iVar1 * 8]),
         iVar3 != 0)) {
        (&DAT_8060ea00)[iVar1 * 8] = 0xffffffff;
    }
    return;
}

// ---------------------------------------------------------------------------
// si_queue_poll_transfer - issue a transfer with a minimum-interval deferral.
// Only proceeds when the bus is idle for the device (slot empty and not the
// active device). Reads the current time, adds the requested minimum interval
// (64-bit) to the device's last transfer time to compute the earliest allowed
// start; if that deadline is still in the future, arms si_poll_alarm_handler
// via os_set_alarm; otherwise fires si_transfer immediately and returns 1 on
// success. On defer or immediate-failure, stores the descriptor + deadline into
// the device-table slot for si_poll_alarm_handler to replay. Returns 1 if the
// transfer was queued/started, 0 if the bus was busy.
// ---------------------------------------------------------------------------
undefined4 si_queue_poll_transfer(uint device, undefined4 *cmd, int cmdLen, undefined4 respPtr,
                                  int respLen, int completeCb, int minIntervalHi,
                                  uint minIntervalLo)
{
    undefined4 uVar1;
    uint  uVar2;
    int   iVar3;
    uint  uVar4;
    uint  uVar5;
    uint  uVar6;
    longlong lVar7;

    os_disable_interrupts();
    if (((&DAT_8060ea00)[device * 8] == 0xffffffff) && (DAT_8049f5f4 != device)) {
        lVar7 = os_get_time();
        uVar2 = (uint)((ulonglong)lVar7 >> 0x20);
        uVar4 = (uint)lVar7;
        if (minIntervalLo != 0 || minIntervalHi != 0) {
            lVar7 = (longlong)CONCAT44(minIntervalHi + *(int *)(&DAT_8060eb40 + device * 8) +
                                          (uint)CARRY4(minIntervalLo,
                                                       *(uint *)(&DAT_8060eb44 + device * 8)),
                                       minIntervalLo + *(uint *)(&DAT_8060eb44 + device * 8));
        }
        uVar6 = (uint)((ulonglong)lVar7 >> 0x20);
        uVar5 = (uint)lVar7;
        if ((uVar2 ^ 0x80000000) < (uint)(uVar4 < uVar5) + (uVar6 ^ 0x80000000)) {
            os_set_alarm((undefined4 *)(&DAT_8060ea80 + device * 0x28), 0x803c0000,
                         uVar6 - ((uVar5 < uVar4) + uVar2), uVar5 - uVar4, si_poll_alarm_handler);
        }
        else {
            iVar3 = si_transfer(device, cmd, cmdLen, respPtr, respLen, completeCb);
            if (iVar3 != 0) {
                os_get_interrupts_enabled();
                return 1;
            }
        }
        (&DAT_8060ea00)[device * 8] = device;
        (&DAT_8060ea04)[device * 8] = (uint)(uintptr_t)cmd;
        (&DAT_8060ea08)[device * 8] = cmdLen;
        (&DAT_8060ea0c)[device * 8] = respPtr;
        (&DAT_8060ea10)[device * 8] = respLen;
        (&DAT_8060ea14)[device * 8] = completeCb;
        *(longlong *)(&DAT_8060ea18 + device * 8) = lVar7;
        os_get_interrupts_enabled();
        uVar1 = 1;
    }
    else {
        os_get_interrupts_enabled();
        uVar1 = 0;
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// si_poll_status_callback - SI poll transfer-completion callback (the
// completion routine queued by si_queue_poll_transfer). Updates the per-device
// state word (clears 0x80, OR-s in the new status bits), timestamps the poll,
// and clears this device's "origin known" bit. For a standard controller that
// reports it needs origin data, it reconciles against the SRAM ex field
// (os_sram_get/set_ex_field): when the stored and reported origin differ, or a
// recalibrate is requested, it issues a follow-up get-origin/recalibrate
// command via si_queue_poll_transfer. Otherwise it fires up to four registered
// per-device completion callbacks (slots at 0x8060EB60) and clears them.
// ---------------------------------------------------------------------------
void si_poll_status_callback(uint device, uint status)
{
    uint uVar1;
    ushort uVar2;
    code *pcVar3;
    uint uVar4;
    uint uVar5;
    int  iVar6;
    uint *puVar7;
    undefined4 *puVar8;
    longlong lVar9;

    iVar6 = device * 4;
    puVar7 = (uint *)(&DAT_8049f608 + iVar6);
    *puVar7 = *puVar7 & 0xffffff7f;
    *puVar7 = *puVar7 | status;
    lVar9 = os_get_time();
    *(longlong *)(&DAT_8060eb20 + device * 8) = lVar9;
    uVar5 = *puVar7;
    uVar1 = DAT_80613438 & ~(0x80000000U >> device);
    uVar4 = DAT_80613438 & 0x80000000U >> device;
    DAT_80613438 = uVar1;
    if (((((status & 0xf) == 0) && ((uVar5 & 0x18000000) == 0x8000000)) &&
         ((uVar5 & 0x80000000) != 0)) && ((uVar5 & 0x4000000) == 0)) {
        uVar2 = os_sram_get_ex_field(device);
        uVar1 = (uint)uVar2 << 8;
        if ((uVar4 == 0) || ((uVar1 & 0x100000) == 0)) {
            if ((uVar5 & 0x100000) == 0) {
                if ((uVar5 & 0x40000000) != 0) {
                    os_sram_set_ex_field(device, (ushort)((uVar5 & 0xcfff00) >> 8) | 0x1000);
                    *(uint *)(&DAT_8060ebf0 + iVar6) = (uVar5 & 0xcfff00) | 0x4e100000;
                    *puVar7 = 0x80;
                    si_queue_poll_transfer(device, (undefined4 *)(&DAT_8060ebf0 + iVar6), 3,
                                           (undefined4)(uintptr_t)puVar7, 3,
                                           (int)(uintptr_t)si_poll_status_callback, 0, 0);
                    return;
                }
                os_sram_set_ex_field(device, 0);
            }
            else if ((uVar1 & 0xcfff00) != (uVar5 & 0xcfff00)) {
                if ((uVar1 & 0x100000) == 0) {
                    uVar1 = (uVar5 & 0xcfff00) | 0x100000;
                    os_sram_set_ex_field(device, (ushort)(uVar1 >> 8));
                }
                *(uint *)(&DAT_8060ebf0 + iVar6) = uVar1 | 0x4e000000;
                *puVar7 = 0x80;
                si_queue_poll_transfer(device, (undefined4 *)(&DAT_8060ebf0 + iVar6), 3,
                                       (undefined4)(uintptr_t)puVar7, 3,
                                       (int)(uintptr_t)si_poll_status_callback, 0, 0);
                return;
            }
            uVar1 = *puVar7;
            iVar6 = 0;
            puVar8 = (undefined4 *)(&DAT_8060eb60 + device * 0x10);
            do {
                pcVar3 = (code *)*puVar8;
                if (pcVar3 != (code *)0x0) {
                    *puVar8 = 0;
                    (*(void (*)(uint, uint))pcVar3)(device, uVar1);
                }
                iVar6 = iVar6 + 1;
                puVar8 = puVar8 + 1;
            } while (iVar6 < 4);
        }
        else {
            *(uint *)(&DAT_8060ebf0 + iVar6) = (uVar1 & 0xcfff00) | 0x4e100000;
            *puVar7 = 0x80;
            si_queue_poll_transfer(device, (undefined4 *)(&DAT_8060ebf0 + iVar6), 3,
                                   (undefined4)(uintptr_t)puVar7, 3,
                                   (int)(uintptr_t)si_poll_status_callback, 0, 0);
        }
    }
    else {
        os_sram_set_ex_field(device, 0);
        uVar1 = *puVar7;
        iVar6 = 0;
        puVar8 = (undefined4 *)(&DAT_8060eb60 + device * 0x10);
        do {
            pcVar3 = (code *)*puVar8;
            if (pcVar3 != (code *)0x0) {
                *puVar8 = 0;
                (*(void (*)(uint, uint))pcVar3)(device, uVar1);
            }
            iVar6 = iVar6 + 1;
            puVar8 = puVar8 + 1;
        } while (iVar6 < 4);
    }
    return;
}

// ---------------------------------------------------------------------------
// si_refresh_poll_status - refresh a device's cached poll status with a
// timeout policy scaled by the bus tick base (DAT_800000f8). If the device is
// currently being polled by hardware (its poll-enable bit is set) it returns
// the cached status unless it timed out or reports "no response" (0x08); if the
// device is not being polled, it applies a two-tier timeout and, when stale,
// kicks a fresh get-status via si_queue_poll_transfer. Returns the status code.
// ---------------------------------------------------------------------------
int si_refresh_poll_status(uint device)
{
    uint uVar1;
    uint uVar2;
    int  iVar3;
    int *piVar4;
    longlong *plVar5;
    longlong lVar6;

    os_disable_interrupts();
    piVar4 = (int *)(&DAT_8049f608 + device * 4);
    iVar3 = *piVar4;
    lVar6 = os_get_time();
    plVar5 = (longlong *)(&DAT_8060eb20 + device * 8);
    uVar2 = (uint)lVar6 - *(uint *)(&DAT_8060eb24 + device * 8);
    uVar1 = (int)((ulonglong)lVar6 >> 0x20) -
            ((uint)((uint)lVar6 < *(uint *)(&DAT_8060eb24 + device * 8)) + *(int *)plVar5);
    if ((DAT_8049f5f8 & 0x80 >> (device & 0x3f)) == 0) {
        if (((uint)((DAT_800000f8 / 4000) * 0x32 < uVar2) + (uVar1 ^ 0x80000000) < 0x80000001) &&
            (iVar3 != 8)) {
            os_get_interrupts_enabled();
            return iVar3;
        }
        if ((uint)((DAT_800000f8 / 4000) * 0x4b < uVar2) + (uVar1 ^ 0x80000000) < 0x80000001) {
            *piVar4 = 0x80;
        }
        else {
            *piVar4 = 0x80;
            iVar3 = 0x80;
        }
    }
    else {
        if (iVar3 != 8) {
            lVar6 = os_get_time();
            *plVar5 = lVar6;
            os_get_interrupts_enabled();
            return iVar3;
        }
        *piVar4 = 0x80;
        iVar3 = 0x80;
    }
    lVar6 = os_get_time();
    *plVar5 = lVar6;
    si_queue_poll_transfer(device, (undefined4 *)&DAT_80613434, 1, (undefined4)(uintptr_t)piVar4, 3,
                           (int)(uintptr_t)si_poll_status_callback, 0,
                           (DAT_800000f8 / 500000) * 0x41 >> 3);
    os_get_interrupts_enabled();
    return iVar3;
}

// ---------------------------------------------------------------------------
// si_get_response_async - request a device's status/type and deliver it via a
// completion callback. Calls si_refresh_poll_status; if the status is available
// immediately (poll-error bit clear) it invokes the callback synchronously,
// otherwise it registers the callback into the first free of four per-device
// callback slots (0x8060EB60), de-duplicating against slots already holding it.
// Returns the status code.
// ---------------------------------------------------------------------------
int si_get_response_async(uint device, undefined *callback)
{
    int  iVar1;
    undefined *puVar2;
    int  iVar3;

    os_disable_interrupts();
    iVar3 = si_refresh_poll_status(device);
    if ((*(uint *)(&DAT_8049f608 + device * 4) & 0x80) == 0) {
        (*(void (*)(uint, int))callback)(device, iVar3);
    }
    else {
        iVar1 = device * 0x10;
        puVar2 = *(undefined **)(&DAT_8060eb60 + iVar1);
        if (puVar2 != callback) {
            if (puVar2 == (undefined *)0x0) {
                *(undefined **)(&DAT_8060eb60 + iVar1) = callback;
            }
            else if (*(undefined **)(&DAT_8060eb64 + iVar1) != callback) {
                if (*(undefined **)(&DAT_8060eb64 + iVar1) == (undefined *)0x0) {
                    *(undefined **)(&DAT_8060eb64 + iVar1) = callback;
                }
                else if (*(undefined **)(&DAT_8060eb68 + iVar1) != callback) {
                    if (*(undefined **)(&DAT_8060eb68 + iVar1) == (undefined *)0x0) {
                        *(undefined **)(&DAT_8060eb68 + iVar1) = callback;
                    }
                    else if ((*(undefined **)(&DAT_8060eb6c + iVar1) != callback) &&
                             (*(undefined **)(&DAT_8060eb6c + iVar1) == (undefined *)0x0)) {
                        *(undefined **)(&DAT_8060eb6c + iVar1) = callback;
                    }
                }
            }
        }
    }
    os_get_interrupts_enabled();
    return iVar3;
}

// ---------------------------------------------------------------------------
// si_set_sampling_rate - program the SI controller poll sampling rate. The rate
// index (rateIndex, forwarded via the variadic-style arg list the original ABI
// used) is clamped to 0..0xB. The per-TV-format sampling table is selected by
// vi_get_tv_format (NTSC / MPAL / GCA default share DAT_8049f6c8; EURGB60/PAL50
// uses the 0x8049f6f8 table); an unknown format logs via OSReport and falls back
// to rate 0. The interlace bit (DAT_cc00206c bit0) doubles the line count.
// Calls si_set_poll_params with the resolved (lineCount, perFrame) pair.
// ---------------------------------------------------------------------------
void si_set_sampling_rate(undefined8 param_1, undefined8 param_2, undefined8 param_3,
                          undefined8 param_4, undefined8 param_5, undefined8 param_6,
                          undefined8 param_7, undefined8 param_8, uint rateIndex,
                          undefined4 param_10, undefined4 param_11, undefined4 param_12,
                          undefined4 param_13, undefined4 param_14, undefined4 param_15,
                          undefined4 param_16)
{
    ushort uVar1;
    int    iVar2;
    undefined4 uVar3;
    undefined2 *puVar4;

    puVar4 = &DAT_8049f6c8;
    if (0xb < rateIndex) {
        rateIndex = 0xb;
    }
    os_disable_interrupts();
    uVar3 = 0;
    DAT_80613440 = rateIndex;
    iVar2 = vi_get_tv_format();
    if (iVar2 == 2) {
        puVar4 = &DAT_8049f6c8;
    }
    else if (iVar2 < 2) {
        if (iVar2 == 0) {
            puVar4 = &DAT_8049f6c8;
        }
        else if (-1 < iVar2) {
            puVar4 = (undefined2 *)0x8049f6f8;
        }
        else {
            OSReport(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     s_SISetSamplingRate__unknown_TV_fo_8049f728, uVar3, param_11, param_12,
                     param_13, param_14, param_15, param_16);
            rateIndex = 0;
        }
    }
    else if (iVar2 == 5) {
        puVar4 = &DAT_8049f6c8;
    }
    else {
        OSReport(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 s_SISetSamplingRate__unknown_TV_fo_8049f728, uVar3, param_11, param_12,
                 param_13, param_14, param_15, param_16);
        rateIndex = 0;
    }
    uVar1 = DAT_cc00206c;
    if ((uVar1 & 1) == 0) {
        iVar2 = 1;
    }
    else {
        iVar2 = 2;
    }
    si_set_poll_params(iVar2 * (uint)(ushort)puVar4[rateIndex * 2],
                       (uint)*(byte *)(puVar4 + rateIndex * 2 + 1));
    os_get_interrupts_enabled();
    return;
}

// ---------------------------------------------------------------------------
// si_refresh_sampling_rate - re-apply the current sampling rate index
// (DAT_80613440) via si_set_sampling_rate; used after a TV-format / display
// mode change so the poll timing tracks the new frame timing.
// ---------------------------------------------------------------------------
void si_refresh_sampling_rate(undefined8 param_1, undefined8 param_2, undefined8 param_3,
                              undefined8 param_4, undefined8 param_5, undefined8 param_6,
                              undefined8 param_7, undefined8 param_8, undefined4 param_9,
                              undefined4 param_10, undefined4 param_11, undefined4 param_12,
                              undefined4 param_13, undefined4 param_14, undefined4 param_15,
                              undefined4 param_16)
{
    si_set_sampling_rate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         DAT_80613440, param_10, param_11, param_12, param_13, param_14, param_15,
                         param_16);
    return;
}
