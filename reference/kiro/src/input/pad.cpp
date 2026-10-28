// TSFP GameCube PAD (controller) driver over the Serial Interface (SI).
//
// Address: 0x803a7e28 | Name: pad_fixup_response       | Date: 2026-09-15
// Address: 0x803a7fcc | Name: pad_probe_next_callback  | Date: 2026-09-15
// Address: 0x803a8090 | Name: pad_disable_port         | Date: 2026-09-15
// Address: 0x803a815c | Name: pad_reset_callback       | Date: 2026-09-15
// Address: 0x803a8234 | Name: pad_type_callback        | Date: 2026-09-15
// Address: 0x803a8560 | Name: pad_origin_callback      | Date: 2026-09-15
// Address: 0x803a86a0 | Name: pad_reset                | Date: 2026-09-15
// Address: 0x803a87b0 | Name: pad_recalibrate          | Date: 2026-09-15
// Address: 0x803a88c4 | Name: PADInit                  | Date: 2026-09-15
//
// This is the Dolphin PAD library: it drives up to four controllers through the
// SI (Serial Interface) transfer layer (the FUN_803bb.. / FUN_803bc.. helpers,
// SI driver, later batch). Ports are brought online through a probe chain:
// pad_reset -> SI reset -> pad_reset_callback -> pad_type_callback (identify the
// controller) -> pad_origin_callback (read/calibrate the analog origin) -> port
// online. Each per-port record is 0xC bytes in the array at DAT_805f37e0; the
// packed poll command word per port lives in the array at DAT_805f37d0.
//
// Driver state bitmasks (one bit per port, MSB = port 0):
//   DAT_80613254  enabled ports
//   DAT_80613258  ports queued for (re)probe
//   DAT_8061325c  "reset origin on next poll" latch
//   DAT_80613260  reset-in-progress
//   DAT_80613264  origin-read-in-progress
//   DAT_80613268  probe-pending
//   DAT_8061326c  recalibrate-pending
//
// NON-MATCHING: SI driver entry points, the per-port state arrays, and the PAD
// driver state block are reached through per-file externs; OSReport/os_* helpers
// resolve via the generated decls; control flow is preserved verbatim.

#include "common.h"

extern "C" {

// ---- PAD per-port state / poll-command arrays (0x805f37xx) ----
extern unsigned char DAT_805f37d0;   // per-port packed poll command words (u32[4])
extern unsigned char DAT_805f37e0;   // per-port state records (0xC bytes each)
extern unsigned char DAT_805f37e2;   // record+2 (button hi / A-stuck workaround)
extern unsigned char DAT_805f37e3;   // record+3
extern unsigned char DAT_805f37e4;   // record+4 (analog A)
extern unsigned char DAT_805f37e5;   // record+5
extern unsigned char DAT_805f37e6;   // record+6
extern unsigned char DAT_805f37e7;   // record+7
extern unsigned char DAT_805f37e8;   // record+8
extern unsigned char DAT_805f37e9;   // record+9
extern int           DAT_805f3810;   // per-port calibrate command words (0x4d.. u32[4])
extern int           DAT_805f3814;
extern int           DAT_805f3818;
extern int           DAT_805f381c;

// ---- PAD driver state block (0x806132xx) ----
extern unsigned int DAT_80613250;   // "PADInit already ran" latch
extern unsigned int DAT_80613254;   // enabled-ports mask
extern unsigned int DAT_80613258;   // queued-for-probe mask
extern unsigned int DAT_8061325c;   // reset-origin latch mask
extern unsigned int DAT_80613260;   // reset-in-progress mask
extern unsigned int DAT_80613264;   // origin-read-in-progress mask
extern unsigned int DAT_80613268;   // probe-pending mask
extern unsigned int DAT_8061326c;   // recalibrate-pending mask
extern int          DAT_80613278;   // optional SI init hook argument
extern int          DAT_80613438;   // "seed repeat mask from time base" flag

// ---- PAD format / config words (0x8061145x / 0x8061146x) ----
extern unsigned int DAT_80611454;   // current probe port index (CLZ result)
extern unsigned int DAT_80611458;   // A-button-stuck wireless fixup mask
extern unsigned int DAT_8061145c;   // SI poll config / reporting mode word
extern unsigned int DAT_80611460;   // SI poll rate / mode
extern char         DAT_80611450;   // PADInit version string
extern unsigned int DAT_80611468;   // SI calibrate command block
extern unsigned int DAT_8061146c;   // SI wireless calibrate command block

// ---- low-memory SRAM-mirror globals ----
extern unsigned short DAT_800030e0;  // button-repeat seed value
extern unsigned char  DAT_800030e3;  // SRAM flags (bit 0x40 = progressive/60Hz)

// ---- SI (Serial Interface) driver entry points (later batch) ----
extern void        FUN_803bb760(unsigned int port, unsigned int cmd);
extern void        FUN_803bb7f0(unsigned int enabledMask);
extern void        FUN_803bb88c(unsigned int portMask);
extern void        FUN_803bb9cc(unsigned int port, void *outBuf);
extern int         FUN_803bbb1c(unsigned int port, void *cmd, int cmdLen,
                                void *dst, int dstLen, void *cb, int a, int b);
extern unsigned int FUN_803bbf20(unsigned int port);
extern void        FUN_803bc0e4(unsigned int port, void *cb);
extern void        FUN_803bc304(int r3, int r4, int r5, int r6, int r7, int r8,
                                int r9, int r10, unsigned int mask, int a,
                                int b, unsigned int c, int d, int e, int f,
                                int g);
extern int         FUN_803a8e98(int arg);
extern void        os_disable_interrupts(void);
extern int         os_get_interrupts_enabled(void);
extern long long   __sar2i(int hi, unsigned int lo, int shift);
extern void       *memset_wrapper(int dst, int val, int len);
// Signatures below match generated_decls.h exactly (pad.cpp includes only
// common.h, which does not pull in generated_decls.h).
extern unsigned long long os_read_time_base(void);
extern void        os_register_version(int fmt);
extern void        os_register_reset_function(int node);
extern void        os_sram_set_ex_field(int index, short value);

// Forward decls: the probe chain re-arms itself across these callbacks.
void pad_type_callback(int arg, unsigned int response);
void pad_fixup_response(unsigned int port);
void pad_probe_next_callback(int arg, unsigned int status);
void pad_origin_callback(unsigned int port, unsigned int status);

// ============================================================================
// pad_fixup_response (0x803a7e28): normalize a raw SI controller response.
// ============================================================================
void pad_fixup_response(unsigned int port)
{
    unsigned int mode;
    int iVar2;

    iVar2 = port * 0xc;
    mode = DAT_8061145c & 0x700;
    if (mode == 0x400) goto LAB_803a7f50;
    if (mode < 0x400) {
        if (mode == 0x200) {
            (&DAT_805f37e4)[iVar2] = (&DAT_805f37e4)[iVar2] & 0xf0;
            (&DAT_805f37e5)[iVar2] = (&DAT_805f37e5)[iVar2] & 0xf0;
            (&DAT_805f37e6)[iVar2] = (&DAT_805f37e6)[iVar2] & 0xf0;
            (&DAT_805f37e7)[iVar2] = (&DAT_805f37e7)[iVar2] & 0xf0;
            goto LAB_803a7f50;
        }
        if (0x1ff < mode) goto LAB_803a7f50;
        if (mode == 0x100) {
            (&DAT_805f37e4)[iVar2] = (&DAT_805f37e4)[iVar2] & 0xf0;
            (&DAT_805f37e5)[iVar2] = (&DAT_805f37e5)[iVar2] & 0xf0;
            (&DAT_805f37e8)[iVar2] = (&DAT_805f37e8)[iVar2] & 0xf0;
            (&DAT_805f37e9)[iVar2] = (&DAT_805f37e9)[iVar2] & 0xf0;
            goto LAB_803a7f50;
        }
        if ((0xff < mode) || (mode != 0)) goto LAB_803a7f50;
    }
    else if (mode != 0x600) {
        if (mode < 0x600) {
            if (mode != 0x500) goto LAB_803a7f50;
        }
        else if (mode != 0x700) goto LAB_803a7f50;
    }
    (&DAT_805f37e6)[iVar2] = (&DAT_805f37e6)[iVar2] & 0xf0;
    (&DAT_805f37e7)[iVar2] = (&DAT_805f37e7)[iVar2] & 0xf0;
    (&DAT_805f37e8)[iVar2] = (&DAT_805f37e8)[iVar2] & 0xf0;
    (&DAT_805f37e9)[iVar2] = (&DAT_805f37e9)[iVar2] & 0xf0;
LAB_803a7f50:
    (&DAT_805f37e2)[iVar2] = (&DAT_805f37e2)[iVar2] + -0x80;
    (&DAT_805f37e3)[iVar2] = (&DAT_805f37e3)[iVar2] + -0x80;
    (&DAT_805f37e4)[iVar2] = (&DAT_805f37e4)[iVar2] + -0x80;
    (&DAT_805f37e5)[iVar2] = (&DAT_805f37e5)[iVar2] + -0x80;
    if ((((DAT_80611458 & (0x80000000U >> port)) != 0) &&
         ('@' < (char)(&DAT_805f37e2)[iVar2])) &&
        (FUN_803bbf20(port) & 0xffff0000) == 0x9000000) {
        (&DAT_805f37e2)[iVar2] = 0;
    }
}

// ============================================================================
// pad_probe_next_callback (0x803a7fcc): SI probe callback - fix up the response,
// re-arm the port, then start the next queued port's type probe.
// ============================================================================
void pad_probe_next_callback(int arg, unsigned int status)
{
    unsigned int port;
    int scratch[4];

    if ((status & 0xf) == 0) {
        pad_fixup_response(DAT_80611454);
        port = DAT_80611454;
        DAT_80613254 = DAT_80613254 | (0x80000000U >> DAT_80611454);
        FUN_803bb9cc(DAT_80611454, scratch);
        FUN_803bb760(port, DAT_8061145c | 0x400000);
        FUN_803bb7f0(DAT_80613254);
    }
    DAT_80611454 = countLeadingZeros(DAT_80613258);
    if (DAT_80611454 != 0x20) {
        DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
        memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
        FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
    }
}

// ============================================================================
// pad_disable_port (0x803a8090): fix up the final response and, on SI error,
// tear the port down.
// ============================================================================
void pad_disable_port(unsigned int port, unsigned int status)
{
    unsigned int bit;

    bit = 0x80000000 >> port;
    if ((DAT_80613254 & bit) != 0) {
        if ((status & 0xf) == 0) {
            pad_fixup_response(port);
        }
        if ((status & 8) != 0) {
            os_disable_interrupts();
            FUN_803bb88c(bit);
            bit = ~bit;
            DAT_80613254 = DAT_80613254 & bit;
            DAT_80613260 = DAT_80613260 & bit;
            DAT_80613264 = DAT_80613264 & bit;
            DAT_80613268 = DAT_80613268 & bit;
            DAT_8061326c = DAT_8061326c & bit;
            os_sram_set_ex_field(port, 0);
            os_get_interrupts_enabled();
        }
    }
}

// ============================================================================
// pad_reset_callback (0x803a815c): SI reset completion - mark port enabled +
// origin-pending, re-arm SI, advance to the next queued port's type probe.
// ============================================================================
void pad_reset_callback(int arg, unsigned int status)
{
    int port;
    int scratch[3];

    port = DAT_80611454;
    if ((status & 0xf) == 0) {
        DAT_80613254 = DAT_80613254 | (0x80000000U >> DAT_80611454);
        FUN_803bb9cc(DAT_80611454, scratch);
        FUN_803bb760(port, DAT_8061145c | 0x400000);
        FUN_803bb7f0(DAT_80613254);
        DAT_80613260 = DAT_80613260 | (0x80000000U >> DAT_80611454);
    }
    DAT_80611454 = countLeadingZeros(DAT_80613258);
    if (DAT_80611454 != 0x20) {
        DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
        memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
        FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
    }
}

// ============================================================================
// pad_type_callback (0x803a8234): SI type-probe completion - record the type,
// then issue origin-read / calibrate, or bring the port online; advance to the
// next queued port on completion.
// ============================================================================
void pad_type_callback(int arg, unsigned int type)
{
    unsigned int port;
    unsigned int bit;
    int status;
    unsigned int hadOrigin;
    int scratch[5];

    port = DAT_80611454;
    bit = 0x80000000 >> DAT_80611454;
    hadOrigin = DAT_8061325c & bit;
    status = 1;
    DAT_8061325c = DAT_8061325c & ~bit;
    if ((type & 0xf) != 0) {
        DAT_80611454 = countLeadingZeros(DAT_80613258);
        if (DAT_80611454 == 0x20) {
            return;
        }
        DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
        memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
        FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
        return;
    }
    *(unsigned int *)(&DAT_805f37d0 + DAT_80611454 * 4) = type & 0xffffff00;
    if (((type & 0x18000000) != 0x8000000) || ((type & 0x1000000) == 0)) {
        DAT_80611454 = countLeadingZeros(DAT_80613258);
        if (DAT_80611454 == 0x20) {
            return;
        }
        DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
        memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
        FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
        return;
    }
    if (1 < DAT_80611460) {
        if (((type & 0x80000000) == 0) || ((type & 0x4000000) != 0)) {
            if (hadOrigin == 0) {
                status = FUN_803bbb1c(port, &DAT_80611468, 1,
                                      &DAT_805f37e0 + port * 0xc, 10,
                                      (void *)pad_origin_callback, 0, 0);
            }
            else {
                status = FUN_803bbb1c(port, &DAT_8061146c, 3,
                                      &DAT_805f37e0 + port * 0xc, 10,
                                      (void *)pad_origin_callback, 0, 0);
            }
        }
        else if ((((type & 0x100000) != 0) && ((type & 0x80000) == 0)) &&
                 ((type & 0x40000) == 0)) {
            if ((type & 0x40000000) == 0) {
                status = FUN_803bbb1c(port, &DAT_805f3810 + port, 3,
                                      &DAT_805f37e0 + port * 0xc, 8,
                                      (void *)pad_probe_next_callback, 0, 0);
            }
            else {
                status = FUN_803bbb1c(port, &DAT_80611468, 1,
                                      &DAT_805f37e0 + port * 0xc, 10,
                                      (void *)pad_origin_callback, 0, 0);
            }
        }
        if (status == 0) {
            DAT_80611454 = countLeadingZeros(DAT_80613258);
            DAT_80613268 = DAT_80613268 | bit;
            if (DAT_80611454 != 0x20) {
                DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
                memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
                FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
            }
        }
        return;
    }
    DAT_80613254 = DAT_80613254 | bit;
    FUN_803bb9cc(port, scratch);
    FUN_803bb760(port, DAT_8061145c | 0x400000);
    FUN_803bb7f0(DAT_80613254);
    DAT_80611454 = countLeadingZeros(DAT_80613258);
    if (DAT_80611454 == 0x20) {
        return;
    }
    DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
    memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
    FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
}

// ============================================================================
// pad_origin_callback (0x803a8560): SI origin-read completion - clear the
// origin/calibrating bits, re-calibrate if requested, or tear the port down on
// error.
// ============================================================================
void pad_origin_callback(unsigned int port, unsigned int status)
{
    unsigned int bit;

    bit = 0x80000000 >> port;
    if ((DAT_80613254 & bit) != 0) {
        DAT_80613260 = DAT_80613260 & ~bit;
        DAT_80613264 = DAT_80613264 & ~bit;
        if (((((status & 0xf) == 0) && ((status & 0x80000000) != 0)) &&
             ((status & 0x100000) != 0)) &&
            ((((status & 0x40000000) != 0 && ((status & 0x4000000) == 0)) &&
              (((status & 0x80000) == 0 && ((status & 0x40000) == 0)))))) {
            FUN_803bbb1c(port, &DAT_80611468, 1, &DAT_805f37e0 + port * 0xc, 10,
                         (void *)pad_origin_callback, 0, 0);
            return;
        }
        os_disable_interrupts();
        FUN_803bb88c(bit);
        bit = ~bit;
        DAT_80613254 = DAT_80613254 & bit;
        DAT_80613260 = DAT_80613260 & bit;
        DAT_80613264 = DAT_80613264 & bit;
        DAT_80613268 = DAT_80613268 & bit;
        DAT_8061326c = DAT_8061326c & bit;
        os_sram_set_ex_field(port, 0);
        os_get_interrupts_enabled();
    }
}

// ============================================================================
// pad_reset (0x803a86a0): reset the given set of PAD ports (bitmask, MSB=port0).
// ============================================================================
int pad_reset(unsigned int portMask)
{
    unsigned int probe;
    unsigned int enabled;

    os_disable_interrupts();
    probe = (portMask | DAT_80613268) & ~(DAT_80613260 | DAT_80613264);
    DAT_80613258 = DAT_80613258 | probe;
    DAT_8061326c = DAT_8061326c & ~probe;
    DAT_80613268 = 0;
    enabled = DAT_80613258 & DAT_80613254;
    if (DAT_80611460 == 4) {
        DAT_8061325c = DAT_8061325c | probe;
    }
    DAT_80613254 = DAT_80613254 & ~probe;
    FUN_803bb88c(enabled);
    if (DAT_80611454 == 0x20) {
        DAT_80611454 = countLeadingZeros(DAT_80613258);
        if (DAT_80611454 != 0x20) {
            DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
            memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
            FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
        }
    }
    os_get_interrupts_enabled();
    return 1;
}

// ============================================================================
// pad_recalibrate (0x803a87b0): recalibrate the given PAD ports (stick origin).
// Same as pad_reset but the reset-origin latch is gated on the SRAM
// progressive/60Hz flag instead of the poll rate.
// ============================================================================
int pad_recalibrate(unsigned int portMask)
{
    unsigned int probe;
    unsigned int enabled;

    os_disable_interrupts();
    probe = (portMask | DAT_80613268) & ~(DAT_80613260 | DAT_80613264);
    DAT_80613258 = DAT_80613258 | probe;
    DAT_8061326c = DAT_8061326c & ~probe;
    DAT_80613268 = 0;
    enabled = DAT_80613258 & DAT_80613254;
    if ((DAT_800030e3 & 0x40) == 0) {
        DAT_8061325c = DAT_8061325c | probe;
    }
    DAT_80613254 = DAT_80613254 & ~probe;
    FUN_803bb88c(enabled);
    if (DAT_80611454 == 0x20) {
        DAT_80611454 = countLeadingZeros(DAT_80613258);
        if (DAT_80611454 != 0x20) {
            DAT_80613258 = DAT_80613258 & ~(0x80000000U >> DAT_80611454);
            memset_wrapper((int)(&DAT_805f37e0 + DAT_80611454 * 0xc), 0, 0xc);
            FUN_803bc0e4(DAT_80611454, (void *)pad_type_callback);
        }
    }
    os_get_interrupts_enabled();
    return 1;
}

// ============================================================================
// PADInit (0x803a88c4): one-time PAD subsystem init.
// ============================================================================
void PADInit(void)
{
    int repeatHi;

    if (DAT_80613250 == 0) {
        os_register_version((int)(intptr_t)&DAT_80611450);
        if (DAT_80613278 != 0) {
            FUN_803a8e98(DAT_80613278);
        }
        DAT_80613250 = 1;
        if (DAT_80613438 != 0) {
            unsigned long long tb = os_read_time_base();
            int hi = (int)(tb >> 0x20);
            unsigned int lo = (unsigned int)tb;
            unsigned int p12 = (unsigned int)__sar2i(hi, lo, 0x10) & 0xffff;
            int iVar4 = (lo & 0xffff) + p12;
            unsigned int m20 = (unsigned int)__sar2i(hi, lo, 0x20) & 0xffff;
            unsigned int m30 = (unsigned int)__sar2i(hi, lo, 0x30) & 0xffff;
            repeatHi = m30 + m20 + iVar4;
            DAT_8061325c = 0xf0000000;
            DAT_800030e0 = (unsigned short)repeatHi & 0x3fff;
        }
        else {
            repeatHi = 0;
        }
        DAT_805f3810 = ((DAT_800030e0 & 0x3fff) << 8) | 0x4d000000;
        DAT_805f3814 = ((DAT_800030e0 & 0x3fff) << 8) | 0x4d400000;
        DAT_805f3818 = ((DAT_800030e0 & 0x3fff) << 8) | 0x4d800000;
        DAT_805f381c = ((DAT_800030e0 & 0x3fff) << 8) | 0x4dc00000;
        FUN_803bc304(0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0x3fff, repeatHi, 0, 0,
                     0, 0, 0);
        os_register_reset_function(-0x7fb65630);
        pad_reset(0xf0000000);
    }
}

// ============================================================================
// batch_1128: PAD read/poll/rumble/config layer.
//
// Address: 0x803a8a14 | Name: PADRead                     | Date: 2026-09-15
// Address: 0x803a8d14 | Name: PADControlAllMotors         | Date: 2026-09-15
// Address: 0x803a8de0 | Name: PADControlMotor             | Date: 2026-09-15
// Address: 0x803a8e98 | Name: PADSetAnalogMode            | Date: 2026-09-15
// Address: 0x803a9650 | Name: pad_recalibration_check     | Date: 2026-09-15
// Address: 0x803a970c | Name: pad_run_sampling_callback   | Date: 2026-09-15
// Address: 0x803a976c | Name: PADSetSamplingCallback      | Date: 2026-09-15
// Address: 0x803a97c0 | Name: PADSetSpec                  | Date: 2026-09-15
// Address: 0x803a983c | Name: pad_set_analog_map_callback | Date: 2026-09-15
// ============================================================================

// ---- additional PAD state used by the read/poll layer ----
extern unsigned int  DAT_80613270;  // installed sampling callback pointer
extern unsigned int  DAT_80613274;  // recalibrate-pending latch
extern unsigned int  DAT_80613284;  // analog-map callback pointer
extern void         *DAT_80611464;  // analog response-unpack routine pointer

// ---- extra SI driver / OS entry points used here ----
extern int          FUN_803bab30(void);                       // SI idle probe
extern int          FUN_803bab50(unsigned int port);          // SI port busy?
extern unsigned int FUN_803bb6e4(unsigned int port);          // SI status
extern void         FUN_803bb774(void);                       // SI poll flush
extern void         FUN_803bb264(int mask);                   // SI enable poll int
extern void         FUN_803bb330(int mask);                   // SI disable poll int
extern void         os_clear_context(int ctx);
extern void         os_set_current_context(unsigned int ctx);

// The probe-callback re-arm target used by pad_fixup path.
extern void         pad_origin_callback(unsigned int port, unsigned int status);

// Forward decl: pad_recalibration_check calls this before it is defined.
unsigned int PADSetSamplingCallback(int cb);

// ============================================================================
// PADRead (0x803a8a14): poll all four controllers into the caller status array.
// The caller array is 6 shorts (0xC bytes) per port. Returns a bitmask of ports
// whose analog origin still needs a follow-up (rumble stop / recovery) pass.
// ============================================================================
unsigned int PADRead(unsigned short *status)
{
    int  err;
    unsigned int st;
    unsigned int retMask;
    unsigned int port;
    unsigned int portBit;
    unsigned char *rec;
    unsigned int origin[2];

    os_disable_interrupts();
    port = 0;
    rec = &DAT_805f37e0;
    retMask = 0;
    do {
        portBit = 0x80000000u >> port;
        if ((DAT_80613268 & portBit) == 0) {
            if ((DAT_80613258 & portBit) == 0) {
                if (DAT_80611454 != port) {
                    if ((DAT_80613254 & portBit) == 0) {
                        *(unsigned char *)(status + 5) = 0xff;
                        memset_wrapper((int)status, 0, 10);
                    } else {
                        err = FUN_803bab50(port);
                        if (err == 0) {
                            st = FUN_803bb6e4(port);
                            if ((st & 8) == 0) {
                                st = FUN_803bbf20(port);
                                if ((st & 0x20000000) == 0) {
                                    retMask = retMask | portBit;
                                }
                                FUN_803bb9cc(port, origin);
                                if ((int)origin[0] == 0) {
                                    /* SI read failed -> mark transfer error */
                                    *(unsigned char *)(status + 5) = 0xfd;
                                    memset_wrapper((int)status, 0, 10);
                                } else if ((origin[0] & 0x80000000) == 0) {
                                    (*(void (*)(unsigned int, unsigned short *,
                                                unsigned int *))DAT_80611464)
                                        (port, status, origin);
                                    if ((*status & 0x2000) == 0) {
                                        *(unsigned char *)(status + 5) = 0;
                                        *status = *status & 0xff7f;
                                    } else {
                                        *(unsigned char *)(status + 5) = 0xfd;
                                        memset_wrapper((int)status, 0, 10);
                                        FUN_803bbb1c(port, (void *)&DAT_80611468,
                                                     1, rec, 10,
                                                     (void *)pad_origin_callback,
                                                     0, 0);
                                    }
                                } else {
                                    *(unsigned char *)(status + 5) = 0xfd;
                                    memset_wrapper((int)status, 0, 10);
                                }
                            } else {
                                FUN_803bb9cc(port, origin);
                                if ((DAT_80613260 & portBit) == 0) {
                                    /* port physically gone: tear it down */
                                    os_disable_interrupts();
                                    FUN_803bb88c(portBit);
                                    portBit = ~portBit;
                                    DAT_80613254 = DAT_80613254 & portBit;
                                    DAT_80613260 = DAT_80613260 & portBit;
                                    DAT_80613264 = DAT_80613264 & portBit;
                                    DAT_80613268 = DAT_80613268 & portBit;
                                    DAT_8061326c = DAT_8061326c & portBit;
                                    os_sram_set_ex_field(port, 0);
                                    os_get_interrupts_enabled();
                                    *(unsigned char *)(status + 5) = 0xff;
                                    memset_wrapper((int)status, 0, 10);
                                } else {
                                    *(unsigned char *)(status + 5) = 0;
                                    memset_wrapper((int)status, 0, 10);
                                    if ((DAT_80613264 & portBit) == 0) {
                                        DAT_80613264 = DAT_80613264 | portBit;
                                        FUN_803bc0e4(port,
                                                     (void *)pad_origin_callback);
                                    }
                                }
                            }
                        } else {
                            *(unsigned char *)(status + 5) = 0xfd;
                            memset_wrapper((int)status, 0, 10);
                        }
                    }
                    goto LAB_next;
                }
            }
            *(unsigned char *)(status + 5) = 0xfe;
            memset_wrapper((int)status, 0, 10);
        } else {
            pad_reset(0);
            *(unsigned char *)(status + 5) = 0xfe;
            memset_wrapper((int)status, 0, 10);
        }
LAB_next:
        port = port + 1;
        rec = rec + 0xc;
        status = status + 6;
        if (3 < (int)port) {
            os_get_interrupts_enabled();
            return retMask;
        }
    } while (1);
}

// ============================================================================
// PADControlAllMotors (0x803a8d14): drive the rumble motor command on every
// enabled controller from a 4-entry command array.
// ============================================================================
void PADControlAllMotors(unsigned int *commands)
{
    bool any;
    unsigned char progressive;
    unsigned int cmd;
    unsigned int port;

    os_disable_interrupts();
    any = false;
    progressive = DAT_800030e3 & 0x20;
    port = 0;
    do {
        if (((DAT_80613254 & 0x80000000u >> port) != 0) &&
            ((FUN_803bbf20(port) & 0x20000000) == 0)) {
            cmd = *commands;
            if ((DAT_80611460 < 2) && (cmd == 2)) {
                cmd = 0;
            }
            if (progressive != 0) {
                cmd = 0;
            }
            FUN_803bb760(port, DAT_8061145c | 0x400000 | (cmd & 3));
            any = true;
        }
        port = port + 1;
        commands = commands + 1;
    } while ((int)port < 4);
    if (any) {
        FUN_803bb774();
    }
    os_get_interrupts_enabled();
}

// ============================================================================
// PADControlMotor (0x803a8de0): drive the rumble motor command on a single port.
// ============================================================================
void PADControlMotor(unsigned int port, unsigned int cmd)
{
    os_disable_interrupts();
    if (((DAT_80613254 & 0x80000000u >> port) != 0) &&
        ((FUN_803bbf20(port) & 0x20000000) == 0)) {
        if ((DAT_80611460 < 2) && (cmd == 2)) {
            cmd = 0;
        }
        if ((DAT_800030e3 & 0x20) != 0) {
            cmd = 0;
        }
        FUN_803bb760(port, DAT_8061145c | 0x400000 | (cmd & 3));
        FUN_803bb774();
    }
    os_get_interrupts_enabled();
}

// ============================================================================
// PADSetAnalogMode (0x803a8e98): choose the analog reporting mode (0..6) and
// install the matching response-unpack routine.
// ============================================================================
void PADSetAnalogMode(int mode)
{
    DAT_80613278 = 0;
    if (mode == 1) {
        DAT_80611464 = (void *)0x803a906c; /* &LAB_803a906c */
    } else if (mode < 1) {
        if (-1 < mode) {
            DAT_80611464 = (void *)0x803a8ef8; /* &LAB_803a8ef8 */
        }
    } else if (mode < 6) {
        DAT_80611464 = (void *)0x803a91e0; /* &LAB_803a91e0 */
    }
    DAT_80611460 = mode;
}

// ============================================================================
// pad_recalibration_check (0x803a9650): SI reset-completion helper.
// ============================================================================
int pad_recalibration_check(int forced)
{
    bool allPresent;
    int idle;
    int result;

    if (DAT_80613270 != 0) {
        PADSetSamplingCallback(0);
    }
    if (forced == 0) {
        result = 0;
        allPresent = false;
        if ((DAT_80613258 == 0) && (DAT_80611454 == 0x20)) {
            allPresent = true;
        }
        if (allPresent && ((idle = FUN_803bab30()) == 0)) {
            result = 1;
        }
        if ((DAT_80613274 == 0) && (result != 0)) {
            DAT_80613274 = pad_recalibrate(0xf0000000);
            result = 0;
        }
    } else {
        DAT_80613274 = 0;
        result = 1;
    }
    return result;
}

// ============================================================================
// pad_run_sampling_callback (0x803a970c): invoke the installed sampling callback
// under a temporary OS context (save/restore around the call).
// ============================================================================
void pad_run_sampling_callback(unsigned int unused, unsigned int savedCtx)
{
    unsigned char tmpCtx[716];

    (void)unused;
    if (DAT_80613270 != 0) {
        os_clear_context((int)tmpCtx);
        os_set_current_context((unsigned int)tmpCtx);
        (*(void (*)(void))DAT_80613270)();
        os_clear_context((int)tmpCtx);
        os_set_current_context(savedCtx);
    }
}

// ============================================================================
// PADSetSamplingCallback (0x803a976c): install/clear the per-poll callback.
// ============================================================================
unsigned int PADSetSamplingCallback(int cb)
{
    unsigned int prev;

    prev = DAT_80613270;
    DAT_80613270 = cb;
    if (cb == 0) {
        FUN_803bb330(-0x7fc568f4);
    } else {
        FUN_803bb264(-0x7fc568f4);
    }
    return prev;
}

// ============================================================================
// PADSetSpec (0x803a97c0): set/clear the controller-spec workaround bit (0x40)
// in the SRAM flags byte; returns the previous state.
// ============================================================================
bool PADSetSpec(int enable)
{
    unsigned char prev;

    os_disable_interrupts();
    prev = DAT_800030e3 & 0x40;
    DAT_800030e3 = DAT_800030e3 & 0xbf;
    if (enable != 0) {
        DAT_800030e3 = DAT_800030e3 | 0x40;
    }
    os_get_interrupts_enabled();
    return prev != 0;
}

// ============================================================================
// pad_set_analog_map_callback (0x803a983c): atomically install a callback ptr.
// ============================================================================
unsigned int pad_set_analog_map_callback(unsigned int cb)
{
    unsigned int prev;

    prev = DAT_80613284;
    os_disable_interrupts();
    DAT_80613284 = cb;
    os_get_interrupts_enabled();
    return prev;
}

} // extern "C"
