// os_thread.cpp - Dolphin OS thread subsystem bring-up (OSThread)
//
// Address: 0x8039f10c | Name: os_init_switch_trampoline  | Date: 2026-09-14
// Address: 0x8039f174 | Name: os_set_switch_thread_callback | Date: 2026-09-14
// Address: 0x8039f1e8 | Name: os_thread_init             | Date: 2026-09-14
// Address: 0x8039f340 | Name: os_init_thread_queue       | Date: 2026-09-14
// Address: 0x8039f350 | Name: os_get_current_thread      | Date: 2026-09-14
//
// __OSThreadInit (os_thread_init, reached from os_kernel_init) sets up the
// cooperative/preemptive thread runtime: it installs the low-memory context-
// switch trampoline, builds the idle/default thread context (DAT_805f2eb0), zeros
// the 32 priority run-queues and the active-thread list, and makes the idle thread
// current. os_set_switch_thread_callback installs the context-switch hook (used by
// the debugger / profiler). os_init_thread_queue (OSInitThreadQueue) zeros a
// two-word queue head/tail and is used all over the SDK. os_get_current_thread
// returns OSGetCurrentThread (DAT_800000e4).
//
// NON-MATCHING: the low-memory global pokes and the queue init are transcribed
// verbatim; the copy/cache helpers resolve through the shared headers.

#include "common.h"

typedef void code;

extern "C" {

// --- OS thread globals --------------------------------------------------------
extern void *DAT_800000d8;   // current OSContext pointer
extern void *DAT_800000dc;   // active-thread list head
extern void *DAT_800000e0;   // active-thread list tail
extern void *DAT_800000e4;   // current OSThread (OSGetCurrentThread)
extern void *DAT_806113f0;   // registered context-switch callback
extern unsigned char DAT_8039f170;   // default (no-op) switch callback stub
extern unsigned char DAT_8039f0ec;   // 0x1c-byte context-switch trampoline blob

// idle/default thread + run-queue BSS (all in the OSThread block at 0x805f3xxx)
extern int   DAT_805f2eb0;   // idle/default OSThread base
extern int   DAT_805f2a98;   // first of the 32 priority run-queues
extern unsigned short DAT_805f3178;
extern unsigned short DAT_805f317a;
extern int   DAT_805f317c;
extern int   DAT_805f3180;
extern int   DAT_805f3184;
extern int   DAT_805f3188;
extern int   DAT_805f3198;
extern int   DAT_805f31a0;
extern int   DAT_805f31a4;
extern int   DAT_805f31a8;
extern int   DAT_805f31ac;
extern int   DAT_805f31b0;
extern int   DAT_805f31b4;
extern void *DAT_805f31b8;
extern unsigned int _DAT_8061c1c0;
extern int   DAT_8061c1c0;
extern int   DAT_80613100;
extern int   DAT_80613104;
extern int   DAT_80613108;

// --- helpers ------------------------------------------------------------------
extern unsigned long long os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void leaf_31e8(int dst, int src, int len);          // small memcpy leaf
extern void os_dc_flush_range_no_sync(int addr, int len);
extern void sync(int);
extern void os_ic_invalidate_range(int addr, int len);
extern void os_clear_context(int ctx);
extern void os_set_current_context(int ctx);
extern void FUN_803a0b84(unsigned int);                    // OSSetCurrentThread bring-up

void os_init_thread_queue(unsigned int *queue);

// Install the low-memory (0x80000c00) context-switch trampoline: copy the 0x1c
// -byte code blob into low memory, then flush it from dcache and invalidate the
// icache so the CPU fetches the freshly written instructions.
void os_init_switch_trampoline(void)
{
    leaf_31e8(0x80000c00, (int)&DAT_8039f0ec, 0x1c);
    os_dc_flush_range_no_sync(0x80000c00, 0x100);
    sync(0);
    os_ic_invalidate_range(0x80000c00, 0x100);
}

// __OSSetSwitchThreadCallback: install the context-switch callback (returns the
// previous one). A null argument installs the built-in no-op stub; the getter
// reports null when the stub is installed.
void *os_set_switch_thread_callback(void *cb)
{
    void *prev;

    os_disable_interrupts();
    prev = DAT_806113f0;
    if (cb == (void *)0) {
        cb = &DAT_8039f170;
    }
    DAT_806113f0 = cb;
    os_get_interrupts_enabled();
    if (prev == &DAT_8039f170) {
        prev = (void *)0;
    }
    return prev;
}

// OSInitThreadQueue: zero a two-word (head, tail) thread queue.
void os_init_thread_queue(unsigned int *queue)
{
    queue[1] = 0;
    queue[0] = 0;
}

// OSGetCurrentThread: return the currently running thread.
void *os_get_current_thread(void)
{
    return DAT_800000e4;
}

// __OSThreadInit: build the cooperative thread runtime. Seeds the idle/default
// thread control block (state, priorities, magic stack sentinel 0xdeadbabe),
// installs it as the current context/thread, zeros the 32 priority run-queues and
// the active-thread list, links the idle thread in, and fires the switch callback
// for the initial context.
void os_thread_init(void)
{
    unsigned int *queue;
    int i;

    DAT_805f3178 = 2;                 // idle thread state = READY
    DAT_805f317a = 1;
    DAT_805f3184 = 0x10;              // base priority
    DAT_805f3180 = 0x10;              // effective priority
    DAT_805f317c = 0;
    DAT_805f3188 = 0xffffffff;        // suspend count / owner sentinel
    DAT_805f31a0 = 0;
    os_init_thread_queue((unsigned int *)&DAT_805f3198);   // owned-mutex queue
    DAT_805f31a8 = 0;
    DAT_805f31a4 = 0;
    DAT_800000d8 = &DAT_805f2eb0;     // current context = idle context

    os_clear_context(0x805f2eb0);
    os_set_current_context(0x805f2eb0);

    DAT_805f31b4 = 0x8062c1c0;        // stack base
    DAT_805f31b8 = &DAT_8061c1c0;     // stack end
    _DAT_8061c1c0 = 0xdeadbabe;       // stack-overflow sentinel

    ((void (*)(void *, int *))DAT_806113f0)(DAT_800000e4, &DAT_805f2eb0);
    DAT_800000e4 = &DAT_805f2eb0;     // current thread = idle
    FUN_803a0b84(0);

    DAT_80613100 = 0;
    i = 0;
    DAT_80613104 = 0;
    queue = (unsigned int *)&DAT_805f2a98;
    do {
        os_init_thread_queue(queue);   // clear each of the 32 priority run-queues
        i = i + 1;
        queue = queue + 2;
    } while (i < 0x20);

    os_init_thread_queue((unsigned int *)&DAT_800000dc);   // active-thread list

    // link the idle thread into the active-thread list.
    if (DAT_800000e0 == (void *)0) {
        DAT_800000dc = &DAT_805f2eb0;
    }
    else {
        *(void **)((int)DAT_800000e0 + 0x2fc) = &DAT_805f2eb0;
    }
    DAT_805f31b0 = (int)DAT_800000e0;
    DAT_805f31ac = 0;
    DAT_800000e0 = &DAT_805f2eb0;

    os_clear_context(0x805f31c8);      // -0x7fa0ce38
    DAT_80613108 = 0;
}

} // extern "C"

// ============================================================================
// batch_1112: OSThread scheduler core (run-queues, reschedule, create/cancel/exit)
//
// Address: 0x8039f35c | Name: os_disable_scheduler          | Date: 2026-09-15
// Address: 0x8039f39c | Name: os_enable_scheduler           | Date: 2026-09-15
// Address: 0x8039f3dc | Name: os_unlink_thread_from_queue   | Date: 2026-09-15
// Address: 0x8039f444 | Name: os_thread_effective_priority  | Date: 2026-09-15
// Address: 0x8039f480 | Name: os_set_effective_priority     | Date: 2026-09-15
// Address: 0x8039f640 | Name: os_reschedule                 | Date: 2026-09-15
// Address: 0x8039f868 | Name: os_reschedule_if_needed       | Date: 2026-09-15
// Address: 0x8039f898 | Name: os_create_thread              | Date: 2026-09-15
// Address: 0x8039fa80 | Name: os_thread_exit_trampoline     | Date: 2026-09-15
// Address: 0x8039fb64 | Name: os_cancel_thread              | Date: 2026-09-15
//
// The OSThread scheduler. Threads live in a 0x318-byte OSThread control block.
// Key TCB offsets used below:
//   +0x84  initial-context link register (return addr = os_thread_exit_trampoline)
//   +0x0c  entry-point argument
//   +0x194/+0x19c/+0x1a2  FP/paired-single context flags
//   +0x2c8 state  (0=dead 1=READY 2=RUNNING 3/4=WAITING 8=MORIBUND)
//   +0x2ca flags  (bit0 = detached)
//   +0x2cc suspend count
//   +0x2d0 current (effective) priority
//   +0x2d4 base priority
//   +0x2d8 exit value / join result
//   +0x2dc run/wait queue pointer   +0x2e0 queue prev   +0x2e4 queue next
//   +0x2e8 join queue (head,tail)
//   +0x2f0 the mutex this thread waits on   +0x2f4 owned-mutex list head
//   +0x2fc active-list next   +0x300 active-list prev
//   +0x304/+0x308 stack base/limit  (0xdeadbabe sentinel at limit)
// Globals: DAT_805f2a98 = 32 priority run-queues (head,tail pairs);
//   DAT_80613100 = run-queue non-empty bitmap (bit 31-prio);
//   DAT_80613104 = pending-reschedule flag; DAT_80613108 = scheduler-disable count.
// NON-MATCHING: pointer arithmetic on the TCB is transcribed verbatim as byte
// offsets on int; the priority-queue table &DAT_805f2a98 is indexed prio*2.

extern "C" {

extern void os_enable_interrupts(void);
extern int  os_get_current_context(void);
extern int  os_save_gprs_context(int thread);
extern void os_load_context(int ctx);

// defined below in this file; os_create_thread stores its address in the TCB.
void os_thread_exit_trampoline(unsigned int exitValue);
extern void os_init_context(int thread, unsigned int entry, unsigned int sp);
extern void os_unlock_all_mutex(int thread);
extern int  FUN_803a0204(int *joinQueue);          // wake join-queue waiters

extern int  DAT_805f2750;      // FP-context feature enabled flag
extern int  DAT_806113d0;      // default MSR/FPSCR seed for FP threads

// OSDisableScheduler: bump the reschedule-disable nesting counter, return prev.
int os_disable_scheduler(void)
{
    int prev;
    os_disable_interrupts();
    prev = DAT_80613108;
    DAT_80613108 = DAT_80613108 + 1;
    os_get_interrupts_enabled();
    return prev;
}

// OSEnableScheduler: drop the reschedule-disable nesting counter, return prev.
int os_enable_scheduler(void)
{
    int prev;
    os_disable_interrupts();
    prev = DAT_80613108;
    DAT_80613108 = DAT_80613108 + -1;
    os_get_interrupts_enabled();
    return prev;
}

// __OSUnlinkThread: remove a READY thread from its priority run-queue and clear
// the run-queue bitmap bit if that queue is now empty.
void os_unlink_thread_from_queue(int thread)
{
    int prev;
    int *queue;
    int next;

    prev = *(int *)(thread + 0x2e0);
    queue = *(int **)(thread + 0x2dc);
    next = *(int *)(thread + 0x2e4);
    if (prev == 0) {
        queue[1] = next;
    } else {
        *(int *)(prev + 0x2e4) = next;
    }
    if (next == 0) {
        *queue = prev;
    } else {
        *(int *)(next + 0x2e0) = prev;
    }
    if (*queue == 0) {
        DAT_80613100 = DAT_80613100 & ~(1 << (0x1f - *(int *)(thread + 0x2d0)));
    }
    *(int *)(thread + 0x2dc) = 0;
}

// __OSGetEffectivePriority: min of the thread's base priority and every waiter
// priority across the mutexes it owns (priority inheritance).
int os_thread_effective_priority(int thread)
{
    int prio;
    int best;
    int *mutex;

    best = *(int *)(thread + 0x2d4);
    for (mutex = *(int **)(thread + 0x2f4); mutex != (int *)0; mutex = (int *)mutex[4]) {
        if ((*mutex != 0) && (prio = *(int *)(*mutex + 0x2d0), prio < best)) {
            best = prio;
        }
    }
    return best;
}

// __OSPromoteThread: apply newPrio to a thread and re-link it by state. For a
// WAITING(4) thread it returns the mutex owner so the caller can walk the chain.
unsigned int os_set_effective_priority(int thread, int newPrio)
{
    unsigned short state;
    int cur;
    int *queue;
    int prevPrio;

    state = *(unsigned short *)(thread + 0x2c8);
    if (state != 3) {
        if (state < 3) {
            if (state == 1) {
                os_unlink_thread_from_queue(thread);
                *(int *)(thread + 0x2d0) = newPrio;
                *(int **)(thread + 0x2dc) = (int *)(&DAT_805f2a98 + *(int *)(thread + 0x2d0) * 2);
                cur = (*(int **)(thread + 0x2dc))[1];
                if (cur == 0) {
                    **(int **)(thread + 0x2dc) = thread;
                } else {
                    *(int *)(cur + 0x2e0) = thread;
                }
                *(int *)(thread + 0x2e4) = cur;
                *(int *)(thread + 0x2e0) = 0;
                *(int *)(*(int *)(thread + 0x2dc) + 4) = thread;
                DAT_80613100 = DAT_80613100 | (1 << (0x1f - *(int *)(thread + 0x2d0)));
                DAT_80613104 = 1;
            } else if (state != 0) {
                DAT_80613104 = 1;
                *(int *)(thread + 0x2d0) = newPrio;
            }
        } else if (state < 5) {
            // WAITING on a mutex: remove from the wait queue, re-insert sorted.
            cur = *(int *)(thread + 0x2e0);
            prevPrio = *(int *)(thread + 0x2e4);
            if (cur == 0) {
                *(int *)(*(int *)(thread + 0x2dc) + 4) = prevPrio;
            } else {
                *(int *)(cur + 0x2e4) = prevPrio;
            }
            if (prevPrio == 0) {
                **(int **)(thread + 0x2dc) = cur;
            } else {
                *(int *)(prevPrio + 0x2e0) = cur;
            }
            *(int *)(thread + 0x2d0) = newPrio;
            queue = *(int **)(thread + 0x2dc);
            for (cur = *queue; (cur != 0 && (*(int *)(cur + 0x2d0) <= *(int *)(thread + 0x2d0)));
                 cur = *(int *)(cur + 0x2e0)) {
            }
            if (cur == 0) {
                cur = queue[1];
                if (cur == 0) {
                    *queue = thread;
                } else {
                    *(int *)(cur + 0x2e0) = thread;
                }
                *(int *)(thread + 0x2e4) = cur;
                *(int *)(thread + 0x2e0) = 0;
                *(int *)(*(int *)(thread + 0x2dc) + 4) = thread;
            } else {
                *(int *)(thread + 0x2e0) = cur;
                prevPrio = *(int *)(cur + 0x2e4);
                *(int *)(cur + 0x2e4) = thread;
                *(int *)(thread + 0x2e4) = prevPrio;
                if (prevPrio == 0) {
                    **(int **)(thread + 0x2dc) = thread;
                } else {
                    *(int *)(prevPrio + 0x2e0) = thread;
                }
            }
            if (*(int *)(thread + 0x2f0) != 0) {
                return *(unsigned int *)(*(int *)(thread + 0x2f0) + 8);
            }
        }
    }
    return 0;
}

// __OSReschedule: the context switcher. Returns 0 (a value only meaningful when
// the current context differs from the current thread, in which case nothing is
// switched).
unsigned int os_reschedule(int preempt)
{
    unsigned int prev;
    unsigned int cur;
    int idx;
    unsigned int *queue;

    if (DAT_80613108 < 1) {
        cur = (unsigned int)os_get_current_context();
        prev = (unsigned int)DAT_800000e4;
        if (cur == (unsigned int)DAT_800000e4) {
            if ((unsigned int)DAT_800000e4 != 0) {
                if (*(short *)((int)DAT_800000e4 + 0x2c8) == 2) {
                    if ((preempt == 0) &&
                        (idx = countLeadingZeros(DAT_80613100),
                         *(int *)((int)DAT_800000e4 + 0x2d0) <= idx)) {
                        return 0;
                    }
                    *(unsigned short *)((int)DAT_800000e4 + 0x2c8) = 1;
                    *(int **)(prev + 0x2dc) = (int *)(&DAT_805f2a98 + *(int *)(prev + 0x2d0) * 2);
                    cur = (*(unsigned int **)(prev + 0x2dc))[1];
                    if (cur == 0) {
                        **(unsigned int **)(prev + 0x2dc) = prev;
                    } else {
                        *(unsigned int *)(cur + 0x2e0) = prev;
                    }
                    *(unsigned int *)(prev + 0x2e4) = cur;
                    *(int *)(prev + 0x2e0) = 0;
                    *(unsigned int *)(*(int *)(prev + 0x2dc) + 4) = prev;
                    DAT_80613100 = DAT_80613100 | (1 << (0x1f - *(int *)(prev + 0x2d0)));
                    DAT_80613104 = 1;
                }
                if (((*(unsigned short *)(prev + 0x1a2) & 2) == 0) &&
                    (idx = os_save_gprs_context(prev), idx != 0)) {
                    return 0;
                }
            }
            if (DAT_80613100 == 0) {
                ((void (*)(void *, int))DAT_806113f0)(DAT_800000e4, 0);
                DAT_800000e4 = 0;
                os_set_current_context(0x805f31c8);
                do {
                    os_enable_interrupts();
                    do {
                    } while (DAT_80613100 == 0);
                    os_disable_interrupts();
                } while (DAT_80613100 == 0);
                os_clear_context(0x805f31c8);
            }
            DAT_80613104 = 0;
            idx = countLeadingZeros(DAT_80613100);
            queue = (unsigned int *)(&DAT_805f2a98 + idx * 2);
            prev = *queue;
            cur = *(unsigned int *)(prev + 0x2e0);
            if (cur == 0) {
                *(int *)(&DAT_805f2a98 + idx * 2 + 1) = 0;   // (&DAT_805f2a9c)[idx*2]
            } else {
                *(int *)(cur + 0x2e4) = 0;
            }
            *queue = cur;
            if (*queue == 0) {
                DAT_80613100 = DAT_80613100 & ~(1 << (0x1f - idx));
            }
            *(int *)(prev + 0x2dc) = 0;
            *(unsigned short *)(prev + 0x2c8) = 2;
            ((void (*)(void *, unsigned int))DAT_806113f0)(DAT_800000e4, prev);
            DAT_800000e4 = (void *)prev;
            os_set_current_context(prev);
            os_load_context(prev);
        } else {
            prev = 0;
        }
    } else {
        prev = 0;
    }
    return prev;
}

// Run the scheduler only if a reschedule is pending.
void os_reschedule_if_needed(void)
{
    if (DAT_80613104 != 0) {
        os_reschedule(0);
    }
}

// OSCreateThread: initialize a TCB and link it into the active-thread list.
unsigned int os_create_thread(int thread, unsigned int entry, unsigned int arg,
                              unsigned int stackBase, int stackSize, int priority,
                              unsigned short detached)
{
    unsigned int sp;
    unsigned int ok;
    int p;
    int rows;

    if ((priority < 0) || (0x1f < priority)) {
        ok = 0;
    } else {
        *(unsigned short *)(thread + 0x2c8) = 1;              // READY
        sp = stackBase & 0xfffffff8;
        *(unsigned short *)(thread + 0x2ca) = detached & 1;
        *(int *)(thread + 0x2d4) = priority;                  // base priority
        *(int *)(thread + 0x2d0) = priority;                  // effective priority
        *(int *)(thread + 0x2cc) = 1;                         // suspend count
        *(int *)(thread + 0x2d8) = -1;
        *(int *)(thread + 0x2f0) = 0;
        *(int *)(thread + 0x2ec) = 0;
        *(int *)(thread + 0x2e8) = 0;
        *(int *)(thread + 0x2f8) = 0;
        *(int *)(thread + 0x2f4) = 0;
        *(int *)(sp - 8) = 0;
        *(int *)(sp - 4) = 0;
        os_init_context(thread, entry, sp - 8);
        *(unsigned int *)(thread + 0x84) = (unsigned int)&os_thread_exit_trampoline;
        *(unsigned int *)(thread + 0xc) = arg;
        *(unsigned int *)(thread + 0x304) = stackBase;
        *(unsigned int *)(thread + 0x308) = stackBase - stackSize;
        **(unsigned int **)(thread + 0x308) = 0xdeadbabe;     // stack sentinel
        *(int *)(thread + 0x30c) = 0;
        *(int *)(thread + 0x310) = 0;
        *(int *)(thread + 0x314) = 0;
        os_disable_interrupts();
        if (DAT_805f2750 != 0) {
            // lazy paired-single/FP context: tag the TCB and clear the PS shadow.
            rows = 4;
            *(unsigned int *)(thread + 0x19c) = *(unsigned int *)(thread + 0x19c) | 0x900;
            *(unsigned short *)(thread + 0x1a2) = *(unsigned short *)(thread + 0x1a2) | 1;
            *(unsigned int *)(thread + 0x194) = (DAT_806113d0 & 0xf8) | 4;
            p = thread;
            do {
                *(unsigned int *)(p + 0x94) = 0xffffffff;
                *(unsigned int *)(p + 0x90) = 0xffffffff;
                *(unsigned int *)(p + 0x1cc) = 0xffffffff;
                *(unsigned int *)(p + 0x1c8) = 0xffffffff;
                *(unsigned int *)(p + 0x9c) = 0xffffffff;
                *(unsigned int *)(p + 0x98) = 0xffffffff;
                *(unsigned int *)(p + 0x1d4) = 0xffffffff;
                *(unsigned int *)(p + 0x1d0) = 0xffffffff;
                *(unsigned int *)(p + 0xa4) = 0xffffffff;
                *(unsigned int *)(p + 0xa0) = 0xffffffff;
                *(unsigned int *)(p + 0x1dc) = 0xffffffff;
                *(unsigned int *)(p + 0x1d8) = 0xffffffff;
                *(unsigned int *)(p + 0xac) = 0xffffffff;
                *(unsigned int *)(p + 0xa8) = 0xffffffff;
                *(unsigned int *)(p + 0x1e4) = 0xffffffff;
                *(unsigned int *)(p + 0x1e0) = 0xffffffff;
                *(unsigned int *)(p + 0xb4) = 0xffffffff;
                *(unsigned int *)(p + 0xb0) = 0xffffffff;
                *(unsigned int *)(p + 0x1ec) = 0xffffffff;
                *(unsigned int *)(p + 0x1e8) = 0xffffffff;
                *(unsigned int *)(p + 0xbc) = 0xffffffff;
                *(unsigned int *)(p + 0xb8) = 0xffffffff;
                *(unsigned int *)(p + 0x1f4) = 0xffffffff;   // +500
                *(unsigned int *)(p + 0x1f0) = 0xffffffff;
                *(unsigned int *)(p + 0xc4) = 0xffffffff;
                *(unsigned int *)(p + 0xc0) = 0xffffffff;
                *(unsigned int *)(p + 0x1fc) = 0xffffffff;
                *(unsigned int *)(p + 0x1f8) = 0xffffffff;
                *(unsigned int *)(p + 0xcc) = 0xffffffff;
                *(unsigned int *)(p + 0xc8) = 0xffffffff;    // +200
                *(unsigned int *)(p + 0x204) = 0xffffffff;
                *(unsigned int *)(p + 0x200) = 0xffffffff;
                p = p + 0x40;
                rows = rows + -1;
            } while (rows != 0);
        }
        p = thread;
        if (DAT_800000e0 != (void *)0) {
            *(int *)((int)DAT_800000e0 + 0x2fc) = thread;
            p = (int)DAT_800000dc;
        }
        DAT_800000dc = (void *)p;
        *(int *)(thread + 0x300) = (int)DAT_800000e0;
        *(int *)(thread + 0x2fc) = 0;
        DAT_800000e0 = (void *)thread;
        os_get_interrupts_enabled();
        ok = 1;
    }
    return ok;
}

// The return address installed in a new thread's context: also OSExitThread tail.
void os_thread_exit_trampoline(unsigned int exitValue)
{
    int thread;
    int prev;
    int next;
    int head;

    os_disable_interrupts();
    thread = (int)DAT_800000e4;
    os_clear_context((int)DAT_800000e4);
    if ((*(unsigned short *)(thread + 0x2ca) & 1) == 0) {
        *(unsigned short *)(thread + 0x2c8) = 8;              // MORIBUND (joinable)
        *(unsigned int *)(thread + 0x2d8) = exitValue;
    } else {
        // detached: unlink from the active-thread list and mark dead.
        prev = *(int *)(thread + 0x2fc);
        next = *(int *)(thread + 0x300);
        head = next;
        if (prev != 0) {
            *(int *)(prev + 0x300) = next;
            head = (int)DAT_800000e0;
        }
        DAT_800000e0 = (void *)head;
        if (next != 0) {
            *(int *)(next + 0x2fc) = prev;
            prev = (int)DAT_800000dc;
        }
        DAT_800000dc = (void *)prev;
        *(unsigned short *)(thread + 0x2c8) = 0;              // dead
    }
    os_unlock_all_mutex(thread);
    FUN_803a0204((int *)(thread + 0x2e8));                    // wake joiners
    DAT_80613104 = 1;
    os_reschedule(0);
    os_get_interrupts_enabled();
}

// OSCancelThread: forcibly remove a thread from queues and terminate it.
void os_cancel_thread(int thread)
{
    unsigned short state;
    int prev;
    int next;
    int head;
    int owner;
    int effPrio;

    os_disable_interrupts();
    state = *(unsigned short *)(thread + 0x2c8);
    if (state == 3) {
        os_get_interrupts_enabled();
        return;
    }
    if (state < 3) {
        if (state == 1) {
            if (*(int *)(thread + 0x2cc) < 1) {
                os_unlink_thread_from_queue(thread);
            }
        } else {
            if (state == 0) {
                os_get_interrupts_enabled();
                return;
            }
            DAT_80613104 = 1;
        }
    } else {
        if (4 < state) {
            os_get_interrupts_enabled();
            return;
        }
        // WAITING on a mutex: remove from the wait queue.
        prev = *(int *)(thread + 0x2e0);
        next = *(int *)(thread + 0x2e4);
        if (prev == 0) {
            *(int *)(*(int *)(thread + 0x2dc) + 4) = next;
        } else {
            *(int *)(prev + 0x2e4) = next;
        }
        if (next == 0) {
            **(int **)(thread + 0x2dc) = prev;
        } else {
            *(int *)(next + 0x2e0) = prev;
        }
        *(int *)(thread + 0x2dc) = 0;
        // re-promote the remaining waiters down the ownership chain.
        if ((*(int *)(thread + 0x2cc) < 1) && (*(int *)(thread + 0x2f0) != 0)) {
            owner = *(int *)(*(int *)(thread + 0x2f0) + 8);
            do {
                if ((0 < *(int *)(owner + 0x2cc)) ||
                    (effPrio = os_thread_effective_priority(owner),
                     *(int *)(owner + 0x2d0) == effPrio)) break;
                owner = (int)os_set_effective_priority(owner, effPrio);
            } while (owner != 0);
        }
    }
    os_clear_context(thread);
    if ((*(unsigned short *)(thread + 0x2ca) & 1) == 0) {
        *(unsigned short *)(thread + 0x2c8) = 8;              // MORIBUND
    } else {
        prev = *(int *)(thread + 0x2fc);
        head = *(int *)(thread + 0x300);
        next = head;
        if (prev != 0) {
            *(int *)(prev + 0x300) = head;
            next = (int)DAT_800000e0;
        }
        DAT_800000e0 = (void *)next;
        if (head != 0) {
            *(int *)(head + 0x2fc) = prev;
            prev = (int)DAT_800000dc;
        }
        DAT_800000dc = (void *)prev;
        *(unsigned short *)(thread + 0x2c8) = 0;              // dead
    }
    os_unlock_all_mutex(thread);
    FUN_803a0204((int *)(thread + 0x2e8));
    if (DAT_80613104 != 0) {
        os_reschedule(0);
    }
    os_get_interrupts_enabled();
}

} // extern "C"

// ============================================================================
// batch_1113: OSThread suspend/resume/sleep/wakeup, idle-function, OSGetTime,
// and the OSCheckActiveThreads debug auditor (os_thread.cpp appended).
//
// Address: 0x8039fd20 | Name: os_resume_thread         | Date: 2026-09-15
// Address: 0x8039ffa8 | Name: os_suspend_thread        | Date: 2026-09-15
// Address: 0x803a0118 | Name: os_sleep_thread          | Date: 2026-09-15
// Address: 0x803a0204 | Name: os_wakeup_thread         | Date: 2026-09-15
// Address: 0x803a0308 | Name: os_set_idle_function     | Date: 2026-09-15
// Address: 0x803a0398 | Name: os_thread_queue_is_valid | Date: 2026-09-15
// Address: 0x803a0434 | Name: os_check_active_threads  | Date: 2026-09-15
// Address: 0x803a0b84 | Name: os_fill_stack_pattern    | Date: 2026-09-15
// Address: 0x803a0c30 | Name: os_read_time_base        | Date: 2026-09-15
// Address: 0x803a0c50 | Name: os_get_time              | Date: 2026-09-15
//
// Suspend/resume/sleep/wakeup manage the same run-queue / wait-queue link fields
// documented in the batch_1112 header. OSGetTime = raw 64-bit PPC time base +
// the boot base offset. OSCheckActiveThreads is a heavy debug-only integrity
// auditor that OSPanics on any broken invariant.
// NON-MATCHING: TCB pointer arithmetic transcribed as byte offsets on int;
// os_read_time_base's two-register PPC return modeled as u64; in
// os_check_active_threads each debug OSReport+OSPanic assertion pair is
// funnelled through the os_check_panic(line) sink (message strings are
// debug-only), preserving every real predicate and the control flow.

extern "C" {

// idle-thread + time globals
extern int  DAT_805f2e60;    // idle-thread present flag
extern unsigned char DAT_805f2b98;   // idle-thread TCB / handle
extern int  DAT_800030d8;    // boot time base offset (high)
extern int  DAT_800030dc;    // boot time base offset (low)

// debug auditor helpers. OSReport/OSPanic/os_get_stack_pointer resolve through
// the shared headers (sdk_stubs.h / generated_decls.h) so they are NOT
// re-declared here (Compile-Clean rule 6). The original body chains an
// OSReport(fmt,...) + OSPanic("OSThread.c",line,...) pair at each failed
// invariant; since those message strings are debug-only and the decompiler's
// 16-arg variadic tail is an ABI artifact, each assertion failure is funnelled
// through this single sink, preserving every real predicate and the control
// flow while staying compile-clean.
extern int  os_thread_is_in_queue(int thread);
extern int  os_mutex_owner_is_valid(int thread);

// OSPanic is declared in generated_decls.h (not included via common.h), so
// declare it per-file. OSReport comes from sdk_stubs.h and is not re-declared.
extern void OSPanic(const char *file, int line, const char *fmt, ...);

static void os_check_panic(int line)
{
    OSPanic("OSThread.c", line, "OSCheckActiveThreads: invariant failed");
}

// PPC time-base register intrinsics (mftb).
extern unsigned int os_mftbu(void);
extern unsigned int os_mftbl(void);

// current stack pointer read (defined in os_context.cpp). generated_decls.h is
// not included by common.h, so cross-TU helpers used here are declared per-file.
extern unsigned int os_get_stack_pointer(void);

// forward decls for peers defined below in this file (those above are already
// declared/defined earlier in the same TU by batch_1112).
int  os_thread_queue_is_valid(int *queue);
unsigned long long os_read_time_base(void);

// OSResumeThread: decrement suspend count; on reaching 0 re-queue the thread.
int os_resume_thread(int thread)
{
    unsigned short state;
    int cur;
    int *mutex;
    int prev;
    int oldCount;

    os_disable_interrupts();
    oldCount = *(int *)(thread + 0x2cc);
    *(int *)(thread + 0x2cc) = oldCount + -1;
    if (*(int *)(thread + 0x2cc) < 0) {
        *(int *)(thread + 0x2cc) = 0;
    } else if (*(int *)(thread + 0x2cc) == 0) {
        state = *(unsigned short *)(thread + 0x2c8);
        if (state == 4) {
            // WAITING on a mutex: remove, recompute effective prio, re-insert sorted.
            cur = *(int *)(thread + 0x2e0);
            prev = *(int *)(thread + 0x2e4);
            if (cur == 0) {
                *(int *)(*(int *)(thread + 0x2dc) + 4) = prev;
            } else {
                *(int *)(cur + 0x2e4) = prev;
            }
            if (prev == 0) {
                **(int **)(thread + 0x2dc) = cur;
            } else {
                *(int *)(prev + 0x2e0) = cur;
            }
            cur = *(int *)(thread + 0x2d4);
            for (mutex = *(int **)(thread + 0x2f4); mutex != (int *)0; mutex = (int *)mutex[4]) {
                if ((*mutex != 0) && (prev = *(int *)(*mutex + 0x2d0), prev < cur)) {
                    cur = prev;
                }
            }
            *(int *)(thread + 0x2d0) = cur;
            {
                int *queue = *(int **)(thread + 0x2dc);
                for (cur = *queue; (cur != 0 && (*(int *)(cur + 0x2d0) <= *(int *)(thread + 0x2d0)));
                     cur = *(int *)(cur + 0x2e0)) {
                }
                if (cur == 0) {
                    cur = queue[1];
                    if (cur == 0) {
                        *queue = thread;
                    } else {
                        *(int *)(cur + 0x2e0) = thread;
                    }
                    *(int *)(thread + 0x2e4) = cur;
                    *(int *)(thread + 0x2e0) = 0;
                    *(int *)(*(int *)(thread + 0x2dc) + 4) = thread;
                } else {
                    *(int *)(thread + 0x2e0) = cur;
                    prev = *(int *)(cur + 0x2e4);
                    *(int *)(cur + 0x2e4) = thread;
                    *(int *)(thread + 0x2e4) = prev;
                    if (prev == 0) {
                        **(int **)(thread + 0x2dc) = thread;
                    } else {
                        *(int *)(prev + 0x2e0) = thread;
                    }
                }
            }
            if (*(int *)(thread + 0x2f0) != 0) {
                cur = *(int *)(*(int *)(thread + 0x2f0) + 8);
                do {
                    if ((0 < *(int *)(cur + 0x2cc)) ||
                        (prev = os_thread_effective_priority(cur),
                         *(int *)(cur + 0x2d0) == prev)) break;
                    cur = (int)os_set_effective_priority(cur, prev);
                } while (cur != 0);
            }
        } else if ((state < 4) && (state == 1)) {
            // READY: recompute effective priority and re-queue at the head.
            cur = *(int *)(thread + 0x2d4);
            for (mutex = *(int **)(thread + 0x2f4); mutex != (int *)0; mutex = (int *)mutex[4]) {
                if ((*mutex != 0) && (prev = *(int *)(*mutex + 0x2d0), prev < cur)) {
                    cur = prev;
                }
            }
            *(int *)(thread + 0x2d0) = cur;
            *(int **)(thread + 0x2dc) = (int *)(&DAT_805f2a98 + *(int *)(thread + 0x2d0) * 2);
            cur = (*(int **)(thread + 0x2dc))[1];
            if (cur == 0) {
                **(int **)(thread + 0x2dc) = thread;
            } else {
                *(int *)(cur + 0x2e0) = thread;
            }
            *(int *)(thread + 0x2e4) = cur;
            *(int *)(thread + 0x2e0) = 0;
            *(int *)(*(int *)(thread + 0x2dc) + 4) = thread;
            DAT_80613100 = DAT_80613100 | (1 << (0x1f - *(int *)(thread + 0x2d0)));
            DAT_80613104 = 1;
        }
        if (DAT_80613104 != 0) {
            os_reschedule(0);
        }
    }
    os_get_interrupts_enabled();
    return oldCount;
}

// OSSuspendThread: increment suspend count; on 0->1 remove from the run system.
int os_suspend_thread(int thread)
{
    unsigned short state;
    int oldCount;
    int cur;
    int next;

    os_disable_interrupts();
    oldCount = *(int *)(thread + 0x2cc);
    *(int *)(thread + 0x2cc) = oldCount + 1;
    if (oldCount == 0) {
        state = *(unsigned short *)(thread + 0x2c8);
        if (state != 3) {
            if (state < 3) {
                if (state == 1) {
                    os_unlink_thread_from_queue(thread);
                } else if (state != 0) {
                    DAT_80613104 = 1;
                    *(unsigned short *)(thread + 0x2c8) = 1;   // RUNNING -> READY
                }
            } else if (state < 5) {
                // WAITING on a mutex: remove, force lowest priority 0x20, re-queue.
                cur = *(int *)(thread + 0x2e0);
                next = *(int *)(thread + 0x2e4);
                if (cur == 0) {
                    *(int *)(*(int *)(thread + 0x2dc) + 4) = next;
                } else {
                    *(int *)(cur + 0x2e4) = next;
                }
                if (next == 0) {
                    **(int **)(thread + 0x2dc) = cur;
                } else {
                    *(int *)(next + 0x2e0) = cur;
                }
                *(int *)(thread + 0x2d0) = 0x20;
                cur = (*(int **)(thread + 0x2dc))[1];
                if (cur == 0) {
                    **(int **)(thread + 0x2dc) = thread;
                } else {
                    *(int *)(cur + 0x2e0) = thread;
                }
                *(int *)(thread + 0x2e4) = cur;
                *(int *)(thread + 0x2e0) = 0;
                *(int *)(*(int *)(thread + 0x2dc) + 4) = thread;
                if (*(int *)(thread + 0x2f0) != 0) {
                    cur = *(int *)(*(int *)(thread + 0x2f0) + 8);
                    do {
                        if ((0 < *(int *)(cur + 0x2cc)) ||
                            (next = os_thread_effective_priority(cur),
                             *(int *)(cur + 0x2d0) == next)) break;
                        cur = (int)os_set_effective_priority(cur, next);
                    } while (cur != 0);
                }
            }
        }
        if (DAT_80613104 != 0) {
            os_reschedule(0);
        }
    }
    os_get_interrupts_enabled();
    return oldCount;
}

// OSSleepThread: block the current thread on a thread queue (priority sorted).
void os_sleep_thread(int *queue)
{
    int self;
    int cur;
    int prev;

    os_disable_interrupts();
    self = (int)DAT_800000e4;
    *(unsigned short *)((int)DAT_800000e4 + 0x2c8) = 4;    // WAITING
    *(int **)(self + 0x2dc) = queue;
    for (cur = *queue; (cur != 0 && (*(int *)(cur + 0x2d0) <= *(int *)(self + 0x2d0)));
         cur = *(int *)(cur + 0x2e0)) {
    }
    if (cur == 0) {
        cur = queue[1];
        if (cur == 0) {
            *queue = self;
        } else {
            *(int *)(cur + 0x2e0) = self;
        }
        *(int *)(self + 0x2e4) = cur;
        *(int *)(self + 0x2e0) = 0;
        queue[1] = self;
    } else {
        *(int *)(self + 0x2e0) = cur;
        prev = *(int *)(cur + 0x2e4);
        *(int *)(cur + 0x2e4) = self;
        *(int *)(self + 0x2e4) = prev;
        if (prev == 0) {
            *queue = self;
        } else {
            *(int *)(prev + 0x2e0) = self;
        }
    }
    DAT_80613104 = 1;
    os_reschedule(0);
    os_get_interrupts_enabled();
}

// OSWakeupThread: make every thread on a queue READY and re-queue runnable ones.
void os_wakeup_thread(int *queue)
{
    int thread;
    int prev;

    os_disable_interrupts();
    while (thread = *queue, thread != 0) {
        prev = *(int *)(thread + 0x2e0);
        if (prev == 0) {
            queue[1] = 0;
        } else {
            *(int *)(prev + 0x2e4) = 0;
        }
        *queue = prev;
        *(unsigned short *)(thread + 0x2c8) = 1;           // READY
        if (*(int *)(thread + 0x2cc) < 1) {
            *(int **)(thread + 0x2dc) = (int *)(&DAT_805f2a98 + *(int *)(thread + 0x2d0) * 2);
            prev = (*(int **)(thread + 0x2dc))[1];
            if (prev == 0) {
                **(int **)(thread + 0x2dc) = thread;
            } else {
                *(int *)(prev + 0x2e0) = thread;
            }
            *(int *)(thread + 0x2e4) = prev;
            *(int *)(thread + 0x2e0) = 0;
            *(int *)(*(int *)(thread + 0x2dc) + 4) = thread;
            DAT_80613100 = DAT_80613100 | (1 << (0x1f - *(int *)(thread + 0x2d0)));
            DAT_80613104 = 1;
        }
    }
    if (DAT_80613104 != 0) {
        os_reschedule(0);
    }
    os_get_interrupts_enabled();
}

// Install (fn!=0) or remove (fn==0) the lowest-priority idle/background thread.
unsigned char *os_set_idle_function(int fn, unsigned int arg, unsigned int stackBase, int stackSize)
{
    if (fn == 0) {
        if (DAT_805f2e60 != 0) {
            os_cancel_thread((int)&DAT_805f2b98);          // 0x805f2b98
        }
    } else if (DAT_805f2e60 == 0) {
        os_create_thread((int)&DAT_805f2b98, fn, arg, stackBase, stackSize, 0x1f, 1);
        os_resume_thread((int)&DAT_805f2b98);
        return &DAT_805f2b98;
    }
    return (unsigned char *)0;
}

// Validate a doubly-linked thread queue's link consistency. Returns 1 if valid.
int os_thread_queue_is_valid(int *queue)
{
    int cur;
    int node;

    cur = *queue;
    if ((cur != 0) && (*(int *)(cur + 0x2e4) != 0)) {
        return 0;
    }
    if ((queue[1] != 0) && (*(int *)(queue[1] + 0x2e0) != 0)) {
        return 0;
    }
    while (true) {
        node = cur;
        if (node == 0) {
            return 1;
        }
        cur = *(int *)(node + 0x2e0);
        if ((cur != 0) && (node != *(int *)(cur + 0x2e4))) break;
        if ((*(int *)(node + 0x2e4) != 0) && (node != *(int *)(*(int *)(node + 0x2e4) + 0x2e0))) {
            return 0;
        }
    }
    return 0;
}

// OSCheckActiveThreads: debug-only integrity auditor; OSPanics on any broken
// scheduler invariant. Returns the number of threads on the active list.
int os_check_active_threads(void)
{
    bool found;
    int chk;
    unsigned int *ownedMutex;
    unsigned int state;
    int i;
    int count;
    int *queue;
    char *m;

    queue = &DAT_805f2a98;
    count = 0;
    os_disable_interrupts();
    i = 0;
    do {
        if ((DAT_80613100 & (1 << (0x1f - i))) == 0) {
            if ((*queue != 0) || (queue[1] != 0)) {
                os_check_panic(0x5e5);
            }
        } else if ((*queue == 0) || (queue[1] == 0)) {
            os_check_panic(0x5e0);
        }
        chk = os_thread_queue_is_valid(queue);
        if (chk == 0) {
            os_check_panic(0x5e7);
        }
        i = i + 1;
        queue = queue + 2;
    } while (i < 0x20);

    if ((DAT_800000dc != 0) && (*(int *)((int)DAT_800000dc + 0x300) != 0)) {
        os_check_panic(0x5ec);
    }
    i = (int)DAT_800000dc;
    if ((DAT_800000e0 != 0) && (*(int *)((int)DAT_800000e0 + 0x2fc) != 0)) {
        os_check_panic(0x5ee);
        i = (int)DAT_800000dc;
    }
    do {
        if (i == 0) {
            os_get_interrupts_enabled();
            return count;
        }
        count = count + 1;
        if ((*(int *)(i + 0x2fc) != 0) && (i != *(int *)(*(int *)(i + 0x2fc) + 0x300))) {
            os_check_panic(0x5f6);
        }
        if ((*(int *)(i + 0x300) != 0) && (i != *(int *)(*(int *)(i + 0x300) + 0x2fc))) {
            os_check_panic(0x5f8);
        }
        if (**(int **)(i + 0x308) != -0x21524542) {     // 0xdeadbabe sentinel
            os_check_panic(0x5fb);
        }
        if ((*(int *)(i + 0x2d0) < 0) || (0x20 < *(int *)(i + 0x2d0))) {
            os_check_panic(0x5fe);
        }
        if (*(int *)(i + 0x2cc) < 0) {
            os_check_panic(0x5ff);
        }
        chk = os_thread_queue_is_valid((int *)(i + 0x2e8));
        if (chk == 0) {
            os_check_panic(0x600);
        }
        state = (unsigned int)*(unsigned short *)(i + 0x2c8);
        if (state == 4) {
            if (*(int *)(i + 0x2dc) == 0) {
                os_check_panic(0x611);
            }
            chk = os_thread_queue_is_valid(*(int **)(i + 0x2dc));
            if (chk == 0) {
                os_check_panic(0x612);
            }
            found = false;
            for (chk = **(int **)(i + 0x2dc); chk != 0; chk = *(int *)(chk + 0x2e0)) {
                if (i == chk) { found = true; break; }
            }
            if (!found) {
                os_check_panic(0x613);
            }
            if (*(int *)(i + 0x2cc) < 1) {
                chk = *(int *)(i + 0x2d4);
                for (ownedMutex = *(unsigned int **)(i + 0x2f4); ownedMutex != (unsigned int *)0;
                     ownedMutex = (unsigned int *)ownedMutex[4]) {
                    m = (char *)*ownedMutex;
                    if ((m != (char *)0) && (*(int *)(m + 0x2d0) < chk)) {
                        chk = *(int *)(m + 0x2d0);
                    }
                }
                if (*(int *)(i + 0x2d0) != chk) {
                    os_check_panic(0x616);
                }
            } else if (*(int *)(i + 0x2d0) != 0x20) {
                os_check_panic(0x61a);
            }
            chk = os_thread_is_in_queue(i);
            if (chk != 0) {
                os_check_panic(0x61c);
            }
        } else if (state < 4) {
            if (state == 2) {
                if (0 < *(int *)(i + 0x2cc)) {
                    os_check_panic(0x60c);
                }
                if (*(int *)(i + 0x2dc) != 0) {
                    os_check_panic(0x60d);
                }
                chk = *(int *)(i + 0x2d4);
                for (ownedMutex = *(unsigned int **)(i + 0x2f4); ownedMutex != (unsigned int *)0;
                     ownedMutex = (unsigned int *)ownedMutex[4]) {
                    m = (char *)*ownedMutex;
                    if ((m != (char *)0) && (*(int *)(m + 0x2d0) < chk)) {
                        chk = *(int *)(m + 0x2d0);
                    }
                }
                if (*(int *)(i + 0x2d0) != chk) {
                    os_check_panic(0x60e);
                }
            } else {
                if ((1 < state) || (state == 0)) goto unknown_state;
                if (*(int *)(i + 0x2cc) < 1) {
                    if (*(unsigned int **)(i + 0x2dc) !=
                        (unsigned int *)(&DAT_805f2a98 + *(int *)(i + 0x2d0) * 2)) {
                        os_check_panic(0x606);
                    }
                    found = false;
                    for (chk = (&DAT_805f2a98)[*(int *)(i + 0x2d0) * 2]; chk != 0;
                         chk = *(int *)(chk + 0x2e0)) {
                        if (i == chk) { found = true; break; }
                    }
                    if (!found) {
                        os_check_panic(0x607);
                    }
                    chk = *(int *)(i + 0x2d4);
                    for (ownedMutex = *(unsigned int **)(i + 0x2f4); ownedMutex != (unsigned int *)0;
                         ownedMutex = (unsigned int *)ownedMutex[4]) {
                        m = (char *)*ownedMutex;
                        if ((m != (char *)0) && (*(int *)(m + 0x2d0) < chk)) {
                            chk = *(int *)(m + 0x2d0);
                        }
                    }
                    if (*(int *)(i + 0x2d0) != chk) {
                        os_check_panic(0x608);
                    }
                }
            }
        } else if (state == 8) {
            if ((*(int *)(i + 0x2f4) != 0) || (*(int *)(i + 0x2f8) != 0)) {
                os_check_panic(0x620);
            }
        } else {
unknown_state:
            os_check_panic(0x626);
        }
        chk = os_mutex_owner_is_valid(i);
        if (chk == 0) {
            os_check_panic(0x62b);
        }
        i = *(int *)(i + 0x2fc);
    } while (true);
}

// __OSThreadFillStack: watermark the current thread's unused stack with a byte.
void os_fill_stack_pattern(unsigned int patternByte)
{
    unsigned int *sp;
    unsigned int *fill;
    unsigned char *span;
    unsigned int words;
    unsigned int val;
    unsigned int blocks;

    val = (patternByte & 0xff) | ((patternByte & 0xff) << 8) | (patternByte << 0x18) |
          ((patternByte & 0xff) << 0x10);
    sp = (unsigned int *)os_get_stack_pointer();
    fill = (unsigned int *)(*(int *)((int)DAT_800000e4 + 0x308) + 4);
    span = (unsigned char *)((int)sp + (3 - (int)fill));
    words = (unsigned int)span >> 2;
    if (fill < sp) {
        blocks = (unsigned int)span >> 5;
        if (blocks != 0) {
            do {
                fill[0] = val; fill[1] = val; fill[2] = val; fill[3] = val;
                fill[4] = val; fill[5] = val; fill[6] = val; fill[7] = val;
                fill = fill + 8;
                blocks = blocks - 1;
            } while (blocks != 0);
            words = words & 7;
            if (words == 0) {
                return;
            }
        }
        do {
            *fill = val;
            fill = fill + 1;
            words = words - 1;
        } while (words != 0);
    }
}

// Read the raw 64-bit PPC time base (TBU:TBL) with a consistent-read retry loop.
unsigned long long os_read_time_base(void)
{
    unsigned int hi;
    unsigned int lo;
    unsigned int hi2;
    do {
        hi = os_mftbu();
        lo = os_mftbl();
        hi2 = os_mftbu();
    } while (hi != hi2);
    return ((unsigned long long)hi << 32) | lo;
}

// OSGetTime: raw time base + the boot base offset, read atomically.
long long os_get_time(void)
{
    long long tb;
    long long base;
    os_disable_interrupts();
    tb = (long long)os_read_time_base();
    base = ((long long)(unsigned int)DAT_800030d8 << 32) | (unsigned int)DAT_800030dc;
    os_get_interrupts_enabled();
    return tb + base;
}

} // extern "C"
