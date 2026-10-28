// Address range: 0x80133bd4 - 0x80133d3c | batch_361 | 2026-09-04
//
// Scripted expression evaluator - the arithmetic core used by the game's
// data-driven value system (menu tweens, tuning curves, script math).
//
//   expr_eval_math_op   0x80133bd4  apply a binary/unary math opcode
//   expr_lookup_opcode  0x80133d3c  map a token id to its opcode via a table
//
// The opcode set (case values) covers: constant/time (3), add (4), sub (5),
// mul (6), div (7), mod (8), negate (9), sin/cos family (10-15), min (0x10),
// max (0x11), clamp (0x12), and scaled-random (0x13).

#include "common.h"

// ---- math helpers (float/double transcendental routines) -----------------
extern double math_poly_trig(double x);      // shared: polynomial sin/cos approx
extern double game_frand(void);              // shared: [0,1) random

extern double FUN_803909ec(double x);        // sqrt-family
extern double FUN_803900e4(double x);        // sine
extern double FUN_80390324(double x);        // cosine
extern double FUN_80164910(double a, double b); // fmod / range wrap (2-arg)

// opcode 3 reads a time-scaled value; defined below (0x80133ed0).
double expr_get_time_value(void);

// ---- tuning constants (rodata singles/doubles) ----------------------------
extern const double DOUBLE_80615c50;         // 2^52 bias for int->double conversion
extern const float  FLOAT_80615c40;          // sin phase offset (pi/2 style)
extern const float  FLOAT_80615c44;          // wrap period for FUN_80164910
extern const float  FLOAT_80615c48;          // default/fallback result

// ---- opcode remap table (0x17 entries, 0x10-byte stride) -------------------
//   +0x00 token id (the value searched for)
//   +0x0c mapped opcode returned on match
extern const unsigned char DAT_8040d96c[];   // table key column base
extern const unsigned char DAT_8040d978[];   // table value column base

// ---------------------------------------------------------------------------
// 0x80133bd4 : evaluate a single math opcode.
//
//   a       first operand (right-hand / scalar)
//   b       second operand (left-hand)
//   acc     current accumulator (used by clamp opcode 0x12 and as passthrough)
//   op      opcode selector
//
// Returns the resulting value (float-precision, widened to double for the ABI).
// Unknown opcodes fall through to the fallback constant.
// ---------------------------------------------------------------------------
double expr_eval_math_op(double a, double b, double acc, unsigned int op)
{
    double result;

    switch (op) {
    case 3:
        result = expr_get_time_value();
        break;
    case 4:
        result = (double)(float)(b + a);
        break;
    case 5:
        result = (double)(float)(b - a);
        break;
    case 6:
        result = (double)(float)(b * a);
        break;
    case 7:
        result = (double)(float)(b / a);
        break;
    case 8: {
        // integer modulo of (b % a), re-expressed in float
        int ib = (int)b;
        int ia = (int)a;
        int rem = ib - (ib / ia) * ia;
        result = (double)(float)((double)CONCAT44(0x43300000, (unsigned int)(rem ^ 0x80000000))
                                 - DOUBLE_80615c50);
        break;
    }
    case 9:
        result = -a;
        break;
    case 10:
        result = math_poly_trig((double)(float)(a - (double)FLOAT_80615c40));
        break;
    case 0xb:
        result = math_poly_trig(a);
        break;
    case 0xc:
        result = (double)(float)FUN_803909ec(a);
        break;
    case 0xd:
        result = FUN_80164910(a, (double)FLOAT_80615c44);
        break;
    case 0xe:
        result = (double)(float)FUN_803900e4(a);
        break;
    case 0xf:
        result = (double)(float)FUN_80390324(a);
        break;
    case 0x10:                              // min(a, b)
        result = b;
        if (a < b) {
            result = a;
        }
        break;
    case 0x11:                              // max(a, b)
        result = a;
        if (a < b) {
            result = b;
        }
        break;
    case 0x12:                              // clamp(acc, a, b) style
        result = acc;
        if (acc <= a) {
            result = a;
            if (b < a) {
                result = b;
            }
        }
        break;
    case 0x13:
        result = (double)(float)(a * game_frand());
        break;
    default:
        result = (double)FLOAT_80615c48;
        break;
    }

    return result;
}

// ---------------------------------------------------------------------------
// 0x80133d3c : map a token id to its opcode using the 0x17-entry remap table.
// Linear scan of the key column; returns the paired value column entry, or
// 0xffffffff when the token is not present.
// ---------------------------------------------------------------------------
unsigned int expr_lookup_opcode(int token)
{
    int off = 0;
    int remaining = 0x17;
    do {
        if (token == *(const int*)(DAT_8040d96c + off)) {
            return *(const unsigned int*)(DAT_8040d978 + off);
        }
        off += 0x10;
        remaining -= 1;
    } while (remaining != 0);
    return 0xffffffff;
}

// ===========================================================================
// Expression stack machine + time source (batch_362)
//
//   expr_evaluate_stack     0x80133d80  run a token stream through the evaluator
//   expr_reset_time         0x80133e48  zero the animation time accumulator
//   expr_advance_time_paused0x80133e54  advance time when gameplay is paused/idle
//   expr_get_time_value     0x80133ed0  time-scaled value consumed by opcode 3
// ===========================================================================

// Math scratch-stack helpers (0x8038xxxx) - push/pop the FP register-save
// frame the evaluator runs inside.  FUN_803826e8 returns a packed pair:
// (count << 32) | tokenBase, matching the raw ABI (r3:r4).
extern unsigned long long FUN_803826e8(void);   // enter FP frame; returns (count<<32)|base
extern void               FUN_80382734(void);    // leave FP frame

// Time / gating helpers used by expr_advance_time_paused.  These are declared
// void in the shared header (generated_decls.h) but the hardware ABI returns a
// status in r3; this TU only includes common.h, so a correctly-typed local
// extern is used (Compile-Clean Standard).
extern int  music_update_tick(void);             // 0!=music still playing
extern int  level_search_by_current_id(void);    // -> level table index
extern int  music_is_muted(void);                // 0!=muted
extern int  FUN_801c2dcc(int handle);            // stream/voice busy test

extern int          DAT_80612078;                // animation time accumulator (ticks)
extern int          DAT_806108e0;                // per-frame time step
extern int          DAT_80611778;                // "has active stream" flag
extern int          DAT_80612614;                // current stream handle
extern int          DAT_806109c0;                // active layout/mode selector
extern unsigned char DAT_804893a4[];             // level table base (0x38 stride)

extern const float  FLOAT_80615c58;              // time scale (mode 0x32)
extern const float  FLOAT_80615c5c;              // time scale (default)

// ---------------------------------------------------------------------------
// 0x80133d80 : evaluate a packed token stream (RPN stack machine).
//
// Each token is a 32-bit float whose exponent field (bits 30..23) being 0xff
// marks it as an *opcode* rather than a literal.  For a literal the value is
// pushed; for an opcode the mantissa (bits 22..0) selects the operation, its
// arity is looked up (expr_lookup_opcode), that many operands are popped, and
// the result (expr_eval_math_op) is pushed back.  The scratch stack lives in a
// 61-float local buffer with 3 slots of head-room below the working area.
// ---------------------------------------------------------------------------
void expr_evaluate_stack(void)
{
    float stack[61];

    unsigned long long frame = FUN_803826e8();
    unsigned int   count = (unsigned int)frame;
    const float*   tokens = (const float*)(unsigned int)(frame >> 0x20);

    float* sp = stack + 3;
    int    tokenOff = 0;
    for (unsigned int i = 0; i < count; i++) {
        float raw = *(const float*)((const char*)tokens + tokenOff);
        unsigned int bits = *(unsigned int*)&raw;

        if (((bits >> 0x17) & 0xff) == 0xff) {
            // opcode token: mantissa = opcode id
            unsigned int op = bits & 0x7fffff;
            int arity = (int)expr_lookup_opcode((int)op);

            // pop `arity` operands off the working stack into stack[0..]
            int dst = 0;
            for (int n = arity; n > 0; n--) {
                sp -= 1;
                *(float*)((char*)stack + dst) = *sp;
                dst += 4;
            }

            double r = expr_eval_math_op((double)stack[0], (double)stack[1],
                                         (double)stack[2], op);
            *sp = (float)r;
        } else {
            *sp = raw;      // literal: push
        }

        sp += 1;
        tokenOff += 4;
    }

    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80133e48 : reset the animation time accumulator to zero.
// ---------------------------------------------------------------------------
void expr_reset_time(void)
{
    DAT_80612078 = 0;
}

// ---------------------------------------------------------------------------
// 0x80133e54 : advance the time accumulator only while gameplay is effectively
// paused - i.e. when music has finished, the current level slot is empty, an
// active stream is still busy, or audio is muted.  This keeps loading/idle
// animations progressing without double-advancing during normal play.
// ---------------------------------------------------------------------------
void expr_advance_time_paused(void)
{
    int cond = music_update_tick();
    if (cond == 0) {
        int levelIdx = level_search_by_current_id();
        cond = (*(int*)(DAT_804893a4 + levelIdx * 0x38) == 0);
    }
    if (cond == 0 && DAT_80611778 != 0) {
        cond = (FUN_801c2dcc(DAT_80612614) != 0);
    }
    if (cond == 0) {
        cond = (music_is_muted() != 0);
    }

    if (cond) {
        DAT_80612078 += DAT_806108e0;
    }
}

// ---------------------------------------------------------------------------
// 0x80133ed0 : produce the time-scaled value consumed by opcode 3.  Converts
// the integer tick accumulator to float and multiplies by a mode-dependent
// scale (a faster rate is used when the active layout selector == 0x32).
// ---------------------------------------------------------------------------
double expr_get_time_value(void)
{
    float scale = FLOAT_80615c5c;
    if (DAT_806109c0 == 0x32) {
        scale = FLOAT_80615c58;
    }
    double t = (double)CONCAT44(0x43300000, (unsigned int)(DAT_80612078 ^ 0x80000000))
               - DOUBLE_80615c50;
    return (double)((float)(t * (double)scale));
}
