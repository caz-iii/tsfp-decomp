// os_boot.cpp - Dolphin OS apploader / DVD boot bring-up
//
// Address: 0x8039ab24 | Name: os_build_arg_block        | Date: 2026-09-14
// Address: 0x8039acac | Name: os_enable_write_gather_sync | Date: 2026-09-14
// Address: 0x8039ace8 | Name: os_dvd_read_blocking      | Date: 2026-09-14
// Address: 0x8039ad60 | Name: os_copy_boot_info         | Date: 2026-09-14
// Address: 0x8039ada0 | Name: os_get_apploader_end      | Date: 2026-09-14
// Address: 0x8039ae64 | Name: os_boot_from_dvd          | Date: 2026-09-14
// Address: 0x8039b2e8 | Name: os_boot_from_dvd_fmt      | Date: 2026-09-14
//
// This is the OS boot path that reads the apploader off the DVD, runs its
// init/main/close callback trio to stream the game image (DOL) into memory,
// and hands control over. os_build_arg_block packs the command-line argv/args
// area consumed by the loaded program; os_boot_from_dvd is the top-level runner
// (an OSBootFromDVD-style entry), with os_boot_from_dvd_fmt formatting a command
// line first. The read helpers wrap the DVD block-read scheduler.

#include "common.h"

// code: Ghidra's function-body placeholder type (a callable target).
typedef void code;

// The BS2/apploader-boot symbols and raw callee entry points not yet defined
// anywhere else. Cross-module helpers (DVDInit, memset_wrapper, msl_*,
// os_dsp_heap_alloc, os_dsp_heap_init, os_ic_invalidate_range,
// os_enable_write_gather_hid0, leaf_31e8, OSReport, sync,
// instructionSynchronize) resolve through the shared generated headers.
extern "C" {
extern int  DAT_800030f0;   // pointer to the current apploader boot-info block
extern int  DAT_800030f4;   // apploader image base on the DVD
extern char DAT_800030e2;   // "is BS2/second-stage" flag byte
extern int  DAT_8061309c;   // cached apploader end address
extern int  DAT_80613098;   // DVD open-complete flag
extern int  DAT_8061304c;   // "already reset" guard
extern char DAT_806113d8;   // "" arg-format seed string
extern int  _DAT_812fdfec;  // BS2 handoff scratch (region end)
extern int  _DAT_812fdff0;  // BS2 handoff scratch (region base)
extern int  DAT_cc003024;   // PI interrupt-mask register

// DVD block-read scheduler + apploader plumbing (raw entry points).
extern void FUN_803a4918(undefined4 *req, uint dst, int len, int offset, int, int); // begin read
extern int  FUN_803a4d68(int req);   // poll read
extern int  FUN_803a5214(void);      // scheduler tick
extern void FUN_8039dcac(int);       // yield / idle
extern int  FUN_803a520c(void);      // DVD status query
extern void FUN_803a4e60(int);
extern void FUN_803a4e70(void);
extern void FUN_803a530c(void *cb);
extern void FUN_8039d068(int);
extern void FUN_8039d0f0(int);
extern void FUN_8039ccb4(void);
extern void FUN_8039cca0(void);
extern void FUN_803a9c14(int);
extern void FUN_803a9c40(int);
extern void FUN_803a4b98(undefined4 *req, int);
extern void FUN_803a9954(int);
extern int  FUN_8039da94(undefined4 **outArgv, undefined4 *outFmt); // print-console acquire

extern int  LAB_8039ad54;         // DVD open-complete callback stub

// Named cross-module helpers (real signatures per generated_decls.h / permissive
// generated_externs.h). Declared per-file because the os/ tree does not route
// through a module shim header. OSReport is in sdk_stubs.h.
extern int  memset_wrapper(int dst, int val, int len);
extern int  msl_strlen(int s);
extern int  msl_strcpy(int *dst, int *src);
extern int  msl_strncmp(int a, int b, int n);
extern int  msl_vsprintf(int arg0, int arg1, int arg2, int buf, int fmt, ...);
extern int  os_dsp_heap_alloc(int size, int align);
extern void os_dsp_heap_init(u32 base);
extern int  os_ic_invalidate_range(int start, int len);
extern int  os_enable_write_gather_hid0(void);
extern void leaf_31e8(u32 dst, u32 src, s32 len);
extern int  DVDInit(int cb, int argc, int argv, int loadAddr);
extern int  sync(int);
extern int  instructionSynchronize(void);
}

// __OSBuildArgList: pack argv strings + the argument descriptor into a 0x2000
// command-line block. Strings are copied down from the top; the argv pointer
// vector (rebased to block-relative offsets) and the argc word sit below them.
undefined4 os_build_arg_block(int block, uint argc, undefined4 *argv)
{
    undefined4 *pTmp;
    int len;
    undefined4 word;
    undefined4 *pSrc;
    undefined4 *pDst;
    undefined4 *pArgvOut;
    uint total;
    uint remaining;
    int *pStr;
    int *pArg;

    memset_wrapper(block, 0, 0x2000);
    if (argc == 0) {
        *(undefined4 *)(block + 8) = 0;
    } else {
        pStr = (int *)(block + 0x2000);
        pArg = (int *)((int)argv + argc * 4);
        remaining = argc;
        while (true) {
            remaining = remaining - 1;
            pArg = pArg + -1;
            if ((int)remaining < 0)
                break;
            len = msl_strlen(*pArg);
            pStr = (int *)((int)pStr - (len + 1));
            msl_strcpy(pStr, (int *)*pArg);
            *pArg = (int)pStr - block;
        }
        remaining = argc + 1;
        pArgvOut = (undefined4 *)(block + ((int)pStr - block & 0xfffffffcU) + remaining * -4);
        total = 0;
        if (remaining != 0) {
            if ((8 < remaining) && (remaining = argc >> 3, pSrc = argv, pDst = pArgvOut, argc != 7)) {
                do {
                    total = total + 8;
                    *pDst = *pSrc;
                    pDst[1] = pSrc[1];
                    pDst[2] = pSrc[2];
                    pDst[3] = pSrc[3];
                    pDst[4] = pSrc[4];
                    pDst[5] = pSrc[5];
                    pDst[6] = pSrc[6];
                    pTmp = pSrc + 7;
                    pSrc = pSrc + 8;
                    pDst[7] = *pTmp;
                    pDst = pDst + 8;
                    remaining = remaining - 1;
                } while (remaining != 0);
            }
            pSrc = argv + total;
            pDst = pArgvOut + total;
            len = (argc + 1) - total;
            if (total < argc + 1) {
                do {
                    word = *pSrc;
                    pSrc = pSrc + 1;
                    *pDst = word;
                    pDst = pDst + 1;
                    len = len + -1;
                } while (len != 0);
            }
        }
        pArgvOut[-1] = argc;
        *(int *)(block + 8) = (int)pArgvOut + (-4 - block);
    }
    return 1;
}

// OSEnableWriteGatherPipe: set HID0 write-gather-pipe enable then fence.
void os_enable_write_gather_sync(void)
{
    os_enable_write_gather_hid0();
    sync(0);
    instructionSynchronize();
}

// Blocking DVD read of len bytes at offset into dst, spinning the scheduler.
void os_dvd_read_blocking(undefined4 dst, undefined4 len, undefined4 offset)
{
    int status;
    undefined4 req[13];

    FUN_803a4918(req, dst, len, offset, 0, 0);
    while (status = FUN_803a4d68((int)req), status != 0) {
        status = FUN_803a5214();
        if (status == 0) {
            FUN_8039dcac(0);
        }
    }
}

// Copy the 0x1c-byte apploader boot-info block to out (zeroing it when unset).
void os_copy_boot_info(undefined4 *out)
{
    if (DAT_800030f0 < 0x80000000) {
        *out = 0;
    } else {
        leaf_31e8((uint)out, DAT_800030f0, 0x1c);
    }
}

// Resolve and cache the apploader's end address (base + on-disk size), reading
// the apploader header off the DVD the first time.
int os_get_apploader_end(void)
{
    uint hdr;
    int status;
    undefined4 req[15];

    if (DAT_8061309c == 0) {
        if (DAT_800030f4 == 0) {
            DAT_8061309c = 0x2440;
        } else {
            hdr = os_dsp_heap_alloc(0x40, 0x20);
            FUN_803a4918(req, hdr, 0x40, DAT_800030f4, 0, 0);
            while (status = FUN_803a4d68((int)req), status != 0) {
                status = FUN_803a5214();
                if (status == 0) {
                    FUN_8039dcac(0);
                }
            }
            DAT_8061309c = DAT_800030f4 + *(int *)(hdr + 0x38);
        }
    }
    return DAT_8061309c;
}

// OSBootFromDVD: the top-level apploader runner. Allocates the boot-info record,
// optionally builds the arg block, inits DVD, streams the apploader header, then
// either runs the BS2 second-stage path (magic mismatch) or invokes the
// apploader init/main/close callback trio to load the game image.
void os_boot_from_dvd(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4,
                      undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8,
                      int loadAddr, undefined4 param_10, undefined4 *argv, undefined4 param_12,
                      int param_13, uint param_14, undefined4 *param_15, undefined4 param_16)
{
    undefined4 *bootInfo;
    undefined4 *argBlock;
    int status;
    uint argc;
    uint scratch;
    undefined4 *argvLocal;
    undefined4 imgBaseTmp;
    undefined4 *param15Local;
    undefined4 reqApp[12];
    undefined4 reqB[12];
    undefined4 reqA[13];
    // Apploader callback trio, populated by the apploader header entry point.
    void (*cbInit)(void *report);
    int  (*cbMain)(undefined4 *dst, undefined4 *len, undefined4 *offset);
    void (*cbClose)(void);
    undefined4 mainArg0;
    undefined4 mainArg1;
    undefined4 mainArg2[2];
    undefined4 reqRead[13];

    argvLocal = argv;
    imgBaseTmp = param_12;
    status = param_13;
    scratch = param_14;
    param15Local = param_15;

    FUN_8039cca0();
    argc = 1;
    bootInfo = (undefined4 *)os_dsp_heap_alloc(0x1c, 1);
    *bootInfo = 1;
    bootInfo[1] = param_10;
    bootInfo[3] = (undefined4)(uintptr_t)argv;
    bootInfo[4] = param_12;
    bootInfo[5] = param_13;
    argBlock = bootInfo;
    if (param_13 == 0) {
        argc = os_dsp_heap_alloc(0x2000, 1);
        bootInfo[6] = argc;
        argBlock = (undefined4 *)(uintptr_t)os_build_arg_block(bootInfo[6], param_14, param_15);
        argc = param_14;
        argvLocal = param_15;
    }
    (void)imgBaseTmp; (void)status; (void)scratch; (void)param15Local;
    DVDInit((int)(intptr_t)argBlock, (int)argc, (int)(intptr_t)argvLocal, 0);
    FUN_803a4e60(1);
    FUN_803a4e70();
    DAT_80613098 = 0;
    FUN_803a530c(&LAB_8039ad54);
    FUN_8039d068(0xffffffe0);
    FUN_8039d0f0(0x400);
    FUN_8039ccb4();
    while (DAT_80613098 != 1) {
        status = FUN_803a5214();
        if (status == 0) {
            FUN_8039dcac(0);
        }
    }
    if ((DAT_8061304c == 0) && (status = FUN_803a520c(), *(char *)(status + 8) != '\0')) {
        FUN_803a9c14(0);
        FUN_803a9c40(0);
        FUN_803a4b98(reqRead, 0);
        while (status = FUN_803a4d68((int)reqRead), status != 0) {
            status = FUN_803a5214();
            if (status == 0) {
                FUN_8039dcac(0);
            }
        }
        FUN_803a9954(0);
    }

    scratch = os_dsp_heap_alloc(0x20, 0x20);
    status = os_get_apploader_end();
    os_dvd_read_blocking(scratch, 0x20, status);
    status = os_get_apploader_end();
    os_dvd_read_blocking(0x81200000, *(int *)(scratch + 0x14) + 0x1fU & 0xffffffe0, status + 0x20);
    os_ic_invalidate_range(0x81200000, *(int *)(scratch + 0x14) + 0x1fU & 0xffffffe0);

    status = msl_strncmp(scratch, -0x7fb675a8, 10);
    if (status < 1) {
        // Magic match: BS2 second-stage handoff.
        DAT_800030e2 = 1;
        _DAT_812fdfec = param_12;
        _DAT_812fdff0 = (int)(uintptr_t)argv;
        status = os_get_apploader_end();
        FUN_803a4918(reqA, 0x81300000, *(int *)(scratch + 0x18) + 0x1fU & 0xffffffe0,
                     status + *(int *)(scratch + 0x14) + 0x20, 0, 0);
        while (status = FUN_803a4d68((int)reqA), status != 0) {
            status = FUN_803a5214();
            if (status == 0) {
                FUN_8039dcac(0);
            }
        }
        os_ic_invalidate_range(0x81300000, *(int *)(scratch + 0x18) + 0x1fU & 0xffffffe0);
        FUN_8039cca0();
        os_enable_write_gather_hid0();
        os_enable_write_gather_sync();
    } else {
        // Magic mismatch: run the apploader callback trio.
        if (loadAddr == -1) {
            if (DAT_8061309c == 0) {
                if (DAT_800030f4 == 0) {
                    DAT_8061309c = 0x2440;
                } else {
                    scratch = os_dsp_heap_alloc(0x40, 0x20);
                    FUN_803a4918(reqB, scratch, 0x40, DAT_800030f4, 0, 0);
                    while (status = FUN_803a4d68((int)reqB), status != 0) {
                        status = FUN_803a5214();
                        if (status == 0) {
                            FUN_8039dcac(0);
                        }
                    }
                    DAT_8061309c = DAT_800030f4 + *(int *)(scratch + 0x38);
                }
            }
            loadAddr = DAT_8061309c + *(int *)(scratch + 0x14) + 0x20;
        }
        bootInfo[2] = loadAddr;
        (*(void (**)(void *, void *, void *))(uintptr_t)(scratch + 0x10))(&cbInit, &cbMain, &cbClose);
        scratch = os_dsp_heap_alloc(0x1c, 1);
        leaf_31e8(scratch, (int)(uintptr_t)bootInfo, 0x1c);
        DAT_800030f0 = scratch;
        (*cbInit)((void *)OSReport);
        os_dsp_heap_init(scratch);
        while (status = (*cbMain)(&mainArg0, &mainArg1, mainArg2), status != 0) {
            FUN_803a4918(reqApp, mainArg0, mainArg1, mainArg2[0], 0, 0);
            while (status = FUN_803a4d68((int)reqApp), status != 0) {
                status = FUN_803a5214();
                if (status == 0) {
                    FUN_8039dcac(0);
                }
            }
        }
        (*cbClose)();
        scratch = os_dsp_heap_alloc(0x1c, 1);
        leaf_31e8(scratch, (int)(uintptr_t)bootInfo, 0x1c);
        DAT_cc003024 = 7;
        DAT_800030f0 = scratch;
        FUN_8039cca0();
        os_enable_write_gather_sync();
    }
}

// OSBootFromDVD with a printf-formatted command line: format the args into a
// scratch buffer, build an argv vector, then dispatch to os_boot_from_dvd.
void os_boot_from_dvd_fmt(undefined8 param_1, undefined8 param_2, undefined8 param_3,
                          undefined8 param_4, undefined8 param_5, undefined8 param_6,
                          undefined8 param_7, undefined8 param_8, undefined4 param_9,
                          undefined4 param_10, int *argvIn, undefined4 param_12, undefined4 param_13,
                          undefined4 param_14, undefined4 param_15, undefined4 param_16)
{
    int *pTmp;
    int *pSrc;
    int *pDst;
    uint argc;
    uint count;
    uint blocks;
    undefined8 acc;
    undefined4 fmtStr;
    undefined4 *argBlockPtr;
    unsigned char buf[24];

    acc = (undefined8)FUN_8039da94(&argBlockPtr, &fmtStr);
    // NON-MATCHING: the original threads the vararg FP-spill through msl_vsprintf;
    // only the buffer + format-seed + the packed varargs are meaningful here.
    (void)param_3; (void)param_4; (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_13; (void)param_14; (void)param_15;
    acc = (undefined8)msl_vsprintf((int)acc, (int)param_2, (int)&DAT_806113d8, (int)buf,
                                   (int)param_9, param_12, param_16);
    count = 0;
    pSrc = argvIn;
    if (argvIn != (int *)0x0) {
        for (; *pSrc != 0; pSrc = pSrc + 1) {
            count = count + 1;
        }
    }
    pSrc = (int *)os_dsp_heap_alloc((count + 2) * 4, 1);
    *pSrc = (int)buf;
    pDst = argvIn + 1;
    pTmp = pSrc + 1;
    argc = count + 1;
    if (1 < (int)argc) {
        blocks = count >> 3;
        if (blocks != 0) {
            do {
                *pTmp = pDst[-1];
                pTmp[1] = *pDst;
                pTmp[2] = pDst[1];
                pTmp[3] = pDst[2];
                pTmp[4] = pDst[3];
                pTmp[5] = pDst[4];
                pTmp[6] = pDst[5];
                int *pWrap = pDst + 6;
                pDst = pDst + 8;
                pTmp[7] = *pWrap;
                pTmp = pTmp + 8;
                blocks = blocks - 1;
            } while (blocks != 0);
            count = count & 7;
            if (count == 0)
                goto done;
        }
        do {
            int *pWrap = pDst + -1;
            pDst = pDst + 1;
            *pTmp = *pWrap;
            pTmp = pTmp + 1;
            count = count - 1;
        } while (count != 0);
    }
done:
    os_boot_from_dvd(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8, -1, param_10,
                     (undefined4 *)(uintptr_t)(int)argBlockPtr, fmtStr, 0, argc,
                     (undefined4 *)pSrc, param_16);
}

// os_run_from_dvd 0x8039da18 - reset into a fresh OS context and boot the DVD
// image with a formatted command line. Sets up a stack-local thread context,
// makes it current, then dispatches to os_boot_from_dvd_fmt with the force-boot
// flag (0x80000000) set on the arg word.
//
// NON-MATCHING: os_clear_context / os_set_current_context resolve to their real
// one-argument prototypes; the vararg FP-spill is dropped.
extern "C" {
extern int  os_disable_interrupts(void);
extern void os_clear_context(u32 ctx);
extern void os_set_current_context(u32 ctx);
extern void os_set_arena_hi(unsigned int hi);
}

void os_run_from_dvd(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4,
                     undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8,
                     uint param_9, undefined4 param_10, undefined4 param_11, undefined4 param_12,
                     undefined4 param_13, undefined4 param_14, undefined4 param_15,
                     undefined4 param_16)
{
    int argv0[2];
    unsigned char ctxBlock[712];

    os_disable_interrupts();
    os_dsp_heap_init(0x81280000);
    os_set_arena_hi(0x812f0000);
    os_clear_context((u32)(uintptr_t)ctxBlock);
    os_set_current_context((u32)(uintptr_t)ctxBlock);
    argv0[0] = 0;
    os_boot_from_dvd_fmt(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8, param_10,
                         param_9 | 0x80000000, argv0, param_12, param_13, param_14, param_15,
                         param_16);
}
