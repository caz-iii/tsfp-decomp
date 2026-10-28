// Address: 0x80388340 | Name: msl_format_engine   | Date: 2026-09-13
// Address: 0x80388ab4 | Name: msl_fmt_float_efg    | Date: 2026-09-13
// Address: 0x803891d0 | Name: msl_dec_round_digits | Date: 2026-09-13
// Address: 0x803892fc | Name: msl_fmt_float_hex    | Date: 2026-09-13
// Address: 0x80389634 | Name: msl_fmt_int64        | Date: 2026-09-13
// Address: 0x80389948 | Name: msl_fmt_int32        | Date: 2026-09-13
// Address: 0x80389ba0 | Name: msl_parse_conv_spec  | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) printf format engine and its per-conversion
// formatters. The engine (msl_format_engine) walks the format string, parses each
// %-conversion into a 16-byte descriptor, fetches the argument, formats it into a
// scratch buffer via one of the helpers below, and emits runs through the caller-
// supplied sink callback (buffer or FILE - see msl_printf.cpp).
//
// Conversion descriptor (16 bytes, filled by msl_parse_conv_spec):
//   word0 (flags):  byte0 = justify/pad (0 right-space, 1 left, 2 zero)
//                   byte1 = sign flag  (1 '+', 2 ' ')
//                   byte2 = alt '#' flag ; byte3 = precision-present flag
//   word1 (conv):   byte0 = length modifier (0 int,1 char,2 short,3 long,
//                                             4 long long,5 long double,6 wide,0xff invalid)
//                   byte1 = conversion char ('d','x','f',...)
//   word2 (int):    field width
//   word3 (int):    precision
//
// The helpers keep their raw FUN_ symbol names so the format-engine call sites and
// any cross-module references resolve at link (static library target). Register-pair
// varargs (unaff_rNN) are the PPC GPR/FPR argument window; here they are read back
// through the varargs walker aiactor_arg_walker exactly as the original does.

#include "common.h"

// --- Named cross-module helpers ---------------------------------------------
extern int *aiactor_arg_walker(char *va, int kind);      // varargs fetch (0x803824b0)
extern uint msl_utf16_to_utf8(int dst, ushort *src, uint cap);  // msl_string.cpp
extern char *msl_memchr(int base, char ch, int n);       // msl_string.cpp
extern void  msl_dec_round_to(double v, int flags, char *outSign);  // msl_decimal.cpp (0x80384490)

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern char *FUN_8038b308(int s, char ch);               // strchr
extern int   FUN_8038b5c8(int s);                        // strlen
extern void  FUN_8038b510(int *dst, int *src);           // small string copy (inf/nan literals)

extern u64  __div2u(u32 loDividend, u32 hiDividend, s32 hiDivisor, u32 loDivisor); // int64_runtime.cpp
extern u64  __mod2u(u32 loDividend, u32 hiDividend, s32 hiDivisor, u32 loDivisor);

extern u8   DAT_80497890[];    // ctype-style class table
extern u8   DAT_80611370;      // empty wide string sentinel
extern u8   DAT_803e1268;      // "(null)"
extern u8   DAT_803e1269, DAT_803e126e, DAT_803e1273, DAT_803e1277;  // inf literals
extern u8   DAT_803e127b, DAT_803e1280, DAT_803e1285, DAT_803e1289;  // nan literals
extern double DOUBLE_8061b9f8; // infinity threshold constant

// Forward declarations of the formatters (used by the engine above their defs).
byte *FUN_80388ab4(double v, int bufEnd, int spec);        // %e/%f/%g
int  *FUN_803892fc(double v, int bufEnd, int spec);        // %a/%A hex float
char *FUN_80389634(uint lo, uint hi, int bufEnd, char *spec); // 64-bit int
char *FUN_80389948(uint v, int bufEnd, char *spec);        // 32-bit int
char *FUN_80389ba0(int fmtAtPercent, char *va, uint *specOut); // parse conversion
void  FUN_803891d0(int decRec, int keepDigits);            // round decimal record

// ---------------------------------------------------------------------------
// msl_dec_round_digits (0x803891d0) - round a big-decimal digit record (from the
// dtoa helper) to keepDigits significant digits, propagating a carry and handling
// the round-half-to-even case. Adjusts the record's exponent/length in place.
void FUN_803891d0(int rec, int keep)
{
    char c1;
    char c2;
    char *scan;
    int   base;
    uint  round;
    char *dig;

    if (-1 < keep) {
        if ((int)(uint)*(byte *)(rec + 4) <= keep) {
            return;
        }
        base = rec + keep;
        dig  = (char *)(base + 5);
        round = (uint)(char)(*(char *)(base + 5) + -0x30);
        if (round == 5) {
            scan = (char *)(rec + (uint)*(byte *)(rec + 4) + 5);
            do {
                scan = scan + -1;
                if (scan <= dig) break;
            } while (*scan == '0');
            if (scan == dig) {
                round = *(byte *)(base + 4) & 1;
            }
            else {
                round = 1;
            }
        }
        else {
            round = ((int)(round ^ 5) >> 1) - ((round ^ 5) & round) >> 0x1f;
        }
        for (; keep != 0; keep = keep + -1) {
            dig = dig + -1;
            c1 = *dig + (char)round;
            c2 = c1 + -0x30;
            round = (int)c2 ^ 9;
            round = -((int)(((int)round >> 1) - (round & (int)c2)) >> 0x1f);
            if ((round == 0) && (c2 != '\0')) {
                *dig = c1;
                break;
            }
        }
        if (round != 0) {
            *(short *)(rec + 2) = *(short *)(rec + 2) + 1;
            *(u8 *)(rec + 4) = 1;
            *(u8 *)(rec + 5) = 0x31;
            return;
        }
        if (keep != 0) {
            *(char *)(rec + 4) = (char)keep;
            return;
        }
    }
    *(u16 *)(rec + 2) = 0;
    *(u8 *)(rec + 4) = 1;
    *(u8 *)(rec + 5) = 0x30;
    return;
}

// ---------------------------------------------------------------------------
// msl_fmt_int32 (0x80389948) - format a 32-bit integer (spec byte5 = conversion:
// d/i signed decimal, u decimal, o octal, x/X hex) into the scratch buffer ending
// at bufEnd, right-to-left. Applies precision zero-fill, '#' alt prefix (0/0x),
// and sign. Returns a pointer to the first formatted byte, or NULL on overflow.
char *FUN_80389948(uint value, int bufEnd, char *spec)
{
    char  conv;
    bool  neg;
    uint  radix;
    uint  fill;
    char *p;
    char *pPrev;
    int   count;
    int   countPrev;
    char  c;
    uint  tmp;

    *(u8 *)(bufEnd + -1) = 0;
    neg = false;
    radix = 10;
    if (((value == 0) && (*(int *)(spec + 0xc) == 0)) &&
        ((spec[3] == '\0') || (spec[5] != 'o'))) {
        return (char *)(bufEnd + -1);
    }
    switch (spec[5]) {
    case 'X': case 'x': radix = 0x10; spec[1] = '\0'; break;
    case 'd': case 'i':
        radix = 10;
        if ((int)value < 0) { value = -value; neg = true; }
        break;
    case 'o': radix = 8;  spec[1] = '\0'; break;
    case 'u': radix = 10; spec[1] = '\0'; break;
    }
    conv = spec[5];
    p = (char *)(bufEnd + -1);
    count = 0;
    do {
        countPrev = count;
        pPrev = p;
        tmp = value - (value / radix) * radix;
        value = value / radix;
        c = (char)tmp;
        if ((int)tmp < 10) c = c + '0';
        else if (conv == 'x') c = c + 'W';
        else c = c + '7';
        pPrev[-1] = c;
        p = pPrev + -1;
        count = countPrev + 1;
    } while (value != 0);
    if (((radix == 8) && (spec[3] != '\0')) && (*p != '0')) {
        count = countPrev + 2;
        p = pPrev + -2;
        *p = '0';
    }
    if (*spec == '\x02') {
        *(u32 *)(spec + 0xc) = *(u32 *)(spec + 8);
        if ((neg) || (spec[1] != '\0')) {
            *(int *)(spec + 0xc) = *(int *)(spec + 0xc) + -1;
        }
        if ((radix == 0x10) && (spec[3] != '\0')) {
            *(int *)(spec + 0xc) = *(int *)(spec + 0xc) + -2;
        }
    }
    countPrev = *(int *)(spec + 0xc);
    if (0x1fd < countPrev + (bufEnd - (int)p)) {
        return (char *)0x0;
    }
    fill = countPrev - count;
    if (count < countPrev) {
        tmp = fill >> 3;
        if (tmp != 0) {
            do {
                p[-1]='0'; p[-2]='0'; p[-3]='0'; p[-4]='0';
                p[-5]='0'; p[-6]='0'; p[-7]='0'; p[-8]='0';
                p = p + -8;
                tmp = tmp - 1;
            } while (tmp != 0);
            fill = fill & 7;
            if (fill == 0) goto after_fill;
        }
        do { p = p + -1; *p = '0'; fill = fill - 1; } while (fill != 0);
    }
after_fill:
    if ((radix == 0x10) && (spec[3] != '\0')) {
        p[-1] = spec[5];
        p = p + -2;
        *p = '0';
    }
    if (neg) { p = p + -1; *p = '-'; }
    else if (spec[1] == '\x01') { p = p + -1; *p = '+'; }
    else if (spec[1] == '\x02') { p = p + -1; *p = ' '; }
    return p;
}

// ---------------------------------------------------------------------------
// msl_fmt_int64 (0x80389634) - the 64-bit (long long) analogue of msl_fmt_int32.
// Divides the {hi,lo} pair by the radix with the runtime 64-bit div/mod helpers.
char *FUN_80389634(uint hi, uint lo, int bufEnd, char *spec)
{
    bool  neg;
    char  c;
    int   dividendHi;
    uint  digit;
    int   count;
    char *p;
    char *pPrev;
    uint  radix;
    int   radixHi;
    uint  fill;
    u64   val;
    int   countPrev;

    p = (char *)(bufEnd + -1);
    neg = false;
    count = 0;
    *(u8 *)(bufEnd + -1) = 0;
    if ((lo == 0 && hi == 0) && (*(int *)(spec + 0xc) == 0)) {
        if (spec[3] == '\0') return p;
        if (spec[5] != 'o') return p;
    }
    val = CONCAT44(hi, lo);
    radix = 10; radixHi = 0;
    switch (spec[5]) {
    case 'X': case 'x': radix = 0x10; spec[1] = '\0'; val = CONCAT44(hi, lo); break;
    case 'd': case 'i':
        radix = 10;
        val = CONCAT44(hi, lo);
        if ((hi ^ 0x80000000) < 0x80000000) {
            neg = true;
            val = CONCAT44(-((lo != 0) + hi), -lo);
        }
        break;
    case 'o': radix = 8;  spec[1] = '\0'; val = CONCAT44(hi, lo); break;
    case 'u': radix = 10; spec[1] = '\0'; val = CONCAT44(hi, lo); break;
    }
    do {
        pPrev = p;
        countPrev = count;
        dividendHi = (uint)(val >> 0x20);
        digit = (uint)__mod2u(dividendHi, (uint)val, radixHi, radix);
        val   = __div2u(dividendHi, (uint)val, radixHi, radix);
        c = (char)digit;
        if ((int)digit < 10) c = c + '0';
        else if (spec[5] == 'x') c = c + 'W';
        else c = c + '7';
        p = pPrev + -1;
        *p = c;
        count = countPrev + 1;
    } while (val != 0);
    if (((radix == 8 && radixHi == 0) && (spec[3] != '\0')) && (*p != '0')) {
        count = countPrev + 2;
        p = pPrev + -2;
        *p = '0';
    }
    if (*spec == '\x02') {
        *(u32 *)(spec + 0xc) = *(u32 *)(spec + 8);
        if ((neg) || (spec[1] != '\0')) {
            *(int *)(spec + 0xc) = *(int *)(spec + 0xc) + -1;
        }
        if ((radix == 0x10 && radixHi == 0) && (spec[3] != '\0')) {
            *(int *)(spec + 0xc) = *(int *)(spec + 0xc) + -2;
        }
    }
    countPrev = *(int *)(spec + 0xc);
    if (0x1fd < countPrev + (bufEnd - (int)p)) {
        return (char *)0x0;
    }
    fill = countPrev - count;
    if (count < countPrev) {
        uint blocks = fill >> 3;
        if (blocks != 0) {
            do {
                p[-1]='0'; p[-2]='0'; p[-3]='0'; p[-4]='0';
                p[-5]='0'; p[-6]='0'; p[-7]='0'; p[-8]='0';
                p = p + -8;
                blocks = blocks - 1;
            } while (blocks != 0);
            fill = fill & 7;
            if (fill == 0) goto after_fill64;
        }
        do { p = p + -1; *p = '0'; fill = fill - 1; } while (fill != 0);
    }
after_fill64:
    if ((radix == 0x10 && radixHi == 0) && (spec[3] != '\0')) {
        p[-1] = spec[5];
        p = p + -2;
        *p = '0';
    }
    if (neg) { p = p + -1; *p = '-'; }
    else if (spec[1] == '\x01') { p = p + -1; *p = '+'; }
    else if (spec[1] == '\x02') { p = p + -1; *p = ' '; }
    return p;
}

// ---------------------------------------------------------------------------
// msl_fmt_float_efg (0x80388ab4) - format a double as %e/%E/%f/%F/%g/%G into the
// scratch buffer via the big-decimal converter (msl_dec_round_to). Handles inf/nan
// literals, precision, exponent field, and the %g significant-digit trimming.
byte *FUN_80388ab4(double value, int bufEnd, int spec)
{
    byte  signChar;
    char  c;
    int   e;
    uint  n;
    int   i;
    byte *src;
    int   iter;
    byte  conv;
    byte *p;
    // Contiguous decimal record written by msl_dec_round_to:
    //   rec[0]      sign byte (0 = non-negative)
    //   rec[2..3]   base-10 exponent (s16)
    //   rec[4]      digit count
    //   rec[5..]    ASCII digits (rec[5] doubles as the 'I'/'N'/'0' class marker)
    char  fmtRec[4];
    char  rec[64];
    short decExp;
    byte  decLen;
    byte *decDigits = (byte *)(rec + 5);
    char *decSign   = rec;   // decSign[0] == rec[0]

    if (0x1fd < *(int *)(spec + 0xc)) {
        return (byte *)0x0;
    }
    fmtRec[0] = 0;
    *(u16 *)(fmtRec + 2) = 0x20;
    msl_dec_round_to(value, (int)fmtRec, rec);
    decLen = (byte)rec[4];
    decExp = *(short *)(rec + 2);
    p = decDigits + decLen;
    while ((1 < decLen && (p = p + -1, *p == 0x30))) {
        decLen = decLen - 1;
        decExp = decExp + 1;
    }
    if (decDigits[0] == 0x49) {          // 'I' -> inf
        if (DOUBLE_8061b9f8 <= value) {
            p = (byte *)(bufEnd + -4);
            if ((DAT_80497890[*(byte *)(spec + 5)] & 0x80) != 0)
                FUN_8038b510((int *)p, (int *)&DAT_803e1273);
            else
                FUN_8038b510((int *)p, (int *)&DAT_803e1277);
            return p;
        }
        p = (byte *)(bufEnd + -5);
        if ((DAT_80497890[*(byte *)(spec + 5)] & 0x80) != 0)
            FUN_8038b510((int *)p, (int *)&DAT_803e1269);
        else
            FUN_8038b510((int *)p, (int *)&DAT_803e126e);
        return p;
    }
    if (decDigits[0] < 0x49) {
        if (decDigits[0] == 0x30) decExp = 0;
    }
    else if (decDigits[0] == 0x4e) {     // 'N' -> nan
        if (decSign[0] == '\0') {
            p = (byte *)(bufEnd + -4);
            if ((DAT_80497890[*(byte *)(spec + 5)] & 0x80) != 0)
                FUN_8038b510((int *)p, (int *)&DAT_803e1285);
            else
                FUN_8038b510((int *)p, (int *)&DAT_803e1289);
            return p;
        }
        p = (byte *)(bufEnd + -5);
        if ((DAT_80497890[*(byte *)(spec + 5)] & 0x80) != 0)
            FUN_8038b510((int *)p, (int *)&DAT_803e127b);
        else
            FUN_8038b510((int *)p, (int *)&DAT_803e1280);
        return p;
    }
    p = (byte *)(bufEnd + -1);
    conv = *(byte *)(spec + 5);
    decExp = (u16)decLen + decExp + -1;
    *(u8 *)(bufEnd + -1) = 0;
    bool doExp = false;   // %e/%E path
    if (conv == 0x65) { doExp = true; }
    else if (conv < 0x65) {
        if (conv != 0x46) {
            if (conv < 0x46) {
                if (conv < 0x45) return p;
                doExp = true;
            }
            else if (0x47 < conv) return p;
            else goto g_path;   // 'G'
        }
    }
    else {
        if (conv == 0x67) goto g_path;   // 'g'
        if (0x66 < conv) return p;
    }
    if (doExp) goto exp_path;

    // ------- %f / %F fixed path (falls through from 'F' above) --------------
fixed_path:
    {
        e = ((uint)decLen - (int)decExp) + -1;
        if (e < 0) e = 0;
        if (*(int *)(spec + 0xc) < e) {
            FUN_803891d0((int)decSign, (uint)decLen - (e - *(int *)(spec + 0xc)));
            e = ((uint)decLen - (int)decExp) + -1;
            if (e < 0) e = 0;
        }
        iter = decExp + 1;
        if (iter < 0) iter = 0;
        if (0x1fd < iter + e) return (byte *)0x0;
        n = *(int *)(spec + 0xc) - e;
        src = decDigits + decLen;
        if (0 < (int)n) {
            uint blk = n >> 3;
            if (blk != 0) {
                do {
                    p[-1]='0';p[-2]='0';p[-3]='0';p[-4]='0';p[-5]='0';p[-6]='0';p[-7]='0';p[-8]='0';
                    p = p + -8; blk = blk - 1;
                } while (blk != 0);
                n = n & 7;
                if (n == 0) goto f_after_a;
            }
            do { p = p + -1; *p = 0x30; n = n - 1; } while (n != 0);
        }
    f_after_a:
        for (i = 0; (i < e && (i < (int)(uint)decLen)); i = i + 1) {
            src = src + -1;
            p = p + -1;
            *p = *src;
        }
        n = e - i;
        if (i < e) {
            uint blk = n >> 3;
            if (blk != 0) {
                do {
                    p[-1]='0';p[-2]='0';p[-3]='0';p[-4]='0';p[-5]='0';p[-6]='0';p[-7]='0';p[-8]='0';
                    p = p + -8; blk = blk - 1;
                } while (blk != 0);
                n = n & 7;
                if (n == 0) goto f_after_b;
            }
            do { p = p + -1; *p = 0x30; n = n - 1; } while (n != 0);
        }
    f_after_b:
        if ((*(int *)(spec + 0xc) != 0) || (*(char *)(spec + 3) != '\0')) {
            p = p + -1; *p = 0x2e;
        }
        if (iter == 0) { p = p + -1; *p = 0x30; }
        else {
            for (i = 0; i < (int)(iter - (uint)decLen); i = i + 1) {
                p = p + -1; *p = 0x30;
            }
            n = iter - i;
            if (i < iter) {
                uint blk = n >> 3;
                if (blk != 0) {
                    do {
                        p[-1]=src[-1];p[-2]=src[-2];p[-3]=src[-3];p[-4]=src[-4];
                        p[-5]=src[-5];p[-6]=src[-6];p[-7]=src[-7];
                        src = src + -8; p = p + -8; *p = *src;
                        blk = blk - 1;
                    } while (blk != 0);
                    n = n & 7;
                    if (n == 0) goto f_after_c;
                }
                do { src = src + -1; p = p + -1; *p = *src; n = n - 1; } while (n != 0);
            }
        }
    f_after_c:
        if (decSign[0] == '\0') {
            if (*(char *)(spec + 1) == '\x01') { p = p + -1; *p = 0x2b; }
            else if (*(char *)(spec + 1) == '\x02') { p = p + -1; *p = 0x20; }
        }
        else { p = p + -1; *p = 0x2d; }
        return p;
    }

g_path:
    if (*(int *)(spec + 0xc) < (int)(uint)decLen) {
        FUN_803891d0((int)decSign, *(int *)(spec + 0xc));
    }
    e = (int)decExp;
    if ((e < -4) || (*(int *)(spec + 0xc) <= e)) {
        if (*(char *)(spec + 3) == '\0') {
            *(uint *)(spec + 0xc) = decLen - 1;
        }
        else {
            *(int *)(spec + 0xc) = *(int *)(spec + 0xc) + -1;
        }
        if (*(char *)(spec + 5) == 'g') *(u8 *)(spec + 5) = 0x65;
        else *(u8 *)(spec + 5) = 0x45;
        goto exp_path;
    }
    if (*(char *)(spec + 3) == '\0') {
        e = (uint)decLen - (e + 1);
        *(int *)(spec + 0xc) = e;
        if (e < 0) *(u32 *)(spec + 0xc) = 0;
    }
    else {
        *(int *)(spec + 0xc) = *(int *)(spec + 0xc) - (e + 1);
    }
    goto fixed_path;

exp_path:
    {
        e = *(int *)(spec + 0xc) + 1;
        if (e < (int)(uint)decLen) {
            FUN_803891d0((int)decSign, e);
        }
        e = (int)decExp;
        signChar = 0x2b;
        if (e < 0) { e = -e; signChar = 0x2d; }
        for (iter = 0; (e != 0 || (iter < 2)); iter = iter + 1) {
            int q = e / 10 + (e >> 0x1f);
            c = (char)e;
            e = q - (q >> 0x1f);
            p[-1] = c + ((char)q - (char)(q >> 0x1f)) * -10 + 0x30;
            p = p + -1;
        }
        conv = *(byte *)(spec + 5);
        p[-1] = signChar;
        e = *(int *)(spec + 0xc);
        p = p + -2;
        *p = conv;
        if (0x1fd < e + (bufEnd - (int)p)) return (byte *)0x0;
        if ((int)(uint)decLen < e + 1) {
            e = (e + 2) - (uint)decLen;
            while (e = e + -1, e != 0) { p = p + -1; *p = 0x30; }
        }
        n = (uint)decLen;
        src = decDigits + n;
        while (n = n - 1, n != 0) {
            src = src + -1; p = p + -1; *p = *src;
        }
        if ((*(int *)(spec + 0xc) != 0) || (*(char *)(spec + 3) != '\0')) {
            p = p + -1; *p = 0x2e;
        }
        p[-1] = decDigits[0];
        if (decSign[0] != '\0') { p[-2] = 0x2d; return p + -2; }
        if (*(char *)(spec + 1) == '\x01') { p[-2] = 0x2b; return p + -2; }
        if (*(char *)(spec + 1) != '\x02') return p + -1;
        p[-2] = 0x20;
        return p + -2;
    }
}

// ---------------------------------------------------------------------------
// msl_fmt_float_hex (0x803892fc) - format a double as %a/%A (C99 hex float) into
// the scratch buffer: "0x1.<hexdigits>p<exp>". Handles inf/nan and the sign/prefix.
int *FUN_803892fc(double value, int bufEnd, int spec)
{
    char  c;
    int  *ret;
    char *p;
    byte  nib;
    uint  digitsLeft;
    uint  cnt;
    u64   bits;
    char  fmtRec[4];
    double valCopy;
    char  rec[64];         // decimal/hex record filled by msl_dec_round_to
    byte *decDigits = (byte *)(rec + 5);
    char  expSpec[4];
    char  decClass;

    if (*(int *)(spec + 0xc) < 0x1fe) {
        fmtRec[0] = 0;
        *(u16 *)(fmtRec + 2) = 0x20;
        bits = (u64)value;
        valCopy = value;
        msl_dec_round_to(value, (int)fmtRec, rec);
        decClass = rec[5];
        if (decClass == 'I') {           // inf
            if (((int)((u64)valCopy >> 48) & 0x8000U) == 0) {
                ret = (int *)(bufEnd + -4);
                if (*(char *)(spec + 5) == 'A') FUN_8038b510(ret, (int *)&DAT_803e1273);
                else FUN_8038b510(ret, (int *)&DAT_803e1277);
            }
            else {
                ret = (int *)(bufEnd + -5);
                if (*(char *)(spec + 5) == 'A') FUN_8038b510(ret, (int *)&DAT_803e1269);
                else FUN_8038b510(ret, (int *)&DAT_803e126e);
            }
        }
        else if (decClass == 'N') {      // nan
            if ((bits & 0x8000000000000000ULL) == 0) {
                ret = (int *)(bufEnd + -4);
                if (*(char *)(spec + 5) == 'A') FUN_8038b510(ret, (int *)&DAT_803e1285);
                else FUN_8038b510(ret, (int *)&DAT_803e1289);
            }
            else {
                ret = (int *)(bufEnd + -5);
                if (*(char *)(spec + 5) == 'A') FUN_8038b510(ret, (int *)&DAT_803e127b);
                else FUN_8038b510(ret, (int *)&DAT_803e1280);
            }
        }
        else {
            // build the binary exponent field via the int32 formatter
            expSpec[0] = '\x01'; expSpec[1] = '\x01'; expSpec[2] = '\0'; expSpec[3] = '\0';
            int biasedExp = (int)(short)((ushort)((uint)(int)((u64)valCopy >> 48) >> 4) & 0x7ff) - 0x3ff;
            p = FUN_80389948((uint)biasedExp, bufEnd, expSpec);
            if (*(char *)(spec + 5) == 'a') p[-1] = 'p';
            else p[-1] = 'P';
            p = p + -1;
            digitsLeft = *(uint *)(spec + 0xc);
            cnt = digitsLeft;
            if (0 < (int)digitsLeft) {
                do {
                    // pull the requested hex mantissa nibble (high then low per BCD byte)
                    byte packed = decDigits[((int)digitsLeft - 1) / 2];
                    if ((digitsLeft & 1) != 0) nib = (byte)(packed >> 4);
                    else                       nib = packed;
                    nib = nib & 0xf;
                    if (nib < 10) c = nib + 0x30;
                    else if (*(char *)(spec + 5) == 'a') c = nib + 0x57;
                    else c = nib + 0x37;
                    p = p + -1; *p = c;
                    digitsLeft = digitsLeft - 1;
                    cnt = cnt - 1;
                } while (cnt != 0);
            }
            if ((*(int *)(spec + 0xc) != 0) || (*(char *)(spec + 3) != '\0')) {
                p = p + -1; *p = '.';
            }
            c = *(char *)(spec + 5);
            p[-1] = '1';
            if (c == 'a') p[-2] = 'x'; else p[-2] = 'X';
            ret = (int *)(p + -3);
            *(char *)ret = '0';
            if (((int)((u64)valCopy >> 48) & 0x8000U) == 0) {
                if (*(char *)(spec + 1) == '\x01') { ret = (int *)(p + -4); *(char *)ret = '+'; }
                else if (*(char *)(spec + 1) == '\x02') { ret = (int *)(p + -4); *(char *)ret = ' '; }
            }
            else { ret = (int *)(p + -4); *(char *)ret = '-'; }
        }
    }
    else {
        ret = (int *)0x0;
    }
    return ret;
}

// ---------------------------------------------------------------------------
// msl_parse_conv_spec (0x80389ba0) - parse one %-conversion starting at
// fmtAtPercent into the 16-byte descriptor *specOut (see the header block for the
// layout), consuming '*' width/precision arguments from the varargs. Returns the
// format pointer just past the conversion character.
//
// The descriptor is a byte-field structure; we mirror the original's packed reads
// through a byte view to keep the exact semantics.
char *FUN_80389ba0(int fmtAtPercent, char *va, uint *specOut)
{
    u8   *o = (u8 *)specOut;   // byte view of the 16-byte descriptor
    char *p;
    uint  ch;
    int  *arg;
    int   width  = 0;
    int   prec   = 0;
    bool  scanning;

    // word0 flags = {0,0,0,1} initial (justify=0, alt from parse); word1 conv = {0,...}
    o[0] = 0; o[1] = 0; o[2] = 0; o[3] = 1;    // 0x01000000 little/packed as in original
    o[4] = 0; o[5] = 0; o[6] = 0; o[7] = 0;

    p = (char *)(fmtAtPercent + 1);
    ch = (uint)*(char *)(fmtAtPercent + 1);
    if (ch == 0x25) {                          // "%%"
        o[5] = 0x25;                           // conv char = '%'
        specOut[0] = 0x1000000;
        specOut[1] = (uint)o[4] | ((uint)o[5] << 8) | ((uint)o[6] << 16) | ((uint)o[7] << 24);
        specOut[2] = 0;
        specOut[3] = 0;
        return (char *)(fmtAtPercent + 2);
    }

    // ---- flags ----
    while (true) {
        scanning = true;
        switch (ch) {
        case 0x20: if (o[5 - 4] != '\x01') o[1] = 2; break;     // ' ' -> sign=space (unless '+')
        case 0x23: o[2] = 1; break;                             // '#'
        case 0x2b: o[1] = 1; break;                             // '+'
        case 0x2d: o[0] = 0; break;                             // '-' left-justify (clears zero via engine)
        case 0x30: if (o[0] != 0) o[0] = 2; break;              // '0' zero-pad
        default: scanning = false; break;
        }
        if (!scanning) break;
        p = p + 1;
        ch = (uint)*p;
    }

    // ---- width ----
    if (ch == 0x2a) {
        arg = aiactor_arg_walker(va, 1);
        width = *(uint *)arg;
        if (width < 0) { width = -width; o[0] = 0; }
        p = p + 1; ch = (uint)*p;
    }
    else {
        while ((DAT_80497890[ch & 0xff] & 0x10) != 0) {
            p = p + 1;
            width = (ch + width * 10) - 0x30;
            ch = (uint)*p;
        }
    }
    if (0x1fd < width) {
        o[5] = 0xff;                            // mark invalid
        specOut[0] = (uint)o[0] | ((uint)o[1] << 8) | ((uint)o[2] << 16) | ((uint)o[3] << 24);
        specOut[1] = (uint)o[4] | ((uint)o[5] << 8) | ((uint)o[6] << 16) | ((uint)o[7] << 24);
        specOut[2] = (uint)width;
        specOut[3] = 0;
        return p + 1;
    }

    // ---- precision ----
    if (ch == 0x2e) {
        p = p + 1;
        o[3] = 1;                               // precision present
        ch = (uint)*p;
        if (ch == 0x2a) {
            arg = aiactor_arg_walker(va, 1);
            prec = *(uint *)arg;
            if (prec < 0) o[3] = 0;
            p = p + 1; ch = (uint)*p;
        }
        else {
            while ((DAT_80497890[ch & 0xff] & 0x10) != 0) {
                p = p + 1;
                prec = (ch + prec * 10) - 0x30;
                ch = (uint)*p;
            }
        }
    }

    // ---- length modifier ----
    scanning = true;
    if (ch == 0x68) {                           // 'h'
        o[4] = 2;
        if (p[1] == 'h') { o[4] = 1; p = p + 1; ch = 0x68; }
    }
    else if (ch == 0x4c) { o[4] = 5; }          // 'L'
    else if (ch == 0x6c) {                      // 'l'
        o[4] = 3;
        if (p[1] == 'l') { o[4] = 4; p = p + 1; ch = 0x6c; }
    }
    else {
        scanning = false;
        o[4] = 0;
    }
    if (scanning) { p = p + 1; ch = (uint)*p; }

    // ---- conversion char + defaulting of precision by conversion ----
    o[5] = (u8)ch;
    switch (ch) {
    case 0x41: case 0x61:                        // A a
        if (o[2] == '\0') prec = 0xd;
        if ((o[4] == 2) || (o[4] == 4) || (o[4] == 1)) o[5] = 0xff;
        break;
    case 0x46: case 0x66:                        // F f
        if ((o[4] == 2) || (o[4] == 4)) o[5] = 0xff;
        else if (o[2] == '\0') prec = 6;
        break;
    case 0x47: case 0x67:                        // G g
        if (prec == 0) prec = 1;
        /* fallthrough */
    case 0x45: case 0x65:                        // E e
        if ((o[4] == 2) || (o[4] == 4) || (o[4] == 1)) o[5] = 0xff;
        else if (o[2] == '\0') prec = 6;
        break;
    case 0x58: case 100: case 0x69:              // X d i
    case 0x6f: case 0x75: case 0x78:             // o u x
        if (o[4] == 5) { o[5] = 0xff; o[4] = 5; }
        else if (o[2] == '\0') prec = 1;
        else if (o[0] == '\x02') o[0] = 1;
        break;
    case 99:                                     // c
        if (o[4] == 3) o[4] = 6;
        else if ((o[2] != '\0') || (o[4] != '\0')) o[5] = 0xff;
        break;
    case 0x6e:                                   // n
        if (o[4] == 5) { o[5] = 0xff; o[4] = 5; }
        break;
    case 0x70:                                   // p
        o[5] = 0x78; o[2] = 1; o[4] = 3; prec = 8;
        break;
    case 0x73:                                   // s
        if (o[4] == 3) o[4] = 6;
        else if (o[4] != '\0') o[5] = 0xff;
        break;
    default:
        o[5] = 0xff;
        break;
    }

    specOut[0] = (uint)o[0] | ((uint)o[1] << 8) | ((uint)o[2] << 16) | ((uint)o[3] << 24);
    specOut[1] = (uint)o[4] | ((uint)o[5] << 8) | ((uint)o[6] << 16) | ((uint)o[7] << 24);
    specOut[2] = (uint)width;
    specOut[3] = (uint)prec;
    return p + 1;
}

// ---------------------------------------------------------------------------
// msl_format_engine (0x80388340) - the printf core. sink(sinkState, ptr[, len])
// emits output; fmt is the format string; va is the varargs cursor. Returns the
// number of characters emitted, or -1 on a sink error.
int FUN_80388340(void *sinkArg, undefined4 sinkState, char *fmt, char *va)
{
    typedef int (*sink_t)(undefined4, const void *, ...);
    sink_t sink = (sink_t)sinkArg;
    byte   firstByte;
    char  *pct;
    int    litLen;
    int    rc;
    uint  *up;
    double *dp;
    uint   uv;
    char  *field;
    int   *ip;
    ushort *wp;
    byte  *out;
    uint   arg_r22 = 0, arg_r23 = 0, arg_r28 = 0;
    char  *emitted;
    double dv;
    char   padChar;
    int    total = 0;
    uint   spec[4];        // 16-byte conversion descriptor
    u8    *so = (u8 *)spec;
    char   scratch[512];
    char   numbuf[17];     // scratch tail used by the number formatters (grows down)
    char  *numEnd = numbuf + 16;
    byte  *localBuf = (byte *)scratch;

    padChar = ' ';
    while (true) {
        if (*fmt == '\0') return total;
        pct = FUN_8038b308((int)fmt, '%');
        if (pct == (char *)0x0) {
            litLen = FUN_8038b5c8((int)fmt);
            if (litLen == 0) return total + litLen;
            rc = sink(sinkState, fmt);
            if (rc != 0) return total + litLen;
            return -1;
        }
        total = total + ((int)pct - (int)fmt);
        if (((int)pct - (int)fmt != 0) && (rc = sink(sinkState, fmt), rc == 0)) {
            return -1;
        }
        fmt = FUN_80389ba0((int)pct, va, spec);
        byte conv = so[5];
        byte lenmod = so[4];
        int  width = (int)spec[2];
        char *precPtr = (char *)(uintptr_t)spec[3];   // unused; keeps frame parity
        (void)precPtr;

        emitted = (char *)0;
        // dispatch by conversion character
        if (conv == 0x25) {                 // %%
            localBuf[0] = 0x25;
            out = localBuf; emitted = (char *)0x1;
        }
        else if (conv == 'c') {             // c
            ip = aiactor_arg_walker(va, 1);
            localBuf[0] = (byte)*ip;
            out = localBuf; emitted = (char *)0x1;
        }
        else if (conv == 's') {             // s
            if (lenmod == 6) {              // wide string
                ip = aiactor_arg_walker(va, 1);
                wp = (ushort *)*ip;
                if (wp == (ushort *)0) wp = (ushort *)&DAT_80611370;
                uv = msl_utf16_to_utf8((int)localBuf, wp, 0x200);
                if ((int)uv < 0) goto invalid;
                out = localBuf;
            }
            else {
                ip = aiactor_arg_walker(va, 1);
                out = (byte *)*ip;
            }
            if (out == (byte *)0) out = &DAT_803e1268;   // "(null)"
            if ((char)spec[0] == '\0') {                 // no precision override
                if (so[2] == '\0') {                     // and no '#'... (byte2)
                    emitted = (char *)FUN_8038b5c8((int)out);
                }
                else {
                    char *hit = msl_memchr((int)out, '\0', (int)spec[3]);
                    emitted = (char *)(uintptr_t)spec[3];
                    if (hit != (char *)0) emitted = hit + -(int)out;
                }
            }
            else {
                emitted = (char *)(uint)*out;
                out = out + 1;
                if ((so[2] != '\0') && ((int)spec[3] < (int)emitted)) {
                    emitted = (char *)(uintptr_t)spec[3];
                }
            }
        }
        else if (conv == 'n') {             // n - store count
            ip = aiactor_arg_walker(va, 1);
            ip = (int *)*ip;
            if (lenmod == 2) *(short *)ip = (short)total;
            else if (lenmod < 2) { if (lenmod == 0) *ip = total; }
            else if (lenmod == 4) { ip[1] = total; *ip = total >> 0x1f; }
            else *ip = total;
            continue;
        }
        else if (conv == 'a' || conv == 'A') {   // hex float
            dp = (double *)aiactor_arg_walker(va, 3);
            dv = *dp;
            out = (byte *)FUN_803892fc(dv, (int)numEnd, (int)spec);
            if (out == (byte *)0) goto invalid;
            emitted = numbuf + 16 - (int)out;
        }
        else if (conv == 'e' || conv == 'E' || conv == 'f' || conv == 'F' ||
                 conv == 'g' || conv == 'G') {   // decimal float
            dp = (double *)aiactor_arg_walker(va, 3);
            dv = *dp;
            out = FUN_80388ab4(dv, (int)numEnd, (int)spec);
            if (out == (byte *)0) goto invalid;
            emitted = numbuf + 16 - (int)out;
        }
        else if (conv == 'd' || conv == 'i' || conv == 'o' || conv == 'u' ||
                 conv == 'x' || conv == 'X') {   // integer
            if (lenmod == 3) { up = (uint *)aiactor_arg_walker(va, 1); arg_r28 = *up; }
            else if (lenmod == 4) { up = (uint *)aiactor_arg_walker(va, 2); arg_r22 = *up; arg_r23 = up[1]; }
            else { up = (uint *)aiactor_arg_walker(va, 1); arg_r28 = *up; }
            // apply signedness / width of the promoted argument
            if (conv == 'd' || conv == 'i') {
                if (lenmod == 2) arg_r28 = (uint)(short)arg_r28;
                if (lenmod == 1) arg_r28 = (uint)(char)arg_r28;
            }
            else {
                if (lenmod == 2) arg_r28 = arg_r28 & 0xffff;
                if (lenmod == 1) arg_r28 = arg_r28 & 0xff;
            }
            if (lenmod == 4) {
                out = (byte *)FUN_80389634(arg_r22, arg_r23, (int)numEnd, (char *)spec);
            }
            else {
                out = (byte *)FUN_80389948(arg_r28, (int)numEnd, (char *)spec);
            }
            if (out == (byte *)0) goto invalid;
            emitted = numbuf + 16 - (int)out;
        }
        else {
        invalid:
            // treat the raw remaining conversion text as a literal
            litLen = FUN_8038b5c8((int)emitted);
            if (litLen == 0) return total + litLen;
            rc = sink(sinkState, emitted);
            if (rc != 0) return total + litLen;
            return -1;
        }

        // ---- pad + emit the formatted field ----
        field = emitted;
        if (so[0] != '\0') {                 // right-justify with pad
            padChar = ' ';
            if (so[0] == '\x02') padChar = '0';
            firstByte = *out;
            if ((((firstByte == 0x2b) || (firstByte == 0x2d)) || (firstByte == 0x20)) &&
                (padChar == '0')) {
                rc = sink(sinkState, out, 1);
                if (rc == 0) return -1;
                out = out + 1;
                field = emitted + -1;
            }
            for (; (int)emitted < width; emitted = emitted + 1) {
                rc = sink(sinkState, &padChar, 1);
                if (rc == 0) return -1;
            }
        }
        if ((field != (char *)0) && (rc = sink(sinkState, out, field), rc == 0)) {
            return -1;
        }
        if (so[0] == '\0') {                 // left-justify: trailing spaces
            for (; (int)emitted < width; emitted = emitted + 1) {
                char sp = 0x20;
                rc = sink(sinkState, &sp, 1);
                if (rc == 0) return -1;
            }
        }
        total = total + (int)emitted;
    }
}
