// MetroTRK hardware-target dispatch and transport-hook layer.
//
// This file sits above the low-level transport drivers. TRK talks to the host
// debugger through a small vtable of function-pointer slots at DAT_804980xx
// (send, recv, open, poll, idle, etc.). trk_select_hardware() installs the
// right set of hooks for the detected debug hardware (BBA / GDEV / AMC-DDH),
// and the thin wrappers here forward the command handlers and the read loop to
// whichever transport is currently installed.
//
// Batch: batch_1090
// Source module: os/trk_hw_dispatch.cpp

#include "common.h"

// ---------------------------------------------------------------------------
// Transport-hook vtable slots (populated by trk_select_hardware).
// Each is a raw code pointer; the wrappers below invoke the current one.
// ---------------------------------------------------------------------------
extern void *DAT_80498068;              // exception-install hook (takes ctx, handler)
extern int (*DAT_8049806c)(void);       // connect-active / idle callback (nullable)
extern int (*DAT_80498070)(void);       // (installed, unused by this file)
extern int (*DAT_80498074)(void);       // transport read/poll hook
extern int (*DAT_80498078)(void);       // transport status hook A
extern int (*DAT_8049807c)(void);       // transport status hook B
extern void (*DAT_80498080)(void);      // transport bring-up hook
extern void *DAT_80498084;              // (installed, unused by this file)
extern int (*DAT_80498088)(void);       // callback hook 0x88 (see trk_call_hook_88)
extern int (*DAT_8049808c)(void);       // callback hook 0x8c (see trk_call_hook_8c)

// Selected-hardware flag: nonzero when the BBA (software) transport is active.
extern unsigned char DAT_805f1940;
// Generic TRK state byte exposed through the accessor pair below.
extern unsigned char DAT_805f1948;

// Diagnostic format strings.
extern char s_Devkit_set_to____ld_803e1bd0[];            // "Devkit set to : %ld\n"
extern char s_MetroTRK___Set_to_BBA_803e1be8[];          // "MetroTRK : Set to BBA\n"
extern char s_MetroTRK___Set_to_GDEV_hardware_803e1c00[];
extern char s_MetroTRK___Set_to_AMC_DDH_hardwa_803e1c24[];
extern char s_MetroTRK___Set_to_UNKNOWN_hardwa_803e1c48[];
extern char s_MetroTRK___Invalid_hardware_ID_p_803e1c74[];
extern char s_MetroTRK___Defaulting_to_GDEV_Ha_803e1ca4[];

// External helpers referenced by the dispatcher / exception path.
// (OSReport is declared in sdk_stubs.h via common.h.)
extern void trk_handle_exception(void *frame, int cause, int a3, int a4, int a5,
                                 int a6, int a7, int a8);
extern void trk_exception_handler(int hi, int lo, int a3, int a4, int a5,
                                  int a6, int a7, int a8);
extern void trk_set_run_state(int state);
extern unsigned long long trk_call_hook_88(void);
extern int trk_call_hook_8c(void);
extern void FUN_8039f39c(void);           // pre-exception save entry

// GDEV / AMC-DDH transport driver entry points installed into the vtable.
extern int FUN_803b8b94(void);            // GDEV bring-up
extern int FUN_803b8b8c(void);            // AMC-DDH bring-up
extern void FUN_80396ebc(void);           extern int FUN_80396d94(void);
extern int FUN_80396cd4(void);            extern int FUN_80396c1c(void);
extern int FUN_80396cb0(void);            extern int FUN_80396c8c(void);
extern int FUN_80396bf8(void);
extern void FUN_80396908(void);           extern int FUN_803967e8(void);
extern int FUN_80396728(void);            extern int FUN_80396670(void);
extern int FUN_80396704(void);            extern int FUN_803966e0(void);
extern int FUN_8039664c(void);

// Address of the 0x500 external-interrupt trampoline installed by trk_init.
extern void trk_hw_ext_interrupt_entry(void);   // == FUN_80396404 below

// BBA (software) transport uses inline-asm label stubs; declare their addresses.
extern void LAB_80396644(void);  extern void LAB_80396634(void);
extern void LAB_8039662c(void);  extern void LAB_80396624(void);
extern void LAB_8039661c(void);  extern void LAB_8039663c(void);
extern void LAB_80396614(void);  extern void LAB_8039660c(void);
extern void LAB_80396604(void);

// External-branch label targets referenced by the GDEV/AMC hook tables.
extern void LAB_80396e90(void);  extern void LAB_80396e88(void);
extern void LAB_80396eb4(void);
extern void LAB_803968dc(void);  extern void LAB_803968d4(void);
extern void LAB_80396900(void);

// ---------------------------------------------------------------------------
// Address: 0x80396058 | Name: trk_transport_status_b | Date: 2026-09-14
// Forwards to transport status hook B and normalises the result to 0/1
// (returns -1..0 arithmetic-shifted so any nonzero becomes -1 => 0/1 truthy).
// ---------------------------------------------------------------------------
int trk_transport_status_b(void)
{
    unsigned int result = DAT_8049807c();
    return (int)(-result | result) >> 0x1f;
}

// ---------------------------------------------------------------------------
// Address: 0x80396094 | Name: trk_transport_status_a | Date: 2026-09-14
// Forwards to transport status hook A, same 0/1 normalisation.
// ---------------------------------------------------------------------------
int trk_transport_status_a(void)
{
    unsigned int result = DAT_80498078();
    return (int)(-result | result) >> 0x1f;
}

// ---------------------------------------------------------------------------
// Address: 0x803960d0 | Name: trk_transport_read | Date: 2026-09-14
// The read/poll hook driven by trk_read_packet.
// ---------------------------------------------------------------------------
void trk_transport_read(void)
{
    DAT_80498074();
}

// ---------------------------------------------------------------------------
// Address: 0x80396100 | Name: trk_transport_idle | Date: 2026-09-14
// The idle callback (from trk_call_idle_hook). Only fires when the BBA
// software transport is NOT active and a hook has been installed.
// ---------------------------------------------------------------------------
void trk_transport_idle(void)
{
    if (DAT_805f1940 == 0 && DAT_8049806c != 0) {
        DAT_8049806c();
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80396148 | Name: trk_install_ext_interrupt | Date: 2026-09-14
// Called from trk_init: installs the 0x500 external-interrupt entry through
// the exception-install hook, then brings the transport up.
// ---------------------------------------------------------------------------
int trk_install_ext_interrupt(void)
{
    // r6 carries the exception-context pointer from the caller's frame.
    void *ctx = 0;
    ((void (*)(void *, void *))DAT_80498068)(ctx, (void *)&trk_hw_ext_interrupt_entry);
    DAT_80498080();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396198 | Name: trk_select_hardware | Date: 2026-09-14
// Installs the transport-hook vtable for the requested debug hardware ID.
//   2 = BBA (software transport, inline-asm label stubs; sets DAT_805f1940)
//   1 = GDEV hardware  (FUN_80396exx driver set, brings up via FUN_803b8b94)
//   0 = AMC-DDH hardware (FUN_803968xx driver set, brings up via FUN_803b8b8c)
//   other = log an error and fall back to GDEV defaults.
// Returns the bring-up status (0 on the BBA path).
// ---------------------------------------------------------------------------
int trk_select_hardware(int hardware_id)
{
    int status = 1;

    OSReport(s_Devkit_set_to____ld_803e1bd0, hardware_id);
    DAT_805f1940 = 0;

    if (hardware_id == 2) {
        OSReport(s_MetroTRK___Set_to_BBA_803e1be8);
        DAT_805f1940 = 1;
        status = 0;
        DAT_80498068 = (void *)&LAB_80396644;
        DAT_80498080 = (void (*)(void))&LAB_80396634;
        DAT_80498084 = (void *)&LAB_8039662c;
        DAT_80498078 = (int (*)(void))&LAB_80396624;
        DAT_8049807c = (int (*)(void))&LAB_8039661c;
        DAT_80498070 = (int (*)(void))&LAB_8039663c;
        DAT_80498074 = (int (*)(void))&LAB_80396614;
        DAT_80498088 = (int (*)(void))&LAB_8039660c;
        DAT_8049808c = (int (*)(void))&LAB_80396604;
        DAT_8049806c = 0;
    } else if (hardware_id == 1) {
        OSReport(s_MetroTRK___Set_to_GDEV_hardware_803e1c00);
        status = FUN_803b8b94();
        DAT_80498068 = (void *)&FUN_80396ebc;
        DAT_80498080 = (void (*)(void))&LAB_80396e90;
        DAT_80498084 = (void *)&LAB_80396e88;
        DAT_80498078 = FUN_80396d94;
        DAT_8049807c = FUN_80396cd4;
        DAT_80498070 = (int (*)(void))&LAB_80396eb4;
        DAT_80498074 = FUN_80396c1c;
        DAT_80498088 = FUN_80396cb0;
        DAT_8049808c = FUN_80396c8c;
        DAT_8049806c = FUN_80396bf8;
    } else if (hardware_id == 0) {
        OSReport(s_MetroTRK___Set_to_AMC_DDH_hardwa_803e1c24);
        status = FUN_803b8b8c();
        DAT_80498068 = (void *)&FUN_80396908;
        DAT_80498080 = (void (*)(void))&LAB_803968dc;
        DAT_80498084 = (void *)&LAB_803968d4;
        DAT_80498078 = FUN_803967e8;
        DAT_8049807c = FUN_80396728;
        DAT_80498070 = (int (*)(void))&LAB_80396900;
        DAT_80498074 = FUN_80396670;
        DAT_80498088 = FUN_80396704;
        DAT_8049808c = FUN_803966e0;
        DAT_8049806c = FUN_8039664c;
    } else {
        OSReport(s_MetroTRK___Set_to_UNKNOWN_hardwa_803e1c48, hardware_id);
        OSReport(s_MetroTRK___Invalid_hardware_ID_p_803e1c74);
        OSReport(s_MetroTRK___Defaulting_to_GDEV_Ha_803e1ca4);
    }
    return status;
}

// ---------------------------------------------------------------------------
// Address: 0x80396404 | Name: trk_hw_ext_interrupt_entry | Date: 2026-09-14
// The 0x500 external-interrupt vector entry installed by trk_install_ext_interrupt.
// Runs the pre-exception save then dispatches with cause 0x500.
// ---------------------------------------------------------------------------
void trk_hw_ext_interrupt_entry(int param_1, unsigned int *frame, int a3, int a4,
                                int a5, int a6, int a7, int a8)
{
    (void)param_1;
    FUN_8039f39c();
    trk_handle_exception(frame, 0x500, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// Address: 0x8039643c | Name: trk_nested_exception_entry | Date: 2026-09-14
// Nested/secondary exception re-entry: halts the target, runs hook 0x88,
// feeds the returned register pair into the core exception handler, then
// runs hook 0x8c.
// ---------------------------------------------------------------------------
int trk_nested_exception_entry(int param_1, int param_2, int a3, int a4, int a5,
                               int a6, int a7, int a8)
{
    (void)param_1; (void)param_2;
    unsigned long long regpair;

    trk_set_run_state(0);
    regpair = trk_call_hook_88();
    trk_exception_handler((int)(regpair >> 0x20), (int)regpair, a3, a4, a5, a6, a7, a8);
    trk_call_hook_8c();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396470 | Name: trk_get_state_byte | Date: 2026-09-14
// ---------------------------------------------------------------------------
unsigned char trk_get_state_byte(void)
{
    return DAT_805f1948;
}

// ---------------------------------------------------------------------------
// Address: 0x80396480 | Name: trk_set_state_byte | Date: 2026-09-14
// ---------------------------------------------------------------------------
void trk_set_state_byte(unsigned char value)
{
    DAT_805f1948 = value;
}

// ============================================================================
// batch_1150: MetroTRK hardware transport default-hook stubs + the debug-comm
// interrupt-mask policy. These sit at the tail of the hw-dispatch layer: the
// nine tiny functions are the default transport-hook vtable slots installed by
// trk_select_hardware for hardware that needs no per-slot action (no-op void
// hooks, "not-ready"/0 pollers, and a "ready"/1 poller), and
// trk_hw_set_int_mask is the routine that masks or unmasks the debug interrupt
// line for the currently-selected transport (USB / serial-EXI / DDH-AMC).
//
// NON-MATCHING: the raw OS interrupt-mask bit constants and the DAT flag are
// preserved verbatim. Per-file externs are declared for the OS interrupt
// helpers and the DAT this batch references.

// ---- batch_1150 per-file externs ------------------------------------------
extern int  os_mask_interrupts(unsigned int mask);       // mask (disable) the given HW irq bits
extern void os_unmask_interrupts(unsigned int mask);     // unmask (enable) the given HW irq bits
extern int  os_get_interrupt_handler(int cause);         // fetch the registered handler for a cause
extern int  DAT_8060e9c0;   // secondary serial/EXI transport present flag

// trk_hw_noop_hook_a (0x803b8b64): default transport hook, no-op void.
void trk_hw_noop_hook_a(void)
{
    return;
}

// trk_hw_noop_hook_b (0x803b8b68): default transport hook, no-op void.
void trk_hw_noop_hook_b(void)
{
    return;
}

// trk_hw_ret0_hook_a (0x803b8b6c): default transport hook, returns 0 (not
// ready / no-op success).
undefined4 trk_hw_ret0_hook_a(void)
{
    return 0;
}

// trk_hw_ret0_hook_b (0x803b8b74): default transport hook, returns 0.
undefined4 trk_hw_ret0_hook_b(void)
{
    return 0;
}

// trk_hw_ret0_hook_c (0x803b8b7c): default transport hook, returns 0.
undefined4 trk_hw_ret0_hook_c(void)
{
    return 0;
}

// trk_hw_noop_hook_c (0x803b8b84): default transport hook, no-op void.
void trk_hw_noop_hook_c(void)
{
    return;
}

// trk_hw_noop_hook_d (0x803b8b88): default transport hook, no-op void.
void trk_hw_noop_hook_d(void)
{
    return;
}

// trk_hw_ret1_hook (0x803b8b8c): default transport hook, returns 1 (ready /
// always-true poll).
undefined4 trk_hw_ret1_hook(void)
{
    return 1;
}

// trk_hw_ret0_hook_d (0x803b8b94): default transport hook, returns 0.
undefined4 trk_hw_ret0_hook_d(void)
{
    return 0;
}

// trk_hw_set_int_mask (0x803b8b9c): mask or unmask the debug-comm interrupt line
// for the selected transport. Dispatches on the transport id:
//   id 1 (USB/UART, irq bit 0x80000): unmask when the transport is present and
//     not in the "masked" state (flags +0x0c bit 0x10), else mask.
//   id 0 (serial / EXI, irq bit 0x410000): unmask when present (or the secondary
//     serial flag DAT_8060e9c0 is set) and not masked, else mask.
//   id 2 (DDH / debugger cable, irq bit 0x40): only when an interrupt-0x19
//     handler is installed; unmask when not masked, else mask.
// Negative or out-of-range ids are ignored.
void trk_hw_set_int_mask(int transportId, int* state)
{
    int handler;

    if (transportId == 1) {
        if ((*state == 0) || ((state[3] & 0x10U) != 0)) {
            os_mask_interrupts(0x80000);
        } else {
            os_unmask_interrupts(0x80000);
        }
    } else if (transportId < 1) {
        if (-1 < transportId) {
            if (((*state == 0) && (DAT_8060e9c0 == 0)) || ((state[3] & 0x10U) != 0)) {
                os_mask_interrupts(0x410000);
            } else {
                os_unmask_interrupts(0x410000);
            }
        }
    } else if (transportId < 3) {
        handler = os_get_interrupt_handler(0x19);
        if ((handler == 0) || ((state[3] & 0x10U) != 0)) {
            os_mask_interrupts(0x40);
        } else {
            os_unmask_interrupts(0x40);
        }
    }
    return;
}
