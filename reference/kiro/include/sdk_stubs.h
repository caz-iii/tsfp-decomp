#ifndef TSFP_SDK_STUBS_H
#define TSFP_SDK_STUBS_H

// Stub declarations for GameCube SDK functions
// Stub — real implementation in tsfp-recomp
//
// These are extern declarations that allow decompiled code to compile
// without the actual SDK implementation. The tsfp-recomp project provides
// the PC-native replacements for these functions.

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// === GX (Graphics) ===
// Stub — real implementation in tsfp-recomp
extern void GXInit(void* base, u32 size);
extern void GXSetViewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);
extern void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht);
extern void GXBegin(u32 type, u32 vtxfmt, u16 nverts);
extern void GXEnd(void);
extern void GXSetVtxDesc(u32 attr, u32 type);
extern void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u32 frac);
extern void GXLoadPosMtxImm(f32 mtx[3][4], u32 id);
extern void GXSetCurrentMtx(u32 id);
extern void GXSetNumChans(u8 nChans);
extern void GXSetChanCtrl(u32 chan, BOOL enable, u32 ambSrc, u32 matSrc, u32 lightMask, u32 diffFn, u32 attnFn);
extern void GXSetTevOrder(u32 tevStage, u32 texCoord, u32 texMap, u32 color);
extern void GXSetTevOp(u32 tevStage, u32 mode);
extern void GXSetBlendMode(u32 type, u32 srcFactor, u32 dstFactor, u32 op);
extern void GXSetZMode(BOOL compare, u32 func, BOOL updateEnable);
extern void GXCopyDisp(void* dest, BOOL clear);
extern void GXSetCopyClear(u32 clearColor, u32 clearZ);
extern void GXDrawDone(void);

// === OS (Operating System) ===
// Stub — real implementation in tsfp-recomp
extern BOOL OSCreateThread(OSThread thread, void* func, void* param, void* stack, u32 stackSize, s32 priority, u16 attr);
extern void OSCreateAlarm(OSAlarm alarm);
extern void OSReport(const char* fmt, ...);
extern void DCFlushRange(void* addr, u32 nBytes);
extern void ICInvalidateRange(void* addr, u32 nBytes);
extern void OSInitMutex(OSMutex mutex);
extern void OSLockMutex(OSMutex mutex);
extern void OSUnlockMutex(OSMutex mutex);
extern u32 OSGetTick(void);
extern void OSYieldThread(void);

// === DVD (Disc) ===
// Stub — real implementation in tsfp-recomp
extern BOOL DVDOpen(const char* fileName, DVDFileInfo fileInfo);
extern BOOL DVDReadAsync(DVDFileInfo fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback);
extern BOOL DVDClose(DVDFileInfo fileInfo);
extern s32 DVDReadPrio(DVDFileInfo fileInfo, void* addr, s32 length, s32 offset, s32 prio);

// === MTX (Matrix) ===
// Stub — real implementation in tsfp-recomp
extern void MTXIdentity(f32 mtx[3][4]);
extern void MTXMultiply(f32 a[3][4], f32 b[3][4], f32 ab[3][4]);
extern void MTXTranspose(f32 src[3][4], f32 dst[4][3]);
extern void MTXScale(f32 mtx[3][4], f32 xS, f32 yS, f32 zS);
extern void MTXTrans(f32 mtx[3][4], f32 xT, f32 yT, f32 zT);

// === PAD (Controller) ===
// Stub — real implementation in tsfp-recomp
extern void PADInit(void);
extern u32 PADRead(unsigned short* status);

// === VI (Video Interface) ===
// Stub — real implementation in tsfp-recomp
extern void VIInit(void);
extern void VIConfigure(GXRenderModeObj rmode);
extern void VISetNextFrameBuffer(void* fb);
extern void VIFlush(void);
extern void VIWaitForRetrace(void);

// === AI (Audio Interface) ===
// Stub — real implementation in tsfp-recomp
extern void AIInit(void* callback);
extern void AIStartDMA(void);

// === DSP ===
// Real implementation decompiled in src/audio/dsp.cpp
extern unsigned int DSPInit(int callback, int arg);

// === CARD (Memory Card) ===
// Stub — real implementation in tsfp-recomp
extern s32 CARDOpen(s32 chan, const char* fileName, void* fileInfo);
extern s32 CARDRead(void* fileInfo, void* addr, s32 length, s32 offset);

// === Math helpers (real implementations decompiled elsewhere / in tsfp-recomp) ===
// Declared here with correct double signatures so the permissive extern
// generator does not emit truncating `int name(...)` stubs for them.
extern double math_atan2(double y, double x);

#ifdef __cplusplus
}
#endif

#endif // TSFP_SDK_STUBS_H
