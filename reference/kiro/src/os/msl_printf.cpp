// Address: 0x80387d84 | Name: msl_memmove_fwd_unaligned  | Date: 2026-09-13
// Address: 0x80387e44 | Name: msl_memmove_back_aligned   | Date: 2026-09-13
// Address: 0x80387eec | Name: msl_memmove_fwd_aligned    | Date: 2026-09-13
// Address: 0x80387fa8 | Name: msl_install_heap_walk      | Date: 2026-09-13
// Address: 0x80387fb8 | Name: msl_vsprintf               | Date: 2026-09-13
// Address: 0x80388098 | Name: msl_vsnprintf              | Date: 2026-09-13
// Address: 0x8038816c | Name: msl_vsprintf_va            | Date: 2026-09-13
// Address: 0x803881e4 | Name: msl_vprintf                | Date: 2026-09-13
// Address: 0x8038827c | Name: msl_printf_buf_sink        | Date: 2026-09-13
// Address: 0x803882e8 | Name: msl_printf_file_sink       | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) formatted-output family + the last three
// memmove copy cores. The printf entry points all funnel into the shared format
// engine FUN_80388340(sink, sinkState, fmt, argInfo):
//   sink       - one of the two callbacks below (buffer or FILE)
//   sinkState  - {char *base; int cap/limit; int written}
//   fmt        - the format string
//   argInfo    - a 4-byte descriptor: [0]=mode (2 = unbounded buffer, 3 = bounded
//                buffer), the rest reserved.
//
// The memmove cores complete the set dispatched by msl_memmove (msl_string.cpp):
//   fwd/unaligned, back/aligned, fwd/aligned. They keep their raw FUN_ symbol
//   names so the externs in msl_string.cpp resolve at link.
//
// PPC varargs note: the register-save prologue (in_cr1 bit) spilling the GPR
// argument window to the stack is modelled here as the leading vararg params;
// the real work reads them back through a va_list pointer (local_28/local_24).

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern int  FUN_80388340(void *sink, void *sinkState, char *fmt, char *argInfo); // format engine
extern int  FUN_8038cfb0(int stream, int mode);   // buffer-state probe / stream check
extern void leaf_31e8(u32 dst, u32 src, s32 len); // memcpy leaf
extern uint msl_fwrite(uint src, uint elemSize, int count, u32 *file);  // msl_stdio.cpp
extern void msl_stdio_lock(void);
extern void msl_stdio_unlock(void);
extern void msl_heap_walk_pools(void);            // heap-walk callback (installed below)
extern void *DAT_80612fd8;                        // heap-walk hook slot
extern u32   DAT_80497638;                        // stdout FILE object

// Forward declarations for the two sink callbacks (used by the entry points above
// their definitions).
undefined4  FUN_8038827c(int *sinkState, uint chunk, int len);       // buffer sink
undefined4 *FUN_803882e8(undefined4 *file, uint chunk, uint len);    // FILE sink

// ===========================================================================
// memmove copy cores (completing the msl_memmove dispatch set)
// ===========================================================================

// msl_memmove_fwd_unaligned (0x80387d84) - forward copy for dst-below-src, NOT
// co-aligned mod 4. Aligns the destination head, then copies word-at-a-time with
// a shift/merge across the misaligned source, finishing with the trailing bytes.
void FUN_80387d84(int dst, int src, uint n)
{
    uint  headWord;
    u8   *pb;
    uint  srcMis;
    uint  carry;
    uint  words;
    uint *psw;
    uint *psw_cur;
    u8   *pdb;
    uint *pdw;
    uint *pdw_cur;
    uint  lshift;
    int   rshift;

    pb  = (u8 *)(src + -1);
    words = -dst & 3;
    pdb = (u8 *)(dst + -1);
    if (words != 0) {
        n = n - words;
        do {
            pb  = pb + 1;
            words = words - 1;
            pdb = pdb + 1;
            *pdb = *pb;
        } while (words != 0);
    }
    srcMis  = (uint)(pb + 1) & 3;
    words   = n >> 3;
    lshift  = (int)(pb + 1) * 8 & 0x18;
    carry   = *(uint *)(pb + (1 - srcMis));
    rshift  = 0x20 - lshift;
    pdw     = (uint *)(pdb + -3);
    psw     = (uint *)(pb + (1 - srcMis));
    do {
        psw_cur = psw;
        pdw_cur = pdw;
        headWord = psw_cur[1];
        words = words - 1;
        pdw_cur[1] = carry << lshift | headWord >> rshift;
        psw = psw_cur + 2;
        carry = *psw;
        pdw = pdw_cur + 2;
        *pdw = headWord << lshift | carry >> rshift;
    } while (words != 0);
    if ((n & 4) != 0) {
        psw = psw_cur + 3;
        pdw = pdw_cur + 3;
        *pdw = carry << lshift | *psw >> rshift;
    }
    words = n & 3;
    pdb = (u8 *)((int)pdw + 3);
    if (words == 0) {
        return;
    }
    pb = (u8 *)((int)psw + (3 - (4 - srcMis)));
    do {
        pb  = pb + 1;
        words = words - 1;
        pdb = pdb + 1;
        *pdb = *pb;
    } while (words != 0);
    return;
}

// msl_memmove_back_aligned (0x80387e44) - backward copy for dst-above-src, dst/src
// co-aligned mod 4. Aligns the tail, then copies in 32-byte unrolled word blocks,
// then word and byte remainders.
void FUN_80387e44(int dst, int src, uint n)
{
    u32   w0;
    uint  align;
    u32   w1;
    u32  *ps;
    u32  *pd;

    pd = (u32 *)(dst + n);
    ps = (u32 *)(src + n);
    align = (uint)pd & 3;
    if (align != 0) {
        n = n - align;
        do {
            ps = (u32 *)((int)ps + -1);
            align = align - 1;
            pd = (u32 *)((int)pd + -1);
            *(u8 *)pd = *(u8 *)ps;
        } while (align != 0);
    }
    for (align = n >> 5; align != 0; align = align - 1) {
        w0 = ps[-2];  pd[-1] = ps[-1];
        w1 = ps[-3];  pd[-2] = w0;
        w0 = ps[-4];  pd[-3] = w1;
        w1 = ps[-5];  pd[-4] = w0;
        w0 = ps[-6];  pd[-5] = w1;
        w1 = ps[-7];  pd[-6] = w0;
        ps = ps + -8; w0 = *ps;
        pd[-7] = w1;
        pd = pd + -8; *pd = w0;
    }
    for (align = n >> 2 & 7; align != 0; align = align - 1) {
        ps = ps + -1;
        pd = pd + -1;
        *pd = *ps;
    }
    align = n & 3;
    if (align != 0) {
        do {
            ps = (u32 *)((int)ps + -1);
            align = align - 1;
            pd = (u32 *)((int)pd + -1);
            *(u8 *)pd = *(u8 *)ps;
        } while (align != 0);
    }
    return;
}

// msl_memmove_fwd_aligned (0x80387eec) - forward copy for dst-below-src, dst/src
// co-aligned mod 4. Aligns the head, then copies in 32-byte unrolled word blocks,
// then word and byte remainders.
void FUN_80387eec(int dst, int src, uint n)
{
    u32   w0;
    u8   *pb;
    u32   w1;
    u8   *pdb;
    u32  *ps;
    uint  align;
    u32  *pd;

    pb  = (u8 *)(src + -1);
    align = -dst & 3;
    pdb = (u8 *)(dst + -1);
    if (align != 0) {
        n = n - align;
        do {
            pb  = pb + 1;
            align = align - 1;
            pdb = pdb + 1;
            *pdb = *pb;
        } while (align != 0);
    }
    ps = (u32 *)(pb + -3);
    pd = (u32 *)(pdb + -3);
    for (align = n >> 5; align != 0; align = align - 1) {
        w0 = ps[2];  pd[1] = ps[1];
        w1 = ps[3];  pd[2] = w0;
        w0 = ps[4];  pd[3] = w1;
        w1 = ps[5];  pd[4] = w0;
        w0 = ps[6];  pd[5] = w1;
        w1 = ps[7];  pd[6] = w0;
        ps = ps + 8; w0 = *ps;
        pd[7] = w1;
        pd = pd + 8; *pd = w0;
    }
    for (align = n >> 2 & 7; align != 0; align = align - 1) {
        ps = ps + 1;
        pd = pd + 1;
        *pd = *ps;
    }
    align = n & 3;
    pb  = (u8 *)((int)ps + 3);
    pdb = (u8 *)((int)pd + 3);
    if (align != 0) {
        do {
            pb  = pb + 1;
            align = align - 1;
            pdb = pdb + 1;
            *pdb = *pb;
        } while (align != 0);
    }
    return;
}

// ===========================================================================
// printf family
// ===========================================================================

// msl_install_heap_walk (0x80387fa8) - orientation fixup: install the heap-walk
// pool callback into the runtime hook slot. Invoked lazily the first time a
// wide-oriented stream needs the pool walker.
void FUN_80387fa8(void)
{
    DAT_80612fd8 = (void *)msl_heap_walk_pools;
}

// msl_printf_buf_sink (0x8038827c) - format-engine sink that copies a formatted
// chunk into a size-bounded output buffer. sinkState = {char *base; int limit;
// int written}. Copies at most (limit - written) bytes and advances written.
// Always returns 1 (never fails - excess is simply dropped).
undefined4 FUN_8038827c(int *sinkState, uint chunk, int len)
{
    int written;
    int copy;

    written = sinkState[2];
    copy = sinkState[1] - written;
    if ((uint)(written + len) <= (uint)sinkState[1]) {
        copy = len;
    }
    leaf_31e8(*sinkState + written, chunk, copy);
    sinkState[2] = sinkState[2] + copy;
    return 1;
}

// msl_printf_file_sink (0x803882e8) - format-engine sink that writes a formatted
// chunk to a FILE via fwrite. Returns the FILE on success, or NULL if the write
// was short (error).
undefined4 *FUN_803882e8(undefined4 *file, uint chunk, uint len)
{
    uint n;

    n = msl_fwrite(chunk, 1, len, (u32 *)file);
    if (len != n) {
        file = (undefined4 *)0x0;
    }
    return file;
}

// msl_vsprintf (0x80387fb8) - unbounded sprintf-into-buffer. mode byte 2 selects
// the buffer sink with an unbounded (0xffffffff) limit. NUL-terminates the buffer
// after the engine returns (clamping to -2 on the engine's -1 error).
void FUN_80387fb8(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int dstBuf, char *fmt, u32 va0, u32 va1, u32 va2, u32 va3, u32 va4, u32 va5)
{
    int  produced;
    int  term;
    char argInfo[4];
    int  sinkState[3];
    // va spill area (kept so the frame layout matches the original)
    u32  vaSpill[8];

    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    vaSpill[0] = va0; vaSpill[1] = va1; vaSpill[2] = va2; vaSpill[3] = va3;
    vaSpill[4] = va4; vaSpill[5] = va5; vaSpill[6] = 0; vaSpill[7] = 0;

    argInfo[0] = '\x02';
    argInfo[1] = '\0'; argInfo[2] = '\0'; argInfo[3] = '\0';
    sinkState[0] = dstBuf;
    sinkState[1] = -1;   // unbounded
    sinkState[2] = 0;
    produced = FUN_80388340((void *)FUN_8038827c, sinkState, fmt, argInfo);
    if (dstBuf != 0) {
        term = -2;
        if (produced != -1) {
            term = produced;
        }
        *(u8 *)(dstBuf + term) = 0;
    }
}

// msl_vsnprintf (0x80388098) - bounded snprintf-into-buffer. mode byte 3 selects
// the buffer sink with an explicit byte limit. NUL-terminates at min(produced,
// cap-1).
void FUN_80388098(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int dstBuf, uint cap, char *fmt, u32 va0, u32 va1, u32 va2, u32 va3, u32 va4)
{
    uint produced;
    uint term;
    char argInfo[4];
    int  sinkState[3];
    u32  vaSpill[8];

    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    vaSpill[0] = va0; vaSpill[1] = va1; vaSpill[2] = va2; vaSpill[3] = va3;
    vaSpill[4] = va4; vaSpill[5] = 0; vaSpill[6] = 0; vaSpill[7] = 0;

    argInfo[0] = '\x03';
    argInfo[1] = '\0'; argInfo[2] = '\0'; argInfo[3] = '\0';
    sinkState[0] = dstBuf;
    sinkState[1] = (int)cap;
    sinkState[2] = 0;
    produced = FUN_80388340((void *)FUN_8038827c, sinkState, fmt, argInfo);
    if (dstBuf != 0) {
        term = cap - 1;
        if (produced < cap) {
            term = produced;
        }
        *(u8 *)(dstBuf + term) = 0;
    }
}

// msl_vsprintf_va (0x8038816c) - sprintf-into-buffer taking an already-collected
// argInfo descriptor. Unbounded (limit -1) buffer sink; NUL-terminates on return.
void FUN_8038816c(int dstBuf, char *fmt, char *argInfo)
{
    int produced;
    int term;
    int sinkState[3];

    sinkState[0] = dstBuf;
    sinkState[1] = -1;
    sinkState[2] = 0;
    produced = FUN_80388340((void *)FUN_8038827c, sinkState, fmt, argInfo);
    if (dstBuf != 0) {
        term = -2;
        if (produced != -1) {
            term = produced;
        }
        *(u8 *)(dstBuf + term) = 0;
    }
}

// msl_vprintf (0x803881e4) - formatted write to the stdout FILE (DAT_80497638)
// via the FILE sink, under the stdio lock. Checks the stream is writable first
// (FUN_8038cfb0 on the stdout FILE); returns the engine result or -1.
int FUN_803881e4(char *fmt, char *argInfo)
{
    int result;

    result = FUN_8038cfb0((int)&DAT_80497638, -1);
    if (result < 0) {
        msl_stdio_lock();
        result = FUN_80388340((void *)FUN_803882e8, &DAT_80497638, fmt, argInfo);
        msl_stdio_unlock();
    }
    else {
        result = -1;
    }
    return result;
}
