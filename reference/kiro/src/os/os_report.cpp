// Address: 0x8039a478 | Name: os_report (Dolphin OS diagnostic report + panic) | Date: 2026-09-14T22:55:00Z
//
// OSReport / OSPanic - the Dolphin OS diagnostic printf and fatal-error path.
// Both format through the MSL vprintf engine (msl_vprintf). OSReport is the
// workhorse used throughout the OS/exception code; OSPanic additionally prints the
// source file/line, dumps a stack backtrace, and halts.
//
// The register-heavy prologue in the original is the PowerPC varargs FP-spill: when
// CR1 bit 1 is set (variadic FP args present) the eight FP doubles are spilled to the
// caller frame before the integer va_list is assembled. That spill is an ABI detail
// handled by the C varargs machinery here, so these are written as ordinary
// (const char *fmt, ...) variadic functions over the MSL engine.

#include "common.h"
#include <cstdarg>

extern int  msl_vprintf(const char *fmt, va_list args);
extern int  FUN_8039cca0(void);        // OSDisableInterrupts
extern void os_halt(void);             // no-return
extern u32  os_get_stack_pointer(void);// 0x80399fec (OSGetStackPointer)

extern const char s_in___s__on_line__d__80498738[];
extern const char s_Address__Back_Chain_LR_Save_80498750[];
extern const char s_0x_08x__0x_08x_0x_08x_80498778[];

// os_report 0x8039a478  (OSReport)
// Formats fmt + args through the MSL vprintf engine to the debug console.
void OSReport(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    msl_vprintf(fmt, args);
    va_end(args);
}

// os_panic 0x8039a4f8  (OSPanic)
// Fatal error: disables interrupts, prints the caller-supplied message, then the
// "in <file> on line <n>" location and a stack backtrace, and halts (no return).
void OSPanic(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    u32    *fp;
    int     depth;

    FUN_8039cca0();                    // OSDisableInterrupts

    va_start(args, fmt);
    msl_vprintf(fmt, args);
    va_end(args);

    OSReport(s_in___s__on_line__d__80498738, file, line);
    OSReport(s_Address__Back_Chain_LR_Save_80498750);

    fp    = (u32 *)(uintptr_t)os_get_stack_pointer();
    depth = 0;
    while ((fp != 0) && (fp != (u32 *)(uintptr_t)0xffffffff) && (depth < 16)) {
        depth++;
        OSReport(s_0x_08x__0x_08x_0x_08x_80498778, fp, fp[0], fp[1]);
        fp = (u32 *)(uintptr_t)fp[0];
    }

    os_halt();                         // no return
}
