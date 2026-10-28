// Address: 0x80386c20 | Name: msl_flush_and_putc  | Date: 2026-09-13
// Address: 0x80386dd8 | Name: msl_stdio_unlock    | Date: 2026-09-13
// Address: 0x80386ddc | Name: msl_stdio_lock      | Date: 2026-09-13
// Address: 0x80386de0 | Name: msl_stdio_init_once | Date: 2026-09-13
// Address: 0x80386de4 | Name: msl_tolower_map     | Date: 2026-09-13
// Address: 0x80386e08 | Name: msl_fwrite_impl     | Date: 2026-09-13
// Address: 0x80387114 | Name: msl_fwrite          | Date: 2026-09-13
// Address: 0x80387190 | Name: msl_fflush          | Date: 2026-09-13
// Address: 0x803872c8 | Name: msl_fclose          | Date: 2026-09-13
// Address: 0x80387484 | Name: msl_freposition     | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) stdio internals - the buffered output core
// that sits above the FILE buffer helpers in msl_file_io.cpp.
//
// FILE object layout (accessed by word offset, same as msl_file_io.cpp):
//   +4  (byte at +5) = open-mode / orientation flag bits
//   +4  (u16 at +1w) = state field (bits 6..8 = validity class 1..2)
//   +8  (byte at +2w)= buffer-state (bits 5..7: 0 idle, 1 writing, 2 reading)
//   +10 (byte)       = error/eof sticky flag ; (word [10]) = transient count
//   [6]  position accumulator     [7] buffer base   [8] capacity
//   [9]  write cursor             [0x10] write cb    [0x11] close cb
//   [0x12] cb handle
// These offsets and the bit tests are preserved verbatim from the original so
// the recompile drives the same FILE struct the game's runtime built.
//
// The stdio lock/unlock/init entries are empty in this single-threaded build -
// documented no-ops that keep the call sites intact.

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern int  FUN_8038cfb0(int stream, int mode);           // buffer-state probe
extern int  FUN_80387484(u32 *file, uint arg, int mode);  // reposition (locked wrapper)
extern void FUN_80387fa8(void);                           // orientation-2 fixup
extern int  FUN_803874f0(u32 *file, uint arg, int mode);  // reposition core (seek)
extern int  FUN_80387760(int file);                       // read-side ftell adjust
extern char *FUN_80387bb4(uint base, char ch, int len);   // memchr for newline
extern void leaf_31e8(u32 dst, u32 src, s32 len);         // memcpy leaf (see generated_decls)

extern u8   DAT_80497990[];   // tolower-style character map

// Named cross-module helpers defined in msl_file_io.cpp / msl_alloc.cpp. Declared
// per-file (matching their definitions) to avoid the generated_externs vs
// generated_decls collision on this TU's own newly-defined stdio lock symbols.
extern int  msl_file_flush_buffer(u32 *file, u32 *bytesOut);
extern void msl_file_reset_buffer(int file);
extern u32  msl_heap_check_all(void);
extern void msl_free_locked(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                            int *block);

// ---------------------------------------------------------------------------
// msl_stdio_lock / msl_stdio_unlock / msl_stdio_init_once (0x80386ddc/dd8/de0)
// Single-threaded build: the stdio critical section is a no-op. Kept as
// documented stubs so every locked wrapper still links.
void msl_stdio_unlock(void)    { /* leave stdio critical section (no-op) */ }
void msl_stdio_lock(void)      { /* enter stdio critical section (no-op) */ }
void msl_stdio_init_once(void) { /* one-time stdio init (no-op) */ }

// ---------------------------------------------------------------------------
// msl_tolower_map (0x80386de4) - map a character through the runtime lower-case
// table; passes 0xffffffff (EOF) through unchanged.
uint msl_tolower_map(uint c)
{
    if (c == 0xffffffff) {
        return 0xffffffff;
    }
    return (uint)DAT_80497990[c & 0xff];
}

// ---------------------------------------------------------------------------
// msl_flush_and_putc (0x80386c20) - append one byte to a FILE's write buffer,
// switching the buffer into write mode (flushing any pending read state) first
// and flushing to the device when the buffer fills or on a newline in
// line-buffered mode. Returns the byte written, or 0xffffffff on error.
uint msl_flush_and_putc(uint ch, u32 *file)
{
    ushort validity;
    byte   mode;
    uint   result;
    int    rc;
    u8    *cursor;

    file[10] = 0;
    validity = *(ushort *)((int)file + 4) >> 6 & 7;
    if ((*(char *)((int)file + 10) == '\0') && (validity != 0)) {
        if (validity == 2) {
            FUN_80387fa8();
        }
        if ((*(byte *)((int)file + 8) >> 5 == 0) && ((*(byte *)((int)file + 4) >> 3 & 2) != 0)) {
            if (((*(byte *)((int)file + 4) >> 3 & 4) != 0) &&
                (rc = FUN_80387484(file, 0, 2), rc != 0)) {
                return 0;
            }
            *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f | 0x20;
            msl_file_reset_buffer((int)file);
        }
        if (*(byte *)((int)file + 8) >> 5 == 1) {
            if ((((*(byte *)((int)file + 4) >> 1 & 3) == 2) ||
                 (file[8] == file[9] - file[7])) &&
                (rc = msl_file_flush_buffer(file, (u32 *)0), rc != 0)) {
                *(u8 *)((int)file + 10) = 1;
                result = 0xffffffff;
                file[10] = 0;
            }
            else {
                file[10] = file[10] + -1;
                cursor = (u8 *)file[9];
                file[9] = (u32)(cursor + 1);
                *cursor = (char)ch;
                mode = *(byte *)((int)file + 4) >> 1 & 3;
                if (mode != 2) {
                    if (((mode == 0) || (ch == 10)) &&
                        (rc = msl_file_flush_buffer(file, (u32 *)0), rc != 0)) {
                        *(u8 *)((int)file + 10) = 1;
                        file[10] = 0;
                        return 0xffffffff;
                    }
                    file[10] = 0;
                }
                result = ch & 0xff;
            }
        }
        else {
            *(u8 *)((int)file + 10) = 1;
            result = 0xffffffff;
            file[10] = 0;
        }
    }
    else {
        result = 0xffffffff;
    }
    return result;
}

// ---------------------------------------------------------------------------
// msl_fwrite_impl (0x80386e08) - the buffered fwrite body. Computes the total
// byte count, switches the FILE into write mode, and copies the payload through
// the buffer in chunks (honouring line-buffering by flushing on newline). When
// the buffer is unbuffered it swaps the payload in as the buffer and flushes
// directly. Returns the number of whole elements written.
uint msl_fwrite_impl(uint src, uint elemSize, int count, u32 *file)
{
    bool   canBuffer;
    bool   direct;
    ushort validity;
    int    rc;
    int    rc2;
    char  *found;
    u32    savedBase;
    char  *remaining;
    u32    savedCap;
    int    written;
    char  *chunk[3];

    rc = FUN_8038cfb0((int)file, 0);
    if (rc == 0) {
        FUN_8038cfb0((int)file, -1);
    }
    remaining = (char *)(elemSize * count);
    if (((remaining == (char *)0) || (*(char *)((int)file + 10) != '\0')) ||
        (validity = *(ushort *)((int)file + 4) >> 6 & 7, validity == 0)) {
        return 0;
    }
    if (validity == 2) {
        FUN_80387fa8();
    }
    canBuffer = true;
    direct = false;
    if (((*(byte *)((int)file + 5) >> 3 & 1) == 0) ||
        ((*(byte *)((int)file + 4) >> 1 & 3) == 2)) {
        direct = true;
    }
    if ((!direct) && ((*(byte *)((int)file + 4) >> 1 & 3) != 1)) {
        canBuffer = false;
    }
    if ((*(byte *)((int)file + 8) >> 5 == 0) && ((*(byte *)((int)file + 4) >> 3 & 2) != 0)) {
        if (((*(byte *)((int)file + 4) >> 3 & 4) != 0) &&
            (rc = FUN_80387484(file, 0, 2), rc != 0)) {
            return 0;
        }
        *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f | 0x20;
        msl_file_reset_buffer((int)file);
    }
    if (*(byte *)((int)file + 8) >> 5 != 1) {
        *(u8 *)((int)file + 10) = 1;
        file[10] = 0;
        return 0;
    }
    written = 0;
    if (remaining != (char *)0) {
        if ((file[9] != file[7]) || (canBuffer)) {
            file[10] = file[8] - (file[9] - file[7]);
            while (true) {
                found = (char *)0;
                chunk[0] = (char *)file[10];
                if (remaining < (char *)file[10]) {
                    chunk[0] = remaining;
                }
                if ((((*(byte *)((int)file + 4) >> 1 & 3) == 1) && (chunk[0] != (char *)0)) &&
                    (found = FUN_80387bb4(src, '\n', (int)chunk[0]), found != (char *)0)) {
                    chunk[0] = found + (1 - src);
                }
                if (chunk[0] != (char *)0) {
                    leaf_31e8(file[9], src, (int)chunk[0]);
                    src = src + (int)chunk[0];
                    written = written + (int)chunk[0];
                    remaining = remaining + -(int)chunk[0];
                    file[9] = (u32)(chunk[0] + file[9]);
                    file[10] = file[10] - (int)chunk[0];
                }
                if ((((file[10] == 0) || (found != (char *)0)) ||
                     ((*(byte *)((int)file + 4) >> 1 & 3) == 0)) &&
                    (rc2 = msl_file_flush_buffer(file, (u32 *)0), rc2 != 0)) break;
                if ((remaining == (char *)0) || (!canBuffer)) goto tailDirect;
            }
            *(u8 *)((int)file + 10) = 1;
            remaining = (char *)0;
            file[10] = 0;
        }
    }
tailDirect:
    if ((remaining != (char *)0) && (!canBuffer)) {
        savedBase = file[7];
        savedCap = file[8];
        file[7] = src;
        file[8] = (u32)remaining;
        file[9] = src + (int)remaining;
        rc2 = msl_file_flush_buffer(file, (u32 *)chunk);
        if (rc2 != 0) {
            *(u8 *)((int)file + 10) = 1;
            file[10] = 0;
        }
        file[7] = savedBase;
        written = written + (int)chunk[0];
        file[8] = savedCap;
        msl_file_reset_buffer((int)file);
        file[10] = 0;
    }
    if ((*(byte *)((int)file + 4) >> 1 & 3) != 2) {
        file[10] = 0;
    }
    return (written + (elemSize - 1)) / elemSize;
}

// ---------------------------------------------------------------------------
// msl_fwrite (0x80387114) - public fwrite: take the stdio lock, run the buffered
// write body, release the lock.
uint msl_fwrite(uint src, uint elemSize, int count, u32 *file)
{
    uint n;

    msl_stdio_lock();
    n = msl_fwrite_impl(src, elemSize, count, file);
    msl_stdio_unlock();
    return n;
}

// ---------------------------------------------------------------------------
// msl_fflush (0x80387190) - flush one FILE (or, when NULL, run the whole-heap
// pool check). Drains a pending write buffer to the device and, for a read
// stream, re-syncs the position via FUN_80387760. Returns 0 on success,
// 0xffffffff on error.
u32 msl_fflush(u32 *file)
{
    u32 result;
    int rc;
    int pos;

    if (file == (u32 *)0) {
        result = msl_heap_check_all();
    }
    else if ((*(char *)((int)file + 10) == '\0') &&
             ((*(ushort *)((int)file + 4) >> 6 & 7) != 0)) {
        if ((*(byte *)((int)file + 4) >> 3 & 7) == 1) {
            result = 0;
        }
        else {
            if (2 < *(byte *)((int)file + 8) >> 5) {
                *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f | 0x40;
            }
            if (*(byte *)((int)file + 8) >> 5 == 2) {
                file[10] = 0;
            }
            if (*(byte *)((int)file + 8) >> 5 == 1) {
                if ((*(ushort *)((int)file + 4) >> 6 & 7) == 1) {
                    pos = FUN_80387760((int)file);
                }
                else {
                    pos = 0;
                }
                rc = msl_file_flush_buffer(file, (u32 *)0);
                if (rc == 0) {
                    result = 0;
                    *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f;
                    file[6] = pos;
                    file[10] = 0;
                }
                else {
                    *(u8 *)((int)file + 10) = 1;
                    result = 0xffffffff;
                    file[10] = 0;
                }
            }
            else {
                result = 0;
                *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f;
            }
        }
    }
    else {
        result = 0xffffffff;
    }
    return result;
}

// ---------------------------------------------------------------------------
// msl_fclose (0x803872c8) - flush the stream (inlined fflush body), invoke the
// device close callback, clear the FILE state, and free the buffer if the FILE
// owns it. Returns 0 on success, -1 on error. The vararg prefix (param_1..8) is
// the register-window shuffle the free callback expects.
int msl_fclose(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8, u32 *file)
{
    ushort validity;
    int    ret;
    int    flushRc;
    uint   err;
    int    pos;

    (void)a1;
    if (file == (u32 *)0) {
        ret = -1;
    }
    else {
        validity = *(ushort *)((int)file + 4) >> 6 & 7;
        if (validity == 0) {
            ret = 0;
        }
        else {
            // inlined fflush body
            if ((*(char *)((int)file + 10) == '\0') && (validity != 0)) {
                if ((*(byte *)((int)file + 4) >> 3 & 7) == 1) {
                    flushRc = 0;
                }
                else {
                    if (2 < *(byte *)((int)file + 8) >> 5) {
                        *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f | 0x40;
                    }
                    if (*(byte *)((int)file + 8) >> 5 == 2) {
                        file[10] = 0;
                    }
                    if (*(byte *)((int)file + 8) >> 5 == 1) {
                        if ((*(ushort *)((int)file + 4) >> 6 & 7) == 1) {
                            pos = FUN_80387760((int)file);
                        }
                        else {
                            pos = 0;
                        }
                        flushRc = msl_file_flush_buffer(file, (u32 *)0);
                        if (flushRc == 0) {
                            flushRc = 0;
                            *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f;
                            file[6] = pos;
                            file[10] = 0;
                        }
                        else {
                            *(u8 *)((int)file + 10) = 1;
                            flushRc = -1;
                            file[10] = 0;
                        }
                    }
                    else {
                        flushRc = 0;
                        *(byte *)((int)file + 8) = *(byte *)((int)file + 8) & 0x1f;
                    }
                }
            }
            else {
                flushRc = -1;
            }
            ret = (*(int (*)(u32))file[0x11])(file[0]);   // device close callback
            *(ushort *)((int)file + 4) = *(ushort *)((int)file + 4) & 0xfe3f;
            file[0] = 0;
            if ((*(byte *)((int)file + 8) >> 4 & 1) != 0) {
                msl_free_locked(a1, a2, a3, a4, a5, a6, a7, a8, (int *)file[7]);
            }
            err = ((flushRc != 0) || (ret != 0)) ? 1u : 0u;
            ret = (int)(-err | err) >> 0x1f;
        }
    }
    return ret;
}

// ---------------------------------------------------------------------------
// msl_freposition (0x80387484) - public seek/reposition: take the stdio lock,
// run the reposition core (FUN_803874f0), release the lock.
u32 msl_freposition(u32 *file, uint arg, int mode)
{
    u32 result;

    msl_stdio_lock();
    result = (u32)FUN_803874f0(file, arg, mode);
    msl_stdio_unlock();
    return result;
}
