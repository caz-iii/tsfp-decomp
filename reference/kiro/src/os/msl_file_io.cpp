// Address: 0x803869ec | Name: msl_file_flush_buffer  | Date: 2026-09-13
// Address: 0x80386ab0 | Name: msl_file_reset_buffer  | Date: 2026-09-13
// Address: 0x80386ae4 | Name: msl_fwrite_string      | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) buffered FILE support - the low-level write
// path shared by printf/puts/fwrite on GameCube.
//
// The FILE object is the MSL __FILE layout accessed here purely by word offset
// (the decompiler surfaced it as an int[] / undefined4[]). The fields touched:
//   [6]  = position accumulator (bytes committed)
//   [7]  = buffer base pointer
//   [8]  = buffer end / capacity marker
//   [9]  = current write pointer
//   [10] = transient byte-count / scratch
//   [0xb]= buffer-alignment mask
//   [0xd]= last-committed position
//   [0x10]= write callback (device sink)
//   [0x12]= callback user handle
// These offsets are preserved verbatim so the recompile drives the same struct.

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern int  FUN_80386ddc(void);                 // MSL stdio lock (returns handle)
extern void FUN_80386dd8(void);                 // MSL stdio unlock
extern int  FUN_8038cfb0(int stream, int mode); // buffer-state probe (<0 => room)
extern uint FUN_80386c20(int ch, u32 *stream);  // slow-path single-char emit

extern int  DAT_80497660;   // remaining space in the fast-path write buffer
extern u8  *PTR_DAT_8049765c; // fast-path write cursor
extern u32  DAT_80497638;    // default stdout FILE object

// ---------------------------------------------------------------------------
// msl_file_flush_buffer (0x803869ec) - commit the pending buffer contents to
// the device via the FILE's write callback, then rewind the write pointer to
// the buffer base. Returns the callback's error code (0 on success).
int msl_file_flush_buffer(u32 *file, u32 *bytesOut)
{
    int rc;

    if (file[9] - file[7] != 0) {
        file[10] = file[9] - file[7];
        rc = (*(int (*)(u32, u32, u32 *, u32))file[0x10])(file[0], file[7], file + 10, file[0x12]);
        if (bytesOut != (u32 *)0) {
            *bytesOut = file[10];
        }
        if (rc != 0) {
            return rc;
        }
        file[6] = file[6] + file[10];
    }
    file[9] = file[7];
    file[10] = file[8];
    file[10] = file[10] - (file[6] & file[0xb]);
    file[0xd] = file[6];
    return 0;
}

// ---------------------------------------------------------------------------
// msl_file_reset_buffer (0x80386ab0) - re-seat the write pointer/limit from the
// buffer base without flushing (used after a direct/unbuffered write path).
void msl_file_reset_buffer(int file)
{
    *(u32 *)(file + 0x24) = *(u32 *)(file + 0x1c);
    *(u32 *)(file + 0x28) = *(u32 *)(file + 0x20);
    *(u32 *)(file + 0x28) =
        *(u32 *)(file + 0x28) - (*(u32 *)(file + 0x18) & *(u32 *)(file + 0x2c));
    *(u32 *)(file + 0x34) = *(u32 *)(file + 0x18);
}

// ---------------------------------------------------------------------------
// msl_fwrite_string (0x80386ae4) - write a NUL-terminated string to stdout under
// the stdio lock, appending a trailing newline. Each byte goes through the
// fast-path (append to the buffer while space remains) or the slow-path
// (FUN_80386c20) when the buffer is full. Returns the last byte written or
// 0xffffffff on error.
uint msl_fwrite_string(u8 *str)
{
    u8   ch;
    u8  *cursor;
    uint result;
    int  room;
    uint status;

    status = 0;
    FUN_80386ddc();
    do {
        ch = *str;
        str = str + 1;
        if ((int)(char)ch == 0) goto done;
        room = FUN_8038cfb0((int)&DAT_80497638, -1);
        if (room < 0) {
            if (DAT_80497660 == 0) {
                DAT_80497660 = DAT_80497660 + -1;
                result = FUN_80386c20((int)(char)ch, &DAT_80497638);
            }
            else {
                result = (uint)ch;
                cursor = PTR_DAT_8049765c + 1;
                DAT_80497660 = DAT_80497660 + -1;
                *PTR_DAT_8049765c = ch;
                PTR_DAT_8049765c = cursor;
            }
        }
        else {
            result = 0xffffffff;
        }
    } while (result != 0xffffffff);
    status = 0xffffffff;
done:
    if (status != 0xffffffff) {
        room = FUN_8038cfb0((int)&DAT_80497638, -1);
        if (room < 0) {
            if (DAT_80497660 == 0) {
                DAT_80497660 = DAT_80497660 + -1;
                status = FUN_80386c20(10, &DAT_80497638);
            }
            else {
                status = 10;
                cursor = PTR_DAT_8049765c + 1;
                DAT_80497660 = DAT_80497660 + -1;
                *PTR_DAT_8049765c = 10;
                PTR_DAT_8049765c = cursor;
            }
        }
        else {
            status = 0xffffffff;
        }
    }
    FUN_80386dd8();
    return status;
}


// ---------------------------------------------------------------------------
// Address: 0x8038cfb0 | Name: msl_file_orient_buffer | Date: 2026-09-13
//
// FILE buffer-state probe / write-orient. Reads the mode bits at byte offset 5 of
// the FILE object (+4 is the 16-bit flags word). If the stream is unbuffered or
// already error/eof it short-circuits; otherwise it commits the stream to the
// requested read (param_2<0) or write (param_2>0) orientation by rewriting the
// mode nibble, returning the orientation just applied.
//   returns  0 : not a valid/oriented stream
//           -1 : locked to the opposite orientation
//            1 : already oriented
//         param_1 : device-managed (mode nibble 3+)
//         param_2 : freshly oriented this call
int FUN_8038cfb0(int param_1, int param_2)
{
    unsigned char bVar1;
    unsigned char bVar2;

    if ((param_1 == 0) || ((*(unsigned short *)(param_1 + 4) >> 6 & 7) == 0)) {
        return 0;
    }
    bVar1 = *(unsigned char *)(param_1 + 5);
    bVar2 = bVar1 >> 4 & 3;
    if (bVar2 == 1) {
        return -1;
    }
    if (bVar2 != 0) {
        if (bVar2 < 3) {
            return 1;
        }
        return param_1;
    }
    if (param_2 < 1) {
        if (param_2 < 0) {
            *(unsigned char *)(param_1 + 5) = bVar1 & 0xcf | 0x10;
        }
    } else {
        *(unsigned char *)(param_1 + 5) = bVar1 & 0xcf | 0x20;
    }
    return param_2;
}

// ---------------------------------------------------------------------------
// Address: 0x8038d040 | Name: msl_write_console | Date: 2026-09-13
//
// Buffered write to the debug/console device. Checks the current MSR/exception
// state (FUN_80397da0 & 0x20000000 == running with interrupts): when safe it lazily
// initialises the console channel (FUN_803ba8c0) once, then hands the bytes to the
// device writer (FUN_803ba930). On success it also mirrors the bytes through the
// normal buffered sink (FUN_8039648c). Returns 1 on device error, 0 on success.
extern unsigned int FUN_80397da0(void);                       // read MSR / status
extern int          FUN_803ba8c0(void);                       // init console channel
extern int          FUN_803ba930(unsigned char *buf, unsigned int n); // device write
extern void         FUN_8039648c(unsigned int handle, unsigned char *buf, unsigned int *pn);
extern int          DAT_80612ff0;                             // console-init-done flag

unsigned int FUN_8038d040(unsigned int param_1, unsigned char *param_2, unsigned int *param_3)
{
    unsigned int uVar1;
    int iVar2;

    uVar1 = FUN_80397da0();
    if ((uVar1 & 0x20000000) == 0) {
        iVar2 = 0;
        if ((DAT_80612ff0 == 0) && (iVar2 = FUN_803ba8c0(), iVar2 == 0)) {
            DAT_80612ff0 = 1;
        }
        if (iVar2 != 0) {
            return 1;
        }
        iVar2 = FUN_803ba930(param_2, *param_3);
        if (iVar2 != 0) {
            *param_3 = 0;
            return 1;
        }
    }
    FUN_8039648c(param_1, param_2, param_3);
    return 0;
}
