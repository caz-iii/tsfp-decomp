// Address: 0x80387844 | Name: msl_utf16_to_utf8   | Date: 2026-09-13
// Address: 0x8038795c | Name: msl_utf8_to_utf16   | Date: 2026-09-13
// Address: 0x80387a7c | Name: msl_utf8_seq_len    | Date: 2026-09-13
// Address: 0x80387b68 | Name: msl_memcmp          | Date: 2026-09-13
// Address: 0x80387bb4 | Name: msl_memrchr         | Date: 2026-09-13
// Address: 0x80387be0 | Name: msl_memchr          | Date: 2026-09-13
// Address: 0x80387c0c | Name: msl_memmove         | Date: 2026-09-13
// Address: 0x80387cd8 | Name: msl_memmove_back_unaligned | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) string/memory + UTF conversion helpers.
// These sit alongside the stdio internals: the UTF routines back wide-char
// stream I/O, and the mem* routines are the C library primitives the runtime
// links against.
//
// UTF encoding (matches the original 1/2/3-byte UTF-8 form):
//   U+0000..U+007F -> 1 byte     U+0080..U+07FF -> 2 bytes
//   U+0800..U+FFFF -> 3 bytes
//
// The functions keep their raw FUN_ symbol names so existing per-file externs
// elsewhere continue to resolve at link (static library target).

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern u32  FUN_8038b4cc(int dst, int src, uint len);   // memcpy (small block)
extern void FUN_80387e44(int dst, int src, uint len);   // memmove: aligned, backward
extern void FUN_80387eec(int dst, int src, uint len);   // memmove: aligned, forward
extern void FUN_80387d84(int dst, int src, uint len);   // memmove: unaligned, forward
extern u32  DAT_8061b9f0;                               // UTF lead-byte prefix table base
void FUN_80387cd8(int dst, int src, uint n);            // fwd decl: defined below (backward unaligned core)

// ---------------------------------------------------------------------------
// msl_utf8_seq_len (0x80387a7c) - classify the UTF-8 sequence starting at 'p',
// bounded by 'avail' bytes. Returns the byte length (0 for a NUL, 1/2/3 for a
// valid sequence), 0xffffffff on a malformed lead/continuation byte, or
// 0xfffffffe when the sequence is truncated by 'avail'.
undefined4 FUN_80387a7c(char *p, uint avail)
{
    uint lead;

    if (avail == 0) {
        return 0xffffffff;
    }
    if (*p == '\0') {
        return 0;
    }
    lead = (uint)*p;
    if ((lead & 0x80) == 0) {
        return 1;
    }
    if ((lead & 0xe0) == 0xc0) {
        if (avail < 2) {
            return 0xfffffffe;
        }
        if ((p[1] & 0x80U) != 0x80) {
            return 0xffffffff;
        }
        return 2;
    }
    if ((lead & 0xf0) != 0xe0) {
        return 0xffffffff;
    }
    if (2 < avail) {
        if (((p[1] & 0x80U) == 0x80) && ((p[2] & 0x80U) == 0x80)) {
            return 3;
        }
        return 0xffffffff;
    }
    if (((avail != 2) || ((p[1] & 0x80U) != 0x80)) && (avail != 1)) {
        return 0xffffffff;
    }
    return 0xfffffffe;
}

// ---------------------------------------------------------------------------
// msl_utf16_to_utf8 (0x80387844) - convert a NUL-terminated UTF-16 (host u16)
// string into a UTF-8 byte buffer of at most 'cap' bytes, NUL-terminating when
// it fits. Returns the number of bytes written (excluding the terminator).
uint FUN_80387844(int dst, ushort *src, uint cap)
{
    ushort  ch;
    byte   *enc;
    uint    code;
    uint    len;
    uint    written;
    undefined4 prefix;
    byte    tmp[16];

    written = 0;
    if ((dst == 0) || (src == (ushort *)0x0)) {
        return 0;
    }
    for (; written <= cap; written = written + len) {
        ch = *src;
        code = (uint)ch;
        if (code == 0) {
            *(u8 *)(dst + written) = 0;
            return written;
        }
        src = src + 1;
        prefix = DAT_8061b9f0;
        (void)prefix;
        if (code < 0x80) {
            len = 1;
        }
        else if (code < 0x800) {
            len = 2;
        }
        else {
            len = 3;
        }
        enc = tmp + len;
        if (len == 2) {
LAB_803878fc:
            enc = enc + -1;
            *enc = (byte)code & 0x3f | 0x80;
            code = code >> 6;
LAB_8038790c:
            enc[-1] = (byte)code | tmp[len - 4];
        }
        else if (len < 2) {
            if (len != 0) goto LAB_8038790c;
        }
        else if (len < 4) {
            code = (uint)(ch >> 6);
            enc = tmp + (len - 1);
            *enc = (byte)ch & 0x3f | 0x80;
            goto LAB_803878fc;
        }
        if (cap < written + len) {
            return written;
        }
        FUN_8038b4cc(dst + written, (int)tmp, len);
    }
    return written;
}

// ---------------------------------------------------------------------------
// msl_utf8_to_utf16 (0x8038795c) - decode one UTF-8 sequence at 'src' (bounded
// by 'avail') into a single host u16 code point written to *out (when out is
// non-NULL). Returns the sequence length consumed, or -1 on a malformed or
// over-long encoding.
int FUN_8038795c(ushort *out, byte *src, uint avail)
{
    byte  b;
    int   canonLen;
    int   seqLen;
    ushort code;

    code = 0;
    if (src == (byte *)0x0) {
        return 0;
    }
    if (avail == 0) {
        return -1;
    }
    seqLen = FUN_80387a7c((char *)src, avail);
    if (seqLen < 0) {
        return -1;
    }
    if (seqLen == 2) {
LAB_803879e4:
        b = *src;
        src = src + 1;
        code = (code | b & 0x3f) << 6;
    }
    else {
        if (1 < seqLen) {
            if (3 < seqLen) goto LAB_80387a08;
            b = *src;
            src = src + 1;
            code = (b & 0xf) << 6;
            goto LAB_803879e4;
        }
        if (seqLen < 1) goto LAB_80387a08;
    }
    code = code | *src & 0x7f;
LAB_80387a08:
    if (code == 0) {
        canonLen = 0;
    }
    else if (code < 0x80) {
        canonLen = 1;
    }
    else if (code < 0x800) {
        canonLen = 2;
    }
    else {
        canonLen = 3;
    }
    if (canonLen == seqLen) {
        if (out != (ushort *)0x0) {
            *out = code;
        }
    }
    else {
        seqLen = -1;
    }
    return seqLen;
}

// ---------------------------------------------------------------------------
// msl_memcmp (0x80387b68) - compare 'n' bytes of two buffers. Returns 1 if the
// first differing byte in 'a' is greater, -1 if less, 0 if equal over n bytes.
undefined4 FUN_80387b68(int a, int b, int n)
{
    int    remaining;
    byte  *pa;
    byte  *pb;

    pa = (byte *)(a + -1);
    pb = (byte *)(b + -1);
    remaining = n + 1;
    do {
        remaining = remaining + -1;
        if (remaining == 0) {
            return 0;
        }
        pa = pa + 1;
        pb = pb + 1;
    } while (*pa == *pb);
    if (*pb <= *pa) {
        return 1;
    }
    return 0xffffffff;
}

// ---------------------------------------------------------------------------
// msl_memrchr (0x80387bb4) - scan 'n' bytes backward from base+n-1 for the last
// occurrence of byte 'ch'. Returns a pointer to it, or NULL if not found.
char *FUN_80387bb4(int base, char ch, int n)
{
    char *p;
    int   remaining;

    p = (char *)(base + n);
    remaining = n + 1;
    do {
        remaining = remaining + -1;
        if (remaining == 0) {
            return (char *)0x0;
        }
        p = p + -1;
    } while (*p != ch);
    return p;
}

// ---------------------------------------------------------------------------
// msl_memchr (0x80387be0) - scan 'n' bytes forward from base for the first
// occurrence of byte 'ch'. Returns a pointer to it, or NULL if not found.
char *FUN_80387be0(int base, char ch, int n)
{
    char *p;
    int   remaining;

    p = (char *)(base + -1);
    remaining = n + 1;
    do {
        remaining = remaining + -1;
        if (remaining == 0) {
            return (char *)0x0;
        }
        p = p + 1;
    } while (*p != ch);
    return p;
}

// ---------------------------------------------------------------------------
// msl_memmove (0x80387c0c) - overlap-safe copy of 'n' bytes from src to dst.
// Chooses among four optimized copy cores by direction (dst above/below src) and
// alignment (dst/src co-aligned mod 4). Small (<32) copies are done byte-wise
// inline. Returns dst.
uint FUN_80387c0c(uint dst, uint src, uint n)
{
    int shift;
    int overlap;
    u8 *ps;
    u8 *pd;

    shift = countLeadingZeros(dst ^ src);
    overlap = dst << shift;
    if (n < 0x20) {
        if (overlap < 0) {
            ps = (u8 *)(src + n);
            pd = (u8 *)(dst + n);
            shift = n + 1;
            while (shift = shift + -1, shift != 0) {
                ps = ps + -1;
                pd = pd + -1;
                *pd = *ps;
            }
        }
        else {
            ps = (u8 *)(src - 1);
            pd = (u8 *)(dst - 1);
            shift = n + 1;
            while (shift = shift + -1, shift != 0) {
                ps = ps + 1;
                pd = pd + 1;
                *pd = *ps;
            }
        }
    }
    else if (((dst ^ src) & 3) == 0) {
        if (overlap < 0) {
            FUN_80387e44(dst, src, n);
        }
        else {
            FUN_80387eec(dst, src, n);
        }
    }
    else if (overlap < 0) {
        FUN_80387cd8(dst, src, n);
    }
    else {
        FUN_80387d84(dst, src, n);
    }
    return dst;
}

// ---------------------------------------------------------------------------
// msl_memmove_back_unaligned (0x80387cd8) - backward copy core for the case
// where dst overlaps above src and the two are NOT co-aligned mod 4. Aligns the
// destination tail, then copies word-at-a-time using a shift/merge across the
// source misalignment, finishing with the leading byte remainder.
void FUN_80387cd8(int dst, int src, uint n)
{
    uint  head;
    uint  words;
    uint  carry;
    int   lshift;
    int   rshift;
    u8   *ps;
    uint *psw;
    uint *psw_prev;
    uint *pdw;
    uint *pdw_prev;

    pdw = (uint *)(dst + n);
    ps = (u8 *)(src + n);
    head = (uint)pdw & 3;
    if (head != 0) {
        n = n - head;
        do {
            ps = ps + -1;
            head = head - 1;
            pdw = (uint *)((int)pdw + -1);
            *(u8 *)pdw = *ps;
        } while (head != 0);
    }
    lshift = ((uint)ps & 3) * 8;
    rshift = ((uint)ps & 3) * -8 + 0x20;
    words = n >> 3;
    carry = *(uint *)(ps + -((uint)ps & 3));
    psw = (uint *)(ps + -((uint)ps & 3));
    do {
        pdw_prev = pdw;
        psw_prev = psw;
        head = psw_prev[-1];
        words = words - 1;
        pdw_prev[-1] = head << lshift | carry >> rshift;
        psw = psw_prev + -2;
        carry = *psw;
        pdw = pdw_prev + -2;
        *pdw = carry << lshift | head >> rshift;
    } while (words != 0);
    if ((n & 4) != 0) {
        psw = psw_prev + -3;
        pdw = pdw_prev + -3;
        *pdw = *psw << lshift | carry >> rshift;
    }
    head = n & 3;
    if (head == 0) {
        return;
    }
    ps = (u8 *)((int)psw + ((uint)ps & 3));
    do {
        ps = ps + -1;
        head = head - 1;
        pdw = (uint *)((int)pdw + -1);
        *(u8 *)pdw = *ps;
    } while (head != 0);
    return;
}
