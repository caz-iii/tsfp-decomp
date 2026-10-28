// Dolphin OS alarm subsystem (OSAlarm) + a few OS bring-up helpers.
//
// A decrementer-driven timer queue: alarms are kept in a doubly-linked list
// sorted by absolute fire time (a 64-bit time-base value), and the PowerPC
// decrementer (DEC) is always programmed to the delta until the head alarm.
// When DEC underflows, the decrementer exception callback pops every expired
// alarm, reprograms DEC for the next one, and dispatches the handler.
//
// The alarm object is accessed by word offset (matching the original layout):
//   [0]  handler function pointer (0 == free)
//   [2]  fire-time hi (time-base high word)
//   [3]  fire-time lo
//   [4]  prev link
//   [5]  next link
//   [6]  period hi   (0 == one-shot)
//   [7]  period lo
//   [8]  start-time hi (for periodic reschedule)
//   [9]  start-time lo
//
// Also includes: the exception-handler-table getter (mirror of the batch_1097
// os_set_exception_handler), an HID2 feature-enable, a hardware-revision read,
// and the alarm-vector installer.
//
// Batch: batch_1098
// Source module: os/os_alarm.cpp

#include "common.h"

// OS globals.
extern void  *DAT_80613064;          // exception-handler table base
extern void  *DAT_80613078;          // alarm queue head
extern void  *puRam8061307c;         // alarm queue tail
extern int    DAT_80613078_i;        // (alias guard, unused)
extern int    DAT_80613078b;         // (alias guard, unused)
extern unsigned int DAT_80613078c;   // (alias guard, unused)
extern unsigned int uRam8061307c;    // tail as raw word
extern int    iRam8061307c;          // tail as int
extern void **puRam8061307c_pp;      // (alias guard, unused)
extern int    DAT_80613078_dec;      // (alias guard, unused)
extern unsigned int DAT_80613078_hi; // (alias guard, unused)
extern unsigned char DAT_80398db0;   // decrementer handler stub body
extern unsigned int uRam8061307c_v;  // (alias guard, unused)
extern int DAT_80613078_head;        // (alias guard, unused)
extern unsigned int DAT_cc006024;    // hardware revision register (low byte)
extern unsigned int DAT_806113bc;    // OS version format string ptr

// SPR intrinsics + timer.
extern unsigned int os_get_hid2(void);
extern void os_set_hid2(unsigned int);
extern void os_set_dec(unsigned int);
extern long long FUN_803a0c50(void);     // read 64-bit time base
extern void FUN_8039cca0(void);          // enter critical section (int save)
extern void FUN_8039ccc8(void);          // leave critical section (int restore)
extern void *os_set_exception_handler(unsigned int index, void *handler);
extern void FUN_8039daa8(int);           // enable decrementer interrupt
extern void FUN_803995e8(void);          // cache/HID sync helper
extern int  OSReport(int, ...);
extern long long __div2i(unsigned int, unsigned int, unsigned int, unsigned int);

// Decrementer-callback callees.
extern unsigned long long FUN_80399f14(unsigned int arg);   // dispatch/return-from-alarm
extern void FUN_8039f35c(void);
extern void FUN_8039a024(int frame);
extern void FUN_80399e2c(unsigned int arg);
extern void FUN_8039f39c(void);
extern void FUN_8039f868(void);

// Local helper (the DEC-reprogram idiom inlined at several sites in the original).
static void os_alarm_program_dec(unsigned int *head);

// ---------------------------------------------------------------------------
// os_get_exception_handler - read the handler installed for exception index n
// from the OS exception-handler table (DAT_80613064). Address: 0x803985a8
// ---------------------------------------------------------------------------
void *os_get_exception_handler(unsigned int index)
{
    return *(void **)((char *)DAT_80613064 + (index & 0xff) * 4);
}

// ---------------------------------------------------------------------------
// os_enable_hid2_features - set HID2 bits 0xa0000000 (locked-cache / write-pipe
// enable), run the cache-sync helper, then sync. Called from os_subsystem_init.
// Address: 0x803986b0
// ---------------------------------------------------------------------------
int os_enable_hid2_features(void)
{
    unsigned int hid2 = os_get_hid2();
    os_set_hid2(hid2 | 0xa0000000);
    FUN_803995e8();
    // sync
    return 0;
}

// ---------------------------------------------------------------------------
// os_get_hw_revision - read the low byte of the hardware revision register at
// 0xcc006024. Address: 0x80398704
// ---------------------------------------------------------------------------
unsigned int os_get_hw_revision(void)
{
    return DAT_cc006024 & 0xff;
}

// ---------------------------------------------------------------------------
// os_register_version - OSReport the OS version string (DAT_806113bc).
// Address: 0x80398718
// ---------------------------------------------------------------------------
void os_register_version(int fmt)
{
    OSReport(fmt, &DAT_806113bc);
}

// ---------------------------------------------------------------------------
// os_alarm_init - install the decrementer alarm exception handler on vector 8
// (once), clearing the alarm queue head/tail and enabling the DEC interrupt.
// Address: 0x80398744
// ---------------------------------------------------------------------------
void os_alarm_init(void)
{
    void *cur = os_get_exception_handler(8);
    if (cur != &DAT_80398db0) {
        uRam8061307c = 0;
        DAT_80613078 = 0;
        os_set_exception_handler(8, &DAT_80398db0);
        FUN_8039daa8((int)0x804982a0);   // enable decrementer interrupt (-0x7fb67d60)
    }
}

// ---------------------------------------------------------------------------
// os_alarm_clear - reset an alarm object (zero the handler and fire-time hi).
// Address: 0x8039879c
// ---------------------------------------------------------------------------
void os_alarm_clear(unsigned int *alarm)
{
    alarm[0] = 0;
    alarm[1] = 0;
}

// ---------------------------------------------------------------------------
// os_alarm_insert - core alarm insertion. For a periodic alarm (period != 0)
// whose start time is already in the past, advances the fire time by whole
// periods to the next future slot. Records the handler and fire time, links the
// alarm into the queue sorted by fire time, and when it becomes the head
// reprograms the decrementer to the delta until it fires (clamped to
// [0, 0x7fffffff]). Address: 0x803987ac
//   fire_hi/fire_lo : absolute 64-bit fire time; handler : callback.
// ---------------------------------------------------------------------------
void os_alarm_insert(unsigned int *alarm, unsigned int now_lo,
                     unsigned int fire_hi, unsigned int fire_lo, void *handler)
{
    unsigned int f_hi = fire_hi;
    unsigned int f_lo = fire_lo;

    // Periodic + start-time in the past: skip forward whole periods.
    if (0x80000000u < (unsigned int)(alarm[7] != 0) + (alarm[6] ^ 0x80000000)) {
        long long tb = FUN_803a0c50();
        unsigned int tb_hi = (unsigned int)((unsigned long long)tb >> 0x20);
        unsigned int tb_lo = (unsigned int)tb;
        f_hi = alarm[8];
        f_lo = alarm[9];
        if ((f_hi ^ 0x80000000) < (unsigned int)(f_lo < tb_lo) + (tb_hi ^ 0x80000000)) {
            unsigned int per_hi = alarm[6];
            unsigned int per_lo = alarm[7];
            long long n = __div2i(tb_hi - ((tb_lo < f_lo) + f_hi), tb_lo - f_lo, per_hi, per_lo);
            unsigned long long steps = (unsigned long long)n + 1;
            unsigned int add_lo = per_lo * (int)steps;
            bool carry = (f_lo + add_lo) < f_lo;
            f_lo = f_lo + add_lo;
            f_hi = f_hi + per_hi * (int)steps
                        + (int)((unsigned long long)per_lo * (steps & 0xffffffff) >> 0x20)
                        + per_lo * (int)(steps >> 0x20) + (unsigned int)carry;
        }
    }

    alarm[0] = (unsigned int)handler;
    alarm[3] = f_lo;
    alarm[2] = f_hi;

    unsigned int *cur = (unsigned int *)DAT_80613078;
    while (cur != (unsigned int *)0) {
        if ((f_hi ^ 0x80000000) < (unsigned int)(f_lo < cur[3]) + (cur[2] ^ 0x80000000)) {
            // insert before cur
            alarm[4] = cur[4];
            cur[4] = (unsigned int)alarm;
            alarm[5] = (unsigned int)cur;
            if (alarm[4] != 0) {
                *(unsigned int **)(alarm[4] + 0x14) = alarm;
                return;
            }
            DAT_80613078 = alarm;
            os_alarm_program_dec(alarm);
            return;
        }
        cur = (unsigned int *)cur[5];
    }

    // append at tail
    alarm[5] = 0;
    bool was_empty = (puRam8061307c == (void *)0);
    unsigned int *tail = (unsigned int *)puRam8061307c;
    alarm[4] = (unsigned int)puRam8061307c;
    puRam8061307c = alarm;
    if (was_empty) {
        DAT_80613078 = alarm;
        puRam8061307c = alarm;
        os_alarm_program_dec(alarm);
    } else {
        tail[5] = (unsigned int)alarm;   // link previous tail's next
    }
}

// ---------------------------------------------------------------------------
// os_alarm_program_dec - program the decrementer to the delta between the head
// alarm's fire time and now, clamped to [0, 0x7fffffff]. Address: inlined at
// several sites in os_alarm_insert / os_alarm_cancel / the DEC callback.
// ---------------------------------------------------------------------------
static void os_alarm_program_dec(unsigned int *head)
{
    long long tb = FUN_803a0c50();
    unsigned int delta_lo = head[3] - (unsigned int)tb;
    unsigned int delta_hi = head[2]
        - ((unsigned int)((unsigned int)head[3] < (unsigned int)tb)
           + (int)((unsigned long long)tb >> 0x20));
    delta_hi ^= 0x80000000;
    if (delta_hi < 0x80000000u) {
        os_set_dec(0);
    } else if (delta_hi < (delta_lo < 0x80000000u) + 0x80000000u) {
        os_set_dec(delta_lo);
    } else {
        os_set_dec(0x7fffffff);
    }
}

// ---------------------------------------------------------------------------
// OSSetAlarm - arm a one-shot alarm to fire 'period' ticks from now.
// Reads the current time base, adds the period, and inserts under an interrupt
// lock. Address: 0x803989fc
// ---------------------------------------------------------------------------
void os_set_alarm(unsigned int *alarm, unsigned int now_arg,
                  unsigned int period_hi, unsigned int period_lo, void *handler)
{
    FUN_8039cca0();
    alarm[7] = 0;                          // one-shot
    alarm[6] = 0;
    long long tb = FUN_803a0c50();
    long long fire = tb + (long long)(((unsigned long long)period_hi << 32) | period_lo);
    os_alarm_insert(alarm, (unsigned int)tb,
                    (unsigned int)((unsigned long long)fire >> 0x20),
                    (unsigned int)fire, handler);
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// OSCancelAlarm - remove an armed alarm from the queue. Relinks its neighbours,
// and if it was the head reprograms the decrementer for the new head (or
// disables it when the queue empties). Clears the handler. Address: 0x80398a64
// ---------------------------------------------------------------------------
void os_cancel_alarm(int *alarm)
{
    FUN_8039cca0();
    if (*alarm == 0) {
        FUN_8039ccc8();
        return;
    }
    int next = alarm[5];
    if (next == 0) {
        iRam8061307c = alarm[4];           // was tail
    } else {
        *(int *)(next + 0x10) = alarm[4];
    }
    if (alarm[4] == 0) {                    // was head
        DAT_80613078 = (void *)next;
        if (next != 0) {
            os_alarm_program_dec((unsigned int *)next);
        }
    } else {
        *(int *)(alarm[4] + 0x14) = next;
    }
    *alarm = 0;
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// os_decrementer_callback - the decrementer (vector 8) exception handler.
// Pops the head alarm, reprograms the decrementer for the next one, re-arms a
// periodic alarm, saves/restores FP context around the handler call, and
// dispatches the alarm's callback. Installed by os_alarm_init; invoked by
// hardware (no direct callers). Address: 0x80398b80
// ---------------------------------------------------------------------------
unsigned long long os_decrementer_callback(unsigned int ctx, unsigned int arg)
{
    unsigned char fp_frame[712];

    long long tb = FUN_803a0c50();
    unsigned int *head = (unsigned int *)DAT_80613078;
    if (head == (unsigned int *)0) {
        return FUN_80399f14(arg);
    }
    // If the head hasn't actually expired yet, reprogram and bail.
    if (((unsigned int)((unsigned long long)tb >> 0x20) ^ 0x80000000)
        < (unsigned int)((unsigned int)tb < head[3]) + (head[2] ^ 0x80000000)) {
        os_alarm_program_dec(head);
        return FUN_80399f14(arg);
    }

    // Pop the head.
    DAT_80613078 = (void *)head[5];
    if (DAT_80613078 == (void *)0) {
        uRam8061307c = 0;
    } else {
        *(unsigned int *)((int)DAT_80613078 + 0x10) = 0;
    }
    void (*handler)(unsigned int *, unsigned int) =
        (void (*)(unsigned int *, unsigned int))*head;
    *head = 0;

    // Re-arm if periodic.
    if (0x80000000u < (unsigned int)(head[7] != 0) + (head[6] ^ 0x80000000)) {
        os_alarm_insert(head, 0x80000000, 0, 0, (void *)handler);
    }

    // Reprogram DEC for the new head.
    if (DAT_80613078 != (void *)0) {
        os_alarm_program_dec((unsigned int *)DAT_80613078);
    }

    // Save FP context, call the handler, restore.
    FUN_8039f35c();
    FUN_8039a024((int)fp_frame);
    FUN_80399e2c((unsigned int)(unsigned long long)fp_frame);
    handler(head, arg);
    FUN_8039a024((int)fp_frame);
    FUN_80399e2c(arg);
    FUN_8039f39c();
    FUN_8039f868();
    return (unsigned long long)FUN_80399f14(arg);
}
