// Batch 349 | Date: 2026-09-04
// CRC32 hashing + wall-clock timestamp helpers.
//
//   crc32_buffer            (0x8012dfbc) reflected CRC32 over a byte buffer
//   crc32_string            (0x8012e000) CRC32 of a NUL-terminated string
//   crc32_buffer_matches    (0x8012e038) equality test vs an expected CRC
//   hud_clock_get_hms       (0x8012deb4) read current clock h/m/s
//   clock_seconds_of_day    (0x8012ded0) seconds-since-midnight from time-of-day
//   clock_format_timestamp_hex (0x8012df08) format a timer value as "xx:xx:.."
//
// The CRC32 uses the standard reflected polynomial (0xEDB88320) via the 256-entry
// table at DAT_8040d388, with init/final value 0xffffffff. The clock helpers read
// the counters seeded by hud_clock_* (see hud_clock.cpp).
//
// Subsystem: game (core utilities). Compile-Clean Standard: faithful bodies +
// per-file externs for raw symbols.

#include "common.h"

// ---- CRC table + clock counters ----
extern unsigned int DAT_8040d388;  // 256-entry CRC32 lookup table (via &DAT)

extern int DAT_8061200c;   // clock hours
extern int DAT_80612008;   // clock minutes
extern int DAT_80612004;   // clock seconds
extern int DAT_80612240;   // timestamp field-order / format flag
extern int DAT_800000f8;   // platform timer bus frequency

// ---- static timestamp buffers / format strings ----
extern int s_xx_xx_xx_xx_xx_xx_8040d348;             // "xx:xx:xx:xx:xx:xx" scratch
extern int s_Invalid_806108d0;                        // "Invalid"
extern int s___2x___2x___2x___2x___2x___2x_8040d368;  // "%02x:%02x:%02x:%02x:%02x:%02x"

// ---- raw helpers ----
extern int  FUN_8038b5c8(int str);                    // strlen
extern void FUN_8038b510(int* dst, int* src);         // strcpy
extern unsigned long long FUN_803a0c30(void);         // read raw platform time (ticks)
extern unsigned long long FUN_80382838(unsigned int hi, unsigned int lo, unsigned int rem_hi,
                                       unsigned int div);  // 64-bit divide
extern unsigned long long FUN_80387fb8(                     // sprintf-style formatter
        unsigned long long acc, double, double, double, double, double, unsigned long long,
        unsigned long long, int fmt_lo, int fmt, unsigned int a, unsigned int b, unsigned int c,
        unsigned int d, unsigned int e, unsigned int f);

extern unsigned long long hud_clock_write_bcd_buffer(int out_buf, unsigned int value);

// ----------------------------------------------------------------------------
// crc32_buffer (0x8012dfbc)
// ----------------------------------------------------------------------------
unsigned int crc32_buffer(unsigned char* param_1, int param_2)
{
    unsigned int uVar2 = 0xffffffff;
    for (; param_2 != 0; param_2 = param_2 - 1) {
        unsigned char bVar1 = *param_1;
        param_1 = param_1 + 1;
        uVar2 = (uVar2 >> 8) ^ (&DAT_8040d388)[(uVar2 ^ bVar1) & 0xff];
    }
    return uVar2 ^ 0xffffffff;
}

// ----------------------------------------------------------------------------
// crc32_string (0x8012e000)
// ----------------------------------------------------------------------------
void crc32_string(unsigned char* param_1)
{
    int len = FUN_8038b5c8((int)param_1);
    crc32_buffer(param_1, len);
}

// ----------------------------------------------------------------------------
// crc32_buffer_matches (0x8012e038)
// Returns 1 if crc32_buffer(buf,len) == expected, else 0 (branchless CLZ trick).
// ----------------------------------------------------------------------------
unsigned int crc32_buffer_matches(unsigned char* param_1, int param_2, int param_3)
{
    unsigned int uVar1 = crc32_buffer(param_1, param_2);
    uVar1 = countLeadingZeros(uVar1 - (unsigned int)param_3);
    return uVar1 >> 5;
}

// ----------------------------------------------------------------------------
// hud_clock_get_hms (0x8012deb4)
// ----------------------------------------------------------------------------
void hud_clock_get_hms(int* param_1, int* param_2, int* param_3)
{
    *param_1 = DAT_8061200c;  // hours
    *param_2 = DAT_80612008;  // minutes
    *param_3 = DAT_80612004;  // seconds
}

// ----------------------------------------------------------------------------
// clock_seconds_of_day (0x8012ded0)
// ----------------------------------------------------------------------------
unsigned int clock_seconds_of_day(void)
{
    unsigned long long t = FUN_803a0c30();
    unsigned long long r = FUN_80382838((unsigned int)(t >> 0x20), (unsigned int)t, 0,
                                        (unsigned int)DAT_800000f8 >> 2);
    return (unsigned int)r;
}

// ----------------------------------------------------------------------------
// clock_format_timestamp_hex (0x8012df08)
// Formats a timer value as "xx:xx:xx:xx:xx:xx" into the static scratch buffer.
// ----------------------------------------------------------------------------
char* clock_format_timestamp_hex(unsigned long long param_1, double param_2, double param_3,
                                 double param_4, double param_5, double param_6,
                                 unsigned long long param_7, unsigned long long param_8,
                                 unsigned int param_9)
{
    unsigned char buf[8];  // BCD scratch: [1]=hh [2]=mm [3]=ss ... written by helper

    if (param_9 == 0xffffffff) {
        FUN_8038b510(&s_xx_xx_xx_xx_xx_xx_8040d348, &s_Invalid_806108d0);
    } else {
        unsigned long long acc = hud_clock_write_bcd_buffer((int)buf, param_9);
        if (DAT_80612240 == 0) {
            FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                    (int)&s_xx_xx_xx_xx_xx_xx_8040d348, (int)&s___2x___2x___2x___2x___2x___2x_8040d368,
                    buf[3], buf[2], buf[1], buf[5], buf[6], buf[7]);
        } else {
            FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                    (int)&s_xx_xx_xx_xx_xx_xx_8040d348, (int)&s___2x___2x___2x___2x___2x___2x_8040d368,
                    buf[2], buf[3], buf[1], buf[5], buf[6], buf[7]);
        }
    }
    return (char*)&s_xx_xx_xx_xx_xx_xx_8040d348;
}
