// os_reset.cpp - Dolphin OS system-reset / reboot subsystem (OSReset)
//
// Address: 0x8039da88 | Name: os_set_reset_callback     | Date: 2026-09-14
// Address: 0x8039da94 | Name: os_get_reset_callback     | Date: 2026-09-14
// Address: 0x8039daa8 | Name: os_register_reset_function| Date: 2026-09-14
// Address: 0x8039db2c | Name: os_call_reset_functions   | Date: 2026-09-14
// Address: 0x8039dbd8 | Name: os_reboot_sync            | Date: 2026-09-14
// Address: 0x8039dbf8 | Name: os_reboot_halt            | Date: 2026-09-14
// Address: 0x8039dc18 | Name: os_hard_reset             | Date: 2026-09-14
// Address: 0x8039dc38 | Name: os_halt_loop              | Date: 2026-09-14
// Address: 0x8039dc44 | Name: os_resume_reset_threads   | Date: 2026-09-14
// Address: 0x8039dcac | Name: os_shutdown_vi_reboot     | Date: 2026-09-14
//
// The OSReset layer drives graceful shutdown and hardware reset/reboot. Drivers
// register a reset function (a callback + a priority key) via
// os_register_reset_function; the list is kept sorted ascending by priority.
// At reset time OSResetSystem calls os_call_reset_functions, which walks the
// sorted list invoking each callback in two phases (query pass with arg 0, then
// the commit pass with arg 1) and folds the boolean results, finishing with the
// terminal shutdown helper FUN_8039e9a8 (returns "all quiesced"). Once all
// drivers are quiesced the reboot leg runs: os_resume_reset_threads wakes the
// threads that were parked for reset, os_shutdown_vi_reboot blanks VI and enters
// the reboot, and os_hard_reset pokes the PI reset register (0xcc003024) to
// physically reset the machine. os_reboot_sync / os_reboot_halt / os_halt_loop
// are the no-return tails.
//
// NON-MATCHING: the intrusive-list pointer arithmetic and the PI/VI register
// writes are transcribed verbatim from the decompiler; the OS interrupt/cache
// primitives resolve through per-file externs with the signatures used here.

#include "common.h"

typedef void code;

extern "C" {

// --- reset-callback storage (set/get pair) ------------------------------------
extern void *DAT_806130c8;   // reset callback function pointer
extern void *DAT_806130cc;   // reset callback user context

// --- registered-reset-function intrusive list --------------------------------
//   node layout: +0x00 callback, +0x04 priority key (u32),
//                +0x08 next, +0x0c prev
extern int   DAT_806130d0;   // head of sorted reset-function list
extern int   iRam806130d4;   // tail of sorted reset-function list

// --- reset/reboot control state -----------------------------------------------
extern int   DAT_806130d8;   // reboot-image / boot-info pointer

// --- hardware registers -------------------------------------------------------
extern volatile unsigned int   DAT_cc003024;  // PI reset control register
extern volatile unsigned short DAT_cc002002;  // VI display-config register

// --- kernel thread queue ------------------------------------------------------
extern int   DAT_800000dc;   // head of the active thread queue

// --- OS primitives used here --------------------------------------------------
extern int  os_disable_interrupts(void);
extern unsigned int os_enable_write_gather_hid0(void);
extern void instructionSynchronize(void);
extern void sync(int);
// countLeadingZeros is a Ghidra intrinsic provided by common.h (do not redeclare).

// terminal shutdown poll: returns nonzero once every driver has quiesced.
extern int FUN_8039e9a8(void);
// resume one thread that was suspended for reset.
extern void FUN_8039fb64(int thread);

// forward decls within this file
void os_reboot_sync(unsigned int bootFlag);
void os_reboot_halt(unsigned int bootFlag);
void os_hard_reset(unsigned int resetCode);
void os_halt_loop(void);
int  os_resume_reset_threads(void);
// os_reboot_sync is reached through a thunk in the resolved image.
extern void thunk_FUN_8039dbd8(unsigned int bootFlag);


// Install the system-wide reset callback (function + user context).
void os_set_reset_callback(void *callback, void *context)
{
    DAT_806130c8 = callback;
    DAT_806130cc = context;
}

// Read back the currently installed reset callback / context pair.
void os_get_reset_callback(void **outCallback, void **outContext)
{
    *outCallback = DAT_806130c8;
    *outContext  = DAT_806130cc;
}

// OSRegisterResetFunction: insert `node` into the reset-function list keeping it
// sorted ascending by the priority key at +0x04. Falls back to appending at the
// tail when the node sorts after every existing entry.
void os_register_reset_function(int node)
{
    int cur;
    int prev;

    // find the first entry whose priority is strictly greater than ours.
    for (cur = DAT_806130d0;
         (cur != 0 && (*(unsigned int *)(cur + 4) <= *(unsigned int *)(node + 4)));
         cur = *(int *)(cur + 8)) {
    }

    if (cur != 0) {
        // insert `node` immediately before `cur`.
        *(int *)(node + 8) = cur;
        prev = *(int *)(cur + 0xc);
        *(int *)(cur + 0xc) = node;
        *(int *)(node + 0xc) = prev;
        if (prev != 0) {
            *(int *)(prev + 8) = node;
            return;
        }
        DAT_806130d0 = node;
        return;
    }

    // sorts after everything: append at the tail.
    cur = node;
    if (iRam806130d4 != 0) {
        *(int *)(iRam806130d4 + 8) = node;
        cur = DAT_806130d0;
    }
    DAT_806130d0 = cur;
    *(int *)(node + 0xc) = iRam806130d4;
    *(int *)(node + 8) = 0;
    iRam806130d4 = node;
}

// __OSCallResetFunctions: invoke every registered reset callback in priority
// order. `pass` is forwarded to each callback (0 = query, 1 = commit). The loop
// runs callbacks that share the current priority key together and stops early if
// any callback vetoes (returns 0). Finishes by polling the terminal shutdown
// helper. Returns true when all callbacks and the terminal poll succeeded.
bool os_call_reset_functions(unsigned int pass)
{
    int *prioSlot;
    unsigned int cleared;
    int prio;
    unsigned int veto;
    int result;
    int *node;

    veto = 0;
    prio = 0;
    node = (int *)DAT_806130d0;
    while ((node != (int *)0) && ((veto == 0) || (prio == node[1]))) {
        result = ((int (*)(unsigned int))(*(code **)node))(pass);
        cleared = (unsigned int)countLeadingZeros((unsigned int)result);
        prioSlot = node + 1;
        node = (int *)node[2];
        veto = veto | (cleared >> 5);
        prio = *prioSlot;
    }

    result = FUN_8039e9a8();
    cleared = (unsigned int)countLeadingZeros((unsigned int)result);
    return veto == 0 && (cleared >> 5) == 0;
}

// Barrier then enter the reboot tail. isync + sync fence all outstanding stores
// (VI/PI register writes) before the machine reboots into `bootFlag`.
void os_reboot_sync(unsigned int bootFlag)
{
    instructionSynchronize();
    sync(0);
    os_reboot_halt(bootFlag);
}

// The reboot tail: spins forever while the hardware reset takes effect.
void os_reboot_halt(unsigned int /*bootFlag*/)
{
    for (;;) {
    }
}

// __OSReboot: trigger a physical machine reset through the PI reset register,
// then halt. Writing 3 arms the reset, the second write latches `resetCode`.
void os_hard_reset(unsigned int resetCode)
{
    DAT_cc003024 = 3;
    DAT_cc003024 = resetCode;
    os_halt_loop();
}

// Dead-end halt loop used after os_hard_reset arms the PI reset.
void os_halt_loop(void)
{
    for (;;) {
    }
}

// Wake the threads that were parked while the system quiesced for reset: walk the
// active thread queue and resume any thread whose state word (+0x2c8) is READY(1)
// or the reset-parked marker(4).
int os_resume_reset_threads(void)
{
    unsigned short state;
    int cur;
    int next;

    next = DAT_800000dc;
    while (cur = next, cur != 0) {
        state = *(unsigned short *)(cur + 0x2c8);
        next = *(int *)(cur + 0x2fc);
        if ((state == 4) || ((state < 4) && (state == 1))) {
            FUN_8039fb64(cur);
        }
    }
    return next;
}

// OSResetSystem reboot leg: blank the VI output, re-enable the write-gather pipe,
// and enter the reboot with the boot flag (index shifted into the reboot code).
void os_shutdown_vi_reboot(int bootIndex)
{
    os_disable_interrupts();
    DAT_cc002002 = 0;
    os_enable_write_gather_hid0();
    thunk_FUN_8039dbd8((unsigned int)(bootIndex << 3));
}

} // extern "C"

// ---------------------------------------------------------------------------
// OSResetSystem entry point + the reset-button (SI/PI) poll and debounce alarm.
//
// Address: 0x8039dcf4 | Name: os_reset_system         | Date: 2026-09-14
// Address: 0x8039def4 | Name: os_get_reset_code       | Date: 2026-09-14
// Address: 0x8039df2c | Name: os_reset_button_poll    | Date: 2026-09-14
// Address: 0x8039e020 | Name: os_get_reset_button_state | Date: 2026-09-14
//
// os_reset_system is the public OSResetSystem: it quiesces every registered
// driver (two-pass os_call_reset_functions), halts the DSP, disables the locked
// cache, resumes reset-parked threads, then either reboots (reset==1) or hands
// off to the DVD apploader (reset==0 / RESTART). os_get_reset_code reads the
// latched reset code (from the PI reset register, or a debug override). The
// reset-button subsystem polls the PI interrupt-cause reset bit (0xcc003000 bit
// 0x10000) with a debounce window derived from the bus clock DAT_800000f8.
// ---------------------------------------------------------------------------

typedef long long s64;

extern "C" {

// reset-code / reset-button state
extern int   DAT_805f26f0;   // reset-code debug-override enable flag
extern unsigned int DAT_805f26f4;  // reset-code debug-override value
extern unsigned int DAT_800000f8;  // bus clock (Hz), used for debounce timing

// reset-button debounce state (all in the OSReset BSS window at 0x806130xx)
extern int   DAT_806130e0;   // registered reset-button callback (code*)
extern int   DAT_806130e4;   // "held" edge flag
extern unsigned int DAT_806130e8;   // debounced button-down state
extern int   DAT_806130f0;   // press-timestamp hi
extern int   DAT_806130f4;   // press-timestamp lo
extern int   DAT_806130f8;   // last-sample-timestamp hi
extern int   DAT_806130fc;   // last-sample-timestamp lo
extern unsigned char DAT_800030e3;  // low-mem console reset-hold config byte
extern int   DAT_80613070;   // extended-hold accumulator base hi
extern unsigned int DAT_80613074;  // extended-hold accumulator base lo

// PI interrupt-cause register (bit 0x10000 = reset switch asserted)
extern volatile unsigned int DAT_cc003000;

// helpers used by the reset path (resolved through the shared headers / externs)
extern long long FUN_803a0c50(void);            // OSGetTime (64-bit tick)
extern void  os_dsp_halt(void);
extern int   FUN_803a97c0(int enable);          // DSP/ARAM refresh gate toggle
extern int   os_lc_disable(void);
extern int   os_get_interrupts_enabled(void);
extern void  os_mask_interrupts(unsigned int mask);
extern int   FUN_8039f35c(void);                // OSReset prologue (report/hook)
extern int   FUN_8039f39c(void);                // OSReset pre-DVD-handoff hook
extern void *FUN_8039e56c(void);                // __OSLockSram (SRAM RTC/config)
extern void  FUN_8039e960(int);                 // mark SRAM field dirty
extern long long __div2i(int, int, int, int);   // MSL 64-bit signed divide
extern void  os_run_from_dvd(unsigned int, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned int, int,
                             int, unsigned int, unsigned int, unsigned int, unsigned int,
                             unsigned int);
// OSReport is the canonical varargs diagnostic printf (declared in the shared
// headers as void OSReport(const char*, ...)); do not redeclare it here.
extern void  OSReport(const char *, ...);
extern void  memset_wrapper(int, int, int);
extern const char s_OSResetSystem____You_can_t_speci_804995b8[];

// OSResetSystem. reset: 0 = restart (re-run apploader), 1 = hard reboot,
// 2 = shutdown/standby. resetCode/forceMenu carry the reboot target. The tail
// clears the low-memory OS globals before the machine restarts.
void os_reset_system(unsigned long long a2, unsigned long long a3, unsigned long long a4,
                     unsigned long long a5, unsigned long long a6, unsigned long long a7,
                     unsigned long long a8, int reset, unsigned int resetCode,
                     int forceMenu, unsigned int v12, unsigned int v13, unsigned int v14,
                     unsigned int v15, unsigned int v16)
{
    unsigned char *sram;
    bool refreshGate;
    bool quiesced;
    int poll;
    int savedForce;
    unsigned long long ret;

    savedForce = forceMenu;
    FUN_8039f35c();

    // reboot with an explicit reset code: mark the SRAM boot flag and clear the
    // reboot-target so the machine returns to the system menu.
    if ((reset == 1) && (forceMenu != 0)) {
        sram = (unsigned char *)FUN_8039e56c();
        sram[0x13] = sram[0x13] | 0x40;
        FUN_8039e960(1);
        resetCode = 0;
    }

    // quiesce phase: run reset callbacks (query until all agree, commit under an
    // interrupt lock), gate ARAM refresh on the shutdown/menu path.
    if ((reset == 2) || ((reset == 0) && ((DAT_806130d8 != 0) || (resetCode == 0xc0010000)))) {
        os_dsp_halt();
        refreshGate = FUN_803a97c0(1);
        do { quiesced = os_call_reset_functions(0); } while (!quiesced);
        do { poll = FUN_8039e9a8(); } while (poll == 0);
        os_disable_interrupts();
        os_call_reset_functions(1);
        os_lc_disable();
        FUN_803a97c0((int)refreshGate);
        ret = (unsigned long long)(unsigned int)os_resume_reset_threads();
    }
    else {
        os_dsp_halt();
        do { quiesced = os_call_reset_functions(0); } while (!quiesced);
        do { poll = FUN_8039e9a8(); } while (poll == 0);
        os_disable_interrupts();
        os_call_reset_functions(1);
        os_lc_disable();
        ret = (unsigned long long)(unsigned int)os_resume_reset_threads();
    }

    if (reset == 1) {
        // hard reboot leg (same as os_shutdown_vi_reboot, inlined here).
        os_disable_interrupts();
        DAT_cc002002 = 0;
        os_enable_write_gather_hid0();
        os_reboot_sync(resetCode << 3);
    }
    else if (reset == 0) {
        // restart leg: hand off to the DVD apploader / system menu.
        if (forceMenu == 1) {
            // invalid-parameter diagnostic (OSResetSystem: You can't specify ...).
            OSReport(s_OSResetSystem____You_can_t_speci_804995b8, savedForce);
        }
        FUN_8039f39c();
        os_run_from_dvd((unsigned int)ret, a2, a3, a4, a5, a6, a7, a8, resetCode,
                        DAT_806130d8, savedForce, v12, v13, v14, v15, v16);
    }

    // wipe the low-memory OS global blocks before the machine actually resets.
    memset_wrapper(-0x7fffffc0, 0, 0x8c);
    memset_wrapper(-0x7fffff2c, 0, 0x14);
    memset_wrapper(-0x7fffff0c, 0, 4);
    memset_wrapper(-0x7fffd000, 0, 0xc0);
    memset_wrapper(-0x7fffcf38, 0, 0xc);
    memset_wrapper(-0x7fffcf1e, 0, 1);
}

// OSGetResetCode: return the latched reset code. Normally read from the PI reset
// register (0xcc003024 >> 3); a debug override (DAT_805f26f0) forces a value with
// the top bit set.
unsigned int os_get_reset_code(void)
{
    unsigned int code;

    if (DAT_805f26f0 == 0) {
        code = DAT_cc003024;
        code = code >> 3;
    }
    else {
        code = DAT_805f26f4 | 0x80000000;
    }
    return code;
}

// Reset-button poll installed by os_kernel_init: waits (bounded by a debounce
// window scaled from the bus clock) for the PI reset-cause bit (0x10000) to clear,
// and if the button is still held fires the registered reset-button callback once.
void os_reset_button_poll(void)
{
    unsigned int window;
    unsigned int cause;
    void (*cb)(void);
    s64 now;
    s64 sample;

    now = FUN_803a0c50();
    window = DAT_800000f8 / 500000;
    do {
        DAT_806130f8 = (int)((unsigned long long)now >> 0x20);
        DAT_806130fc = (unsigned int)now;
        sample = FUN_803a0c50();
        now = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
        if (((unsigned int)((unsigned int)sample - DAT_806130fc < ((window * 100) >> 3)) + 0x80000000u)
            <= (unsigned int)(((int)((unsigned long long)sample >> 0x20)
                - ((unsigned int)((unsigned int)sample < (unsigned int)DAT_806130fc) + DAT_806130f8)) ^ 0x80000000)) {
            break;
        }
        cause = DAT_cc003000;
    } while ((cause & 0x10000) == 0);

    window = DAT_cc003000;
    if ((window & 0x10000) == 0) {
        DAT_806130e4 = 1;
        DAT_806130e8 = 1;
        os_mask_interrupts(0x200);
        cb = (void (*)(void))DAT_806130e0;
        if (cb != (void (*)(void))0) {
            DAT_806130e0 = 0;
            cb();
        }
    }
    DAT_cc003000 = 2;
}

// OSGetResetButtonState: debounced read of the reset button. Samples the PI
// reset-cause bit and applies the press/hold debounce timing (windows scaled from
// the bus clock DAT_800000f8 and the console reset-hold config DAT_800030e3),
// returning the debounced down/up state.
unsigned int os_get_reset_button_state(void)
{
    unsigned int busDiv;
    unsigned char pressed;
    s64 now;
    s64 pressTs;
    unsigned int nowHi;
    unsigned int nowLo;
    unsigned int cause;
    unsigned int state;
    s64 sampleTs;
    unsigned long long q0;
    unsigned long long q1;

    os_disable_interrupts();
    now = FUN_803a0c50();
    sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
    nowHi = (unsigned int)((unsigned long long)now >> 0x20);
    nowLo = (unsigned int)now;
    cause = DAT_cc003000;
    if ((cause & 0x10000) == 0) {
        if (DAT_806130e4 == 0) {
            DAT_806130e4 = 1;
            state = (unsigned int)(DAT_806130f4 != 0 || DAT_806130f0 != 0);
            pressTs = (s64)CONCAT44(DAT_806130f0, DAT_806130f4);
            sampleTs = now;
        }
        else {
            pressed = 1;
            if ((DAT_806130f4 == 0 && DAT_806130f0 == 0) &&
                ((unsigned int)(((DAT_800000f8 / 500000) * 100 >> 3) < (nowLo - (unsigned int)DAT_806130fc))
                 + ((nowHi - ((unsigned int)(nowLo < (unsigned int)DAT_806130fc) + DAT_806130f8)) ^ 0x80000000)
                 < 0x80000001u)) {
                pressed = 0;
            }
            state = (unsigned int)pressed;
            pressTs = (s64)CONCAT44(DAT_806130f0, DAT_806130f4);
            sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
        }
    }
    else if (DAT_806130e4 == 0) {
        if ((DAT_806130f4 == 0 && DAT_806130f0 == 0) ||
            (((unsigned int)(nowLo - (unsigned int)DAT_806130f4 < (DAT_800000f8 / 4000) * 0x28) + 0x80000000u)
             <= (unsigned int)((nowHi - ((unsigned int)(nowLo < (unsigned int)DAT_806130f4) + DAT_806130f0)) ^ 0x80000000))) {
            state = 0;
            pressTs = 0;
            sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
        }
        else {
            state = 1;
            pressTs = (s64)CONCAT44(DAT_806130f0, DAT_806130f4);
            sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
        }
    }
    else {
        DAT_806130e4 = 0;
        state = DAT_806130e8;
        pressTs = now;
        if (DAT_806130e8 == 0) {
            pressTs = 0;
            sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
        }
    }
    DAT_806130f8 = (int)((unsigned long long)sampleTs >> 0x20);
    DAT_806130fc = (unsigned int)sampleTs;
    DAT_806130f0 = (int)((unsigned long long)pressTs >> 0x20);
    DAT_806130f4 = (unsigned int)pressTs;
    DAT_806130e8 = state;

    // extended hold detection using the console reset-hold config nibble.
    if ((DAT_800030e3 & 0x1f) != 0) {
        unsigned int holdSecs = (DAT_800030e3 & 0x1f) * 0x3c;
        busDiv = DAT_800000f8 >> 2;
        unsigned int prodLo = holdSecs * busDiv;
        unsigned int sumLo = DAT_80613074 + prodLo;
        unsigned int sumHi = DAT_80613070
            + (int)((unsigned long long)holdSecs * (unsigned long long)busDiv >> 0x20)
            + (unsigned int)CARRY4(DAT_80613074, prodLo);
        if ((sumHi ^ 0x80000000) < (unsigned int)(sumLo < nowLo) + (nowHi ^ 0x80000000)) {
            q0 = (unsigned long long)__div2i(nowHi - ((nowLo < sumLo) + sumHi), nowLo - sumLo, 0, busDiv);
            q1 = (unsigned long long)__div2i((unsigned int)(q0 >> 0x20), (unsigned int)q0, 0, 2);
            sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
            pressTs = (s64)CONCAT44(DAT_806130f0, DAT_806130f4);
            if ((q1 & 1) == 0) {
                state = 1;
                pressTs = (s64)CONCAT44(DAT_806130f0, DAT_806130f4);
                sampleTs = (s64)CONCAT44(DAT_806130f8, DAT_806130fc);
            }
            else {
                state = 0;
            }
        }
    }
    DAT_806130f8 = (int)((unsigned long long)sampleTs >> 0x20);
    DAT_806130fc = (unsigned int)sampleTs;
    DAT_806130f0 = (int)((unsigned long long)pressTs >> 0x20);
    DAT_806130f4 = (unsigned int)pressTs;
    os_get_interrupts_enabled();
    return state;
}

} // extern "C"
