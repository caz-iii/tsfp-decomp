// ============================================================================
// Address range: 0x8017e058 - 0x8017e664 | batch_468 (part 2/2) | Date: 2026-09-06
// Subsystem: graphics (GX render-mode / vertex-format init + boot-sequence polling).
// tag gx-rendermode.
//
// Helpers used by gx_init_render_mode (0x8017e6cc, the next frontier) to bring up the
// GX pipeline.  gx_config_vtx_format programs the per-viewport vertex-attribute
// descriptor table (FUN_803c6274 configures one attribute: viewport, attr id, enable,
// component-count code, format code) for 8 viewport layouts.  The boot pollers gate the
// render-mode init loop: boot_should_advance / boot_is_ready poll the boot state
// machine, boot_skip_requested reports a skip/press.
//
//   gx_config_vtx_format   0x8017e058  program the per-viewport vertex-attr table
//   boot_should_advance    0x8017e5f0  boot state is 1 or 5 -> advance
//   boot_is_ready          0x8017e638  boot ready flag -> !bit as 0/1
//   boot_skip_requested    0x8017e664  boot skip/press request

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

// ---- raw runtime helpers (batch_468 part 2) ---------------------------------
extern void FUN_803c6274(int viewport, int attr, int enable, int compCode, int fmtCode);  // GX set vtx attr
extern int  FUN_803a7b08(void);          // boot/attract state machine id
extern unsigned short FUN_803a7b70(void); // boot ready sub-state
extern unsigned char  FUN_8039ec00(void); // boot auto-advance flag
extern unsigned char  FUN_8039ed80(void); // boot ready bit

// gx_config_vtx_format (0x8017e058)
// Program the per-viewport GX vertex-attribute descriptor table for one of 8 viewport
// layouts (param_layout 0..7).  Each layout configures attributes 9..0xf via
// FUN_803c6274(viewport, attr, enable, componentCode, formatCode).  Attributes 9 and
// 10 (position/normal) and 0xd..0xf (tex-coords) vary their component/format per layout
// (e.g. code 3+10 vs 4+0 vs 3+0xe); 0xb/0xc (colours) are constant (enable,5,0).
void gx_config_vtx_format(int param_layout)
{
    switch (param_layout) {
    case 0:
        FUN_803c6274(0, 9, 1, 4, 0);
        FUN_803c6274(0, 10, 0, 4, 0);
        FUN_803c6274(0, 0xb, 1, 5, 0);
        FUN_803c6274(0, 0xc, 1, 5, 0);
        FUN_803c6274(0, 0xd, 1, 4, 0);
        FUN_803c6274(0, 0xe, 1, 4, 0);
        FUN_803c6274(0, 0xf, 1, 4, 0);
        break;
    case 1:
        FUN_803c6274(1, 9, 1, 3, 10);
        FUN_803c6274(1, 10, 0, 4, 0);
        FUN_803c6274(1, 0xb, 1, 5, 0);
        FUN_803c6274(1, 0xc, 1, 5, 0);
        FUN_803c6274(1, 0xd, 1, 4, 0);
        FUN_803c6274(1, 0xe, 1, 4, 0);
        FUN_803c6274(1, 0xf, 1, 4, 0);
        break;
    case 2:
        FUN_803c6274(2, 9, 1, 4, 0);
        FUN_803c6274(2, 10, 0, 3, 0xe);
        FUN_803c6274(2, 0xb, 1, 5, 0);
        FUN_803c6274(2, 0xc, 1, 5, 0);
        FUN_803c6274(2, 0xd, 1, 4, 0);
        FUN_803c6274(2, 0xe, 1, 4, 0);
        FUN_803c6274(2, 0xf, 1, 4, 0);
        break;
    case 3:
        FUN_803c6274(3, 9, 1, 3, 10);
        FUN_803c6274(3, 10, 0, 3, 0xe);
        FUN_803c6274(3, 0xb, 1, 5, 0);
        FUN_803c6274(3, 0xc, 1, 5, 0);
        FUN_803c6274(3, 0xd, 1, 4, 0);
        FUN_803c6274(3, 0xe, 1, 4, 0);
        FUN_803c6274(3, 0xf, 1, 4, 0);
        break;
    case 4:
        FUN_803c6274(4, 9, 1, 4, 0);
        FUN_803c6274(4, 10, 0, 4, 0);
        FUN_803c6274(4, 0xb, 1, 5, 0);
        FUN_803c6274(4, 0xc, 1, 5, 0);
        FUN_803c6274(4, 0xd, 1, 3, 10);
        FUN_803c6274(4, 0xe, 1, 3, 10);
        FUN_803c6274(4, 0xf, 1, 3, 10);
        break;
    case 5:
        FUN_803c6274(5, 9, 1, 3, 10);
        FUN_803c6274(5, 10, 0, 4, 0);
        FUN_803c6274(5, 0xb, 1, 5, 0);
        FUN_803c6274(5, 0xc, 1, 5, 0);
        FUN_803c6274(5, 0xd, 1, 3, 10);
        FUN_803c6274(5, 0xe, 1, 3, 10);
        FUN_803c6274(5, 0xf, 1, 3, 10);
        break;
    case 6:
        FUN_803c6274(6, 9, 1, 4, 0);
        FUN_803c6274(6, 10, 0, 3, 0xe);
        FUN_803c6274(6, 0xb, 1, 5, 0);
        FUN_803c6274(6, 0xc, 1, 5, 0);
        FUN_803c6274(6, 0xd, 1, 3, 10);
        FUN_803c6274(6, 0xe, 1, 3, 10);
        FUN_803c6274(6, 0xf, 1, 3, 10);
        break;
    case 7:
        FUN_803c6274(7, 9, 1, 3, 10);
        FUN_803c6274(7, 10, 0, 3, 0xe);
        FUN_803c6274(7, 0xb, 1, 5, 0);
        FUN_803c6274(7, 0xc, 1, 5, 0);
        FUN_803c6274(7, 0xd, 1, 3, 10);
        FUN_803c6274(7, 0xe, 1, 3, 10);
        FUN_803c6274(7, 0xf, 1, 3, 10);
        break;
    }
}

// boot_should_advance (0x8017e5f0)
// Return 1 when the boot/attract state machine (FUN_803a7b08) is in state 1 or 5,
// signalling the render-mode init loop should advance.
int boot_should_advance(void)
{
    int state = FUN_803a7b08();
    if (state == 1 || FUN_803a7b08() == 5) {
        return 1;
    }
    return 0;
}

// boot_is_ready (0x8017e638)
// Return the boot ready flag as 0/1: countLeadingZeros(1 - readyBit) >> 5 maps a
// non-zero ready bit to 1 and a zero bit to 0.
unsigned int boot_is_ready(void)
{
    unsigned char bit = FUN_8039ed80();
    unsigned int clz = countLeadingZeros(1 - (unsigned int)bit);
    return clz >> 5;
}

// boot_skip_requested (0x8017e664)
// Poll input and report a boot skip/press request: when the boot state machine is idle
// (FUN_803a7b08()==0) and a ready sub-state exists (FUN_803a7b70()!=0), return 1 if the
// auto-advance flag is set (FUN_8039ec00()==1) or the player-0 A button (0x10) was
// pressed.
int boot_skip_requested(void)
{
    input_update_all();
    int result = 0;
    if (FUN_803a7b08() == 0 && FUN_803a7b70() != 0) {
        if (FUN_8039ec00() == 1 || (input_get_buttons_pressed(0) & 0x10) != 0) {
            result = 1;
        }
    }
    return result;
}

// ============================================================================
// gx_init_render_mode (0x8017e6cc) | batch_501 | Date: 2026-09-06
// Subsystem: graphics (GX/VI render-mode bringup). tag gx-rendermode.
//
// The full render-mode initialiser referenced above as "the next frontier".  It:
//   1. resolves the active TV format via the boot/attract state machine
//      (FUN_803a7b08) and, when the caller forces progressive, promotes NTSC(1)
//      -> progressive(5);
//   2. selects the framebuffer geometry (480i / 480p / PAL 574) into the global
//      resolution block (DAT_806109c4 width, DAT_806109c8 height, etc.) or calls
//      OSPanic on an unknown format;
//   3. clears the five GX display-list / FIFO scratch blocks (memset_wrapper),
//      builds the GXRenderModeObj at DAT_80598ee0 (leaf_31e8 copies the template),
//      and programs VI (VIConfigure) + allocates the external framebuffer
//      (FUN_803c4214) if not already present;
//   4. programs the copy/scale filter (gx_set_tex_transform_params, FUN_803cb540,
//      FUN_803c784c ...), fog, TEV default stage, and the 8 vertex-format layouts
//      (gx_config_vtx_format 0..7);
//   5. allocates the occlusion-query EFB copy textures (FUN_803c8960 / FUN_803c8b84)
//      keyed on the frame width, fills the 0x40x0x40 occlusion result grid, installs
//      the occlusion query callbacks, and finally re-arms the default viewport /
//      screen scale / particle pools / render state.
//
// Non-matching: the register-pair return of __save_gpr_25 (used by the decompiler to
// carry both the caller's "force progressive" request and the resolved format) is
// modelled explicitly via the forceProgressive parameter and local flags.  Float
// spill temporaries the decompiler surfaced as f28..f31 locals are irrelevant to the
// integer control flow and are dropped.
// ============================================================================

// ---- raw symbols referenced by gx_init_render_mode (per-file externs) -------
// NOTE: FUN_803a7b08 (top of file), leaf_31e8 / VIConfigure / OSPanic / the
// occ_query_* callbacks and memset_wrapper resolve through the shared headers
// (generated_decls.h / sdk_stubs.h / generated_externs.h) and are intentionally
// NOT re-declared here to avoid linkage/overload conflicts.
extern "C" {
extern void  VIWaitForRetrace(void);
extern void  FUN_803994d0(unsigned int addr, int len);             // DCFlushRange
extern void  FUN_8039955c(int addr, int len);                      // DCInvalidate/alloc-tag
extern unsigned int  FUN_803c4214(unsigned long long a1, double a2, double a3,
                                  unsigned long long a4, unsigned long long a5,
                                  unsigned long long a6, unsigned long long a7,
                                  unsigned long long a8, unsigned int fb, int len,
                                  int a11, int a12, int a13, int a14, int a15, int a16); // alloc XFB
extern void  FUN_803c7708(unsigned int *rmSrc, void *rmDst, int a3, int a4);   // init render-mode obj
extern void  FUN_803cb540(int x, int y, unsigned int w, unsigned int h);       // copy-filter rect
extern void  FUN_803c784c(int x, int y, unsigned int w, unsigned int h);       // scissor/copy rect
extern double FUN_803c7b24(unsigned int h, unsigned int xfbHeight);            // vertical scale
extern void  FUN_803c7d5c(double scale);                                       // set vertical scale
extern void  FUN_803c7944(unsigned int w);                                     // set copy width
extern void  FUN_803c7ea0(unsigned char a1, void *a2, char a3, void *a4);      // copy filter coeffs
extern void  FUN_803c80a8(int mode);                                           // set field mode
extern void  FUN_803cac44(int a, int b, int c);                                // fog range setup
extern void  FUN_803cabec(int a);                                              // fog enable
extern void  FUN_803cacac(unsigned int a, int b);                              // fog type
extern void  FUN_803cae20(unsigned int a, unsigned int b);                     // interlace fields
extern void  FUN_803c80bc(int a, int b);                                       // set dither/gamma
extern void  FUN_803a7840(int a);                                              // apply render mode
extern void  FUN_803a7710(void);                                               // commit GX state
extern void  FUN_803c8874(int a);                                              // set cull mode
extern void  FUN_803c86a4(int a, unsigned int *pColor);                        // set ambient/reg colour
extern void  FUN_803c878c(int a, unsigned int *pColor);                        // set material colour
extern void  FUN_803c88b0(int a, int b, int c, int d, int e, int f, int g);    // set chan control
extern int   FUN_803c8960(unsigned int w, unsigned int h, int fmt, char mipmap, int a5); // alloc EFB tex
extern int  *FUN_80398f4c(int heap, int size);                                 // alloc from heap
extern void  FUN_803c8b84(unsigned int *pObj, unsigned int data, unsigned int w, unsigned int h,
                          int fmt, int a6, int a7, char a8);                    // init tex-obj
extern unsigned int FUN_803c9040(int desc);                                    // tex-obj data ptr
extern void  FUN_803c9288(unsigned int *pObj, int wrap);                       // set tex wrap
extern void  FUN_803ca39c(int stage, int a, int b, int c, int d);              // set TEV combine
extern void  FUN_803c6aac(int a);                                              // set num tev stages
extern void  FUN_803cab98(int a, int b, int c, int d);                         // set tev alpha op
extern void  FUN_803c766c(int a);                                              // set z mode
extern void  FUN_803c71c8(void *cb);                                           // set occ result cb
extern void  FUN_803c5a6c(void *cb);                                           // set occ poll cb
extern void  FUN_803a5f0c(void *cb);                                           // set retrace cb
extern void  FUN_803a78ac(int a);                                              // enable draw done cb
extern void  occ_result_grid_store(unsigned int x, unsigned int y, unsigned int v);

// GX/VI resolution + render-mode globals
extern int   DAT_80612398_deref;   // *DAT_80612398 (boot-phase flag); see access below
extern int  *DAT_80612398;         // ptr to boot-phase flag
extern int   DAT_80612240;         // progressive-scan available flag
extern int   DAT_80612244;         // PAL flag (0 = 60Hz, 1 = 50Hz)
extern int   DAT_806109c0;         // field refresh (0x3c / 0x32)
extern int   DAT_806109c4;         // framebuffer width
extern int   DAT_806109c8;         // framebuffer height
extern unsigned char DAT_80598ee0[]; // GXRenderModeObj storage block
extern unsigned short DAT_80598ee4;  // rmObj.fbWidth
extern unsigned short DAT_80598ee6;  // rmObj.efbHeight
extern unsigned short DAT_80598ee8;  // rmObj.xfbHeight
extern unsigned short DAT_80598eee;  // rmObj.viWidth
extern unsigned char  DAT_80598ef0;  // rmObj.viYOrigin
extern unsigned char  DAT_80598ef8;  // rmObj.field_rendering
extern unsigned char  DAT_80598ef9;  // rmObj.aa flag
extern unsigned char  DAT_80598efa;  // rmObj.sample-pattern base
extern unsigned char  DAT_80598f12;  // rmObj.vfilter base
extern unsigned char *DAT_806123b4;  // external framebuffer ptr
extern void          *DAT_806123b8;  // active render-mode obj ptr
extern int            DAT_806123c0;  // pending copy-clear flag source
extern int            DAT_806123c4;  // draw-done token
extern int            DAT_806123c8;  // vsync-enabled flag
extern int            DAT_806123bc;  // committed copy-clear flag
extern unsigned int   DAT_8060f940;  // XFB scratch base
extern int            DAT_806113c0;  // EFB copy heap id
extern int            DAT_80611748[];// scissor rect array
extern unsigned char  DAT_80610a78;  // fog-mode selector
extern unsigned char  DAT_80610a7c;  // OSPanic file-name storage
extern int            DAT_80610abc;  // (unused here) DSP token mirror
extern float FLOAT_806166a4, FLOAT_806166a8, FLOAT_806166ac, FLOAT_806166b0;
extern float FLOAT_806166b4, FLOAT_806166b8, FLOAT_806166bc;
extern float DAT_80616694, DAT_80616698;             // default ambient / material colours
extern double DOUBLE_806166c0, DOUBLE_806166c8;      // 2^52 bias constants
extern float FLOAT_80610a4c;                         // aspect scalar
extern float DAT_8058fc10, DAT_8058fc14, DAT_8058fc18, DAT_8058fc1c, DAT_8058fc20, DAT_8058fc24;
extern float FLOAT_8060f95c, FLOAT_8060faf8;
extern int   DAT_8060f950, DAT_8060f954;
extern const char s_GXInitRenderMode__bad_tv_format_80411400[];
// EFB-copy tex-obj descriptor blocks
extern unsigned int DAT_8058fbf0[], DAT_8058fbd0[], DAT_8058fbb0[], DAT_8058fb90[], DAT_8058fb70[];
extern unsigned int DAT_80598f1c[];
} // extern "C"

void gx_init_render_mode(unsigned long long param_1, double param_2, double param_3,
                         unsigned long long param_4, unsigned long long param_5,
                         unsigned long long param_6, unsigned long long param_7,
                         unsigned long long param_8, int forceProgressive)
{
    // resolved-format carry (decompiler: high/low halves of the __save_gpr_25 pair)
    int progressiveArmed = 0;   // low word: XFB uses progressive template
    int progressivePromote = 0; // high word: promote NTSC -> progressive

    unsigned int defAmbient  = DAT_80616694;
    unsigned int defMaterial = DAT_80616698;

    if (DAT_80612398 != 0 && *DAT_80612398 != 0 && forceProgressive == 0) {
        if (boot_should_advance() != 0 && boot_is_ready() != 0) {
            progressivePromote = 1;
        } else if (boot_skip_requested() != 0 && FUN_8039ec00() != 0) {
            progressiveArmed = 1;
        }
    } else {
        progressiveArmed = forceProgressive & 1;
        progressivePromote = (forceProgressive >> 1) & 1;
    }

    // Clear the five GX FIFO / display-list scratch blocks.
    memset_wrapper(0x80598ee0 - 0xc0, 0xff, 0xc0);
    memset_wrapper(0x80598ee0 - 0x60, 0xff, 0x140);
    memset_wrapper(0x80598ee0 + 0x80, 0xff, 0x140);
    memset_wrapper(0x80598ee0 + 0x1c0, 0xff, 0x100);
    memset_wrapper(0x80598ee0 + 0x2c0, 0xff, 0x100);

    int fmt = FUN_803a7b08();
    DAT_80612240 = (int)(countLeadingZeros((unsigned int)fmt) >> 5);
    if (fmt == 5) {
        fmt = 1;
    }
    if (progressivePromote != 0 && fmt == 1) {
        fmt = 5;
    }

    // Select framebuffer geometry by TV format.
    if (fmt == 0 || fmt == 2 || fmt == 5) {          // NTSC / MPAL / progressive
        DAT_80612244 = 0;
        DAT_806109c0 = 0x3c;
        FLOAT_8060f95c = FLOAT_806166a4;
        DAT_806109c8 = 0x1c0;
        FLOAT_8060faf8 = FLOAT_806166a8;
    } else if (fmt == 1) {                            // PAL
        DAT_80612244 = 1;
        DAT_806109c0 = 0x32;
        FLOAT_8060f95c = FLOAT_806166ac;
        DAT_806109c8 = 0x200;
        FLOAT_8060faf8 = FLOAT_806166b0;
    } else {
        OSPanic(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                (void *)&DAT_80610a7c, 0x46d, s_GXInitRenderMode__bad_tv_format_80411400,
                0, 0, 0, 0, 0);
        return;
    }

    DAT_806109c4 = 0x280;
    DAT_8058fc1c = (float)((double)(int)(DAT_806109c8 - 1) - 0.0) ;
    DAT_8058fc10 = FLOAT_806166b4;
    DAT_8058fc14 = FLOAT_806166b4;
    DAT_8058fc18 = FLOAT_806166b8;
    DAT_8058fc20 = FLOAT_806166b4;
    DAT_8058fc24 = FLOAT_806166a4;
    DAT_8060f950 = 4;
    DAT_8060f954 = 3;
    FLOAT_80610a4c = (DAT_80612244 != 0) ? FLOAT_806166bc : FLOAT_806166a4;

    // Choose the render-mode template for this format (and progressive variant).
    unsigned int *rmTemplate;
    switch (fmt) {
    case 2:  rmTemplate = (unsigned int *)0x8049fbd8; break;   // MPAL
    case 0:  rmTemplate = (progressiveArmed != 0) ? (unsigned int *)0x8049fb9c
                                                  : (unsigned int *)0x8049fb60; break; // NTSC (int/prog)
    case 1:  rmTemplate = (unsigned int *)0x8049fc14; break;   // PAL
    case 5:  rmTemplate = (unsigned int *)0x8049fc50; break;   // progressive
    default:
        OSPanic((double)DAT_8058fc1c, param_3, param_2, param_4, param_5, param_6, param_7, param_8,
                (void *)&DAT_80610a7c, 0x495, s_GXInitRenderMode__bad_tv_format_80411400,
                0, 0, 0, 0, 0);
        return;
    }

    int copyMode;
    if (DAT_80612244 == 0) { copyMode = 0x10; FUN_803c7708(rmTemplate, &DAT_80598ee0, 0, 0x10); }
    else                   { copyMode = 8;    FUN_803c7708(rmTemplate, &DAT_80598ee0, 0, 8); }
    DAT_806123b8 = &DAT_80598ee0;

    // leaf_31e8 copies the render-mode template's tail fields (void; no return).
    leaf_31e8((unsigned int)0x80598f12, (unsigned int)0x8061669c, 7);

    DAT_80598eee = 0x280;
    DAT_80598ee4 = (unsigned short)DAT_806109c4;
    // VIConfigure(GXRenderModeObj) — programs VI from the render-mode object we built.
    VIConfigure(*(GXRenderModeObj *)&DAT_80598ee0);

    if (DAT_806123b4 == 0) {
        unsigned int xfb = (DAT_8060f940 + 0x1f) & 0xffffffe0;
        memset_wrapper((int)xfb, 0, 0x10000);
        FUN_803994d0(xfb, 0x10000);
        DAT_806123b4 = (unsigned char *)FUN_803c4214(0, param_3, param_2, param_4, param_5,
                                                     param_6, param_7, param_8, xfb, 0x10000,
                                                     0x10000, copyMode, 0x280, 0, 0, 0);
    }

    // Program copy/scale filter from the resolved EFB/XFB dimensions.
    unsigned int efbH = DAT_80598ee4;
    unsigned int xfbH = DAT_80598ee6;
    gx_set_tex_transform_params(FLOAT_806166b4, FLOAT_806166b4,
                                (double)(float)((double)(int)efbH - 0.0),
                                (double)(float)((double)(int)xfbH - 0.0),
                                FLOAT_806166b4, FLOAT_806166a4);
    FUN_803cb540(0, 0, (unsigned int)DAT_80598ee4, (unsigned int)DAT_80598ee6);
    FUN_803c784c(0, 0, (unsigned int)DAT_80598ee4, (unsigned int)DAT_80598ee6);
    double vscale = FUN_803c7b24((unsigned int)DAT_80598ee6, (unsigned int)DAT_80598ee8);
    FUN_803c7d5c(vscale);
    FUN_803c7944((unsigned int)DAT_80598ee4);
    FUN_803c7ea0(DAT_80598ef9, &DAT_80598efa, '\x01', &DAT_80598f12);
    FUN_803c80a8(0);
    gx_set_fog_color(1, 0, 0xffffff);
    FUN_803cac44(1, 3, 1);
    FUN_803cabec(1);
    if (DAT_80598ef9 == 0) { FUN_803cacac((unsigned int)DAT_80610a78, 0); }
    else                   { FUN_803cacac(2, 0); }
    FUN_803cae20((unsigned int)DAT_80598ef8,
                 ((unsigned int)DAT_80598ee8 - (unsigned int)DAT_80598ef0) >> 0x1f);
    FUN_803c80bc(DAT_806123c4, 1);
    FUN_803a7840(DAT_806123c4);
    DAT_806123c8 = 1;
    DAT_806123bc = DAT_806123c0;
    FUN_803a7710();
    VIWaitForRetrace();
    if ((DAT_80598ee0[0] & 1) != 0) {
        VIWaitForRetrace();
    }

    // Program the 8 vertex-format layouts.
    for (int layout = 0; layout < 8; ++layout) {
        gx_config_vtx_format(layout);
    }

    FUN_803c8874(1);
    unsigned int ambient  = defAmbient;
    FUN_803c86a4(4, &ambient);
    unsigned int material = defMaterial;
    FUN_803c878c(4, &material);
    FUN_803c88b0(4, 0, 0, 1, 0, 2, 2);
    FUN_803c88b0(5, 0, 0, 0, 0, 2, 2);
    FUN_803c6aac(0);
    gx_cached_set_tev_color_op(0, 0xff, 0xff, 4);
    FUN_803cab98(0, 1, 0, 0);
    FUN_803c766c(0);
    gx_cached_set_tev_order(1, 1, 4, 0x36);

    unsigned int halfW = ((unsigned int)((int)DAT_806109c4 >> 1) +
                          (unsigned int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0)) & 0xffff;
    unsigned int halfH = ((unsigned int)((int)DAT_806109c8 >> 1) +
                          (unsigned int)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0)) & 0xffff;

    int efbTex = FUN_803c8960(halfW, 0x100, 4, '\0', 0);
    FUN_803c8960(halfW, 0x100, 1, '\0', 0);

    // occlusion-query EFB copy textures: allocate their backing data.
    int occHeap = FUN_803c9040(0x80590bf0);   // -0x7fa70410
    if (occHeap == 0) {
        int *d0 = FUN_80398f4c(DAT_806113c0, efbTex);
        FUN_803c8b84(DAT_8058fbf0, (unsigned int)d0, halfW, 0x100, 4, 0, 0, '\0');
        int *d1 = FUN_80398f4c(DAT_806113c0, efbTex);
        FUN_803c8b84(DAT_8058fbd0, (unsigned int)d1, halfW, halfH, 4, 0, 0, '\0');
    } else {
        unsigned int d0 = FUN_803c9040(0x80590bf0);
        FUN_803c8b84(DAT_8058fbf0, d0, halfW, 0x100, 4, 0, 0, '\0');
        unsigned int d1 = FUN_803c9040(0x80590bd0);   // -0x7fa70430
        FUN_803c8b84(DAT_8058fbd0, d1, halfW, halfH, 4, 0, 0, '\0');
    }
    unsigned int dgrid;
    dgrid = FUN_803c9040(0x80590bd0);
    FUN_803c8b84(DAT_8058fbb0, dgrid, halfW, halfH, 5, 0, 0, '\0');
    dgrid = FUN_803c9040(0x80590bd0);
    FUN_803c8b84(DAT_8058fb90, dgrid, halfW, halfH, 1, 0, 0, '\0');
    dgrid = FUN_803c9040(0x80590bd0);
    FUN_803c8b84(DAT_8058fb70, dgrid, halfW, halfH, 4, 0, 0, '\0');
    FUN_803c8b84(DAT_80598f1c, 0x805928a0, 0x40, 0x40, 6, 0, 0, '\0');
    FUN_803c9288(DAT_80598f1c, 7);

    FUN_803ca39c(1, 3, 2, 1, 0);
    FUN_803ca39c(2, 0, 1, 2, 2);
    FUN_803ca39c(3, 2, 3, 0, 1);

    // Fill the 0x40 x 0x40 occlusion-result colour grid (row/col ramp, 0..0xff).
    memset_wrapper(0x805928a0, 0, 0x4000);
    for (int y = 0, ry = 0; y < 0x40; ++y, ry += 0xff) {
        int rowVal = ry / 0x3f;
        if (rowVal > 0xff) rowVal = 0xff;
        for (int x = 0, rx = 0; x < 0x40; ++x, rx += 0xff) {
            unsigned int colVal = (unsigned int)(rx / 0x3f);
            if ((int)colVal > 0xff) colVal = 0xff;
            occ_result_grid_store((unsigned int)x, (unsigned int)y, colVal | (rowVal << 0x18));
        }
    }
    FUN_8039955c(0x805928a0, 0x2000);

    // Install occlusion-query callbacks and re-arm the default render state.
    FUN_803c71c8((void *)occ_query_result_dispatch);
    FUN_803c5a6c((void *)occ_query_poll);
    FUN_803a5f0c((void *)occ_query_flush);
    FUN_803a78ac(1);
    FUN_803a7710();
    viewport_reset_default_bounds();
    screen_scale_update();
    particle_set_pool_sizes();
    gx_reset_render_state();
}
