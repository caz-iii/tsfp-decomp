// Low-level PowerPC special-purpose-register (SPR) accessor intrinsics.
//
// The Metrowerks/Dolphin-OS runtime primitives that read and write the Gekko
// SPRs directly (HID0, L2CR, the decrementer, and the performance-monitor
// counters), plus the syscall trampoline and the CPU halt. Each is a single
// PowerPC instruction plus blr in the original; on a non-matching PC build the
// SPRs do not exist, so the getters return their cached shadow and the setters
// record into it. The shadows live in os_spr_shadow[] indexed by SprId so the
// values round-trip for any caller that reads back what it wrote.
//
// Original instructions (see plate comments in the IDB):
//   os_get_hid0   0x80397a7c  mfspr r3,HID0
//   os_set_hid0   0x80397a84  mtspr HID0,r3
//   os_get_l2cr   0x80397a8c  mfspr r3,L2CR
//   os_set_l2cr   0x80397a94  mtspr L2CR,r3
//   os_set_dec    0x80397a9c  mtspr DEC,r3
//   os_syscall    0x80397aa4  sc
//   os_halt       0x80397aac  sync; <spin forever>
//   os_set_mmcr0  0x80397ac0  mtspr MMCR0,r3
//   os_set_mmcr1  0x80397ac8  mtspr MMCR1,r3
//   os_set_pmc1   0x80397ad0  mtspr PMC1,r3
//
// Batch: batch_1095
// Source module: os/os_spr_intrinsics.cpp

#include "common.h"

enum SprId {
    kSprHid0 = 0,
    kSprL2cr,
    kSprDec,
    kSprMmcr0,
    kSprMmcr1,
    kSprPmc1,
    kSprCount
};

// Shadow storage for the SPRs on the non-matching build.
static u32 os_spr_shadow[kSprCount] = { 0, 0, 0, 0, 0, 0 };

// ---------------------------------------------------------------------------
// os_get_hid0 - read HID0 (mfspr r3,HID0). Address: 0x80397a7c
// ---------------------------------------------------------------------------
u32 os_get_hid0(void)
{
    return os_spr_shadow[kSprHid0];
}

// ---------------------------------------------------------------------------
// os_set_hid0 - write HID0 (mtspr HID0,r3). Address: 0x80397a84
// ---------------------------------------------------------------------------
void os_set_hid0(u32 value)
{
    os_spr_shadow[kSprHid0] = value;
}

// ---------------------------------------------------------------------------
// os_get_l2cr - read L2CR (mfspr r3,L2CR). Address: 0x80397a8c
// ---------------------------------------------------------------------------
u32 os_get_l2cr(void)
{
    return os_spr_shadow[kSprL2cr];
}

// ---------------------------------------------------------------------------
// os_set_l2cr - write L2CR (mtspr L2CR,r3). Address: 0x80397a94
// ---------------------------------------------------------------------------
void os_set_l2cr(u32 value)
{
    os_spr_shadow[kSprL2cr] = value;
}

// ---------------------------------------------------------------------------
// os_set_dec - write the decrementer (mtspr DEC,r3). Address: 0x80397a9c
// ---------------------------------------------------------------------------
void os_set_dec(u32 value)
{
    os_spr_shadow[kSprDec] = value;
}

// ---------------------------------------------------------------------------
// os_syscall - execute a supervisor system call (sc). Address: 0x80397aa4
// Trampoline behind OSPanic and other privileged OS routines; no-op on PC.
// ---------------------------------------------------------------------------
void os_syscall(void)
{
    // 'sc' has no PC equivalent; the privileged transition is a no-op here.
}

// ---------------------------------------------------------------------------
// os_halt - drain the store queue (sync) then spin forever. Address: 0x80397aac
// The dead-end reached from OSPanic / fatal logging paths.
// ---------------------------------------------------------------------------
void os_halt(void)
{
    for (;;) {
        // hardware sync + infinite loop: the CPU never returns from here.
    }
}

// ---------------------------------------------------------------------------
// os_set_mmcr0 - write perf-monitor control 0 (mtspr MMCR0,r3). Addr: 0x80397ac0
// ---------------------------------------------------------------------------
void os_set_mmcr0(u32 value)
{
    os_spr_shadow[kSprMmcr0] = value;
}

// ---------------------------------------------------------------------------
// os_set_mmcr1 - write perf-monitor control 1 (mtspr MMCR1,r3). Addr: 0x80397ac8
// ---------------------------------------------------------------------------
void os_set_mmcr1(u32 value)
{
    os_spr_shadow[kSprMmcr1] = value;
}

// ---------------------------------------------------------------------------
// os_set_pmc1 - write perf-monitor counter 1 (mtspr PMC1,r3). Addr: 0x80397ad0
// ---------------------------------------------------------------------------
void os_set_pmc1(u32 value)
{
    os_spr_shadow[kSprPmc1] = value;
}

// ===========================================================================
// batch_1096: remaining SPR/FPSCR accessor intrinsics + a HID0 feature-bit
// enable. Same category as the batch_1095 block above; the shadow storage is
// extended so the additional SPRs also round-trip on the non-matching build.
//
// Original instructions (see plate comments in the IDB):
//   os_set_pmc2    0x80397ad8  mtspr PMC2,r3
//   os_set_pmc3    0x80397ae0  mtspr PMC3,r3
//   os_set_pmc4    0x80397ae8  mtspr PMC4,r3
//   os_get_fpscr   0x80397af0  mffs f31 -> low 32 bits
//   os_set_fpscr   0x80397b10  mtfsf 0xff,f31
//   os_get_hid2    0x80397b38  mfspr r3,HID2
//   os_set_hid2    0x80397b40  mtspr HID2,r3
//   os_get_wpar    0x80397b48  sync; mfspr r3,WPAR
//   os_set_wpar    0x80397b54  mtspr WPAR,r3
//   os_hid0_enable_feature 0x80397b5c  HID0 |= 0x200
// ===========================================================================

enum SprId2 {
    kSprPmc2 = 0,
    kSprPmc3,
    kSprPmc4,
    kSprFpscr,
    kSprHid2,
    kSprWpar,
    kSprCount2
};

static u32 os_spr_shadow2[kSprCount2] = { 0, 0, 0, 0, 0, 0 };

// ---------------------------------------------------------------------------
// os_set_pmc2 - write perf-monitor counter 2 (mtspr PMC2,r3). Addr: 0x80397ad8
// ---------------------------------------------------------------------------
void os_set_pmc2(u32 value)
{
    os_spr_shadow2[kSprPmc2] = value;
}

// ---------------------------------------------------------------------------
// os_set_pmc3 - write perf-monitor counter 3 (mtspr PMC3,r3). Addr: 0x80397ae0
// ---------------------------------------------------------------------------
void os_set_pmc3(u32 value)
{
    os_spr_shadow2[kSprPmc3] = value;
}

// ---------------------------------------------------------------------------
// os_set_pmc4 - write perf-monitor counter 4 (mtspr PMC4,r3). Addr: 0x80397ae8
// ---------------------------------------------------------------------------
void os_set_pmc4(u32 value)
{
    os_spr_shadow2[kSprPmc4] = value;
}

// ---------------------------------------------------------------------------
// os_get_fpscr - read the FP status/control register (mffs f31, return low 32
// bits). The original stashes the 64-bit mffs result on the stack and returns
// the low word. Address: 0x80397af0
// ---------------------------------------------------------------------------
u32 os_get_fpscr(void)
{
    return os_spr_shadow2[kSprFpscr];
}

// ---------------------------------------------------------------------------
// os_set_fpscr - write all eight FPSCR fields (mtfsf 0xff,f31). The original
// builds a double whose low word is the argument and moves it into FPSCR.
// Address: 0x80397b10
// ---------------------------------------------------------------------------
void os_set_fpscr(u32 value)
{
    os_spr_shadow2[kSprFpscr] = value;
}

// ---------------------------------------------------------------------------
// os_get_hid2 - read Gekko HID2 (mfspr r3,HID2). Address: 0x80397b38
// ---------------------------------------------------------------------------
u32 os_get_hid2(void)
{
    return os_spr_shadow2[kSprHid2];
}

// ---------------------------------------------------------------------------
// os_set_hid2 - write Gekko HID2 (mtspr HID2,r3). Address: 0x80397b40
// ---------------------------------------------------------------------------
void os_set_hid2(u32 value)
{
    os_spr_shadow2[kSprHid2] = value;
}

// ---------------------------------------------------------------------------
// os_get_wpar - drain the store queue (sync) then read the write-gather-pipe
// address register WPAR (mfspr r3,WPAR). Address: 0x80397b48
// ---------------------------------------------------------------------------
u32 os_get_wpar(void)
{
    return os_spr_shadow2[kSprWpar];
}

// ---------------------------------------------------------------------------
// os_set_wpar - write the write-gather-pipe address register WPAR
// (mtspr WPAR,r3). Address: 0x80397b54
// ---------------------------------------------------------------------------
void os_set_wpar(u32 value)
{
    os_spr_shadow2[kSprWpar] = value;
}

// ---------------------------------------------------------------------------
// os_hid0_enable_feature - read HID0, set bit 0x200, write it back. Enables the
// corresponding Gekko HID0 feature bit (read-modify-write via os_get_hid0 /
// os_set_hid0). Address: 0x80397b5c
// ---------------------------------------------------------------------------
void os_hid0_enable_feature(void)
{
    u32 hid0 = os_get_hid0();
    os_set_hid0(hid0 | 0x200);
}
