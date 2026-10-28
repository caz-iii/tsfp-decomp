// os_sram.cpp - Dolphin OS SRAM / RTC-config shadow over EXI (OSSram)
//
// Address: 0x8039e2b8 | Name: os_sram_flush          | Date: 2026-09-14
// Address: 0x8039e318 | Name: os_sram_write_range    | Date: 2026-09-14
// Address: 0x8039e430 | Name: os_sram_init           | Date: 2026-09-14
// Address: 0x8039e56c | Name: os_sram_lock           | Date: 2026-09-14
// Address: 0x8039e5c8 | Name: os_sram_lock_ex        | Date: 2026-09-14
// Address: 0x8039e624 | Name: os_sram_unlock         | Date: 2026-09-14
//
// The console SRAM holds the RTC bias, display/sound config, language, and flash
// board ID. The OS keeps a 0x40-byte RAM shadow (DAT_805f2a40) plus a dirty
// offset (DAT_805f2a80: first byte not yet written back; 0x40 = clean). The block
// is transferred over EXI channel 0 to the RTC/SRAM chip:
//   FUN_803b9fb8 = EXILock, FUN_803b97f8 = EXISelect(freq 3), FUN_803b8c90 = EXIImm
//   (command word), FUN_803b8eec/FUN_803b8f8c = EXIImmEx/EXIDma (payload),
//   FUN_803b9078 = EXISync, FUN_803b9924 = EXIDeselect, FUN_803ba0ac = EXIUnlock.
// Each EXI call returns a bool; the code folds failures via countLeadingZeros so
// the final result is 1 only if every step succeeded.
//
// os_sram_lock / os_sram_lock_ex disable interrupts and hand out a pointer into
// the shadow (config region / flash-ID region), guarding against re-entry with
// DAT_805f2a88. os_sram_unlock recomputes the two checksum halfwords, then flushes
// the dirty tail back to the chip and re-enables interrupts. os_sram_init reads
// the whole block once at boot (called from os_kernel_init).
//
// NON-MATCHING: the EXI transaction sequence and checksum walk are transcribed
// verbatim; the EXI driver entry points resolve through the shared headers.

#include "common.h"

extern "C" {

// --- SRAM RAM shadow + bookkeeping (all in the OSSram BSS block) --------------
extern unsigned short DAT_805f2a40;  // shadow base (checksum lo lives here)
extern unsigned short DAT_805f2a42;  // checksum hi
extern unsigned short DAT_805f2a4c;  // first config halfword (checksum starts here)
extern unsigned char  DAT_805f2a52;  // flags byte (boot mode bits)
extern unsigned short DAT_805f2a54;  // flash-ID / ex region base
extern unsigned short DAT_805f2a7c;  // combined config flags
extern int   DAT_805f2a80;   // dirty offset (0x40 = clean)
extern int   DAT_805f2a84;   // saved interrupt-enable state across lock
extern int   DAT_805f2a88;   // lock flag (1 = locked)
extern int   DAT_805f2a8c;   // last transfer result (1 = ok)

// --- OS primitives ------------------------------------------------------------
extern unsigned long long os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void os_dc_invalidate_range(int addr, int len);

// --- EXI channel-0 driver (resolved through shared headers) -------------------
extern int FUN_803b9fb8(int chan, int dev, int cmd);          // EXILock
extern int FUN_803b97f8(int chan, int dev, int freq);         // EXISelect
extern int FUN_803b8c90(int chan, unsigned char *buf, int len, int rw, int cb); // EXIImm (cmd word)
extern int FUN_803b8eec(int chan, unsigned char *buf, int len, int rw);         // EXIImmEx (write payload)
extern int FUN_803b8f8c(int chan, int addr, int len, int rw, int cb);           // EXIDma (read payload)
extern int FUN_803b9078(int chan);                            // EXISync
extern int FUN_803b9924(int chan);                            // EXIDeselect
extern int FUN_803ba0ac(int chan);                            // EXIUnlock

// --- checksum helpers (defined elsewhere in the SRAM group) -------------------
extern unsigned short FUN_8039efc4(void);   // read SRAM language/config field
extern void FUN_8039f034(unsigned short);   // (re)compute + stash checksum

// forward decls within this file
unsigned int os_sram_write_range(unsigned char *dst, int offset, int len);
unsigned int os_sram_unlock(int recompute, unsigned int dirtyOffset);


// Flush the dirty tail of the shadow (from DAT_805f2a80 to 0x40) back to the SRAM
// chip; on success mark the shadow clean.
void os_sram_flush(void)
{
    DAT_805f2a8c = os_sram_write_range((unsigned char *)((int)&DAT_805f2a40 + DAT_805f2a80),
                                       DAT_805f2a80, 0x40 - DAT_805f2a80);
    if (DAT_805f2a8c != 0) {
        DAT_805f2a80 = 0x40;
    }
}

// Write `len` bytes of the shadow (starting at byte `offset`) to the SRAM chip
// over EXI: lock, select at freq 3, issue the write command word
// (offset*0x40 + 0x100 | 0xa0000000), stream the payload, sync, deselect, unlock.
// Returns 1 iff every EXI step succeeded.
unsigned int os_sram_write_range(unsigned char *dst, int offset, int len)
{
    unsigned int cImm;
    unsigned int cSync;
    unsigned int cData;
    int ok;
    unsigned int result;
    int cDesel;
    unsigned int cmd[2];

    ok = FUN_803b9fb8(0, 1, -0x7fc61d48);   // EXILock
    if (ok == 0) {
        result = 0;
    }
    else {
        ok = FUN_803b97f8(0, 1, 3);         // EXISelect
        if (ok == 0) {
            FUN_803ba0ac(0);                // EXIUnlock
            result = 0;
        }
        else {
            cmd[0] = (unsigned int)(offset * 0x40 + 0x100) | 0xa0000000;
            cImm  = (unsigned int)countLeadingZeros(FUN_803b8c90(0, (unsigned char *)cmd, 4, 1, 0));
            cSync = (unsigned int)countLeadingZeros(FUN_803b9078(0));
            cData = (unsigned int)countLeadingZeros(FUN_803b8eec(0, dst, len, 1));
            cDesel = countLeadingZeros(FUN_803b9924(0));
            FUN_803ba0ac(0);                // EXIUnlock
            result = (unsigned int)countLeadingZeros((cImm | cSync | cData | (unsigned int)cDesel) >> 5);
            result = result >> 5;
        }
    }
    return result;
}

// __OSInitSram: read the whole 0x40-byte SRAM block into the shadow once at boot
// (invoked from os_kernel_init). Clears the lock state, invalidates the shadow's
// dcache range, then performs the EXI read (command word 0x20). Finishes by
// seeding the cached config field.
void os_sram_init(void)
{
    unsigned int cImm;
    unsigned int cSel;
    unsigned int cData;
    unsigned int cSync;
    int ok;
    unsigned short cfg;
    unsigned int result;
    unsigned char cmd[8];

    result = 0;
    DAT_805f2a84 = 0;
    DAT_805f2a88 = 0;
    os_dc_invalidate_range(0x805f2a40, 0x40);
    ok = FUN_803b9fb8(0, 1, 0);             // EXILock
    if (ok != 0) {
        ok = FUN_803b97f8(0, 1, 3);         // EXISelect
        if (ok == 0) {
            FUN_803ba0ac(0);                // EXIUnlock
        }
        else {
            cmd[0] = 0x20;                  // SRAM read command
            cmd[1] = 0;
            cmd[2] = 1;
            cmd[3] = 0;
            cImm  = (unsigned int)countLeadingZeros(FUN_803b8c90(0, cmd, 4, 1, 0));
            cSel  = (unsigned int)countLeadingZeros(FUN_803b9078(0));
            cData = (unsigned int)countLeadingZeros(FUN_803b8f8c(0, 0x805f2a40, 0x40, 0, 0));
            {
                unsigned int cSync2 = (unsigned int)countLeadingZeros(FUN_803b9078(0));
                cSync = (unsigned int)countLeadingZeros(FUN_803b9924(0));
                FUN_803ba0ac(0);            // EXIUnlock
                result = (unsigned int)countLeadingZeros((cImm | cSel | cData | cSync2 | cSync) >> 5);
                result = result >> 5;
            }
        }
    }
    DAT_805f2a80 = 0x40;
    DAT_805f2a8c = result;
    cfg = FUN_8039efc4();
    FUN_8039f034(cfg);
}

// __OSLockSram: disable interrupts, mark the shadow locked, and return a pointer
// to the config region base. Returns null if already locked.
unsigned short *os_sram_lock(void)
{
    unsigned short *p;
    unsigned long long prev;

    p = &DAT_805f2a40;
    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        p = (unsigned short *)0;
    }
    return p;
}

// __OSLockSramEx: like os_sram_lock but returns the flash-ID / extended region.
unsigned short *os_sram_lock_ex(void)
{
    unsigned short *p;
    unsigned long long prev;

    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        p = &DAT_805f2a54;
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        p = (unsigned short *)0;
    }
    return p;
}

// __OSUnlockSram: if `recompute` is set, recompute the two checksum halfwords over
// the config words (0x805f2a4c..); clamp the dirty offset to `dirtyOffset`; then
// flush the dirty tail back to the chip over EXI, clear the lock, and restore
// interrupts. Returns the transfer result (1 = ok).
unsigned int os_sram_unlock(int recompute, unsigned int dirtyOffset)
{
    unsigned short chk;
    unsigned int cImm;
    unsigned int cSync;
    unsigned int cData;
    unsigned int cDesel;
    int ok;
    int len;
    unsigned char *dst;
    unsigned short *cfg;
    int count;
    unsigned int cmd[3];

    if (recompute != 0) {
        if (dirtyOffset == 0) {
            // recompute the checksum over the four config words.
            if (2 < ((unsigned char)DAT_805f2a52 & 3)) {
                DAT_805f2a52 = (unsigned char)(DAT_805f2a52 & 0xfc);
            }
            DAT_805f2a42 = 0;
            DAT_805f2a40 = 0;
            cfg = &DAT_805f2a4c;
            count = 4;
            do {
                DAT_805f2a40 = (unsigned short)(DAT_805f2a40 + *cfg);
                chk = *cfg;
                cfg = cfg + 1;
                DAT_805f2a42 = (unsigned short)(DAT_805f2a42 + (unsigned short)~chk);
                count = count + -1;
            } while (count != 0);
        }
        if ((int)dirtyOffset < DAT_805f2a80) {
            DAT_805f2a80 = (int)dirtyOffset;
        }
        cImm = (unsigned int)DAT_805f2a80;
        // clear a stale display/sound config combination.
        if ((DAT_805f2a80 < 0x15) &&
            (((DAT_805f2a7c & 0x7c00) == 0x5000) || ((DAT_805f2a7c & 0xc0) == 0xc0))) {
            DAT_805f2a7c = 0;
        }
        len = 0x40 - DAT_805f2a80;
        dst = (unsigned char *)((int)&DAT_805f2a40 + DAT_805f2a80);
        ok = FUN_803b9fb8(0, 1, -0x7fc61d48);   // EXILock
        if (ok == 0) {
            DAT_805f2a8c = 0;
        }
        else {
            ok = FUN_803b97f8(0, 1, 3);         // EXISelect
            if (ok == 0) {
                FUN_803ba0ac(0);                // EXIUnlock
                DAT_805f2a8c = 0;
            }
            else {
                cmd[0] = cImm * 0x40 + 0x100 | 0xa0000000;
                cImm   = (unsigned int)countLeadingZeros(FUN_803b8c90(0, (unsigned char *)cmd, 4, 1, 0));
                cSync  = (unsigned int)countLeadingZeros(FUN_803b9078(0));
                cData  = (unsigned int)countLeadingZeros(FUN_803b8eec(0, dst, len, 1));
                cDesel = (unsigned int)countLeadingZeros(FUN_803b9924(0));
                FUN_803ba0ac(0);                // EXIUnlock
                cImm = (unsigned int)countLeadingZeros((cImm | cSync | cData | cDesel) >> 5);
                DAT_805f2a8c = cImm >> 5;
            }
        }
        if (DAT_805f2a8c != 0) {
            DAT_805f2a80 = 0x40;
        }
    }
    DAT_805f2a88 = 0;
    os_get_interrupts_enabled();
    return (unsigned int)DAT_805f2a8c;
}

} // extern "C"

// ---------------------------------------------------------------------------
// SRAM commit wrappers, generic block read, and the video/sound config-field
// accessors (OSGetProgressiveMode / OSGetEuRgb60Mode / OSGetSoundMode etc.).
//
// Address: 0x8039e960 | Name: os_sram_commit          | Date: 2026-09-14
// Address: 0x8039e984 | Name: os_sram_commit_from     | Date: 2026-09-14
// Address: 0x8039e9a8 | Name: os_sram_last_result     | Date: 2026-09-14
// Address: 0x8039e9b8 | Name: os_sram_read_block      | Date: 2026-09-14
// Address: 0x8039eadc | Name: os_get_progressive_mode | Date: 2026-09-14
// Address: 0x8039eb5c | Name: os_set_progressive_mode | Date: 2026-09-14
// Address: 0x8039ec00 | Name: os_get_eurgb60_mode     | Date: 2026-09-14
// Address: 0x8039ec70 | Name: os_set_eurgb60_mode     | Date: 2026-09-14
// Address: 0x8039ed14 | Name: os_get_sound_mode       | Date: 2026-09-14
// Address: 0x8039ed80 | Name: os_get_video_flag_11_6  | Date: 2026-09-14
//
// The get/set accessors all follow the same shape: lock the SRAM shadow
// (inline __OSLockSram; null pointer if already locked, i.e. re-entrant), read
// or read-modify a config byte, then unlock. Getters call os_sram_unlock(0,0)
// (no recompute/flush); setters that actually change a bit call
// os_sram_unlock(1,0) to recompute the checksum and flush the block.
//
// Config bytes (offsets into the shadow base &DAT_805f2a40):
//   +0x13 bit 2  progressive-scan enable
//   +0x13 bit 7  EuRGB60 (PAL60) enable
//   +0x11 bit 6  video-mode flag (checked at boot)
//   +0x12        sound/display-mode byte
// ---------------------------------------------------------------------------

extern "C" {

// Read a config byte at `byteOff` from the SRAM shadow under a lock, then unlock
// without flushing. Returns 0 if the shadow was already locked (re-entrant).
static inline unsigned char sram_lock_read_byte(int byteOff)
{
    unsigned char *base;
    unsigned long long prev;

    base = (unsigned char *)&DAT_805f2a40;
    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned char *)0;
    }
    unsigned char v = base[byteOff];
    os_sram_unlock(0, 0);
    return v;
}

// Mark the SRAM shadow dirty and flush the whole block (dirtyOffset 0). Used by
// os_reset_system to persist the SRAM boot flag before a reboot.
void os_sram_commit(int recompute)
{
    os_sram_unlock(recompute, 0);
}

// Flush the SRAM block from byte offset 0x14 to the end.
void os_sram_commit_from(int recompute)
{
    os_sram_unlock(recompute, 0x14);
}

// Return the result code of the last SRAM/flash EXI transfer (1 = ok). Used as
// the terminal "all quiesced" poll by os_call_reset_functions.
unsigned int os_sram_last_result(void)
{
    return (unsigned int)DAT_805f2a8c;
}

// Generic EXI read of a SRAM/flash region: invalidate the destination dcache
// range, then over EXI channel 0 issue a read command (block << 6), stream the
// payload into `dst`, sync, deselect, unlock. Returns 1 iff every step ok.
unsigned int os_sram_read_block(unsigned int dst, int len, int block)
{
    unsigned int cImm;
    unsigned int cSel;
    unsigned int cData;
    unsigned int cSync;
    unsigned int cDesel;
    int ok;
    unsigned int result;
    int cmd[2];

    os_dc_invalidate_range((int)dst, len);
    ok = FUN_803b9fb8(0, 1, 0);             // EXILock
    if (ok == 0) {
        result = 0;
    }
    else {
        ok = FUN_803b97f8(0, 1, 3);         // EXISelect
        if (ok == 0) {
            FUN_803ba0ac(0);                // EXIUnlock
            result = 0;
        }
        else {
            cmd[0] = block << 6;
            cImm   = (unsigned int)countLeadingZeros(FUN_803b8c90(0, (unsigned char *)cmd, 4, 1, 0));
            cSel   = (unsigned int)countLeadingZeros(FUN_803b9078(0));
            cData  = (unsigned int)countLeadingZeros(FUN_803b8f8c(0, (int)dst, len, 0, 0));
            cSync  = (unsigned int)countLeadingZeros(FUN_803b9078(0));
            cDesel = (unsigned int)countLeadingZeros(FUN_803b9924(0));
            FUN_803ba0ac(0);                // EXIUnlock
            result = (unsigned int)countLeadingZeros((cImm | cSel | cData | cSync | cDesel) >> 5);
            result = result >> 5;
        }
    }
    return result;
}

// OSGetProgressiveMode: read config byte +0x13 bit 2.
bool os_get_progressive_mode(void)
{
    return (sram_lock_read_byte(0x13) & 4) != 0;
}

// OSSetProgressiveMode: set/clear config byte +0x13 bit 2; recompute+flush only
// when the value actually changes.
void os_set_progressive_mode(unsigned char enable)
{
    unsigned char *base;
    unsigned char bit;
    unsigned long long prev;

    base = (unsigned char *)&DAT_805f2a40;
    bit = (unsigned char)((enable & 1) << 2);
    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned char *)0;
    }
    if (bit == (base[0x13] & 4)) {
        os_sram_unlock(0, 0);
    }
    else {
        base[0x13] = (unsigned char)(base[0x13] & 0xfb);
        base[0x13] = (unsigned char)(base[0x13] | bit);
        os_sram_unlock(1, 0);
    }
}

// OSGetEuRgb60Mode: read config byte +0x13 bit 7.
unsigned char os_get_eurgb60_mode(void)
{
    return (unsigned char)(sram_lock_read_byte(0x13) >> 7);
}

// OSSetEuRgb60Mode: set/clear config byte +0x13 bit 7; recompute+flush on change.
void os_set_eurgb60_mode(char enable)
{
    unsigned char *base;
    unsigned long long prev;

    base = (unsigned char *)&DAT_805f2a40;
    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned char *)0;
    }
    if ((unsigned char)(enable << 7) == (base[0x13] & 0x80)) {
        os_sram_unlock(0, 0);
    }
    else {
        base[0x13] = (unsigned char)(base[0x13] & 0x7f);
        base[0x13] = (unsigned char)(base[0x13] | (enable << 7));
        os_sram_unlock(1, 0);
    }
}

// OSGetSoundMode-style accessor: read the display/sound-mode byte at +0x12.
unsigned char os_get_sound_mode(void)
{
    return sram_lock_read_byte(0x12);
}

// Read the video-mode flag at config byte +0x11 bit 6 (checked at boot).
unsigned char os_get_video_flag_11_6(void)
{
    return (unsigned char)((sram_lock_read_byte(0x11) >> 6) & 1);
}

} // extern "C"

// ---------------------------------------------------------------------------
// Extended-region (flash-ID / language-config) accessors and the +0x11 bit-6
// setter. The ex-region base is &DAT_805f2a54; halfword accessors index it as a
// u16 array and commit with dirty offset 0x14.
//
// Address: 0x8039edf0 | Name: os_set_video_flag_11_6  | Date: 2026-09-14
// Address: 0x8039ee94 | Name: os_sram_get_ex_field    | Date: 2026-09-14
// Address: 0x8039ef18 | Name: os_sram_set_ex_field    | Date: 2026-09-14
// Address: 0x8039efc4 | Name: os_sram_get_config      | Date: 2026-09-14
// Address: 0x8039f034 | Name: os_sram_set_config      | Date: 2026-09-14
// ---------------------------------------------------------------------------

extern "C" {

// Set/clear SRAM config byte +0x11 bit 6 (the flag os_get_video_flag_11_6 reads);
// recompute+flush only on change.
void os_set_video_flag_11_6(unsigned char enable)
{
    unsigned char *base;
    unsigned char bit;
    unsigned long long prev;

    base = (unsigned char *)&DAT_805f2a40;
    bit = (unsigned char)((enable & 1) << 6);
    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned char *)0;
    }
    if (bit == (base[0x11] & 0x40)) {
        os_sram_unlock(0, 0);
    }
    else {
        base[0x11] = (unsigned char)(base[0x11] & 0xbf);
        base[0x11] = (unsigned char)(base[0x11] | bit);
        os_sram_unlock(1, 0);
    }
}

// Read halfword `index` of the extended (flash-ID/language) region under a lock,
// unlock without flush (dirty offset 0x14). index is relative to ex-base+0xe.
unsigned short os_sram_get_ex_field(int index)
{
    unsigned short *base;
    unsigned short v;
    unsigned long long prev;

    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        base = &DAT_805f2a54;
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned short *)0;
    }
    v = base[index + 0xe];
    os_sram_unlock(0, 0x14);
    return v;
}

// Write halfword `index` of the extended region; recompute+flush on change.
void os_sram_set_ex_field(int index, short value)
{
    unsigned short *base;
    unsigned long long prev;

    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        base = &DAT_805f2a54;
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned short *)0;
    }
    if (base[index + 0xe] == (unsigned short)value) {
        os_sram_unlock(0, 0x14);
    }
    else {
        base[index + 0xe] = (unsigned short)value;
        os_sram_unlock(1, 0x14);
    }
}

// Read the SRAM config/language halfword at ex-region [0x14].
unsigned short os_sram_get_config(void)
{
    unsigned short *base;
    unsigned short v;
    unsigned long long prev;

    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        base = &DAT_805f2a54;
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned short *)0;
    }
    v = base[0x14];
    os_sram_unlock(0, 0x14);
    return v;
}

// Write the SRAM config/language halfword at ex-region [0x14]. Sanitizes an
// invalid display/sound combination (0x5000 mask or 0xc0 bits) to 0 first;
// recompute+flush on change.
void os_sram_set_config(unsigned short value)
{
    unsigned short *base;
    unsigned long long prev;

    if (((value & 0x7c00) == 0x5000) || ((value & 0xc0) == 0xc0)) {
        value = 0;
    }
    prev = os_disable_interrupts();
    if (DAT_805f2a88 == 0) {
        base = &DAT_805f2a54;
        DAT_805f2a88 = 1;
        DAT_805f2a84 = (int)(prev >> 0x20);
    }
    else {
        os_get_interrupts_enabled();
        base = (unsigned short *)0;
    }
    if (value == base[0x14]) {
        os_sram_unlock(0, 0x14);
    }
    else {
        base[0x14] = value;
        os_sram_unlock(1, 0x14);
    }
}

} // extern "C"
