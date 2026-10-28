// batch_1085: MetroTRK low-level support - the lock primitive stubs used across the
// TRK code, a simple host "notify" request, PPC cache-coherency and MMIO-safe copy
// helpers, a word-aligned memset, and the exception/interrupt entry that snapshots
// the full CPU register context before handing control to the debug monitor.
//
// On this single-processor GameCube TRK build the "lock" primitives are no-ops
// (interrupts are already masked around the critical sections), so trk_lock_* just
// return 0. The cache helpers issue the PPC dcbst/dcbf/icbi/isync sequence needed to
// make freshly written code visible to the instruction fetcher (TRK patches
// breakpoints into the running image).
//
// Non-matching decomp: PPC cache intrinsics and the interrupt-return builtin are
// declared as permissive per-file externs; the TRK buffer/transaction helpers resolve
// as cross-module declarations (common.h does not pull generated_decls.h).

#include "common.h"

// --- TRK buffer/transaction helpers (defined in trk_message.cpp/trk_host_io.cpp) --
int  trk_buffer_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int *out_index, unsigned int **out_ptr, int a11, int a12,
                      int a13, int a14, int a15, int a16);
void trk_buffer_free(int idx);
int  trk_host_transact(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                       int req_msg, int *out_reply_idx, unsigned int a11, unsigned int a12,
                       int a13, int a14, int a15, int a16);

// --- request builders + monitor entry (raw, undocumented) -------------------
extern int  FUN_8039420c(int msg);   // build opcode-0x90 request body
extern int  FUN_80394188(int msg);   // build default request body
extern int  FUN_80394428(void);      // enter the TRK debug monitor loop
extern void FUN_80395698(void);      // pre-return-from-interrupt fixup

// --- PPC cache-coherency / sync intrinsics (permissive; PPC builtins) -------
void dataCacheBlockStore(unsigned int addr);
void dataCacheBlockFlush(unsigned int addr);
void instructionCacheBlockInvalidate(unsigned int addr);
void instructionSynchronize(void);
int  sync(...);              // matches generated_externs.h decl
int  returnFromInterrupt(...);   // matches generated_externs.h decl

// --- saved-context globals written by the exception entry -------------------
extern void        *DAT_805f13bc;    // -> saved FP context block
extern void        *DAT_805f13c0;    // -> GPR save area (&DAT_805f13b4)
extern void        *DAT_805f13e8;    // -> secondary FP context
extern unsigned int DAT_805f13b4;    // r0
extern unsigned char *DAT_805f13b8;  // r1 (stack pointer)
extern unsigned int DAT_805f13c4, DAT_805f13c8, DAT_805f13cc, DAT_805f13d0;  // r3..r6
extern unsigned int DAT_805f13d4, DAT_805f13d8, DAT_805f13dc, DAT_805f13e0;  // r7..r10... (r11)
extern unsigned int DAT_805f13e4;    // r12
extern unsigned int DAT_805f13ec, DAT_805f13f0, DAT_805f13f4, DAT_805f13f8;  // r14..r17
extern unsigned int DAT_805f13fc, DAT_805f1400, DAT_805f1404, DAT_805f1408;  // r18..r21
extern unsigned int DAT_805f140c, DAT_805f1410, DAT_805f1414, DAT_805f1418;  // r22..r25
extern unsigned int DAT_805f141c, DAT_805f1420, DAT_805f1424, DAT_805f1428;  // r26..r29
extern unsigned int DAT_805f142c, DAT_805f1430;                              // r30..r31
extern unsigned int DAT_805f1434, DAT_805f1438, DAT_805f143c, DAT_805f1440;  // LR, CTR, XER, MSR
extern unsigned int DAT_805f1444, DAT_805f1448;                              // DAR, DSISR
extern char        *DAT_805f1454;    // -> "monitor active" flag
extern unsigned int DAT_805f1450;    // re-entry flag word
extern unsigned int DAT_805f1464;    // return value slot
extern unsigned int DAT_80498010;    // scheduler-suspend flag
extern float        FLOAT_8061b5e0;
extern float        FLOAT_80617940;

// ---------------------------------------------------------------------------
// trk_lock_release (0x8039378c) - release the TRK critical-section lock (no-op on
// this single-core, interrupts-masked build).
unsigned int trk_lock_release(void) { return 0; }

// ---------------------------------------------------------------------------
// trk_lock_acquire (0x80393794) - acquire the TRK critical-section lock (no-op).
unsigned int trk_lock_acquire(void) { return 0; }

// ---------------------------------------------------------------------------
// trk_lock_init (0x8039379c) - initialise a TRK lock object (no-op).
unsigned int trk_lock_init(void) { return 0; }

// ---------------------------------------------------------------------------
// trk_host_notify (0x803937a4) - send a simple host request (opcode select: 0x90 uses
// FUN_8039420c to fill the body, otherwise FUN_80394188), run the transaction, and
// free the buffers. Returns the transaction status.
int trk_host_notify(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                    int opcode, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    int idx, req_idx, reply_idx[2];
    int rc = trk_buffer_alloc(a1, a2, a3, a4, a5, a6, a7, a8, &idx, (unsigned int **)&req_idx,
                              a11, a12, a13, a14, a15, a16);
    if (rc == 0) {
        int body = (opcode == 0x90) ? FUN_8039420c(req_idx) : FUN_80394188(req_idx);
        rc = trk_host_transact(body, a2, a3, a4, a5, a6, a7, a8, req_idx, reply_idx, 2, 3, 1, a14, a15, a16);
        if (rc == 0) {
            trk_buffer_free(reply_idx[0]);
        }
        trk_buffer_free(idx);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_flush_cache_range (0x8039383c) - make a written code/data range coherent with
// the instruction fetcher: for each 8-byte block issue dcbst + dcbf + sync + icbi,
// then a final isync. Used when TRK patches breakpoints into the running image.
void trk_flush_cache_range(unsigned int addr, int len)
{
    unsigned int p = addr & 0xfffffff1;
    int remaining = len + (int)(addr - p);
    do {
        dataCacheBlockStore(p);
        dataCacheBlockFlush(p);
        sync(0);
        instructionCacheBlockInvalidate(p);
        p = p + 8;
        remaining = remaining - 8;
    } while (-1 < remaining);
    instructionSynchronize();
}

// ---------------------------------------------------------------------------
// trk_memset (0x80393874) - fill `n` bytes at `dst` with byte `val`. Aligns to a
// 4-byte boundary, splats the byte into a word and stores 32 bytes per iteration,
// then mops up the remaining words and bytes. Standard MSL-style fast memset.
void trk_memset(int dst, unsigned char val, unsigned int n)
{
    unsigned int word = val;
    unsigned char *p = (unsigned char *)(dst - 1);
    if (0x1f < n) {
        unsigned int lead = ~(unsigned int)(size_t)p & 3;
        if (lead != 0) {
            n = n - lead;
            do { lead = lead - 1; p = p + 1; *p = val; } while (lead != 0);
        }
        if (val != 0) {
            word = word | (word << 8) | ((unsigned int)val << 0x18) | (word << 0x10);
        }
        unsigned int *pw = (unsigned int *)(p - 3);
        for (unsigned int i = n >> 5; i != 0; i = i - 1) {
            pw[1] = word; pw[2] = word; pw[3] = word; pw[4] = word;
            pw[5] = word; pw[6] = word; pw[7] = word;
            pw = pw + 8;
            *pw = word;
        }
        for (unsigned int i = (n >> 2) & 7; i != 0; i = i - 1) {
            pw = pw + 1;
            *pw = word;
        }
        p = (unsigned char *)((int)pw + 3);
        n = n & 3;
    }
    while (n != 0) {
        n = n - 1;
        p = p + 1;
        *p = (unsigned char)word;
    }
}

// ---------------------------------------------------------------------------
// trk_nop_392c (0x8039392c) - empty hook (no-op).
void trk_nop_392c(void) { }

// ---------------------------------------------------------------------------
// trk_nop_3934 (0x80393934) - empty hook (no-op).
void trk_nop_3934(void) { }

// ---------------------------------------------------------------------------
// trk_memcpy_sync (0x8039393c) - copy `n` bytes with a sync barrier around each byte.
// Used for MMIO / uncached-region access where ordering matters.
void trk_memcpy_sync(int dst, int src, int n)
{
    for (int i = 0; i != n; i = i + 1) {
        sync(0);
        sync(0);
        *(unsigned char *)(i + dst) = *(unsigned char *)(i + src);
    }
    sync(0);
}

// ---------------------------------------------------------------------------
// trk_exception_handler (0x80393ba8) - the CPU exception/interrupt entry. Snapshots
// the full register context (r0-r31, LR, CTR, XER, MSR, DAR, DSISR) into the TRK save
// area and points the context descriptors at the FP save blocks. If the "monitor
// active" flag is set this is a nested/debug fault, so it flags re-entry and jumps
// straight into the monitor loop; otherwise it clears the scheduler-suspend flag,
// runs the pre-return fixup, and returns from the interrupt to resume the program.
//
// The raw register reads (in_MSR, in_LR, r14-r31, SRR1, ...) are PPC context the
// decompiler surfaces as pseudo-locals; represented here as opaque captures since a
// portable C body cannot read the live special registers.
unsigned int trk_exception_handler(unsigned int r0, unsigned int r3, unsigned int r4,
                                   unsigned int r5, unsigned int r6, unsigned int r7,
                                   unsigned int r8)
{
    // Context values the decompiler reads from live registers/SPRs.
    unsigned int r1 = 0, r11 = 0, r12 = 0;
    unsigned int r14 = 0, r15 = 0, r16 = 0, r17 = 0, r18 = 0, r19 = 0, r20 = 0, r21 = 0;
    unsigned int r22 = 0, r23 = 0, r24 = 0, r25 = 0, r26 = 0, r27 = 0, r28 = 0, r29 = 0;
    unsigned int r30 = 0, r31 = 0;
    unsigned int in_LR = 0, in_CTR = 0, in_XER = 0, in_MSR = 0, in_DAR = 0, in_DSISR = 0, in_SRR1 = 0;

    DAT_805f13bc = &FLOAT_8061b5e0;
    DAT_805f13c0 = (void *)&DAT_805f13b4;
    DAT_805f13e8 = &FLOAT_80617940;
    unsigned int msr_mask = in_MSR & 0x9000;

    DAT_805f13b4 = r0;
    DAT_805f13b8 = (unsigned char *)(size_t)r1;
    DAT_805f13c4 = r3;  DAT_805f13c8 = r4;  DAT_805f13cc = r5;  DAT_805f13d0 = r6;
    DAT_805f13d4 = r7;  DAT_805f13d8 = r8;  DAT_805f13dc = 0;   DAT_805f13e0 = r11;
    DAT_805f13e4 = r12;
    DAT_805f13ec = r14; DAT_805f13f0 = r15; DAT_805f13f4 = r16; DAT_805f13f8 = r17;
    DAT_805f13fc = r18; DAT_805f1400 = r19; DAT_805f1404 = r20; DAT_805f1408 = r21;
    DAT_805f140c = r22; DAT_805f1410 = r23; DAT_805f1414 = r24; DAT_805f1418 = r25;
    DAT_805f141c = r26; DAT_805f1420 = r27; DAT_805f1424 = r28; DAT_805f1428 = r29;
    DAT_805f142c = r30; DAT_805f1430 = r31;
    DAT_805f1434 = in_LR;   DAT_805f1438 = in_CTR;  DAT_805f143c = in_XER;
    DAT_805f1440 = in_MSR;  DAT_805f1444 = in_DAR;  DAT_805f1448 = in_DSISR;

    if (*DAT_805f1454 != '\0') {
        DAT_805f1450 = (DAT_805f1450 & 0xffffff00u) | 1;   // mark re-entry
        sync(0);
        sync(0);
        return (unsigned int)FUN_80394428();               // enter monitor
    }
    DAT_80498010 = DAT_80498010 & 0xffffff00u;             // clear scheduler-suspend
    FUN_80395698();
    returnFromInterrupt(msr_mask, in_SRR1);
    return DAT_805f1464;
}
