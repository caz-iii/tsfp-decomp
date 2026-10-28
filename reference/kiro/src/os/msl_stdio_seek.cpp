// Address: 0x803874f0 | Name: msl_reposition_core | Date: 2026-09-13
// Address: 0x80387760 | Name: msl_ftell_core      | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) stdio seek/tell cores. These sit below the
// locked public wrappers in msl_stdio.cpp:
//   msl_freposition (0x80387484) -> msl_reposition_core (0x803874f0)
//   ftell public wrapper         -> msl_ftell_core       (0x80387760)
//
// FILE object layout matches msl_stdio.cpp / msl_file_io.cpp (word offsets):
//   (u16 at +1w) state field (bits 6..8 = validity class 1..2)
//   (byte at +2w) buffer-state (bits 5..7: 0 idle, 1 writing, 2 reading)
//   +10 (byte)  sticky error/eof ; (word [10]) transient remaining count
//   [6] position accumulator  [7] buffer base  [8] capacity  [9] cursor
//   [0xd] committed file position  [0xe] read/write device callback  [0x12] cb handle
// Offsets and bit tests are preserved verbatim from the original.
//
// The C functions keep their raw FUN_ symbol names so the existing per-file
// externs in msl_stdio.cpp continue to resolve at link (static library target).

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern int  msl_file_flush_buffer(u32 *file, u32 *bytesOut);  // named helper (msl_file_io.cpp)
extern int  DAT_80612fe8;                                     // MSL errno
extern void msl_stdio_lock(void);                             // stdio critical section (msl_stdio.cpp)
extern void msl_stdio_unlock(void);

// ---------------------------------------------------------------------------
// msl_reposition_core (0x803874f0) - the seek core. Validates the stream, flushes
// any pending write buffer, and (for mode 1 = SEEK_CUR) folds the in-buffer
// offset - counting embedded newlines for text streams - into the target
// position before committing it to the FILE state. mode 2 = SEEK_END simply
// resets the buffer state. Returns 0 on success, 0xffffffff on error.
// (defined under its raw FUN_ symbol so the extern in msl_stdio.cpp resolves)
undefined4 FUN_803874f0(u32 *file, uint offset, int mode)
{
    char   c;
    uint   bufState;
    ushort validity;
    undefined4 result;
    int    flushRc;
    int    delta;
    char  *cursor;
    uint   target[4];

    if (((*(ushort *)(file + 1) >> 6 & 7) == 1) && (*(char *)((int)file + 10) == '\0')) {
        target[0] = offset;
        if ((*(byte *)(file + 2) >> 5 == 1) &&
            (flushRc = msl_file_flush_buffer(file, (u32 *)0x0), flushRc != 0)) {
            *(u8 *)((int)file + 10) = 1;
            result = 0xffffffff;
            file[10] = 0;
            DAT_80612fe8 = 0x28;
        }
        else {
            if (mode == 1) {
                mode = 0;
                delta = 0;
                validity = *(ushort *)(file + 1) >> 6 & 7;
                int adjust;
                if (((validity == 1) || (validity == 2)) && (*(char *)((int)file + 10) == '\0')) {
                    bufState = (uint)(*(byte *)(file + 2) >> 5);
                    if (bufState == 0) {
                        adjust = file[6];
                    }
                    else {
                        cursor = (char *)file[7];
                        adjust = file[0xd] + (file[9] - (int)cursor);
                        if (2 < bufState) {
                            delta = bufState - 2;
                            adjust = adjust - delta;
                        }
                        if ((*(byte *)((int)file + 5) >> 3 & 1) == 0) {
                            for (delta = (file[9] - (int)cursor) - delta; delta != 0; delta = delta + -1) {
                                c = *cursor;
                                cursor = cursor + 1;
                                if (c == '\n') {
                                    adjust = adjust + 1;
                                }
                            }
                        }
                    }
                }
                else {
                    adjust = -1;
                    DAT_80612fe8 = 0x28;
                }
                target[0] = target[0] + adjust;
            }
            if (((mode == 2) || ((*(byte *)(file + 1) >> 3 & 7) == 3)) ||
                ((*(byte *)(file + 2) >> 5 != 2) && (*(byte *)(file + 2) >> 5 != 3))) {
                *(byte *)(file + 2) = *(byte *)(file + 2) & 0x1f;
            }
            else if ((target[0] < (uint)file[6]) && ((uint)file[0xd] <= target[0])) {
                file[9] = file[7] + (target[0] - file[0xd]);
                file[10] = file[6] - target[0];
                *(byte *)(file + 2) = *(byte *)(file + 2) & 0x1f | 0x40;
            }
            else {
                *(byte *)(file + 2) = *(byte *)(file + 2) & 0x1f;
            }
            if (*(byte *)(file + 2) >> 5 == 0) {
                if (((void *)file[0xe] != (void *)0x0) &&
                    (flushRc = (*(int (*)(u32, uint *, int, u32))file[0xe])
                                   (*file, target, mode, file[0x12]), flushRc != 0)) {
                    *(u8 *)((int)file + 10) = 1;
                    file[10] = 0;
                    DAT_80612fe8 = 0x28;
                    return 0xffffffff;
                }
                *(u8 *)((int)file + 9) = 0;
                file[6] = target[0];
                file[10] = 0;
            }
            result = 0;
        }
    }
    else {
        result = 0xffffffff;
        DAT_80612fe8 = 0x28;
    }
    return result;
}

// ---------------------------------------------------------------------------
// msl_ftell_core (0x80387760) - read-side position adjust used by fflush/ftell.
// Takes the stdio lock and returns the current logical file position, folding in
// the in-buffer cursor offset and counting embedded newlines for text streams.
// Returns -1 on an invalid stream.
// (defined under its raw FUN_ symbol so the extern in msl_stdio.cpp resolves)
int FUN_80387760(int file)
{
    char  c;
    uint  bufState;
    ushort validity;
    int   delta;
    int   inBuf;
    char *cursor;
    int   pos;

    msl_stdio_lock();
    delta = 0;
    validity = *(ushort *)(file + 4) >> 6 & 7;
    if (((validity == 1) || (validity == 2)) && (*(char *)(file + 10) == '\0')) {
        bufState = (uint)(*(byte *)(file + 8) >> 5);
        if (bufState == 0) {
            pos = *(int *)(file + 0x18);
        }
        else {
            cursor = *(char **)(file + 0x1c);
            inBuf = *(int *)(file + 0x24) - (int)cursor;
            pos = *(int *)(file + 0x34) + inBuf;
            if (2 < bufState) {
                delta = bufState - 2;
                pos = pos - delta;
            }
            if ((*(byte *)(file + 5) >> 3 & 1) == 0) {
                for (inBuf = inBuf - delta; inBuf != 0; inBuf = inBuf + -1) {
                    c = *cursor;
                    cursor = cursor + 1;
                    if (c == '\n') {
                        pos = pos + 1;
                    }
                }
            }
        }
    }
    else {
        pos = -1;
        DAT_80612fe8 = 0x28;
    }
    msl_stdio_unlock();
    return pos;
}
