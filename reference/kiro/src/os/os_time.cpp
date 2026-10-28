// TSFP GameCube OS time / calendar + C++ runtime init (from main.dol, PPC/Gekko).
//
// Address: 0x803a0cb4 | Name: os_days_to_calendar         | Date: 2026-09-15
// Address: 0x803a0e50 | Name: os_ticks_to_calendar_time   | Date: 2026-09-15
// Address: 0x803a1054 | Name: os_call_static_initializers | Date: 2026-09-15
// Address: 0x803a1074 | Name: os_run_ctor_table           | Date: 2026-09-15
// Address: 0x803a10c8 | Name: os_exit_halt                | Date: 2026-09-15
//
// os_days_to_calendar / os_ticks_to_calendar_time are OSCalendarTime helpers.
// The calendar struct written by these routines (OSCalendarTime layout, int
// fields) is:
//   out[0]  sec      out[1]  min      out[2]  hour
//   out[3] (=+0xc)   mday    out[4] (=+0x10)  mon (0-11)
//   out[5] (=+0x14)  year    out[6] (=+0x18)  wday (0-6)
//   out[7] (=+0x1c)  yday    out[8] (=+0x20)  msec  out[9] (=+0x24) usec
// (os_days_to_calendar addresses the date block via byte offsets +0xc..+0x1c;
// os_ticks_to_calendar_time addresses the time block via int indices 0,1,2,8,9.)
//
// os_run_ctor_table (__init_cpp) runs the C++ static constructor list; the thin
// os_call_static_initializers wrapper is the SDK entry the CRT calls. os_exit_halt
// is the no-return tail of the exit path.
//
// NON-MATCHING: the original computes the day/date split with the compiler's
// signed div-by-constant idiom (mul-hi + shift) and reads month cumulative-day
// tables from 0x80499e48 (common year) / 0x80499e18 (leap year); here the same
// tables are inlined as literals and the divisions written directly. The
// tick->time conversion, which the decompiler emits as split-register
// __div2i/__mod2i pairs (a PPC 64-bit-emulation artifact), is expressed with
// native 64-bit arithmetic over the same divisor constants.

#include "common.h"

extern "C" {

// runtime-populated low-memory globals
extern unsigned int DAT_800000f8;   // bus clock in Hz; TB frequency = clock >> 2

// C++ static constructor table (null-terminated array of void(*)()).
extern void (*DAT_803cc080)(void);

// no-return CPU halt (defined in os_boot/os_reset).
extern void os_halt(void);

// Cumulative day-of-year at the START of each month (index 0 = January = 0),
// i.e. days elapsed before month m. The original indexes 12 entries backwards
// from a table base. Common year at 0x80499e48, leap year at 0x80499e18.
static const int kMonthYdayCommon[13] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
};
static const int kMonthYdayLeap[13] = {
    0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366
};

static bool is_leap_year(int year)
{
    if ((year % 4) != 0) return false;
    if ((year % 100) == 0 && (year % 400) != 0) return false;
    return true;
}

// __OSGetCalendarDate equivalent: decompose an absolute day count (days since
// the epoch year 0 used by the SDK) into year/month/day/day-of-week/day-of-year.
void os_days_to_calendar(int days, int *out)
{
    int year;
    long yearStartDays;
    int leapAdjust;
    const int *monthTab;
    int m;
    int dayOfYear;

    // Day of week: the epoch day is a known weekday; (days + 6) mod 7 gives it.
    out[6] = (days + 6) % 7;             // wday, out+0x18

    // Find the year whose day range contains `days`. yearStartDays counts the
    // days before year `year`, accounting for leap days via the /4 -/100 +/400
    // rule (leapAdjust = leap days in [0, year)).
    year = days / 365;
    for (;;) {
        yearStartDays = (long)year * 365;
        if (year < 1) {
            leapAdjust = 0;
        } else {
            leapAdjust = ((year - 1) / 4) - ((year - 1) / 100) + ((year + 3) / 400);
        }
        if (yearStartDays + leapAdjust <= days) break;
        year = year - 1;
    }

    out[5] = year;                       // year, out+0x14
    dayOfYear = days - (int)(yearStartDays + leapAdjust);
    out[7] = dayOfYear;                  // yday, out+0x1c

    monthTab = is_leap_year(year) ? kMonthYdayLeap : kMonthYdayCommon;

    // Walk months high->low to find the month containing dayOfYear.
    m = 12;
    do {
        m = m - 1;
    } while (dayOfYear < monthTab[m]);
    out[4] = m;                          // mon (0-11), out+0x10
    out[3] = (dayOfYear - monthTab[m]) + 1;   // mday (1-based), out+0xc
}

// OSTicksToCalendarTime: convert a 64-bit OS tick count into a full calendar
// time. TB frequency (ticks/sec) = busClock >> 2 (DAT_800000f8 >> 2).
void os_ticks_to_calendar_time(unsigned int ticksHi, unsigned int ticksLo, int *out)
{
    unsigned long long ticks = ((unsigned long long)ticksHi << 32) | ticksLo;
    unsigned int clock = DAT_800000f8;
    unsigned int tbFreq = clock >> 2;               // ticks per second

    // Sub-second fields (matching the original divisor constants).
    //   usec = (ticks / (clock/4000))  % 1000
    //   msec = (ticks*8 / (clock/500000)) % 1000
    unsigned int usecDiv = clock / 4000;
    unsigned int msecDiv = clock / 500000;
    out[9] = (int)(((ticks * 8ULL) / (unsigned long long)msecDiv) % 1000ULL);   // msec
    out[8] = (int)((ticks / (unsigned long long)usecDiv) % 1000ULL);            // usec

    // Whole seconds since the epoch, then split into days + seconds-of-day.
    // 0xb2575 (730613) is the SDK's day offset from year 0 to the 1970 epoch;
    // 0x15180 (86400) seconds per day.
    unsigned long long totalSecs = ticks / (unsigned long long)tbFreq;
    long long days = (long long)(totalSecs / 86400ULL) + 0xb2575;
    long long secOfDay = (long long)(totalSecs % 86400ULL);
    if (secOfDay < 0) {
        days = days - 1;
        secOfDay = secOfDay + 86400;
    }

    os_days_to_calendar((int)days, out);

    // Hours / minutes / seconds from seconds-of-day.
    int s = (int)secOfDay;
    out[2] = (s / 3600);            // hour
    out[1] = (s / 60) % 60;         // min
    out[0] = s % 60;                // sec
}

// __init_cpp: invoke every C++ static constructor in the null-terminated table.
void os_run_ctor_table(void)
{
    void (**ctor)(void) = &DAT_803cc080;
    for (; *ctor != (void (*)(void))0; ctor = ctor + 1) {
        (**ctor)();
    }
}

// SDK entry the CRT calls to run global constructors.
void os_call_static_initializers(void)
{
    os_run_ctor_table();
}

// No-return exit tail: stop the CPU.
void os_exit_halt(void)
{
    os_halt();
}

// ============================================================================
// batch_1124: OS time-base read.
//
// Address: 0x803a0c48 | Name: os_get_time_base | Date: 2026-09-15
//
// os_get_time_base is the one-instruction leaf `mftb r3, TBL; blr` - it reads
// the 32-bit lower half of the PowerPC time-base register (the free-running
// bus-clock tick counter) and returns it. It is the primitive underneath
// OSGetTime; the TB frequency is the bus clock (DAT_800000f8) >> 2.
//
// NON-MATCHING: there is no `mftb` on the x86 host, so the raw register read is
// expressed through the platform time-base shim.
// ============================================================================
extern unsigned int platform_read_time_base_lower(void);

unsigned int os_get_time_base(void)
{
    return platform_read_time_base_lower();
}

} // extern "C"
