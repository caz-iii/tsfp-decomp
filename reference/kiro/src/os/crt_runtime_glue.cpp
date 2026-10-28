// Address: 0x803825c0 | Name: rt_double_to_uint          | Date: 2026-09-13
// Address: 0x80382648 | Name: __save_fpr_25             | Date: 2026-09-13
// Address: 0x8038264c | Name: __save_fpr_26             | Date: 2026-09-13
// Address: 0x80382650 | Name: __save_fpr_27             | Date: 2026-09-13
// Address: 0x80382654 | Name: __save_fpr_28             | Date: 2026-09-13
// Address: 0x80382694 | Name: __restore_fpr_25          | Date: 2026-09-13
// Address: 0x80382698 | Name: __restore_fpr_26          | Date: 2026-09-13
// Address: 0x8038269c | Name: __restore_fpr_27          | Date: 2026-09-13
// Address: 0x803826a0 | Name: __restore_fpr_28          | Date: 2026-09-13
//
// CodeWarrior for GameCube (Metrowerks) PowerPC EABI compiler runtime glue.
//
// This translation unit collects the low-level, compiler-emitted helper block
// found at 0x803825c0..0x803826b0 in main.dol:
//
//   * rt_double_to_uint  - the runtime double->unsigned-int conversion helper
//     (~430 call sites; emitted for every (unsigned)double cast).
//   * __save_fpr_NN / __restore_fpr_NN - the floating-point register spill /
//     reload glue that non-leaf functions branch into from their prologue and
//     epilogue. The originals are a single fall-through run of stfd/lfd against
//     r11 (the caller's frame pointer) terminated by a blr; entry point NN saves
//     or restores fNN..f31.
//
// The save/restore routines are pure register/frame plumbing with no portable C
// equivalent - in the native x86 recompile the host compiler emits its own
// callee-saved-register handling, so these are provided only as documented,
// self-consistent no-ops to keep the symbol set complete and the file
// compile-clean. Only rt_double_to_uint carries real behaviour.

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
// rt_double_to_uint compares its argument against three runtime double
// constants living in the .sdata2/.data pool. They are read as doubles.
extern double DAT_803e10d8;  // low bound  (0.0)
extern double DAT_803e10e0;  // high bound (2^32)
extern double DAT_803e10e8;  // signed pivot (2^31)

// ---------------------------------------------------------------------------
// rt_double_to_uint (0x803825c0)
//
// Runtime helper for casting a double to a 32-bit unsigned int, matching the
// CodeWarrior float->unsigned lowering:
//   - below the low bound            -> 0
//   - at/above the high bound (2^32) -> 0xffffffff
//   - in [2^31, 2^32)                -> subtract 2^31, truncate, rebias by
//                                       INT_MIN so the unsigned value wraps
//   - otherwise                      -> plain truncation
// The rebias uses the two's-complement identity (x - 2^31) + INT_MIN == x for
// the unsigned bit pattern.
uint rt_double_to_uint(double param_1)
{
    int result;
    bool aboveSignedPivot;

    result = 0;
    if ((DAT_803e10d8 <= param_1) && (result = -1, param_1 < DAT_803e10e0)) {
        aboveSignedPivot = DAT_803e10e8 <= param_1;
        if (aboveSignedPivot) {
            param_1 = param_1 - DAT_803e10e8;
        }
        result = (int)param_1;
        if (aboveSignedPivot) {
            result = result + (int)0x80000000;  // += INT_MIN (rebias)
        }
    }
    return (uint)result;
}

// ---------------------------------------------------------------------------
// Floating-point register save glue (0x80382648..0x80382664).
// Entry __save_fpr_NN spills fNN..f31 to the caller frame; in the original the
// entries fall through into one another and share the single trailing blr.
void __save_fpr_25(void) { /* stfd f25..f31, -0x38(r11).. ; fall through */ }
void __save_fpr_26(void) { /* stfd f26..f31, -0x30(r11).. ; fall through */ }
void __save_fpr_27(void) { /* stfd f27..f31, -0x28(r11).. ; fall through */ }
void __save_fpr_28(void) { /* stfd f28..f31, -0x20(r11).. ; fall through */ }

// ---------------------------------------------------------------------------
// Floating-point register restore glue (0x80382694..0x803826b0).
// Entry __restore_fpr_NN reloads fNN..f31 from the caller frame; paired with
// the __save_fpr_* block above.
void __restore_fpr_25(void) { /* lfd f25..f31, -0x38(r11).. ; fall through */ }
void __restore_fpr_26(void) { /* lfd f26..f31, -0x30(r11).. ; fall through */ }
void __restore_fpr_27(void) { /* lfd f27..f31, -0x28(r11).. ; fall through */ }
void __restore_fpr_28(void) { /* lfd f28..f31, -0x20(r11).. ; fall through */ }

// ---------------------------------------------------------------------------
// batch_1064: remainder of the CodeWarrior runtime glue block.
//
// Address: 0x80382658 | Name: __save_fpr_29    | Date: 2026-09-13
// Address: 0x803826a4 | Name: __restore_fpr_29 | Date: 2026-09-13
// Address: 0x803826b4 | Name: __save_gpr_14    | Date: 2026-09-13
// Address: 0x803826b8 | Name: __save_gpr_15    | Date: 2026-09-13
// Address: 0x803826bc | Name: __save_gpr_16    | Date: 2026-09-13
// Address: 0x803826c0 | Name: __save_gpr_17    | Date: 2026-09-13
// Address: 0x803826c4 | Name: __save_gpr_18    | Date: 2026-09-13
// Address: 0x803826c8 | Name: __save_gpr_19    | Date: 2026-09-13
// Address: 0x803826cc | Name: __save_gpr_20    | Date: 2026-09-13
// Address: 0x803826d0 | Name: __save_gpr_21    | Date: 2026-09-13
//
// __save_fpr_29 / __restore_fpr_29 are the mid-sequence entry points of the FPR
// spill/reload runs documented above (spill/reload f29..f31). The __save_gpr_NN
// entries are the general-purpose-register spill glue: a shared fall-through run
// of stw rNN,-off(r11) terminated by the blr at 0x803826fc; entry NN stores
// rNN..r31 into the caller frame. Like the FPR glue these are compiler-emitted
// register plumbing with no portable C equivalent, provided as documented
// no-ops to keep the runtime symbol set complete and this TU compile-clean.

void __save_fpr_29(void)    { /* stfd f29..f31, -0x18(r11).. ; fall through */ }
void __restore_fpr_29(void) { /* lfd  f29..f31, -0x18(r11).. ; fall through */ }

void __save_gpr_14(void) { /* stw r14..r31, -0x48(r11).. ; fall through */ }
void __save_gpr_15(void) { /* stw r15..r31, -0x44(r11).. ; fall through */ }
void __save_gpr_16(void) { /* stw r16..r31, -0x40(r11).. ; fall through */ }
void __save_gpr_17(void) { /* stw r17..r31, -0x3c(r11).. ; fall through */ }
void __save_gpr_18(void) { /* stw r18..r31, -0x38(r11).. ; fall through */ }
void __save_gpr_19(void) { /* stw r19..r31, -0x34(r11).. ; fall through */ }
void __save_gpr_20(void) { /* stw r20..r31, -0x30(r11).. ; fall through */ }
void __save_gpr_21(void) { /* stw r21..r31, -0x2c(r11).. ; fall through */ }

// ---------------------------------------------------------------------------
// batch_1065: rest of the GPR save block + start of the GPR restore block.
//
// Address: 0x803826d4 | Name: __save_gpr_22    | Date: 2026-09-13
// Address: 0x803826d8 | Name: __save_gpr_23    | Date: 2026-09-13
// Address: 0x803826dc | Name: __save_gpr_24    | Date: 2026-09-13
// Address: 0x803826e0 | Name: __save_gpr_25    | Date: 2026-09-13
// Address: 0x803826e4 | Name: __save_gpr_26    | Date: 2026-09-13
// Address: 0x803826e8 | Name: __save_gpr_27    | Date: 2026-09-13
// Address: 0x803826ec | Name: __save_gpr_28    | Date: 2026-09-13
// Address: 0x803826f0 | Name: __save_gpr_29    | Date: 2026-09-13
// Address: 0x80382700 | Name: __restore_gpr_14 | Date: 2026-09-13
// Address: 0x80382704 | Name: __restore_gpr_15 | Date: 2026-09-13
//
// __save_gpr_22..29 continue the GPR spill run to the blr at 0x803826fc
// (__save_gpr_29 folds r29..r31 + the trailing blr into one 16-byte body).
// __restore_gpr_14/15 open the paired GPR reload run (lwz rN..r31, -off(r11))
// which terminates at the blr at 0x80382748. Compiler-emitted register
// plumbing, documented no-ops in the recompile.

void __save_gpr_22(void) { /* stw r22..r31, -0x28(r11).. ; fall through */ }
void __save_gpr_23(void) { /* stw r23..r31, -0x24(r11).. ; fall through */ }
void __save_gpr_24(void) { /* stw r24..r31, -0x20(r11).. ; fall through */ }
void __save_gpr_25(void) { /* stw r25..r31, -0x1c(r11).. ; fall through */ }
void __save_gpr_26(void) { /* stw r26..r31, -0x18(r11).. ; fall through */ }
void __save_gpr_27(void) { /* stw r27..r31, -0x14(r11).. ; fall through */ }
void __save_gpr_28(void) { /* stw r28..r31, -0x10(r11).. ; fall through */ }
void __save_gpr_29(void) { /* stw r29..r31, -0x0c(r11).. ; blr */ }

void __restore_gpr_14(void) { /* lwz r14..r31, -0x48(r11).. ; fall through */ }
void __restore_gpr_15(void) { /* lwz r15..r31, -0x44(r11).. ; fall through */ }

// ---------------------------------------------------------------------------
// batch_1066: continuation of the GPR restore block.
//
// Address: 0x80382708 | Name: __restore_gpr_16 | Date: 2026-09-13
// Address: 0x8038270c | Name: __restore_gpr_17 | Date: 2026-09-13
// Address: 0x80382710 | Name: __restore_gpr_18 | Date: 2026-09-13
// Address: 0x80382714 | Name: __restore_gpr_19 | Date: 2026-09-13
// Address: 0x80382718 | Name: __restore_gpr_20 | Date: 2026-09-13
// Address: 0x8038271c | Name: __restore_gpr_21 | Date: 2026-09-13
// Address: 0x80382720 | Name: __restore_gpr_22 | Date: 2026-09-13
// Address: 0x80382724 | Name: __restore_gpr_23 | Date: 2026-09-13
// Address: 0x80382728 | Name: __restore_gpr_24 | Date: 2026-09-13
// Address: 0x8038272c | Name: __restore_gpr_25 | Date: 2026-09-13
//
// __restore_gpr_16..25 continue the shared lwz reload run to the blr at
// 0x80382748; entry NN reloads rNN..r31 from the caller frame via r11.
// Compiler-emitted register plumbing, documented no-ops in the recompile.

void __restore_gpr_16(void) { /* lwz r16..r31, -0x40(r11).. ; fall through */ }
void __restore_gpr_17(void) { /* lwz r17..r31, -0x3c(r11).. ; fall through */ }
void __restore_gpr_18(void) { /* lwz r18..r31, -0x38(r11).. ; fall through */ }
void __restore_gpr_19(void) { /* lwz r19..r31, -0x34(r11).. ; fall through */ }
void __restore_gpr_20(void) { /* lwz r20..r31, -0x30(r11).. ; fall through */ }
void __restore_gpr_21(void) { /* lwz r21..r31, -0x2c(r11).. ; fall through */ }
void __restore_gpr_22(void) { /* lwz r22..r31, -0x28(r11).. ; fall through */ }
void __restore_gpr_23(void) { /* lwz r23..r31, -0x24(r11).. ; fall through */ }
void __restore_gpr_24(void) { /* lwz r24..r31, -0x20(r11).. ; fall through */ }
void __restore_gpr_25(void) { /* lwz r25..r31, -0x1c(r11).. ; fall through */ }

// ---------------------------------------------------------------------------
// batch_1067: tail of the GPR restore block (end of the CodeWarrior register
// spill/reload glue region).
//
// Address: 0x80382730 | Name: __restore_gpr_26 | Date: 2026-09-13
// Address: 0x80382734 | Name: __restore_gpr_27 | Date: 2026-09-13
// Address: 0x80382738 | Name: __restore_gpr_28 | Date: 2026-09-13
// Address: 0x8038273c | Name: __restore_gpr_29 | Date: 2026-09-13
//
// __restore_gpr_26..29 close the shared lwz reload run; entry NN reloads
// rNN..r31 from the caller frame via r11. __restore_gpr_29 folds r29..r31 and
// the terminating blr (0x80382748) into its 16-byte body. This is the final
// entry of the compiler-emitted register-plumbing block; documented no-ops in
// the native x86 recompile, where the host compiler emits its own
// callee-saved-register handling.

void __restore_gpr_26(void) { /* lwz r26..r31, -0x18(r11).. ; fall through */ }
void __restore_gpr_27(void) { /* lwz r27..r31, -0x14(r11).. ; fall through */ }
void __restore_gpr_28(void) { /* lwz r28..r31, -0x10(r11).. ; fall through */ }
void __restore_gpr_29(void) { /* lwz r29..r31, -0x0c(r11).. ; blr */ }
