// Address: 0x8038b258 | Name: msl_strstr   | Date: 2026-09-13
// Address: 0x8038b2c0 | Name: msl_strrchr  | Date: 2026-09-13
// Address: 0x8038b308 | Name: msl_strchr   | Date: 2026-09-13
// Address: 0x8038b338 | Name: msl_strncmp  | Date: 2026-09-13
// Address: 0x8038b378 | Name: msl_strcmp   | Date: 2026-09-13
// Address: 0x8038b4a0 | Name: msl_strcat   | Date: 2026-09-13
// Address: 0x8038b4cc | Name: msl_strncpy  | Date: 2026-09-13
// Address: 0x8038b510 | Name: msl_strcpy   | Date: 2026-09-13
// Address: 0x8038b5c8 | Name: msl_strlen   | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) C string primitives. strcmp/strcpy use the
// classic word-at-a-time NUL-detection trick ((w + 0xfefefeff) & 0x80808080) once
// the operands are 4-byte co-aligned. These keep their raw FUN_ symbol names so
// the many cross-module call sites (printf/scanf engines, etc.) resolve at link.

#include "common.h"

// ---------------------------------------------------------------------------
// msl_strstr (0x8038b258) - locate the first occurrence of substring `needle` in
// `haystack`. Returns a pointer to the match, or NULL. An empty needle returns
// haystack.
char *FUN_8038b258(char *haystack, char *needle)
{
    char *hit;
    char *h;
    char *n;

    if (needle == (char *)0x0) return haystack;
    hit = haystack + -1;
    if (*needle == '\0') return haystack;
    do {
        do {
            h = hit;
            hit = h + 1;
            if (*hit == '\0') return (char *)0x0;
        } while (*hit != *needle);
        n = needle + -1;
        do {
            h = h + 1;
            n = n + 1;
            if (*h != *n) break;
        } while (*h != '\0');
        if (*n == '\0') return hit;
    } while (true);
}

// ---------------------------------------------------------------------------
// msl_strrchr (0x8038b2c0) - locate the last occurrence of byte `c` in string s
// (including the terminator when c == 0). Returns a pointer or NULL.
char *FUN_8038b2c0(int s, char c)
{
    char *p;
    char *last;

    p = (char *)(s + -1);
    last = (char *)0x0;
    while (true) {
        p = p + 1;
        if (*p == '\0') break;
        if (*p == c) last = p;
    }
    if (last != (char *)0x0) return last;
    if (c != '\0') return (char *)0x0;
    return p;
}

// ---------------------------------------------------------------------------
// msl_strchr (0x8038b308) - locate the first occurrence of byte `c` in string s
// (including the terminator when c == 0). Returns a pointer or NULL.
char *FUN_8038b308(int s, char c)
{
    char *p;

    p = (char *)(s + -1);
    while (true) {
        p = p + 1;
        if (*p == '\0') break;
        if (*p == c) return p;
    }
    if (c == '\0') return p;
    return (char *)0x0;
}

// ---------------------------------------------------------------------------
// msl_strncmp (0x8038b338) - compare at most n bytes of two strings. Returns the
// byte difference at the first mismatch, or 0 if equal within n / both terminate.
int FUN_8038b338(int a, int b, int n)
{
    uint  ca;
    byte *pa;
    byte *pb;
    int   left;

    pa = (byte *)(a + -1);
    pb = (byte *)(b + -1);
    left = n + 1;
    while (true) {
        left = left + -1;
        if (left == 0) return 0;
        pa = pa + 1;
        ca = (uint)*pa;
        pb = pb + 1;
        if (ca != *pb) break;
        if (ca == 0) return 0;
    }
    return ca - *pb;
}

// ---------------------------------------------------------------------------
// msl_strcmp (0x8038b378) - compare two NUL-terminated strings. Once co-aligned
// mod 4, scans 4 bytes at a time using the word NUL-test, falling back to bytewise
// at a mismatch or misalignment. Returns <0, 0, or >0.
int FUN_8038b378(uint *a, uint *b)
{
    int  d;
    int  i;
    uint wa;
    uint wb;
    uint align;

    wa = (uint)*(byte *)a;
    if (wa - *(byte *)b != 0) {
        return wa - *(byte *)b;
    }
    align = (uint)a & 3;
    if (((uint)b & 3) == align) {
        if (align != 0) {
            if (wa == 0) return 0;
            for (i = 3 - align; i != 0; i = i + -1) {
                a = (uint *)((int)a + 1);
                b = (uint *)((int)b + 1);
                d = (uint)*(byte *)a - (uint)*(byte *)b;
                if (d != 0) return d;
                if (*(byte *)a == 0) return 0;
            }
            a = (uint *)((int)a + 1);
            b = (uint *)((int)b + 1);
        }
        wb = *b;
        wa = *a;
        while ((wa + 0xfefefeff & 0x80808080) == 0) {
            if (wa != wb) {
                if (wb < wa) return 1;
                return -1;
            }
            a = a + 1;
            b = b + 1;
            wb = *b;
            wa = *a;
        }
        wa = (uint)*(byte *)a;
        if (wa - *(byte *)b != 0) {
            return wa - *(byte *)b;
        }
    }
    if (wa == 0) return 0;
    do {
        a = (uint *)((int)a + 1);
        b = (uint *)((int)b + 1);
        d = (uint)*(byte *)a - (uint)*(byte *)b;
        if (d != 0) return d;
    } while (*(byte *)a != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// msl_strcat (0x8038b4a0) - append src to the end of dst (dst must have room).
void FUN_8038b4a0(int dst, int src)
{
    char  c;
    char *ps;
    char *pd;
    char *end;

    ps = (char *)(src + -1);
    end = (char *)(dst + -1);
    do {
        pd = end;
        end = pd + 1;
    } while (pd[1] != '\0');
    do {
        ps = ps + 1;
        c = *ps;
        pd = pd + 1;
        *pd = c;
    } while (c != '\0');
    return;
}

// ---------------------------------------------------------------------------
// msl_strncpy (0x8038b4cc) - copy at most n bytes of src to dst, NUL-padding the
// remainder of the n bytes if src is shorter.
void FUN_8038b4cc(int dst, int src, int n)
{
    char  c;
    char *ps;
    int   left;
    char *pd;

    ps = (char *)(src + -1);
    pd = (char *)(dst + -1);
    left = n + 1;
    do {
        left = left + -1;
        if (left == 0) return;
        ps = ps + 1;
        c = *ps;
        pd = pd + 1;
        *pd = c;
    } while (c != '\0');
    while (left = left + -1, left != 0) {
        pd = pd + 1;
        *pd = '\0';
    }
    return;
}

// ---------------------------------------------------------------------------
// msl_strcpy (0x8038b510) - copy src to dst including the terminator. Once
// co-aligned mod 4, copies a word at a time using the NUL-test, else bytewise.
void FUN_8038b510(int *dst, int *src)
{
    char  c;
    uint  align;
    int  *pd;
    int   w;
    int  *pdPrev;

    align = (uint)src & 3;
    if (((uint)dst & 3) == align) {
        if (align != 0) {
            c = *(char *)src;
            *(char *)dst = c;
            if (c == '\0') return;
            for (w = 3 - align; w != 0; w = w + -1) {
                src = (int *)((int)src + 1);
                c = *(char *)src;
                dst = (int *)((int)dst + 1);
                *(char *)dst = c;
                if (c == '\0') return;
            }
            dst = (int *)((int)dst + 1);
            src = (int *)((int)src + 1);
        }
        w = *src;
        if ((w + 0xfefefeffU & 0x80808080) == 0) {
            pd = dst + -1;
            do {
                pdPrev = pd;
                pd = pdPrev + 1;
                *pd = w;
                src = src + 1;
                w = *src;
            } while ((w + 0xfefefeffU & 0x80808080) == 0);
            dst = pdPrev + 2;
        }
    }
    c = *(char *)src;
    *(char *)dst = c;
    if (c != '\0') {
        do {
            src = (int *)((int)src + 1);
            c = *(char *)src;
            dst = (int *)((int)dst + 1);
            *(char *)dst = c;
        } while (c != '\0');
        return;
    }
    return;
}

// ---------------------------------------------------------------------------
// msl_strlen (0x8038b5c8) - return the length of the NUL-terminated string.
int FUN_8038b5c8(int s)
{
    int   len;
    char *p;

    p = (char *)(s + -1);
    len = -1;
    do {
        p = p + 1;
        len = len + 1;
    } while (*p != '\0');
    return len;
}
