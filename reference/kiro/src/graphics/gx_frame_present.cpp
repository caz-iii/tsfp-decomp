// ============================================================================
// Address range: 0x8017f05c - 0x8017fea4 | batch_469 | Date: 2026-09-06
// Subsystem: graphics (GX FIFO / frame boot + present + GPU-wait/flush pipeline).
// tag gx-present.
//
// The display/present layer that wraps gx_init_render_mode (0x8017e6cc): full VI/GX/DVD/
// AI subsystem boot, per-frame VI retrace flip, the main GPU-flush / wait-for-draw-done
// loop with GPU-hang detection + FIFO diagnostics, the FIFO error-recovery re-init,
// the packed indirect-texture-stage config every drawer funnels through, the fog-param
// programmer, and the character->font-glyph index mapper.
//
//   gx_fifo_reinit_recover  0x8017f05c  rebuild the GX command FIFO + reinit render mode
//   gx_subsystem_boot       0x8017f240  VI/GX/DVD/AI/etc. one-time subsystem bring-up
//   vi_present_frame        0x8017f300  VI retrace wait + double-buffer flip
//   gx_post_present_setup   0x8017f3a4  post-present tex-transform / env-tex state
//   gx_draw_done_tokens     0x8017f490  emit GX draw-done + breakpoint tokens
//   occ_query_flush_guard   0x8017f4c8  drain a pending occlusion query at frame end
//   gx_frame_flush_wait     0x8017f538  MASSIVE end-frame GPU flush + wait + hang dump
//   gx_indirect_stage_config 0x8017fab4 program TEV indirect stages from packed flags
//   gx_set_fog_params_frame 0x8017fd0c  program fog colour/params for the frame
//   font_char_to_glyph      0x8017fea4  map an ASCII/latin-1 char to a font glyph index

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

// ---- typed aliases for helpers whose generated_decls proto is void / simplified ------
// mem_alloc_aligned: canonical decl is void(align,size,type); the FIFO allocator call
// site passes the PPC register-spill arg list and consumes the returned block pointer.
typedef unsigned int (*MemAllocAlignedFn)(unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          int, int, int, int, int, int, int, int);
#define GX_FIFO_ALLOC ((MemAllocAlignedFn)mem_alloc_aligned)

// occ_scratch_setup: canonical decl is void(int); here its returned framebuffer handle
// is consumed by gx_init_render_mode.
typedef unsigned long long (*OccScratchSetupFn)(int);
#define OCC_SCRATCH_SETUP ((OccScratchSetupFn)occ_scratch_setup)

// ---- raw GX / VI / DVD / AI runtime helpers ---------------------------------
extern unsigned long long FUN_803826f0(void);     // spawn-frame scratch (r3:r4 pair)
extern void        FUN_8038273c(void);             // spawn-frame pop
extern void        FUN_80382730(void);             // spawn-frame pop
extern int         FUN_803826e4(void);             // spawn-frame scratch (single)
extern unsigned int FUN_803c5de0(void);            // GX get CPU FIFO ptr
extern unsigned int FUN_803c5a5c(void *fifo);      // GX get FIFO size a
extern unsigned int FUN_803c5a64(int fifo);        // GX get FIFO size b
extern void        FUN_803994d0(unsigned int base, unsigned int size); // flush data cache
extern void        FUN_803c6d78(void);             // GX abort/reset FIFO
extern void        FUN_803c5630(int *fifo);        // GX set CPU FIFO
extern void        FUN_803c572c(int *fifo);        // GX set GP FIFO
extern void        FUN_803c5368(void *fifo);       // GX FIFO init a
extern unsigned long long FUN_803c5490(void *fifo);// GX FIFO init b
extern unsigned int FUN_803c4214(unsigned long long p1, unsigned long long p2,
                                 unsigned long long p3, unsigned long long p4,
                                 unsigned long long p5, unsigned long long p6,
                                 unsigned long long p7, unsigned long long p8,
                                 unsigned int cpuFifo, unsigned int sizeA, unsigned int sizeB,
                                 int r6, int r7, int r8, int r9, int r10); // GX init FIFO obj
extern void        FUN_803c681c(void);             // GX flush texture state
extern void        FUN_803c94d8(void);             // GX re-select tex-copy state
extern void        FUN_803a78ac(int mode);         // set GP fifo owner
extern unsigned long long FUN_803a7710(void);      // resume/collect frame ptr
extern void        FUN_803c6f40(int token);        // GX write breakpoint token
extern unsigned int FUN_803c80bc(int slot, unsigned int flag); // GX copy display
extern void        gx_setup_fifo_base(int *out, unsigned int base, unsigned int size);
extern void        gx_init_render_mode(unsigned long long p1, unsigned long long p2,
                                       unsigned long long p3, unsigned long long p4,
                                       unsigned long long p5, unsigned long long p6,
                                       unsigned long long p7, unsigned long long p8);

// full subsystem boot chain
extern unsigned long long FUN_80397e04(unsigned long long, unsigned long long, unsigned long long,
                                       unsigned long long, unsigned long long, unsigned long long,
                                       unsigned long long, unsigned long long, unsigned int, unsigned int,
                                       int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern void        VIInit(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                          int, int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern unsigned int FUN_803a88c4(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                 int, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern unsigned long long DVDInit(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                  unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                  unsigned int, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern int         FUN_803a5974(int on);
extern unsigned long long FUN_803b2d00(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                       unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                       unsigned int, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern unsigned int FUN_803aa2ac(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                 int, int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern unsigned long long FUN_803abde4(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                       unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                       unsigned int, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern unsigned long long FUN_803a9c6c(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                       unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                       int, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern void        FUN_803abfb0(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                int, int, int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
extern void        FUN_8039f174(void *cb);
extern int         FUN_80184660(void);
extern void        FUN_80186ac4(int);
extern int         FUN_803a520c(void);
extern void        FUN_803996d8(void);

// present / retrace
extern void        FUN_803a7840(int slot);
extern void        VIWaitForRetrace(void);
extern int         FUN_801a2f0c(void);

// post-present state
extern void        FUN_803cb410(double, double, double, double, double, double, unsigned int);
extern unsigned short FUN_803a79d4(void);
extern void        FUN_803c7ea0(char, unsigned char *, char, unsigned char *);
extern void        FUN_8018fbac(void);

// draw-done tokens
extern void        FUN_803c7000(void);

// frame flush + hang dump
extern unsigned int FUN_801c0894(void);
extern void        FUN_803cabec(int);
extern void        FUN_803c78c8(int, int, unsigned int, unsigned int);
extern void        FUN_803c7978(short, int, int, int);
extern unsigned int FUN_803c9040(int);
extern void        FUN_803c8224(unsigned int, int);
extern void        FUN_8018fbb0(void);
extern void        FUN_803c5de8(void);             // (address form used below)
extern int         FUN_803c5de8_i(void);
extern void        FUN_803c59c4(int obj, void *out1, void *out2);
extern short       FUN_803c6ff4(void);
extern void        FUN_8039cca0(void);
extern void        FUN_8039ccc8(void);
extern void        FUN_803c57c8(void *a, void *b, void *c, void *d, void *e);
extern void        FUN_80386ae4(unsigned char *msg);
extern void        FUN_803c5818(int *obj, void *a, void *b, void *c, void *d, void *e, void *f);
extern void        FUN_803c5ab0(unsigned int bp);
extern int         FUN_8018fbf8(void);
extern unsigned int FUN_803cacac(unsigned int a, int b);
extern unsigned int FUN_8039e020(void);
extern int         FUN_801a30cc(void);
extern void        FUN_8018043c(unsigned long long, double, double, unsigned long long,
                                unsigned long long, unsigned long long, unsigned long long, unsigned long long);

// indirect texture stage config
extern void        FUN_803c623c(void);
extern void        FUN_803c5df0(int stage, int mode);

// fog params
extern void        FUN_803ca8c4(unsigned short *out, unsigned int w, float *viewRow);
extern void        FUN_803caa74(int mode, unsigned int halfW, unsigned short *tbl);
extern void        FUN_803ca6b0(double, double, double, double, unsigned int, unsigned int *);

// ---- globals ----------------------------------------------------------------
extern unsigned int *DAT_806123b4;  // active GX FIFO object
extern int   DAT_80612388;          // FIFO breakpoint write cursor
extern int   DAT_8061238c;          // FIFO breakpoint target cursor
extern int   DAT_80612390;          // last submitted breakpoint token
extern unsigned char DAT_80610a7a;  // GP fifo owner flag
extern int   DAT_80612364;          // pending occlusion-query flag
extern int   DAT_806123c4;          // front framebuffer slot
extern int   DAT_806109c4;          // screen width
extern int   DAT_806109c8;          // screen height
extern int   DAT_80612378;          // boot phase latch
extern int   DAT_80612370;          // audio/DVD handle
extern int   DAT_806123bc;          // current display slot
extern int   DAT_806123c8;          // present frame counter
extern int   DAT_806123c0;          // back framebuffer slot
extern int   DAT_80612394;          // present-pending flag
extern char  DAT_80612361;          // frame-in-progress flag
extern char  DAT_80612360;          // frame-stall request
extern int   DAT_806123b8;          // active viewport/render-mode record
extern float FLOAT_806166b4;        // tex-transform scale
extern double DOUBLE_806166c8;      // int->float magic bias
extern float FLOAT_806166a4;        // tex-transform w
extern int   DAT_8061239c;          // 2D-mode-active flag
extern int   DAT_80612384;          // occlusion result-ring read cursor
extern int   DAT_805978a0;          // occlusion result-ring token base
extern int   DAT_805968a0;          // occlusion result-ring breakpoint base
extern int   DAT_800000f8;          // OS timer clock divisor
extern int   DAT_80612362;          // frame-done flag
extern char  DAT_806123a0[1];       // GPU stall counter (byte-addressed below)
extern int   DAT_806123a0_i;
extern unsigned char DAT_80610a78;  // pending anti-alias mode
extern unsigned char DAT_80610a79;  // applied anti-alias mode
extern int   DAT_80610a88;          // frame budget prev
extern int   DAT_80610a8c;          // frame budget prev-prev
extern int   DAT_80612028;          // frame budget cur
extern int   DAT_80612374;          // first-frame latch
extern int   DAT_8061237c;          // draw-command depth
extern int   DAT_80612368;          // suppress-post flag
extern float FLOAT_80610a4c;        // occlusion-alpha scale
extern int   DAT_806125fc;          // fx enable mask A
extern int   DAT_80612600;          // fx enable mask B
extern int   DAT_80612614;          // game-mode id
extern int   DAT_805cc614;          // sub-mode id
extern int   DAT_80610a90;          // cached indirect-flag word
extern int   DAT_80610a94;          // cached indirect frame stamp
extern int   DAT_80612020;          // indirect frame stamp
extern char  DAT_806123a4;          // fog-init-once flag
extern int   DAT_80611d54;          // active render context
extern int   DAT_80612292;          // fog-enabled flag
extern float FLOAT_806166b4_f;      // (alias; unused)
extern unsigned char DAT_806166d0[1]; // per-mode fog-density byte table

// diagnostic strings
extern char s_GPU_err___d__d__d__80411448[1];
extern char s_GP_Status__d__d__d__d__d_8041145c[1];
extern char s_CPU_Ptrs__x__x_80411478[1];
extern char s_GPU_Ptrs__x__x_80411488[1];
extern char s_Current_Breakpoint__x_80411498[1];
extern char s_Fifo__d__d__d__d__d__d_804114b0[1];

// forward decls for same-file callees used earlier in the TU
void gx_frame_flush_wait(unsigned long long p1, unsigned long long p2, double p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8);
void gx_fifo_reinit_recover(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                            unsigned long long p4, unsigned long long p5, unsigned long long p6,
                            unsigned long long p7, unsigned long long p8);

// gx_fifo_reinit_recover (0x8017f05c)
// Recover from a GPU/FIFO error: allocate a temporary 0x10000 FIFO, abort the current
// GP FIFO, rebuild a fresh command FIFO object, re-run gx_init_render_mode, then clear
// the screen twice around occlusion-ring bookkeeping and free the temp FIFO.
void gx_fifo_reinit_recover(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                            unsigned long long p4, unsigned long long p5, unsigned long long p6,
                            unsigned long long p7, unsigned long long p8)
{
    int fifoBuf[38];
    unsigned long long frame = FUN_803826f0();
    unsigned int temp = GX_FIFO_ALLOC(frame, p2, p3, p4, p5, p6, p7, p8,
                                      0x20, 0x10000, 0, 0, 0, 0, 0, 0);
    unsigned int *cpuFifo = (unsigned int *)FUN_803c5de0();
    unsigned int sizeA = FUN_803c5a5c(cpuFifo);
    unsigned int sizeB = FUN_803c5a64((int)cpuFifo);

    memset_wrapper(temp, 0, 0x10000);
    FUN_803994d0(temp, 0x10000);
    FUN_803c6d78();
    FUN_803c5630((int *)DAT_806123b4);
    FUN_803c572c((int *)DAT_806123b4);
    gx_setup_fifo_base(fifoBuf, temp, 0x10000);
    FUN_803c5368(fifoBuf);
    unsigned long long obj = FUN_803c5490(fifoBuf);

    memset_wrapper((unsigned int)cpuFifo, 0, sizeA);
    FUN_803994d0((unsigned int)cpuFifo, sizeA);
    DAT_806123b4 = (unsigned int *)FUN_803c4214(obj, p2, p3, p4, p5, p6, p7, p8,
                                                (unsigned int)cpuFifo, sizeA, sizeB, 0, 0, 0, 0, 0);
    FUN_803c5368(DAT_806123b4);
    FUN_803c5490(DAT_806123b4);
    DAT_80612388 = 0;
    DAT_8061238c = 0;
    DAT_80612390 = 0;
    DAT_80610a7a = 1;
    FUN_803c681c();
    FUN_803c94d8();
    FUN_803a78ac(0);
    {
        unsigned long long fb = FUN_803a7710();
        gx_init_render_mode(fb, p2, p3, p4, p5, p6, p7, p8);
    }
    DAT_80612364 = 0;
    FUN_803c6f40(0xbeef);
    gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f007f);
    occ_query_end_bookkeeping(0xa33c);
    occ_result_ring_pop();
    FUN_803c80bc(DAT_806123c4, 0);
    FUN_803c6f40(0xdead);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f007f);
    FUN_803c6f40(0xbeef);
    gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f007f);
    mem_free_top(temp);
    FUN_8038273c();
}

// gx_subsystem_boot (0x8017f240)
// One-time bring-up of the whole display/IO stack: VI (VIInit), audio/DVD (DVDInit),
// AI, streaming, then allocate the render-mode framebuffer scratch and run
// gx_init_render_mode.  Called from heap_alloc_init.
void gx_subsystem_boot(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, unsigned int p9,
                       unsigned int p10, int p11, unsigned int p12, unsigned int p13,
                       unsigned int p14, unsigned int p15, unsigned int p16)
{
    DAT_80612378 = 0;
    unsigned long long v = FUN_80397e04(p1, p2, p3, p4, p5, p6, p7, p8,
                                        p9, p10, p11, p12, p13, p14, p15, p16);
    VIInit(0, p2, p3, p4, p5, p6, p7, p8,
           (int)(v >> 0x20), (int)v, p11, p12, p13, p14, p15, p16);
    FUN_803a78ac(1);
    unsigned long long fb = FUN_803a7710();
    unsigned int a = FUN_803a88c4(0, p2, p3, p4, p5, p6, p7, p8,
                                  (int)(fb >> 0x20), (unsigned int)fb, p11, p12, p13, p14, p15, p16);
    unsigned long long d = DVDInit(0, p2, p3, p4, p5, p6, p7, p8, a, (unsigned int)fb,
                                   p11, p12, p13, p14, p15, p16);
    int on = FUN_803a5974(1);
    d = FUN_803b2d00(d, p2, p3, p4, p5, p6, p7, p8, (unsigned int)on, 0, p11, p12, p13, p14, p15, p16);
    unsigned int a2 = FUN_803aa2ac(d, p2, p3, p4, p5, p6, p7, p8, 0, 0, p11, p12, p13, p14, p15, p16);
    d = FUN_803abde4(d, p2, p3, p4, p5, p6, p7, p8, a2, 0, p11, p12, p13, p14, p15, p16);
    d = FUN_803a9c6c(d, p2, p3, p4, p5, p6, p7, p8, 0, 0, p11, p12, p13, p14, p15, p16);
    FUN_803abfb0(0, p2, p3, p4, p5, p6, p7, p8, (int)(d >> 0x20), (int)d, p11, p12, p13, p14, p15, p16);
    FUN_8039f174((void *)0);
    int iVar2 = FUN_80184660();
    occ_scratch_begin(iVar2);
    FUN_80186ac4(0);
    DAT_80612370 = FUN_803a520c();
    DAT_806123b4 = 0;
    unsigned long long fbHandle = OCC_SCRATCH_SETUP((int)(((DAT_806109c4 & 0xffff) + 0xf) * 0x400 & 0x3ffc000));
    gx_init_render_mode(fbHandle, p2, p3, p4, p5, p6, p7, p8);
    FUN_803996d8();
}

// vi_present_frame (0x8017f300)
// Present the just-rendered frame: advance the display slot, on the 4th present hand the
// GP FIFO back to VI, optionally wait for retrace (twice when a UI overlay is active),
// then flip the double-buffered framebuffer slot.
void vi_present_frame(int doWait)
{
    FUN_803a7840(DAT_806123bc);
    if (DAT_806123c8 == 4) {
        FUN_803a78ac(0);
    }
    DAT_806123c8 = DAT_806123c8 + 1;
    FUN_803a7710();
    DAT_80612394 = 0;
    if (doWait != 0) {
        if (DAT_80612361 != '\0') {
            DAT_80612360 = 1;
        }
        VIWaitForRetrace();
        if (FUN_801a2f0c() != 0) {
            VIWaitForRetrace();
        }
    }
    {
        int next = DAT_806123c4;
        if (DAT_806123bc == DAT_806123c4) {
            next = DAT_806123c0;
        }
        DAT_806123bc = next;
    }
}

// gx_post_present_setup (0x8017f3a4)
// Re-establish tex-transform / env-tex matrices after the present flip using the active
// render-mode record's dimensions (int->float magic-bias dequant), then flush texture
// state and mark the 2D subsystem dirty.
void gx_post_present_setup(void)
{
    int rec = DAT_806123b8;
    double scale = (double)FLOAT_806166b4;
    if (*(char *)(DAT_806123b8 + 0x18) == '\0') {
        gx_set_tex_transform_params(
            scale, scale,
            (double)(float)((double)CONCAT44(0x43300000, (unsigned int)*(unsigned short *)(DAT_806123b8 + 4)) - DOUBLE_806166c8),
            (double)(float)((double)CONCAT44(0x43300000, (unsigned int)*(unsigned short *)(DAT_806123b8 + 6)) - DOUBLE_806166c8),
            scale, (double)FLOAT_806166a4);
    } else {
        unsigned short mode = FUN_803a79d4();
        FUN_803cb410(
            scale, scale,
            (double)(float)((double)CONCAT44(0x43300000, (unsigned int)*(unsigned short *)(rec + 4)) - DOUBLE_806166c8),
            (double)(float)((double)CONCAT44(0x43300000, (unsigned int)*(unsigned short *)(rec + 6)) - DOUBLE_806166c8),
            scale, (double)FLOAT_806166a4, (unsigned int)mode);
    }
    FUN_803c681c();
    FUN_803c94d8();
    FUN_803c7ea0('\0', (unsigned char *)0, '\0', (unsigned char *)0);
    DAT_8061239c = 1;
    FUN_8018fbac();
}

// gx_draw_done_tokens (0x8017f490)
// Flush the pipeline and write the GX draw-done + start-of-frame breakpoint tokens.
void gx_draw_done_tokens(void)
{
    FUN_803c7000();
    FUN_803c6f40(0xbeef);
    FUN_803c6f40(0xbeef);
}

// occ_query_flush_guard (0x8017f4c8)
// If an occlusion query is pending this frame, push the end-bookkeeping token and,
// when the FIFO breakpoint cursors have caught up (CLZ of the difference is non-zero
// i.e. equal), pop the result ring and emit the frame-done token.
void occ_query_flush_guard(void)
{
    if (DAT_80612364 != 0) {
        unsigned int caught = countLeadingZeros((unsigned int)(DAT_8061238c - DAT_80612388));
        occ_query_end_bookkeeping(0xa33c);
        if ((caught >> 5) != 0) {
            occ_result_ring_pop();
        }
        FUN_803c6f40(0xdead);
        DAT_80612364 = 0;
    }
}

// gx_frame_flush_wait (0x8017f538)
// End-of-frame GPU flush + wait-for-draw-done, with GPU-hang detection.  Draws the
// occlusion-test cover quad, programs the copy region, submits the draw-done breakpoint,
// then spins polling the GPU breakpoint token for up to ~6 timer ticks; if the GPU
// stalls it dumps FIFO/GP/CPU-pointer diagnostics and re-inits the FIFO
// (gx_fifo_reinit_recover).  On success it copies out the front buffer, updates the
// anti-alias mode, and kicks the next-frame path.
void gx_frame_flush_wait(unsigned long long p1, unsigned long long p2, double p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8)
{
    unsigned char covByte[7];
    int i, stallCount = 0;
    unsigned int prevToken = 0;
    unsigned int a, b, c;

    lens_flare_occlusion_test_render();
    unsigned int fx = FUN_801c0894();
    if (((DAT_806125fc & DAT_80612600 & fx & 0x4000) == 0) ||
        ((DAT_80612614 == 0x65) && (DAT_805cc614 != 5))) {
        gx_set_zmode(1);
    } else {
        gx_set_zmode(0);
    }

    // build the 7-byte cover-quad colour clamp table from the render-mode record
    {
        double s = (double)FLOAT_80610a4c;
        for (i = 0; i < 7; i++) {
            unsigned int raw = (unsigned int)*(unsigned char *)(DAT_806123b8 + i + 0x32);
            int v = (int)(s * (double)(float)((double)CONCAT44(0x43300000, raw) - DOUBLE_806166c8));
            covByte[i] = (unsigned char)v;
        }
    }

    gx_set_blend_mode(4);
    FUN_803cabec(1);
    FUN_803c78c8(0, 0, DAT_806109c4 & 0xffff, DAT_806109c8 & 0xffff);
    FUN_803c7978((short)(((int)DAT_806109c4 >> 1) +
                         (unsigned short)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0)),
                 0x100, 4, 1);
    unsigned int copyObj = FUN_803c9040(-0x7fa70410);
    FUN_803c8224(copyObj, 0);
    FUN_803c7ea0(*(char *)(DAT_806123b8 + 0x19), (unsigned char *)(DAT_806123b8 + 0x1a),
                 '\x01', covByte);
    gx_set_color_update_mode(0);
    FUN_8018fbb0();

    int startTime = os_get_time_base();
    FUN_803c6f40(0xbeef);

    // count how many outstanding ring entries carry the a33c token
    {
        int outstanding = 0;
        int idx;
        for (idx = DAT_80612384; DAT_80612388 != idx; ) {
            if (*(int *)(&DAT_805978a0 + idx * 4) == 0xa33c) {
                outstanding = outstanding + 1;
            }
            idx = (idx + 1) & 0x3ff; // ring wrap (0x400 entries)
        }
        if ((outstanding != 0) && (outstanding != 1)) {
            do {
                occ_result_ring_pop();
            } while (*(int *)(&DAT_805978a0 + DAT_80612384 * 4) != 0xa33c);
        }
    }

    {
        int obj = (int)FUN_803c5de8_i();
        int cpuHi, cpuLo;
        FUN_803c59c4(obj, &cpuHi, &cpuLo);
    }

    // spin until draw-done breakpoint token matches, or GPU hang detected
    do {
        short tok = FUN_803c6ff4();
        if (tok == (short)-0x4111) {
            goto done;
        }
        if ((DAT_80610a7a == 0) && (prevToken == (unsigned int)DAT_80612390)) {
            stallCount = stallCount + 1;
            if (5000 < stallCount) {
                FUN_8039cca0();
                unsigned short r = (unsigned short)FUN_803c6ff4();
                if ((unsigned int)r == (unsigned int)DAT_80612390) {
                    occ_query_result_dispatch(DAT_80612390 & 0xffff);
                }
                FUN_8039ccc8();
                stallCount = 0;
            }
        } else {
            stallCount = 0;
            prevToken = DAT_80612390 & 0xffff;
        }
        {
            unsigned char e0, e1, e2, e3, e4;
            FUN_803c57c8(&e0, &e1, &e2, &e3, &e4);
            (void)e1; (void)e2; (void)e3;
            int cpuA, cpuB;
            int obj = (int)FUN_803c5de0();
            FUN_803c59c4(obj, &cpuA, &cpuB);
            if ((e0 == 0) || (cpuA != *(int *)(&DAT_805968a0 + DAT_80612384 * 4)) ||
                (*(int *)(&DAT_805978a0 + DAT_80612384 * 4) == 0xa33c)) {
                DAT_806123a0_i = 0;
            } else {
                DAT_806123a0_i = DAT_806123a0_i + 1;
                if (10000 < DAT_806123a0_i) {
                    FUN_8039cca0();
                    occ_query_poll();
                    FUN_8039ccc8();
                    DAT_806123a0_i = 0;
                }
            }
        }
    } while ((unsigned int)(os_get_time_base() - startTime) / (DAT_800000f8 >> 2) < 6);

    // --- GPU hang: dump diagnostics then re-init the FIFO ---
    {
        unsigned short tok = (unsigned short)FUN_803c6ff4();
        debug_log_formatted(s_GPU_err___d__d__d__80411448, 0, 0, 0, 0, 0, 0, 0,
                            (unsigned int)DAT_80610a7a, (unsigned int)DAT_80612390,
                            (unsigned int)tok, 0, 0, 0, 0, 0);
        FUN_80386ae4((unsigned char *)0);
    }
    {
        unsigned char g0, g1, g2, g3, g4;
        FUN_803c57c8(&g0, &g1, &g2, &g3, &g4);
        debug_log_formatted(s_GP_Status__d__d__d__d__d_8041145c, 0, 0, 0, 0, 0, 0, 0,
                            (unsigned int)g0, (unsigned int)g1, (unsigned int)g2,
                            (unsigned int)g3, (unsigned int)g4, 0, 0, 0);
        FUN_80386ae4((unsigned char *)0);
    }
    {
        int cpuA, cpuB;
        int obj = (int)FUN_803c5de0();
        FUN_803c59c4(obj, &cpuA, &cpuB);
        debug_log_formatted(s_CPU_Ptrs__x__x_80411478, 0, 0, 0, 0, 0, 0, 0,
                            (unsigned int)cpuA, (unsigned int)cpuB, 0, 0, 0, 0, 0, 0);
        FUN_80386ae4((unsigned char *)0);
        obj = (int)FUN_803c5de8_i();
        FUN_803c59c4(obj, &cpuA, &cpuB);
        debug_log_formatted(s_GPU_Ptrs__x__x_80411488, 0, 0, 0, 0, 0, 0, 0,
                            (unsigned int)cpuA, (unsigned int)cpuB, 0, 0, 0, 0, 0, 0);
        FUN_80386ae4((unsigned char *)0);
        debug_log_formatted(s_Current_Breakpoint__x_80411498, 0, 0, 0, 0, 0, 0, 0,
                            (unsigned int)*(int *)(&DAT_805968a0 + DAT_80612384 * 4), (unsigned int)cpuB,
                            0, 0, 0, 0, 0, 0);
        FUN_80386ae4((unsigned char *)0);
        FUN_803c5ab0(*(unsigned int *)(&DAT_805968a0 + DAT_80612384 * 4));
    }
    {
        int *obj = (int *)FUN_803c5de8_i();
        unsigned char f0, f1, f4, f5, f6;
        int fifoWr;
        FUN_803c5818(obj, &f0, &f1, &fifoWr, &f4, &f5, &f6);
        debug_log_formatted(s_Fifo__d__d__d__d__d__d_804114b0, 0, 0, 0, 0, 0, 0, 0,
                            (unsigned int)f0, (unsigned int)f1, (unsigned int)fifoWr,
                            (unsigned int)f4, (unsigned int)f5, (unsigned int)f6, 0, 0);
        FUN_80386ae4((unsigned char *)0);
    }
    gx_fifo_reinit_recover(0, p2, p3, p4, p5, p6, p7, p8);

done:
    DAT_80612362 = 1;
    occ_query_end_bookkeeping(0xa33c);
    FUN_803c80bc(DAT_806123c4, 0);
    DAT_80612361 = 1;
    FUN_8018fbf8();
    DAT_80610a8c = DAT_80610a88;
    DAT_80610a88 = DAT_80612028;
    if ((unsigned int)DAT_80610a78 != (unsigned int)DAT_80610a79) {
        FUN_803cacac((unsigned int)DAT_80610a78, 0);
        DAT_80610a79 = DAT_80610a78;
    }
    DAT_8061239c = 0;
    unsigned int busy = FUN_8039e020();
    if ((busy == 0) || (DAT_80612378 != 0)) {
        if ((DAT_80612374 != 0) &&
            (DAT_80612378 = 1, DAT_8061237c == 0 && FUN_801a30cc() == 0) &&
            (DAT_80612368 == 0)) {
            FUN_8018043c(0, p3, p3, p4, p5, p6, p7, p8);
        }
    } else {
        DAT_80612374 = 1;
    }
}

// gx_indirect_stage_config (0x8017fab4)
// When the packed indirect-texture flag word (from the spawn-frame scratch) changes,
// reprogram the TEV indirect stages: bit 0x40 -> stage 0; then decode 5 bit-fields
// (0..3, 4..5, 6..8-ish, 9..10, and high bits) each into an indirect stage/mode pair,
// plus a run of stages for the high 0x2000+ bits.
void gx_indirect_stage_config(void)
{
    unsigned int flags = (unsigned int)FUN_803826e4();
    if ((flags != (unsigned int)DAT_80610a90) || (DAT_80610a94 != DAT_80612020)) {
        unsigned int f03  = flags & 3;
        unsigned int f0c  = flags & 0xc;
        unsigned int f30  = flags & 0x30;
        unsigned int f180 = flags & 0x180;
        unsigned int f600 = flags & 0x600;
        FUN_803c623c();
        if ((flags & 0x40) != 0) {
            FUN_803c5df0(0, 1);
        }
        if (f03 == 2)      { FUN_803c5df0(9, 2); }
        else if (f03 < 2)  { if (f03 != 0) { FUN_803c5df0(9, 1); } }
        else if (f03 < 4)  { FUN_803c5df0(9, 3); }

        if (f180 == 0x100)      { FUN_803c5df0(10, 2); }
        else if (f180 < 0x100)  { if (f180 == 0x80) { FUN_803c5df0(10, 1); } }
        else if (f180 == 0x180) { FUN_803c5df0(10, 3); }

        if (f0c == 8)      { FUN_803c5df0(0xb, 2); }
        else if (f0c < 8)  { if (f0c == 4) { FUN_803c5df0(0xb, 1); } }
        else if (f0c == 0xc) { FUN_803c5df0(0xb, 3); }

        if (f600 == 0x400)      { FUN_803c5df0(0xc, 2); }
        else if (f600 < 0x400)  { if (f600 == 0x200) { FUN_803c5df0(0xc, 1); } }
        else if (f600 == 0x600) { FUN_803c5df0(0xc, 3); }

        if (f30 == 0x20)      { FUN_803c5df0(0xd, 2); }
        else if (f30 < 0x20)  { if (f30 == 0x10) { FUN_803c5df0(0xd, 1); } }
        else if (f30 == 0x30) { FUN_803c5df0(0xd, 3); }

        if ((flags & 0x800) != 0) {
            FUN_803c5df0(0xc, 3);
        }
        if ((flags & 0x2000) != 0) {
            int stage = 0xe;
            unsigned int bit;
            for (bit = 0x2000; (flags & bit) != 0; bit = bit << 1) {
                FUN_803c5df0(stage, 1);
                stage = stage + 1;
            }
        }
        DAT_80610a94 = DAT_80612020;
        DAT_80610a90 = (int)flags;
    }
    FUN_80382730();
}

// gx_set_fog_params_frame (0x8017fd0c)
// Program per-frame fog: on the first fog-init with an active render context, build the
// fog range table from the view projection; then either disable fog (when the packed
// colour word's high half is zero) or program the doubled/saturated RGB fog colour with
// the per-mode density byte.
void gx_set_fog_params_frame(unsigned long long param_1, double param_2, unsigned long long param_3,
                             unsigned long long param_4, double param_5, double param_6,
                             int param_9)
{
    unsigned short rangeTbl[24];
    unsigned int colorWords[2];
    unsigned int colorPacked;

    unsigned long long frame = FUN_803826f0();
    unsigned int packed = (unsigned int)frame;
    double f1 = 0.0;

    if ((DAT_806123a4 == '\0') && (DAT_80611d54 != 0)) {
        DAT_806123a4 = '\x01';
        FUN_803ca8c4(rangeTbl, DAT_806109c4 & 0xffff, *(float **)(DAT_80611d54 + 0x204));
        FUN_803caa74(1, ((int)DAT_806109c4 >> 1) +
                        ((unsigned int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0) & 0xffff),
                     rangeTbl);
    }

    if ((int)(frame >> 0x20) == 0) {
        f1 = (double)FLOAT_806166b4;
        colorWords[1] = 0;
        DAT_80612292 = 0;
        colorWords[0] = 0;
        FUN_803ca6b0(f1, f1, f1, f1, 0, &colorWords[0]);
    } else {
        DAT_80612292 = 1;
        unsigned int r = (packed >> 0x18) << 1;      if (r >= 0xff) r = 0xff;
        unsigned int g = (packed >> 0x10 & 0xff) << 1; if (g >= 0xff) g = 0xff;
        unsigned int b = (packed >> 8 & 0xff) << 1;    unsigned char bb = 0xff; if (b < 0xff) bb = (unsigned char)b;
        colorPacked = ((unsigned int)(unsigned char)r << 24) | ((unsigned int)(unsigned char)g << 16) |
                      ((unsigned int)bb << 8) | (unsigned int)(unsigned char)packed;
        colorWords[0] = colorPacked;
        FUN_803ca6b0(f1, param_2, param_5, param_6,
                     (unsigned int)(unsigned char)(&DAT_806166d0)[param_9], &colorWords[0]);
    }
    FUN_8038273c();
}

// font_char_to_glyph (0x8017fea4)
// Map an ASCII / latin-1 character to a font glyph atlas index: A-Z -> 0.., a-z -> 0..,
// 0-9 -> digit glyphs, then a table of punctuation and accented-vowel forms.  Returns
// -1 for an unmapped character.
int font_char_to_glyph(char c)
{
    int i = (int)c;
    if ((i >= 0x41) && (i <= 0x5a)) return i - 0x41;      // A-Z
    if ((i >= 0x61) && (i <= 0x7a)) return i - 0x61;      // a-z
    if ((i >= 0x30) && (i <= 0x39)) return i - 0x16;      // 0-9

    switch (c) {
    case ',':    return 0x25;
    case '.':    return 0x24;
    case '\'':   return 0x2d;
    case '(':    return 0x2d;
    case ')':    return 0x2e;
    case '-':    return 0x2f;
    case '?':    return 0x2c;
    }
    // accented-vowel / extended latin-1 forms (signed char literals)
    if ((c == -0x2a) || (c == -10))  return 0x26;
    if ((c == -0x40) || (c == -0x20)) return 0x27;
    if (c == -0x21)                   return 0x28;
    if ((c == -0x38) || (c == -0x18)) return 0x29;
    if ((c == -0x3f) || (c == -0x1f)) return 0x2a;
    if ((c == -0x37) || (c == -0x17)) return 0x2b;
    if ((c == -0x2f) || (c == -0xf))  return 0x30;
    if ((c == -0x2d) || (c == -0xd))  return 0x31;
    if ((c == -4)    || (c == -0x24)) return 0x78;
    if ((c == -0x1c) || (c == -0x3c)) return 100;
    if ((c == -6)    || (c == -0x26)) return 0x32;
    if ((c == -0x13) || (c == -0x33)) return 0x33;
    return -1;
}
