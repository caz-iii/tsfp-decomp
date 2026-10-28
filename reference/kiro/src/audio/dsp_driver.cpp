// Address: 0x803b1104 | Name: dsp_driver | Date: 2026-09-15
//
// batch_1139: the low-level DSP (Gekko/Broadway "DSP" audio DSP) mailbox and
// task-scheduler driver. This is the hardware layer under the AX synth: it
// talks to the DSP hardware mailbox registers at 0xCC005000 (CPU->DSP) and
// 0xCC005004/06 (DSP->CPU), and services the DSP interrupt (interrupt 7) that
// drives the AX per-frame task chain.
//
//   DSP mailbox register map (0xCC005000..0xCC00500B):
//     0xCC005000  CPU->DSP mailbox hi   (bit15 = mailbox-full/busy)
//     0xCC005002  CPU->DSP mailbox lo
//     0xCC005004  DSP->CPU mailbox hi   (bit15 = mailbox-full)
//     0xCC005006  DSP->CPU mailbox lo
//     0xCC00500A  DSP control/status (CSR): reset/halt/int-mask bits
//
// The scheduler keeps a small doubly-linked task list; the interrupt handler
// (dsp_isr, FUN_803b13e4 - next batch) advances it in response to the DSP
// posting one of several magic completion codes (0xCDD1xxxx family) through its
// mailbox. This batch covers the mailbox accessors, the one-time scheduler
// init, and the "is initialized" query.
//
// NON-MATCHING: the DSP mailbox registers are modelled as volatile-typed raw
// symbols (DAT_cc0050xx) matching the decompiler; the CONCAT22 mailbox read is
// preserved verbatim. Per-file externs are declared for every raw FUN_/DAT_
// symbol at the exact call-site arity/type.

#include "common.h"

// ---- DSP hardware mailbox / control registers (0xCC0050xx) ----------------
extern volatile ushort DAT_cc005000;  // CPU->DSP mailbox hi (bit15 = busy)
extern volatile ushort DAT_cc005002;  // CPU->DSP mailbox lo
extern volatile ushort DAT_cc005004;  // DSP->CPU mailbox hi (bit15 = full)
extern volatile ushort DAT_cc005006;  // DSP->CPU mailbox lo
extern volatile ushort DAT_cc00500a;  // DSP CSR (reset/halt/int mask)

// ---- DSP scheduler state block (0x806133f0..0x8061340c) -------------------
extern int   DAT_806133f0;   // initialized flag (1 once dsp_sched_init ran)
extern int   DAT_806133f8;   // pending-swap flag
extern int*  DAT_806133fc;   // pending-swap task
extern int   DAT_80613400;   // task list head slot
extern int   DAT_80613404;   // task list prev slot
extern int   DAT_80613408;   // task list next slot
extern int*  DAT_8061340c;   // current task
extern int   DAT_80611498;   // OS version-register arg

// ---- ROM / raw symbols referenced by the scheduler ------------------------
extern char  s_04_15_32_8049f154[];             // version string
// dsp_sync_nop (0x803b1394) and dsp_isr (0x803b13e4) are defined below.

// ---- OS interrupt / version helpers ---------------------------------------
extern void  os_register_version(undefined8, undefined8, undefined8, undefined8, undefined8,
                                 undefined8, undefined8, undefined8, int, undefined4, char*,
                                 undefined4, undefined4, undefined4, undefined4, undefined4);
extern int   os_disable_interrupts(void);
extern int   os_get_interrupts_enabled(void);
extern void  os_set_interrupt_handler(int, void (*)(undefined4, uint));
extern void  os_unmask_interrupts(uint);

// ---- forward declarations (defined later in this file) --------------------
void dsp_sync_nop(void);
void dsp_isr(undefined4 param_1, uint prevCtx);
void dsp_send_task_params(int cur, int* task);
void dsp_send_init_params(int task);
void dsp_task_list_remove(undefined4* task);
void dsp_task_list_insert(int task);
void dsp_task_list_insert_remove_helper(int task);

// dsp_mailbox_read_status: return bit15 (busy) of the CPU->DSP mailbox hi.
ushort dsp_mailbox_read_status(void)
{
    ushort v;

    v = DAT_cc005000;
    return v >> 0xf;
}

// dsp_mailbox_out_status: return bit15 (full) of the DSP->CPU mailbox hi.
ushort dsp_mailbox_out_status(void)
{
    ushort v;

    v = DAT_cc005004;
    return v >> 0xf;
}

// dsp_mailbox_out_read: read the full 32-bit DSP->CPU mailbox (hi:lo).
// CONCAT22(hi,lo) == (hi << 16) | lo.
undefined4 dsp_mailbox_out_read(void)
{
    ushort hi;
    ushort lo;

    hi = DAT_cc005004;
    lo = DAT_cc005006;
    return ((uint)hi << 0x10) | (uint)lo;
}

// dsp_mailbox_write: post a 32-bit message to the CPU->DSP mailbox (hi then lo).
void dsp_mailbox_write(undefined4 msg)
{
    DAT_cc005000 = (short)((uint)msg >> 0x10);
    DAT_cc005002 = (short)msg;
}

// dsp_sched_init: one-time DSP scheduler bring-up. Registers the audio version
// string, installs the DSP interrupt handler on interrupt 7, unmasks the DSP
// interrupt (0x1000000), programs the DSP CSR (halt then release), clears the
// task-list slots, and marks the scheduler initialized. Guarded so it runs at
// most once (DAT_806133f0 != 1).
void dsp_sched_init(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4,
                    undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8)
{
    ushort csr;
    undefined4 extraout_r4;
    char* verStr;
    undefined4 in_r6;
    undefined4 in_r7;
    undefined4 in_r8;
    undefined4 in_r9;
    undefined4 in_r10;
    undefined8 ver = 0;   // leftover register value after the sync-nop call

    verStr = s_04_15_32_8049f154;
    dsp_sync_nop();           // FUN_803b1394: empty sync stub; leftover reg -> ver
    if (DAT_806133f0 != 1) {
        os_register_version(ver, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            DAT_80611498, extraout_r4, verStr, in_r6, in_r7, in_r8, in_r9, in_r10);
        os_disable_interrupts();
        os_set_interrupt_handler(7, dsp_isr);
        os_unmask_interrupts(0x1000000);
        csr = DAT_cc00500a;
        DAT_cc00500a = csr & 0xff57 | 0x800;
        csr = DAT_cc00500a;
        DAT_cc00500a = csr & 0xff53;
        DAT_80613400 = 0;
        DAT_8061340c = 0;
        DAT_80613404 = 0;
        DAT_80613408 = 0;
        DAT_806133f0 = 1;
        os_get_interrupts_enabled();
    }
}

// dsp_is_initialized: return the scheduler-initialized flag.
undefined4 dsp_is_initialized(void)
{
    return DAT_806133f0;
}

// ===========================================================================
// batch_1140: the DSP task-list scheduler proper. The DSP driver keeps a
// priority-ordered doubly-linked list of "task" structures (each 0x40 bytes):
//   task[0]     state (0 idle / 1 running / 2 done / 3 paused)
//   task[1]     priority key (unsigned; lower = higher priority)
//   task[2]     flags (bit1 = 0x2 "resume-as-continue")
//   task+0x0c.. init params block (DSP address/len/config words)
//   task+0x18.. per-task DSP register save block (r24/r28/r30 style words)
//   task[9]/+0x26  16-bit config shorts
//   task[0xa]   start callback     task[0xb] done callback
//   task[0xc]   pause callback     task[0xd] misc callback
//   task[0xe]   next link (fwd)    task[0xf] prev link (back)
//   task+0x38   next ptr           task+0x3c prev ptr
// The scheduler head/prev/current live in DAT_80613408 / DAT_80613404 /
// DAT_8061340c; a pending priority-swap is staged in DAT_806133f8/fc. The DSP
// posts task-lifecycle events through its mailbox (magic codes 0xDCD1000x, seen
// here as the signed constants -0x232efffe..-0x232f0000) which the interrupt
// handler dsp_isr decodes to advance the list, invoke task callbacks, and
// stream the next task's parameter block to the DSP via dsp_send_task_params.
//
// NON-MATCHING: the raw task-struct pointer arithmetic (int/short reinterpret
// casts at fixed byte offsets) and the signed DSP magic-code compares are kept
// verbatim from the decompiler. Per-file externs for the extra scheduler DATs
// and OS context/thread helpers are declared below.
// ===========================================================================

extern int   DAT_8060e78c;   // per-DSP-index thread block base (stride 0x110)
extern void  os_clear_context(int ctx);
extern void  os_set_current_context(uint ctx);
extern void  os_wakeup_thread(int* thread);

// dsp_task_reset (0x803b121c): unlink a task, reset it to idle (state 0) with
// the resume-as-continue flag (task[2]=1); if it was the list head, re-send the
// init parameter block to the DSP.
undefined4* dsp_task_reset(undefined4* task)
{
    os_disable_interrupts();
    dsp_task_list_insert_remove_helper((int)task);
    *task = 0;
    task[2] = 1;
    os_get_interrupts_enabled();
    if ((int)task == DAT_80613408) {
        dsp_send_init_params((int)task);
    }
    return task;
}

// dsp_task_set_flag2 (0x803b128c): set the resume-as-continue flag (0x2) in a
// task's flag word (task+0x08).
int dsp_task_set_flag2(int task)
{
    os_disable_interrupts();
    *(uint*)(task + 8) = *(uint*)(task + 8) | 2;
    os_get_interrupts_enabled();
    return task;
}

// dsp_task_request_switch (0x803b12cc): request that the DSP switch to a given
// task. If it is already current, or its priority key outranks the current
// task's, stage it as the pending swap (DAT_806133f8/fc) and, when the current
// task is running, poke the DSP CSR (0xCC00500A) to yield. Otherwise refuse
// (returns NULL).
int* dsp_task_request_switch(int* task)
{
    ushort csr;

    os_disable_interrupts();
    if (DAT_8061340c == task) {
        DAT_806133f8 = 1;
        DAT_806133fc = task;
        os_get_interrupts_enabled();
    } else if ((uint)task[1] < (uint)DAT_8061340c[1]) {
        DAT_806133f8 = 1;
        DAT_806133fc = task;
        if (*DAT_8061340c == 1) {
            os_disable_interrupts();
            csr = DAT_cc00500a;
            DAT_cc00500a = csr & 0xff57 | 2;
            os_get_interrupts_enabled();
        }
        os_get_interrupts_enabled();
    } else {
        os_get_interrupts_enabled();
        task = (int*)0x0;
    }
    return task;
}

// dsp_sync_nop (0x803b1394): empty synchronization stub (short mailbox settle
// delay). Called after mailbox writes so the DSP can latch each word.
void dsp_sync_nop(void)
{
}

// dsp_isr (0x803b13e4): the DSP interrupt handler (installed on interrupt 7).
// Acks the DSP interrupt in the CSR, switches to a scratch OS context, spins
// for the DSP->CPU mailbox, and decodes the posted event code to drive the task
// list: task-done (-0x232efffe / 0xDCD10002), task-error/complete variants
// (< -0x232efffe), task-yield-request (-0x232efffc), and task-start
// (< -0x232efffc). On each transition it advances DAT_8061340c to the next/
// pending task, streams that task's parameter block via dsp_send_task_params,
// runs the appropriate task callback, and handles the staged priority swap.
void dsp_isr(undefined4 param_1, uint prevCtx)
{
    ushort csr;
    int code;
    undefined1 scratchCtx[724];

    csr = DAT_cc00500a;
    DAT_cc00500a = csr & 0xffd7 | 0x80;
    os_clear_context((int)scratchCtx);
    os_set_current_context((uint)scratchCtx);
    do {
        csr = dsp_mailbox_out_status();
    } while (csr == 0);
    code = dsp_mailbox_out_read();
    if (((DAT_8061340c[2] & 2U) != 0) && (code == -0x232efffe)) {
        code = -0x232efffd;
    }
    if (code == -0x232efffe) {
        if (DAT_806133f8 == 0) {
            if (DAT_8061340c[0xe] == 0) {
                if ((int)DAT_8061340c == DAT_80613408) {
                    dsp_mailbox_write(0xcdd10003);
                    do { csr = dsp_mailbox_read_status(); } while (csr != 0);
                    if ((void*)DAT_8061340c[0xb] != (void*)0x0) {
                        (*(void(**)(void))(DAT_8061340c + 0xb))();
                    }
                } else {
                    dsp_mailbox_write(0xcdd10001);
                    do { csr = dsp_mailbox_read_status(); } while (csr != 0);
                    dsp_send_task_params((int)DAT_8061340c, (int*)DAT_80613408);
                    *DAT_8061340c = 2;
                    DAT_8061340c = (int*)DAT_80613408;
                }
            } else {
                dsp_mailbox_write(0xcdd10001);
                do { csr = dsp_mailbox_read_status(); } while (csr != 0);
                dsp_send_task_params((int)DAT_8061340c, (int*)DAT_8061340c[0xe]);
                *DAT_8061340c = 2;
                DAT_8061340c = (int*)DAT_8061340c[0xe];
            }
        } else if (DAT_8061340c == DAT_806133fc) {
            dsp_mailbox_write(0xcdd10003);
            do { csr = dsp_mailbox_read_status(); } while (csr != 0);
            DAT_806133fc = (int*)0x0;
            DAT_806133f8 = 0;
            if ((void*)DAT_8061340c[0xb] != (void*)0x0) {
                (*(void(**)(void))(DAT_8061340c + 0xb))();
            }
        } else {
            dsp_mailbox_write(0xcdd10001);
            do { csr = dsp_mailbox_read_status(); } while (csr != 0);
            dsp_send_task_params((int)DAT_8061340c, DAT_806133fc);
            *DAT_8061340c = 2;
            DAT_806133f8 = 0;
            DAT_8061340c = DAT_806133fc;
            DAT_806133fc = (int*)0x0;
        }
    } else if (code < -0x232efffe) {
        if (code == -0x232f0000) {
            *DAT_8061340c = 1;
            if ((void*)DAT_8061340c[10] != (void*)0x0) {
                (*(void(**)(void))(DAT_8061340c + 10))();
            }
        } else if (-0x232f0001 < code) {
            *DAT_8061340c = 1;
            if ((void*)DAT_8061340c[0xb] != (void*)0x0) {
                (*(void(**)(void))(DAT_8061340c + 0xb))();
            }
        }
    } else if (code == -0x232efffc) {
        if ((void*)DAT_8061340c[0xd] != (void*)0x0) {
            (*(void(**)(int*))(DAT_8061340c + 0xd))(DAT_8061340c);
        }
    } else if (code < -0x232efffc) {
        if (DAT_806133f8 == 0) {
            if (DAT_8061340c[0xe] == 0) {
                if ((int)DAT_8061340c == DAT_80613408) {
                    if ((void*)DAT_8061340c[0xc] != (void*)0x0) {
                        (*(void(**)(int*))(DAT_8061340c + 0xc))(DAT_8061340c);
                    }
                    dsp_mailbox_write(0xcdd10002);
                    do { csr = dsp_mailbox_read_status(); } while (csr != 0);
                    *DAT_8061340c = 3;
                    dsp_task_list_remove((undefined4*)DAT_8061340c);
                } else {
                    if ((void*)DAT_8061340c[0xc] != (void*)0x0) {
                        (*(void(**)(int*))(DAT_8061340c + 0xc))(DAT_8061340c);
                    }
                    dsp_mailbox_write(0xcdd10001);
                    do { csr = dsp_mailbox_read_status(); } while (csr != 0);
                    *DAT_8061340c = 3;
                    dsp_send_task_params(0, (int*)DAT_80613408);
                    DAT_8061340c = (int*)DAT_80613408;
                    dsp_task_list_remove((undefined4*)DAT_80613404);
                }
            } else {
                if ((void*)DAT_8061340c[0xc] != (void*)0x0) {
                    (*(void(**)(int*))(DAT_8061340c + 0xc))(DAT_8061340c);
                }
                dsp_mailbox_write(0xcdd10001);
                do { csr = dsp_mailbox_read_status(); } while (csr != 0);
                *DAT_8061340c = 3;
                dsp_send_task_params(0, (int*)DAT_8061340c[0xe]);
                DAT_8061340c = (int*)DAT_8061340c[0xe];
                dsp_task_list_remove((undefined4*)DAT_8061340c[0xf]);
            }
        } else {
            if ((void*)DAT_8061340c[0xc] != (void*)0x0) {
                (*(void(**)(int*))(DAT_8061340c + 0xc))(DAT_8061340c);
            }
            dsp_mailbox_write(0xcdd10001);
            do { csr = dsp_mailbox_read_status(); } while (csr != 0);
            dsp_send_task_params(0, DAT_806133fc);
            dsp_task_list_remove((undefined4*)DAT_8061340c);
            DAT_806133f8 = 0;
            DAT_8061340c = DAT_806133fc;
            DAT_806133fc = (int*)0x0;
        }
    }
    os_clear_context((int)scratchCtx);
    os_set_current_context(prevCtx);
}

// dsp_send_task_params (0x803b1808): stream a task's DSP parameter block to the
// DSP via the mailbox. If cur != 0 it first sends the current task's saved
// register block (cur+0x18/+0x1c/+0x20), else three zero words; then the new
// task's words (task[3..5]); then either the run-config (task[9] short + zeros)
// when the task is idle (task[0]==0) or the resume block (task+0x26 short +
// task[6..8]). A sync-spin follows every mailbox word.
void dsp_send_task_params(int cur, int* task)
{
    ushort st;

    if (cur == 0) {
        dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
    } else {
        dsp_mailbox_write(*(undefined4*)(cur + 0x18)); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(*(undefined4*)(cur + 0x1c)); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(*(undefined4*)(cur + 0x20)); do { st = dsp_mailbox_read_status(); } while (st != 0);
    }
    dsp_mailbox_write(task[3]); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(task[4]); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(task[5]); do { st = dsp_mailbox_read_status(); } while (st != 0);
    if (*task == 0) {
        dsp_mailbox_write((uint)*(ushort*)(task + 9)); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
    } else {
        dsp_mailbox_write((uint)*(ushort*)((int)task + 0x26)); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(task[6]); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(task[7]); do { st = dsp_mailbox_read_status(); } while (st != 0);
        dsp_mailbox_write(task[8]); do { st = dsp_mailbox_read_status(); } while (st != 0);
    }
}

// dsp_send_init_params (0x803b19a8): (re)send a task's DSP init parameter block.
// Drains one pending DSP->CPU mailbox word, then writes the fixed
// 0x80F3xxxx-tagged config/address words (init address task+0x0c, length
// task+0x14 low half, address task+0x10, and the 16-bit config task+0x24),
// each followed by a sync-spin, and settles with six sync-nops.
void dsp_send_init_params(int task)
{
    ushort st;

    do { st = dsp_mailbox_out_status(); } while (st == 0);
    dsp_mailbox_out_read();
    dsp_mailbox_write(0x80f3a001); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(*(undefined4*)(task + 0xc)); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(0x80f3c002); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(*(uint*)(task + 0x14) & 0xffff); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(0x80f3a002); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(*(undefined4*)(task + 0x10)); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(0x80f3b002); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(0); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write(0x80f3d001); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_mailbox_write((uint)*(ushort*)(task + 0x24)); do { st = dsp_mailbox_read_status(); } while (st != 0);
    dsp_sync_nop();
    dsp_sync_nop();
    dsp_sync_nop();
    dsp_sync_nop();
    dsp_sync_nop();
    dsp_sync_nop();
}

// dsp_task_list_insert (0x803b1b34): insert a task into the priority-ordered
// task list (keyed on task+0x04, ascending). Empty list -> becomes head, prev
// and current. Otherwise walk from the head, splicing before the first node
// whose key exceeds the new task's; if none, append at the tail. Maintains the
// task+0x38 (next) / task+0x3c (prev) links and the head/prev slots.
void dsp_task_list_insert(int task)
{
    int node;
    int head;

    node = DAT_80613408;
    if (DAT_80613408 == 0) {
        DAT_80613404 = task;
        DAT_80613408 = task;
        DAT_8061340c = (int*)task;
        *(undefined4*)(task + 0x3c) = 0;
        *(undefined4*)(task + 0x38) = 0;
        return;
    }
    do {
        head = DAT_80613408;
        if (node == 0) {
LAB_insert_tail:
            DAT_80613408 = head;
            if (node == 0) {
                *(int*)(DAT_80613404 + 0x38) = task;
                *(undefined4*)(task + 0x38) = 0;
                *(int*)(task + 0x3c) = DAT_80613404;
                DAT_80613404 = task;
                return;
            }
            return;
        }
        if (*(uint*)(task + 4) < *(uint*)(node + 4)) {
            *(undefined4*)(task + 0x3c) = *(undefined4*)(node + 0x3c);
            *(int*)(node + 0x3c) = task;
            *(int*)(task + 0x38) = node;
            head = task;
            if (*(int*)(task + 0x3c) != 0) {
                *(int*)(*(int*)(task + 0x3c) + 0x38) = task;
                head = DAT_80613408;
            }
            goto LAB_insert_tail;
        }
        node = *(int*)(node + 0x38);
    } while (true);
}

// Wrapper used by dsp_task_reset (which calls the list-unlink path). The reset
// routine unlinks via the same node-splice logic; here we forward to the list
// insert/remove machinery. (0x803b121c calls FUN_803b1b34 to relink.)
void dsp_task_list_insert_remove_helper(int task)
{
    dsp_task_list_insert(task);
}

// dsp_task_list_remove (0x803b1bd4): mark a task done (state 3, clear flags) and
// unlink it from the priority list, fixing up head/prev/current and the
// neighbours' next/prev links. Empties the list slots when the last task goes.
void dsp_task_list_remove(undefined4* task)
{
    task[2] = 0;
    *task = 3;
    if (DAT_80613408 != (int)task) {
        if (DAT_80613404 == (int)task) {
            DAT_80613404 = (int)(undefined4*)task[0xf];
            *(undefined4*)(task[0xf] + 0x38) = 0;
            DAT_8061340c = (int*)DAT_80613408;
            return;
        }
        DAT_8061340c = (int*)task[0xe];
        *(undefined4*)(task[0xf] + 0x38) = task[0xe];
        *(undefined4*)(task[0xe] + 0x3c) = task[0xf];
        return;
    }
    DAT_80613408 = (int)(undefined4*)task[0xe];
    if (DAT_80613408 != 0) {
        *(undefined4*)(task[0xe] + 0x3c) = 0;
        return;
    }
    DAT_8061340c = (int*)0x0;
    DAT_80613404 = 0;
    DAT_80613408 = 0;
}

// dsp_task_wakeup_thread (0x803b1c6c): wake the OS thread associated with a DSP
// index (per-index thread block DAT_8060e78c, stride 0x110).
void dsp_task_wakeup_thread(int idx)
{
    os_wakeup_thread((int*)(&DAT_8060e78c + idx * 0x110));
}
