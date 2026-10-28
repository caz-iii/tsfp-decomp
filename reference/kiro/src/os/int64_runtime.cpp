// Address: 0x8038274c | Name: __div2u  | Date: 2026-09-13
// Address: 0x80382838 | Name: __div2i  | Date: 2026-09-13
// Address: 0x80382970 | Name: __mod2u  | Date: 2026-09-13
// Address: 0x80382a54 | Name: __mod2i  | Date: 2026-09-13
// Address: 0x80382b60 | Name: __shl2i  | Date: 2026-09-13
// Address: 0x80382b84 | Name: __shr2u  | Date: 2026-09-13
//
// CodeWarrior for GameCube (Metrowerks) PowerPC EABI 64-bit integer runtime.
//
// PowerPC Gekko/Broadway is a 32-bit core with no native 64-bit divide, modulo,
// or variable shift, so the compiler lowers every `long long` divide/modulo/shift
// to a call into this helper block at 0x8038274c..0x80382bb0. Each 64-bit operand
// is passed as a hi/lo register pair (r3:r4 = dividend hi:lo, r5:r6 = divisor
// hi:lo) and the 64-bit result is returned in r3:r4. The decompiler surfaces the
// pairs as separate uint parameters and the return as CONCAT44(hi, lo); this TU
// preserves that ABI shape 1:1 so the arithmetic matches the original exactly.
//
// The divide/modulo core is the classic restoring long-division: normalise both
// operands by their leading-zero counts, then run a shift-subtract loop for the
// number of quotient bits. countLeadingZeros() and CONCAT44() are the Ghidra
// intrinsics provided by common.h.

#include "common.h"

// ---------------------------------------------------------------------------
// __div2u (0x8038274c) - unsigned 64 / 64 -> 64-bit quotient.
//   dividend = hiDividend:loDividend, divisor = hiDivisor:loDivisor.
// Returns 0 when the divisor is larger than the dividend (quotient underflow).
u64 __div2u(u32 loDividend, u32 hiDividend, s32 hiDivisor, u32 loDivisor)
{
    u32 carryLo;
    u32 remLo;
    int dividendClz;
    u32 accLo;
    u32 accHi;
    u32 quotHi;
    u32 quotLo;
    u32 remHi;
    u32 tmp;
    int divisorClz;
    u32 trial;
    int bitCount;
    bool addCarry;
    bool subBorrow;

    dividendClz = countLeadingZeros(loDividend);
    divisorClz = countLeadingZeros(hiDividend);
    if (loDividend == 0) {
        dividendClz = divisorClz + 0x20;
    }
    divisorClz = countLeadingZeros(hiDivisor);
    bitCount = countLeadingZeros(loDivisor);
    if (hiDivisor == 0) {
        divisorClz = bitCount + 0x20;
    }
    if (dividendClz <= divisorClz) {
        bitCount = 0x40 - (divisorClz + 1);
        divisorClz = dividendClz + bitCount;
        bitCount = (0x40 - dividendClz) - bitCount;
        if (bitCount < 0x20) {
            quotLo = loDividend >> bitCount | hiDividend << 0x20 - bitCount;
            quotHi = hiDividend >> bitCount;
        }
        else {
            quotLo = hiDividend >> bitCount + -0x20;
            quotHi = 0;
        }
        if (divisorClz < 0x20) {
            accLo = hiDividend << divisorClz | loDividend >> 0x20 - divisorClz;
            accHi = loDividend << divisorClz;
        }
        else {
            accLo = loDividend << divisorClz + -0x20;
            accHi = 0;
        }
        addCarry = false;
        do {
            subBorrow = CARRY4(accHi, (u32)addCarry);
            carryLo = accHi + addCarry;
            addCarry = CARRY4(accHi, carryLo);
            accHi = accHi + carryLo;
            carryLo = (u32)(subBorrow || addCarry);
            subBorrow = CARRY4(accLo, carryLo);
            carryLo = accLo + carryLo;
            addCarry = CARRY4(accLo, carryLo);
            accLo = accLo + carryLo;
            carryLo = (u32)(subBorrow || addCarry);
            remLo = quotLo + carryLo;
            remHi = quotLo + remLo;
            carryLo = quotHi * 2 + (u32)(CARRY4(quotLo, carryLo) || CARRY4(quotLo, remLo));
            remLo = (u32)(remHi < loDivisor) + hiDivisor;
            trial = carryLo - remLo;
            quotHi = carryLo;
            quotLo = remHi;
            if (-1 < (s32)trial) {
                quotHi = trial;
                quotLo = remHi - loDivisor;
            }
            addCarry = -1 < (s32)trial || remLo <= carryLo;
            bitCount = bitCount + -1;
        } while (bitCount != 0);
        quotHi = accHi + addCarry;
        return CONCAT44(accLo * 2 + (u32)(CARRY4(accHi, (u32)addCarry) || CARRY4(accHi, quotHi)),
                        accHi + quotHi);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// __div2i (0x80382838) - signed 64 / 64 -> 64-bit quotient.
// Records the sign of each operand, negates negatives to run the unsigned core,
// then re-applies the combined sign to the quotient.
u64 __div2i(u32 loDividend, u32 hiDividend, u32 hiDivisor, u32 loDivisor)
{
    u32 carryLo;
    u32 tmp;
    u32 dividendSign;
    u32 divisorSign;
    int dividendClz;
    u32 accLo;
    u32 accHi;
    u32 quotHi;
    u32 quotLo;
    u32 remHi;
    u32 remLo;
    int divisorClz;
    u32 trial;
    int bitCount;
    bool addCarry;
    bool nonZero;

    dividendSign = hiDividend & 0x80000000;
    if (dividendSign != 0) {
        nonZero = loDividend != 0;
        loDividend = -loDividend;
        hiDividend = -(nonZero + hiDividend);
    }
    divisorSign = hiDivisor & 0x80000000;
    if (divisorSign != 0) {
        nonZero = loDivisor != 0;
        loDivisor = -loDivisor;
        hiDivisor = -(nonZero + hiDivisor);
    }
    dividendClz = countLeadingZeros(hiDividend);
    bitCount = countLeadingZeros(loDividend);
    if (hiDividend == 0) {
        dividendClz = bitCount + 0x20;
    }
    bitCount = countLeadingZeros(hiDivisor);
    divisorClz = countLeadingZeros(loDivisor);
    if (hiDivisor == 0) {
        bitCount = divisorClz + 0x20;
    }
    if (bitCount < dividendClz) {
        bitCount = 0;
        dividendClz = 0;
    }
    else {
        divisorClz = 0x40 - (bitCount + 1);
        bitCount = dividendClz + divisorClz;
        divisorClz = (0x40 - dividendClz) - divisorClz;
        if (divisorClz < 0x20) {
            quotLo = loDividend >> divisorClz | hiDividend << 0x20 - divisorClz;
            quotHi = hiDividend >> divisorClz;
        }
        else {
            quotLo = hiDividend >> divisorClz + -0x20;
            quotHi = 0;
        }
        if (bitCount < 0x20) {
            accLo = hiDividend << bitCount | loDividend >> 0x20 - bitCount;
            accHi = loDividend << bitCount;
        }
        else {
            accLo = loDividend << bitCount + -0x20;
            accHi = 0;
        }
        addCarry = false;
        do {
            nonZero = CARRY4(accHi, (u32)addCarry);
            carryLo = accHi + addCarry;
            addCarry = CARRY4(accHi, carryLo);
            accHi = accHi + carryLo;
            carryLo = (u32)(nonZero || addCarry);
            nonZero = CARRY4(accLo, carryLo);
            carryLo = accLo + carryLo;
            addCarry = CARRY4(accLo, carryLo);
            accLo = accLo + carryLo;
            carryLo = (u32)(nonZero || addCarry);
            remLo = quotLo + carryLo;
            remHi = quotLo + remLo;
            carryLo = quotHi * 2 + (u32)(CARRY4(quotLo, carryLo) || CARRY4(quotLo, remLo));
            remLo = (remHi < loDivisor) + hiDivisor;
            trial = carryLo - remLo;
            quotHi = carryLo;
            quotLo = remHi;
            if (-1 < (s32)trial) {
                quotHi = trial;
                quotLo = remHi - loDivisor;
            }
            addCarry = -1 < (s32)trial || remLo <= carryLo;
            divisorClz = divisorClz + -1;
        } while (divisorClz != 0);
        quotHi = accHi + addCarry;
        bitCount = accHi + quotHi;
        dividendClz = accLo * 2 + (u32)(CARRY4(accHi, (u32)addCarry) || CARRY4(accHi, quotHi));
        if (dividendSign != divisorSign) {
            nonZero = bitCount != 0;
            bitCount = -bitCount;
            dividendClz = -((u32)nonZero + dividendClz);
        }
    }
    return CONCAT44(dividendClz, bitCount);
}

// ---------------------------------------------------------------------------
// __mod2u (0x80382970) - unsigned 64 % 64 -> 64-bit remainder.
// Same normalise + shift-subtract core as __div2u, but returns the running
// remainder (accumulator) instead of the quotient. When the divisor exceeds the
// dividend the dividend is returned unchanged.
u64 __mod2u(u32 loDividend, u32 hiDividend, s32 hiDivisor, u32 loDivisor)
{
    u32 carryLo;
    u32 tmp;
    int dividendClz;
    u32 accLo;
    u32 remLo;
    u32 remHi;
    u32 quotLo;
    u32 accHi;
    int divisorClz;
    u32 trial;
    int bitCount;
    bool addCarry;
    bool subBorrow;

    dividendClz = countLeadingZeros(loDividend);
    divisorClz = countLeadingZeros(hiDividend);
    if (loDividend == 0) {
        dividendClz = divisorClz + 0x20;
    }
    divisorClz = countLeadingZeros(hiDivisor);
    bitCount = countLeadingZeros(loDivisor);
    if (hiDivisor == 0) {
        divisorClz = bitCount + 0x20;
    }
    if (divisorClz < dividendClz) {
        return CONCAT44(loDividend, hiDividend);
    }
    bitCount = 0x40 - (divisorClz + 1);
    divisorClz = dividendClz + bitCount;
    bitCount = (0x40 - dividendClz) - bitCount;
    if (bitCount < 0x20) {
        remLo = loDividend >> bitCount | hiDividend << 0x20 - bitCount;
        accLo = hiDividend >> bitCount;
    }
    else {
        remLo = hiDividend >> bitCount + -0x20;
        accLo = 0;
    }
    if (divisorClz < 0x20) {
        quotLo = loDividend << divisorClz | hiDividend >> 0x20 - divisorClz;
        accHi = hiDividend << divisorClz;
    }
    else {
        quotLo = hiDividend << divisorClz + -0x20;
        accHi = 0;
    }
    addCarry = false;
    do {
        subBorrow = CARRY4(accHi, (u32)addCarry);
        carryLo = accHi + addCarry;
        addCarry = CARRY4(accHi, carryLo);
        accHi = accHi + carryLo;
        carryLo = (u32)(subBorrow || addCarry);
        subBorrow = CARRY4(quotLo, carryLo);
        carryLo = quotLo + carryLo;
        addCarry = CARRY4(quotLo, carryLo);
        quotLo = quotLo + carryLo;
        carryLo = (u32)(subBorrow || addCarry);
        tmp = remLo + carryLo;
        remHi = remLo + tmp;
        carryLo = accLo * 2 + (u32)(CARRY4(remLo, carryLo) || CARRY4(remLo, tmp));
        tmp = (u32)(remHi < loDivisor) + hiDivisor;
        trial = carryLo - tmp;
        accLo = carryLo;
        remLo = remHi;
        if (-1 < (s32)trial) {
            accLo = trial;
            remLo = remHi - loDivisor;
        }
        addCarry = -1 < (s32)trial || tmp <= carryLo;
        bitCount = bitCount + -1;
    } while (bitCount != 0);
    return CONCAT44(accLo, remLo);
}

// ---------------------------------------------------------------------------
// __mod2i (0x80382a54) - signed 64 % 64 -> 64-bit remainder.
// The sign of the remainder follows the dividend (C99 semantics), so only the
// dividend sign is re-applied to the result.
u64 __mod2i(u32 loDividend, u32 hiDividend, s32 hiDivisor, u32 loDivisor)
{
    bool dividendNegative;
    int dividendClz;
    u32 accLo;
    u32 accHi;
    u32 remLo;
    u32 remHi;
    u32 tmp;
    int divisorClz;
    u32 trial;
    int bitCount;
    bool addCarry;
    bool nonZero;

    dividendNegative = (s32)hiDividend < 0;
    if (dividendNegative) {
        nonZero = loDividend != 0;
        loDividend = -loDividend;
        hiDividend = -(nonZero + hiDividend);
    }
    if (hiDivisor < 0) {
        nonZero = loDivisor != 0;
        loDivisor = -loDivisor;
        hiDivisor = -((u32)nonZero + hiDivisor);
    }
    dividendClz = countLeadingZeros(hiDividend);
    divisorClz = countLeadingZeros(loDividend);
    if (hiDividend == 0) {
        dividendClz = divisorClz + 0x20;
    }
    divisorClz = countLeadingZeros(hiDivisor);
    bitCount = countLeadingZeros(loDivisor);
    if (hiDivisor == 0) {
        divisorClz = bitCount + 0x20;
    }
    if (dividendClz <= divisorClz) {
        bitCount = 0x40 - (divisorClz + 1);
        divisorClz = dividendClz + bitCount;
        bitCount = (0x40 - dividendClz) - bitCount;
        if (bitCount < 0x20) {
            remLo = loDividend >> bitCount | hiDividend << 0x20 - bitCount;
            accLo = hiDividend >> bitCount;
        }
        else {
            remLo = hiDividend >> bitCount + -0x20;
            accLo = 0;
        }
        if (divisorClz < 0x20) {
            accHi = loDividend << divisorClz | hiDividend >> 0x20 - divisorClz;
            remHi = hiDividend << divisorClz;
        }
        else {
            accHi = hiDividend << divisorClz + -0x20;
            remHi = 0;
        }
        addCarry = false;
        hiDividend = accLo;
        loDividend = remLo;
        do {
            nonZero = CARRY4(remHi, (u32)addCarry);
            accLo = remHi + addCarry;
            addCarry = CARRY4(remHi, accLo);
            remHi = remHi + accLo;
            accLo = (u32)(nonZero || addCarry);
            nonZero = CARRY4(accHi, accLo);
            accLo = accHi + accLo;
            addCarry = CARRY4(accHi, accLo);
            accHi = accHi + accLo;
            accLo = (u32)(nonZero || addCarry);
            remLo = loDividend + accLo;
            tmp = loDividend + remLo;
            accLo = hiDividend * 2 + (u32)(CARRY4(loDividend, accLo) || CARRY4(loDividend, remLo));
            remLo = (u32)(tmp < loDivisor) + hiDivisor;
            trial = accLo - remLo;
            hiDividend = accLo;
            loDividend = tmp;
            if (-1 < (s32)trial) {
                hiDividend = trial;
                loDividend = tmp - loDivisor;
            }
            addCarry = -1 < (s32)trial || remLo <= accLo;
            bitCount = bitCount + -1;
        } while (bitCount != 0);
    }
    if (dividendNegative) {
        return CONCAT44(-((loDividend != 0) + hiDividend), -loDividend);
    }
    return CONCAT44(hiDividend, loDividend);
}

// ---------------------------------------------------------------------------
// __shl2i (0x80382b60) - 64-bit logical left shift by a variable amount.
//   value = hi:lo, shift = param_3. Handles shift >= 32 via the
//   (lo << shift-32) term that the >> 32-shift term collapses to zero for small
//   shifts, matching the branchless PPC lowering.
u64 __shl2i(s32 hiValue, u32 loValue, int shift)
{
    return CONCAT44(hiValue << shift | loValue >> 0x20 - shift | loValue << shift + -0x20,
                    loValue << shift);
}

// ---------------------------------------------------------------------------
// __shr2u (0x80382b84) - 64-bit logical right shift by a variable amount.
//   value = hi:lo, shift = param_3. Mirror of __shl2i for the low word, with the
//   (hi >> shift-32) term covering shifts >= 32.
u64 __shr2u(u32 hiValue, u32 loValue, int shift)
{
    return CONCAT44(hiValue >> shift,
                    loValue >> shift | hiValue << 0x20 - shift | hiValue >> shift + -0x20);
}

// ---------------------------------------------------------------------------
// __sar2i (0x80382ba8) - 64-bit arithmetic (signed) right shift by a variable
// amount.
//   value = hiValue:loValue, shift = param_3. Unlike __shr2u this uses a
//   sign-propagating shift on the high word and, for shift >= 32, folds the
//   sign-extended high word into the low word via the (hiValue >> (shift-32))
//   arithmetic-shift term. The shift amounts are masked with 0x3f to match the
//   PPC sr{a}w encoding.
u64 __sar2i(s32 hiValue, u32 loValue, int shift)
{
    u32 lo;

    lo = loValue >> shift | hiValue << 0x20 - shift;
    if (0 < (int)(shift - 0x20)) {
        lo = lo | (u32)(hiValue >> (shift - 0x20 & 0x3f));
    }
    return CONCAT44(hiValue >> (shift & 0x3f), lo);
}
