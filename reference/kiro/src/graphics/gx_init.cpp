// gx_init.cpp - GX (GameCube graphics) subsystem bring-up and CP/GP FIFO driver
// ---------------------------------------------------------------------------
// Non-matching decompilation of the GX core-init layer (main.dol 0x803c3dc0
// range). This is the code that stands up the GameCube graphics pipe: the main
// register/CP init routine (gx_init), the SDK render-state defaults pass
// (GXInit), the command-processor FIFO plumbing (GP/CPU FIFO pointers, limits,
// attach), the abort/draw-done reset path, the vertex-attribute-table (VAT)
// lookup callback, and the CP break/underflow interrupt handler.
//
// The GX write port DAT_cc008000 is the CP FIFO write-gather register: single
// bytes select the opcode (0x08 = load-CP-reg, 0x10 = load-BP-reg, 0x61 =
// load-XF/other), followed by the 32-bit payloads. The decompiler renders the
// sub-byte writes as `DAT_cc008000._0_1_ = ...`; here they are expressed as
// byte/word writes to a volatile pointer with the identical ordering.
//
// NON-MATCHING: the flat DAT_8061c048 (GX state block) + fixed-offset
// arithmetic, the DAT_cc008000 write-gather packing, the (double)CONCAT44
// magic-constant int->float conversions, and the raw register bit masks are
// kept verbatim. Cross-module GX helpers are declared as per-file externs
// matching each call site.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs: raw symbols referenced by this translation unit.
// ---------------------------------------------------------------------------

// GX global state block base (allocated at init) and companion constants.
extern int          DAT_8061c048;      // GX internal state structure base
extern float        FLOAT_8061c04c;
extern float        FLOAT_8061c050;    // 0.0f
extern int          DAT_8061c054;
extern int          DAT_8061c058;
extern int          DAT_8061c05c;
extern float        FLOAT_8061c060;    // 1.0f
extern float        FLOAT_8061c064;
extern double       DOUBLE_8061c068;   // 2^52 int->double bias constant

// CP / PE / VI hardware register block pointers, cached at init.
extern int*         DAT_8061357c;      // CP status/registers mirror (cc000000)
extern int          DAT_80613578;      // PI/GP FIFO regs (cc003000)
extern int          DAT_80613580;      // PE regs (cc001000)
extern int          DAT_80613584;      // MI/CP token regs (cc004000)

// GP/CPU FIFO descriptor tracking + draw-sync state.
extern uint*        DAT_806135a0;      // current GP FIFO descriptor
extern undefined4*  DAT_806135a4;      // current CPU FIFO descriptor
extern char         DAT_806135ac;      // GP==CPU (single-FIFO) flag
extern int          DAT_806135b0;      // draw thread suspended flag
extern void*        DAT_806135a8;      // GX draw-done wait thread
extern int          DAT_806135b4;      // draw-done user callback (code*)
extern int          DAT_806135b8;      // draw-done counter

// Draw-done poll timing state (token stability + timebase window).
extern int          DAT_80613584_tok;  // (unused alias placeholder)
extern int          DAT_80613588;      // last observed CP token value
extern int          DAT_80613590;      // last token time (hi)
extern int          DAT_80613594;      // last token time (lo)
extern int          DAT_80613598;      // draw-done poll primed flag
extern int          DAT_8061359c;      // reset-function registered flag

// Version-blob pointer + misc init tables.
extern int          DAT_806114d0;
extern uint         DAT_8049f92c;      // fog/AA per-format param table
extern uint         DAT_8049f820;      // default attr-array descriptor
extern uint         DAT_8049f840;      // default VCD/VAT seed
extern uint         DAT_8049f910;      // default projection/viewport seed
extern uint         DAT_8060ed30;      // GX FIFO object (returned handle)
extern int          DAT_800000f8;      // bus clock (for CP throttle divisors)

// The GX write-gather FIFO register (command processor immediate port).
extern volatile unsigned int   DAT_cc008000;
static volatile unsigned char* const kGxFifoByte = (volatile unsigned char*)&DAT_cc008000;

// Render-mode format descriptor tables selected by TV format in GXInit.
extern int DAT_8049fb60;   // NTSC
extern int DAT_8049fbd8;   // MPAL
extern int DAT_8049fc14;   // PAL
extern int DAT_8049fc50;   // GCA / EURGB60
extern int LAB_803c3ebc;   // abort/draw-done reset entry inside gx_abort_handler

// OS helpers.
extern int      os_read_time_base(void);
extern void     os_register_version(undefined8, undefined8, undefined8, undefined8,
                                     undefined8, undefined8, undefined8, undefined8,
                                     int, int, undefined4, undefined4, undefined4,
                                     undefined4, undefined4, undefined4);
extern void     os_register_reset_function(int);
extern uint     os_get_hid2(void);
extern void     os_set_hid2(uint);
extern void     os_set_wpar(uint);
extern void     os_disable_interrupts(void);
extern void     os_get_interrupts_enabled(void);
extern void     os_syscall(void);
extern void     os_resume_thread(void*);
extern void     os_suspend_thread(void*);
extern void     os_clear_context(int);
extern void     os_set_current_context(uint);
extern int      vi_get_tv_format(void);

// Cross-module GX helpers (raw addresses; typed per call site).
extern int  FUN_803c906c(int);                       // VAT: current format index
extern uint FUN_803c908c(int);                       // VAT: alt-descriptor flag
extern void FUN_803c5a6c(int);
extern void FUN_803c71c8(int);
extern void FUN_803c7294(int);
extern void FUN_803c6c0c(void);
extern void FUN_803c6ae8(int, int);
extern void FUN_803c5bb8(void);
extern void FUN_803c7358(void);
void gx_flush_indbump_reg(void);   // forward decl; defined below (0x803c9f6c)
extern void FUN_803c93ac(uint*, int, uint, int, uint, int, uint);
extern void FUN_803c94a0(uint*, int, int);
extern void FUN_803c9f3c(int);
extern void FUN_803c9764(int);
extern void FUN_803c5cb0(int, int);
extern void FUN_803c5c80(int, int);
extern void FUN_803c5c4c(char);
extern void FUN_803c5c28(void);
extern void FUN_803c5c04(void);
// GXInit render-state default helpers (all raw addresses).
extern void FUN_803c7e28(undefined1*, int);
extern void FUN_803c682c(int, int, int, int, int, int);
extern void FUN_803c6aac(int);
extern void FUN_803c623c(void);
extern void FUN_803c681c(void);
extern void FUN_803c67d8(uint, int, int);
extern void FUN_803c64d0(uint, int*);
extern void FUN_803c75a4(int, int);
extern void FUN_803c75e4(int, int);
extern void FUN_803c7624(int, int, int);
extern void FUN_803cb1c8(float*, int);
extern void FUN_803cb248(float*, int);
extern void FUN_803cb298(int);
extern void FUN_803cb2cc(float*, int, int);
extern void FUN_803cb468(double, double, double, double, double, double);
extern void FUN_803cb13c(float*);
extern void FUN_803c76b0(int);
extern void FUN_803c766c(int);
extern void FUN_803cb5f8(int);
extern void FUN_803cb540(int, int, uint, uint);
extern void FUN_803cb5b8(int, int);
extern void FUN_803c8874(int);
extern void FUN_803c88b0(int, int, int, int, int, int, int);
extern void FUN_803c86a4(int, uint*);
extern void FUN_803c878c(int, uint*);
extern void FUN_803c94d8(void);
extern void FUN_803c9520(void*);
extern void FUN_803c9534(void*);
extern void FUN_803c8b84(uint*, int, int, int, int, int, int, char);
extern void FUN_803c9288(uint*, int);
extern void FUN_803ca4ec(int, int, int, int);
extern void FUN_803ca688(int);
extern void FUN_803c9f90(int, int);
extern void FUN_803ca41c(int, int, int, int, int);
extern void FUN_803ca460(int, int, int);
extern void FUN_803ca29c(uint, int);
extern void FUN_803ca2f8(uint, int);
extern void FUN_803ca354(uint, int, int);
extern void FUN_803ca39c(int, int, int, int, int);
extern void FUN_803c9ef0(uint);
extern void FUN_803c9ecc(int);
extern void FUN_803c9c9c(int, int, int);
extern void FUN_803ca6b0(double, double, double, double, int, uint*);
extern void FUN_803caa74(int, int, ushort*);
extern void FUN_803cab98(int, int, int, int);
extern void FUN_803cabec(int);
extern void FUN_803cac18(int);
extern void FUN_803cac44(int, int, int);
extern void FUN_803cac78(int);
extern void FUN_803cad80(int);
extern void FUN_803cadac(int, int);
extern void FUN_803cacac(int, int);
extern void FUN_803cade8(int, int);
extern void FUN_803cae20(uint, uint);
extern void FUN_803c784c(int, int, uint, uint);
extern void FUN_803c7944(uint);
extern void FUN_803c7d5c(double);
extern void FUN_803c7acc(int);
extern void FUN_803c7ea0(char, char*, char, char*);
extern void FUN_803c80a8(int);
extern void FUN_803c7aa8(int);
extern void FUN_803c83b0(void);
extern void FUN_803c7154(int);
extern void FUN_803c70d8(int);
extern void FUN_803c7190(int);
extern void FUN_803c70f0(int, int, int, int);
extern void FUN_803c70a4(int, int);
extern void FUN_803c70b8(int);
extern void FUN_803c716c(int, int);
extern void FUN_803c71a8(int, int, int);
extern void FUN_803cb6a4(int, int);
extern void FUN_803cbeec(void);

// Forward declarations for the functions defined below (mutual references).
int   gx_get_vat_entry(int state, uint format);
int   gx_abort_handler(int mode);
void  gx_load_default_bp_regs(void);
void* gx_init(undefined8 a1, undefined8 a2, undefined8 a3, undefined8 a4,
              undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8,
              int fifo_size, uint fifo_hi, undefined4 v11, undefined4 v12,
              undefined4 v13, undefined4 v14, undefined4 v15, undefined4 v16);
void  GXInit(void);
void  gx_cp_interrupt_handler(undefined4 arg, uint prev_ctx);
void  gx_setup_fifo_base(int* fifo, int base, uint size);
void  gx_fifo_set_rw_ptrs(int fifo, int read_ptr, int write_ptr);
void  gx_fifo_set_limits(int fifo, undefined4 lo, undefined4 hi);
void  gx_attach_cpu_fifo(uint* fifo);
void  gx_attach_gp_fifo(undefined4* fifo);
void  gx_save_gp_fifo(int* fifo);
void  gx_read_gp_fifo_ptrs(int* fifo);
void  gx_get_gp_read_ptr(int* fifo);
void  gx_get_cp_status(byte* rdIdle, byte* cmdIdle, byte* readIdle, byte* brk, byte* underflow);
void  gx_get_fifo_status(int* fifo, undefined1* overHi, undefined1* underLo, int* count,
                         undefined1* isGp, undefined1* isCpu, byte* wrap);
void  gx_sync_fifo_ptrs(int fifo, undefined4* readPtrOut, undefined4* writePtrOut);
undefined4 gx_fifo_get_base(undefined4* fifo);
undefined4 gx_fifo_get_size(int fifo);
undefined4 gx_set_draw_done_callback(undefined4 cb);
void  gx_set_cp_break(undefined4 breakAddr);
void  gx_clear_cp_break(void);
void  gx_cp_init(void);
void  gx_cp_fifo_enable(void);
void  gx_cp_fifo_disable(void);
void  gx_cp_set_underflow_irq(char enable);
void  gx_cp_set_watermark_irqs(uint underEn, uint overEn);
void  gx_cp_set_watermark_irq_status(uint underClr, uint overClr);
void  gx_cp_fifo_reset(void);
undefined4 gx_get_current_gp_fifo(void);
undefined4 gx_get_current_cpu_fifo(void);
void  gx_set_vcd_attr(undefined4 attr, uint value);
void  gx_send_vcd(void);
void  gx_calc_vertex_size(void);
void  gx_clear_vcd(void);
void  gx_set_vat_fmt(uint fmtIdx, undefined4 attr, uint cnt, uint type, uint frac);
void  gx_set_vat_fmt_array(uint fmtIdx, int* attrList);
void  gx_send_vat(void);
void  gx_set_array_ptr(int attr, uint base, uint stride);
void  gx_invalidate_vertex_cache(void);

// Cross-module CP draw-sync / flush helper used by the FIFO-attach layer below.
extern void FUN_803c6b7c(void);   // GX draw-sync / CP flush

// Vertex-attribute byte-size lookup tables (component-count per format field).
extern unsigned char DAT_806114d8;   // color-format size table
extern unsigned char DAT_806114dc;   // texcoord-format size table
extern unsigned char DAT_806114e0;   // position/normal-format size table

// CP init/reset support (raw addresses).
extern void os_set_interrupt_handler(int, void*);
extern void os_unmask_interrupts(int);
extern void* os_get_current_thread(void);
extern int  DAT_806135bc;               // last CP break address

// Ghidra CONCAT22 intrinsic: assemble a 32-bit value from a 16-bit high half
// and a 16-bit low half (hi<<16 | lo). Used to recombine the split FIFO-pointer
// register pairs stored in the CP mirror. (common.h provides CONCAT44 only.)
#ifndef CONCAT22
static inline unsigned int gx_concat22(unsigned short hi, unsigned short lo) {
    return ((unsigned int)hi << 16) | (unsigned int)lo;
}
#define CONCAT22(hi, lo) gx_concat22((unsigned short)(hi), (unsigned short)(lo))
#endif

// Address: 0x803c3dc0 | Name: gx_get_vat_entry | Date: 2026-09-15T03:29:00Z
// Return the address of the vertex-attribute-table (VAT) descriptor row that
// matches the current vertex format index and the alt-descriptor flag. The
// three sub-tables at +0x208 / +0x288 / +0x308 (each row 0x10 bytes) cover the
// three coordinate/normal/color configurations; the row index is derived from
// the requested format masked to a multiple of 8.
int gx_get_vat_entry(int state, uint format) {
    int fmt = FUN_803c906c(state);
    uint alt = FUN_803c908c(state);
    int row = (int)format + (((int)format >> 3) +
              (uint)((int)format < 0 && (format & 7) != 0)) * -8;

    if (fmt != 7) {
        if (fmt < 7) {
            if (5 < fmt) {
                if ((alt & 0xff) != 0) {
                    return DAT_8061c048 + row * 0x10 + 0x308;
                }
                return DAT_8061c048 + row * 0x10 + 0x288;
            }
        } else if (fmt < 0xb) {
            return DAT_8061c048 + row * 0x10 + 0x208;
        }
    }
    if ((alt & 0xff) == 0) {
        return DAT_8061c048 + row * 0x10 + 0x208;
    }
    return DAT_8061c048 + row * 0x10 + 0x288;
}

// Address: 0x803c3ee0 | Name: gx_abort_handler | Date: 2026-09-15T03:29:00Z
// Dual-purpose GX draw-sync/abort routine.
//   mode == 0 : draw-done poller. Primes on first call (latch the stable CP
//               token from cc004000+0x4e and record the timebase), then on
//               later calls waits until at least the min interval has elapsed
//               and the token stops advancing before reporting completion.
//   mode != 0 : hard abort. Detach the GP/CPU FIFOs, drain the CP FIFO write
//               port with eight zero words, syscall, reset the CP status
//               mirror, and re-run the boot BP-register load. (The entry at
//               LAB_803c3ebc is registered as GXInit's abort callback.)
int gx_abort_handler(int mode) {
    if (mode == 0) {
        if (DAT_80613598 == 0) {
            short* tok = (short*)(DAT_80613584 + 0x4e);
            short prev = *tok;
            bool changed;
            do {
                DAT_80613588 = *(int*)tok;
                changed = *tok != prev;
                prev = *tok;
            } while (changed);
            {
                unsigned long long t = (unsigned long long)os_read_time_base();
                DAT_80613590 = (int)(t >> 0x20);
                DAT_80613594 = (int)t;
            }
            DAT_80613598 = 1;
            return 0;
        }
        {
            unsigned long long t = (unsigned long long)os_read_time_base();
            int  now_hi = (int)(t >> 0x20);
            uint now_lo = (uint)t;
            int* tok = (int*)(DAT_80613584 + 0x4e);
            short prev = *(short*)tok;
            bool changed;
            do {
                changed = *(short*)tok != prev;
                prev = *(short*)tok;
            } while (changed);

            if ((now_hi - ((uint)(now_lo < (uint)DAT_80613594) + DAT_80613590) ^ 0x80000000) <
                ((uint)((now_lo - (uint)DAT_80613594) < 10) + 0x80000000)) {
                return 0;
            }
            if (*tok != DAT_80613588) {
                DAT_80613588 = *tok;
                DAT_80613590 = now_hi;
                DAT_80613594 = now_lo;
                return 0;
            }
        }
    } else {
        FUN_803c5a6c(0);
        FUN_803c71c8(0);
        FUN_803c7294(0);
        // Drain the CP FIFO write port: eight zero words.
        for (int i = 0; i < 8; ++i) {
            DAT_cc008000 = 0;
        }
        os_syscall();
        *(short*)((char*)DAT_8061357c + 2) = 0;
        *(short*)((char*)DAT_8061357c + 4) = 3;
        *(unsigned char*)(DAT_8061c048 + 0x5aa) = 1;
        FUN_803c6c0c();
    }
    return 1;
}

// Address: 0x803c4070 | Name: gx_load_default_bp_regs | Date: 2026-09-15T03:29:00Z
// Stream the default BP (blitting-processor) register block through the CP
// write-gather port. Two passes of four texture/tev slots each write the
// 0x40000000/0x80000000-tagged register words from the GX state block, then a
// short tail programs the copy-filter / EFB defaults and the final 0x5800000f
// draw-done token setup.
void gx_load_default_bp_regs(void) {
    unsigned char slot = 0;
    int off = 0;
    int pass = 2;
    do {
        int base = DAT_8061c048;
        int a = off + 0x3c;
        *(uint*)(DAT_8061c048 + off + 0x1c) =
            *(uint*)(DAT_8061c048 + off + 0x1c) & 0xbfffffff | 0x40000000;
        *(uint*)(base + a) = *(uint*)(base + a) & 0x7fffffff | 0x80000000;
        kGxFifoByte[0] = 8;
        kGxFifoByte[0] = slot | 0x80;
        int b = off + 0x40;
        DAT_cc008000 = *(uint*)(base + a);
        *(uint*)(base + off + 0x20) = *(uint*)(base + off + 0x20) & 0xbfffffff | 0x40000000;
        *(uint*)(base + b) = *(uint*)(base + b) & 0x7fffffff | 0x80000000;
        kGxFifoByte[0] = 8;
        kGxFifoByte[0] = (unsigned char)(slot + 1) | 0x80;
        unsigned char slot3 = slot + 3;
        a = off + 0x44;
        DAT_cc008000 = *(uint*)(base + b);
        *(uint*)(base + off + 0x24) = *(uint*)(base + off + 0x24) & 0xbfffffff | 0x40000000;
        int c = off + 0x28;
        *(uint*)(base + a) = *(uint*)(base + a) & 0x7fffffff | 0x80000000;
        kGxFifoByte[0] = 8;
        kGxFifoByte[0] = (unsigned char)(slot + 2) | 0x80;
        slot = slot + 4;
        b = off + 0x48;
        off = off + 0x10;
        DAT_cc008000 = *(uint*)(base + a);
        *(uint*)(base + c) = *(uint*)(base + c) & 0xbfffffff | 0x40000000;
        *(uint*)(base + b) = *(uint*)(base + b) & 0x7fffffff | 0x80000000;
        kGxFifoByte[0] = 8;
        kGxFifoByte[0] = slot3 | 0x80;
        DAT_cc008000 = *(uint*)(base + b);
        pass = pass + -1;
    } while (pass != 0);

    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = 0x1000;
    DAT_cc008000 = 0x3f;
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = 0x1012;
    DAT_cc008000 = 1;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = 0x5800000f;
}

// Address: 0x803c4214 | Name: gx_init | Date: 2026-09-15T03:29:00Z
// Main GX subsystem bring-up. Registers the kernel version blob, resets the
// draw-sync flags, caches the CP/PE/PI/MI hardware register bases, sets up the
// GP FIFO from the caller-supplied buffer (fifo_size / fifo_hi), enables the
// write-gather pipe in HID2, programs the full default CP/XF/BP register set
// (attribute tables, texture-region/tlut-region allocations, CP throttle
// divisors derived from the bus clock), and finally hands off to GXInit for the
// render-state defaults. Returns the GX FIFO object handle (&DAT_8060ed30).
void* gx_init(undefined8 a1, undefined8 a2, undefined8 a3, undefined8 a4,
              undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8,
              int fifo_size, uint fifo_hi, undefined4 v11, undefined4 v12,
              undefined4 v13, undefined4 v14, undefined4 v15, undefined4 v16) {
    os_register_version(a1, a2, a3, a4, a5, a6, a7, a8, DAT_806114d0,
                        0x80610000, v11, v12, v13, v14, v15, v16);
    int st = DAT_8061c048;
    *(unsigned char*)(DAT_8061c048 + 0x5a8) = 0;
    *(unsigned char*)(st + 0x5a9) = 1;
    *(unsigned char*)(st + 0x5aa) = 1;
    *(undefined4*)(st + 0x594) = 0;
    *(undefined4*)(st + 0x598) = 0;
    FUN_803c6ae8(1, 0);
    DAT_8061357c = (int*)0xcc000000;
    DAT_80613578 = (int)0xcc003000;
    DAT_80613580 = (int)0xcc001000;
    DAT_80613584 = (int)0xcc004000;
    FUN_803c5bb8();
    gx_setup_fifo_base((int*)&DAT_8060ed30, fifo_size, fifo_hi);
    gx_attach_cpu_fifo((uint*)&DAT_8060ed30);
    gx_attach_gp_fifo((undefined4*)&DAT_8060ed30);
    if (DAT_8061359c == 0) {
        os_register_reset_function(-0x7fb60614);  // &gx reset thunk 0x8049f9ec
        DAT_8061359c = 1;
    }
    FUN_803c7358();
    uint hid2 = os_get_hid2();
    os_set_wpar(0xc008000);
    os_set_hid2(hid2 | 0x40000000);

    st = DAT_8061c048;
    int off = 0;
    uint idx = 0;
    *(undefined4*)(DAT_8061c048 + 0x204) = 0;
    *(uint*)(st + 0x204) = *(uint*)(st + 0x204) & 0xffffff;
    int reg0 = 0xc0;
    *(undefined4*)(st + 0x124) = 0xff;
    int reg1 = 0xc1;
    *(uint*)(st + 0x124) = *(uint*)(st + 0x124) & 0xffffff | 0xf000000;
    *(undefined4*)(st + 0x7c) = 0;
    *(uint*)(st + 0x7c) = *(uint*)(st + 0x7c) & 0xffffff | 0x22000000;

    int st1 = st;
    int loop = 8;
    do {
        st1 = DAT_8061c048;
        uint half = idx >> 1;
        int m0 = off + 0x130;
        *(undefined4*)(DAT_8061c048 + m0) = 0;
        int m1 = off + 0x170;
        *(undefined4*)(st1 + m1) = 0;
        int t0 = half * 4 + 0x100;
        *(undefined4*)(st1 + t0) = 0;
        int t1 = half * 4 + 0x1b0;
        *(undefined4*)(st1 + off + 0x554) = 0xff;
        *(uint*)(st1 + m0) = reg0 << 0x18 | *(uint*)(st1 + m0) & 0xffffff;
        uint half2 = (idx + 1) >> 1;
        int m0b = off + 0x134;
        *(uint*)(st1 + m1) = reg1 << 0x18 | *(uint*)(st1 + m1) & 0xffffff;
        int m1b = off + 0x174;
        int cb = off + 0x558;
        *(uint*)(st1 + t1) = (half + 0xf6) * 0x1000000 | *(uint*)(st1 + t1) & 0xffffff;
        int reg0b = reg0 + 2;
        int reg1b = reg1 + 2;
        int t1b = half2 * 4 + 0x1b0;
        *(uint*)(st1 + t0) = (half + 0x28) * 0x1000000 | *(uint*)(st1 + t0) & 0xffffff;
        uint t0b = half2 * 4 + 0x100;
        *(undefined4*)(st1 + m0b) = 0;
        off = off + 8;
        *(undefined4*)(st1 + m1b) = 0;
        idx = idx + 2;
        *(undefined4*)(st1 + t0b) = 0;
        *(undefined4*)(st1 + cb) = 0xff;
        reg0 = reg0 + 4;
        *(uint*)(st1 + m0b) = reg0b * 0x1000000 | *(uint*)(st1 + m0b) & 0xffffff;
        reg1 = reg1 + 4;
        *(uint*)(st1 + m1b) = reg1b * 0x1000000 | *(uint*)(st1 + m1b) & 0xffffff;
        *(uint*)(st1 + t1b) = (half2 + 0xf6) * 0x1000000 | *(uint*)(st1 + t1b) & 0xffffff;
        *(uint*)(st1 + t0b) = (half2 + 0x28) * 0x1000000 | *(uint*)(st1 + t0b) & 0xffffff;
        loop = loop + -1;
    } while (loop != 0);

    *(undefined4*)(st1 + 0x120) = 0;
    off = 0;
    *(uint*)(st1 + 0x120) = *(uint*)(st1 + 0x120) & 0xffffff | 0x27000000;
    int r0 = 0x30;
    int r1 = 0x31;
    loop = 2;
    do {
        st1 = DAT_8061c048;
        int a0 = off + 0xb8;
        int b0 = off + 0xd8;
        *(undefined4*)(DAT_8061c048 + a0) = 0;
        *(undefined4*)(st1 + b0) = 0;
        *(uint*)(st1 + a0) = r0 << 0x18 | *(uint*)(st1 + a0) & 0xffffff;
        a0 = off + 0xbc;
        *(uint*)(st1 + b0) = r1 << 0x18 | *(uint*)(st1 + b0) & 0xffffff;
        b0 = off + 0xdc;
        *(undefined4*)(st1 + a0) = 0;
        *(undefined4*)(st1 + b0) = 0;
        *(uint*)(st1 + a0) = (r0 + 2) * 0x1000000 | *(uint*)(st1 + a0) & 0xffffff;
        a0 = off + 0xc0;
        *(uint*)(st1 + b0) = (r1 + 2) * 0x1000000 | *(uint*)(st1 + b0) & 0xffffff;
        b0 = off + 0xe0;
        *(undefined4*)(st1 + a0) = 0;
        *(undefined4*)(st1 + b0) = 0;
        int r0c = r0 + 6;
        *(uint*)(st1 + a0) = (r0 + 4) * 0x1000000 | *(uint*)(st1 + a0) & 0xffffff;
        a0 = off + 0xc4;
        int r1c = r1 + 6;
        *(uint*)(st1 + b0) = (r1 + 4) * 0x1000000 | *(uint*)(st1 + b0) & 0xffffff;
        b0 = off + 0xe4;
        off = off + 0x10;
        *(undefined4*)(st1 + a0) = 0;
        *(undefined4*)(st1 + b0) = 0;
        r0 = r0 + 8;
        *(uint*)(st1 + a0) = r0c * 0x1000000 | *(uint*)(st1 + a0) & 0xffffff;
        r1 = r1 + 8;
        *(uint*)(st1 + b0) = r1c * 0x1000000 | *(uint*)(st1 + b0) & 0xffffff;
        loop = loop + -1;
    } while (loop != 0);

    *(uint*)(st1 + 0xf8)  = *(uint*)(st1 + 0xf8)  & 0xffffff | 0x20000000;
    *(uint*)(st1 + 0xfc)  = *(uint*)(st1 + 0xfc)  & 0xffffff | 0x21000000;
    *(uint*)(st1 + 0x1d0) = *(uint*)(st1 + 0x1d0) & 0xffffff | 0x41000000;
    uint di = 0;
    *(uint*)(st1 + 0x1d4) = *(uint*)(st1 + 0x1d4) & 0xffffff | 0x42000000;
    *(uint*)(st1 + 0x1d8) = *(uint*)(st1 + 0x1d8) & 0xffffff | 0x40000000;
    *(uint*)(st1 + 0x1dc) = *(uint*)(st1 + 0x1dc) & 0xffffff | 0x43000000;
    *(uint*)(st1 + 0x1fc) = *(uint*)(st1 + 0x1fc) & 0xfffffe7f;
    *(float*)(st1 + 0x510) = FLOAT_8061c04c;
    *(float*)(st1 + 0x50c) = FLOAT_8061c050;
    *(undefined4*)(st1 + 0x5ac) = 0;
    *(unsigned char*)(st1 + 0x5ab) = 0;

    uint clkdiv = (uint)DAT_800000f8 / 500;
    gx_flush_indbump_reg();
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = clkdiv >> 0xb | 0x69000400;
    gx_flush_indbump_reg();
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = clkdiv / 0x1080 | 0x46000200;
    gx_load_default_bp_regs();

    // Seed the eight blend/logic-op register triplets from the per-mode table.
    uint* tbl = &DAT_8049f92c;
    off = 0;
    do {
        FUN_803c93ac((uint*)(DAT_8061c048 + off + 0x208), 0, tbl[0], 0, tbl[8], 0, di);
        FUN_803c93ac((uint*)(DAT_8061c048 + off + 0x288), 0, tbl[0x10], 0, tbl[0x18], 0, di);
        FUN_803c93ac((uint*)(DAT_8061c048 + off + 0x308), 1, tbl[0x20], 0, tbl[0x28], 0, di);
        tbl = tbl + 1;
        off = off + 0x10;
        di = di + 1;
    } while (di < 8);

    // Texture-region allocations: 16 32KB regions then 4 128KB regions.
    idx = 0;
    int tmem = 0xc0000;
    off = 0;
    do {
        FUN_803c94a0((uint*)(DAT_8061c048 + off + 0x388), tmem, 0x10);
        tmem = tmem + 0x2000;
        off = off + 0x10;
        idx = idx + 1;
    } while (idx < 0x10);
    idx = 0;
    tmem = 0xe0000;
    do {
        FUN_803c94a0((uint*)(DAT_8061c048 + (idx + 0x10) * 0x10 + 0x388), tmem, 0x40);
        tmem = tmem + 0x8000;
        idx = idx + 1;
    } while (idx < 4);

    *(short*)((char*)DAT_8061357c + 6) = 0;
    st1 = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x5a4) = *(uint*)(DAT_8061c048 + 0x5a4) & 0xffffff0f;
    kGxFifoByte[0] = 8;
    kGxFifoByte[0] = 0x20;
    DAT_cc008000 = *(uint*)(st1 + 0x5a4);
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = 0x1006;
    DAT_cc008000 = 0;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = 0x23000000;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = 0x24000000;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = 0x67000000;
    FUN_803c9f3c(0);
    FUN_803c9764(2);
    GXInit();
    return &DAT_8060ed30;
}

// Address: 0x803c4814 | Name: GXInit | Date: 2026-09-15T03:29:00Z
// Render-state defaults pass, run at the tail of gx_init. Selects the render-
// mode format descriptor by TV format, then walks the entire GX default state:
// clears the EFB, sets the 8 attribute-array descriptors, texture coord gens,
// TEV stages, projection/viewport/scissor, copy filter, Z/blend modes, fog,
// light channels, and finally registers the draw-done (gx_abort_handler) and
// VAT (gx_get_vat_entry) callbacks.
void GXInit(void) {
    int* puVar1;
    int iVar2;
    uint uVar3;
    double dVar4;
    uint local_98, local_94, local_90, local_8c, local_88;
    undefined4 local_84;
    uint local_80, local_7c;
    undefined4 local_78;
    float local_74, local_70, local_6c, local_68, local_64, local_60;
    float local_5c, local_58, local_54, local_50, local_4c, local_48;
    uint auStack_44[9];
    undefined4 local_20;
    uint uStack_1c;
    undefined4 local_18;
    uint uStack_14;

    local_78 = DAT_8061c054;
    local_7c = DAT_8061c058;
    local_80 = DAT_8061c05c;
    iVar2 = vi_get_tv_format();
    if (iVar2 == 2) {
        puVar1 = &DAT_8049fbd8;
    } else if (iVar2 < 2) {
        if (iVar2 == 0) {
            puVar1 = &DAT_8049fb60;
        } else if (-1 < iVar2) {
            puVar1 = &DAT_8049fc14;
        } else {
            puVar1 = &DAT_8049fb60;
        }
    } else if (iVar2 == 5) {
        puVar1 = &DAT_8049fc50;
    } else {
        puVar1 = &DAT_8049fb60;
    }
    char* mode = (char*)puVar1;

    local_84 = local_78;
    FUN_803c7e28((undefined1*)&local_84, 0xffffff);
    FUN_803c682c(0, 1, 4, 0x3c, 0, 0x7d);
    FUN_803c682c(1, 1, 5, 0x3c, 0, 0x7d);
    FUN_803c682c(2, 1, 6, 0x3c, 0, 0x7d);
    FUN_803c682c(3, 1, 7, 0x3c, 0, 0x7d);
    FUN_803c682c(4, 1, 8, 0x3c, 0, 0x7d);
    FUN_803c682c(5, 1, 9, 0x3c, 0, 0x7d);
    FUN_803c682c(6, 1, 10, 0x3c, 0, 0x7d);
    FUN_803c682c(7, 1, 0xb, 0x3c, 0, 0x7d);
    FUN_803c6aac(1);
    FUN_803c623c();
    FUN_803c681c();
    uVar3 = 9;
    do {
        FUN_803c67d8(uVar3, DAT_8061c048, 0);
        uVar3 = uVar3 + 1;
    } while (uVar3 < 0x19);
    uVar3 = 0;
    do {
        FUN_803c64d0(uVar3, (int*)&DAT_8049f840);
        uVar3 = uVar3 + 1;
    } while (uVar3 < 8);
    FUN_803c75a4(6, 0);
    FUN_803c75e4(6, 0);
    FUN_803c7624(0, 0, 0);
    FUN_803c7624(1, 0, 0);
    FUN_803c7624(2, 0, 0);
    FUN_803c7624(3, 0, 0);
    FUN_803c7624(4, 0, 0);
    FUN_803c7624(5, 0, 0);
    FUN_803c7624(6, 0, 0);
    FUN_803c7624(7, 0, 0);
    local_74 = FLOAT_8061c060;
    local_70 = FLOAT_8061c050;
    local_6c = FLOAT_8061c050;
    local_68 = FLOAT_8061c050;
    local_64 = FLOAT_8061c050;
    local_60 = FLOAT_8061c060;
    local_5c = FLOAT_8061c050;
    local_58 = FLOAT_8061c050;
    local_54 = FLOAT_8061c050;
    local_50 = FLOAT_8061c050;
    local_4c = FLOAT_8061c060;
    local_48 = FLOAT_8061c050;
    FUN_803cb1c8(&local_74, 0);
    FUN_803cb248(&local_74, 0);
    FUN_803cb298(0);
    FUN_803cb2cc(&local_74, 0x3c, 0);
    FUN_803cb2cc(&local_74, 0x7d, 0);
    uStack_14 = (uint)*(unsigned short*)(mode + 4);
    uStack_1c = (uint)*(unsigned short*)(mode + 8);
    dVar4 = (double)FLOAT_8061c050;
    local_18 = 0x43300000;
    local_20 = 0x43300000;
    FUN_803cb468(dVar4, dVar4,
                 (double)(float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_8061c068),
                 (double)(float)((double)CONCAT44(0x43300000, uStack_1c) - DOUBLE_8061c068),
                 dVar4, (double)FLOAT_8061c060);
    FUN_803cb13c((float*)&DAT_8049f910);
    FUN_803c76b0(0);
    FUN_803c766c(2);
    FUN_803cb5f8(0);
    FUN_803cb540(0, 0, (uint)*(unsigned short*)(mode + 4), (uint)*(unsigned short*)(mode + 6));
    FUN_803cb5b8(0, 0);
    FUN_803c8874(0);
    FUN_803c88b0(4, 0, 0, 1, 0, 0, 2);
    local_88 = local_7c;
    FUN_803c86a4(4, &local_88);
    local_8c = local_80;
    FUN_803c878c(4, &local_8c);
    FUN_803c88b0(5, 0, 0, 1, 0, 0, 2);
    local_90 = local_7c;
    FUN_803c86a4(5, &local_90);
    local_94 = local_80;
    FUN_803c878c(5, &local_94);
    FUN_803c94d8();
    FUN_803c9520((void*)gx_get_vat_entry);
    FUN_803c9534((void*)&LAB_803c3ebc);
    FUN_803c8b84(auStack_44, (int)&DAT_8049f820, 4, 4, 3, 0, 0, '\0');
    FUN_803c9288(auStack_44, 0);
    FUN_803c9288(auStack_44, 1);
    FUN_803c9288(auStack_44, 2);
    FUN_803c9288(auStack_44, 3);
    FUN_803c9288(auStack_44, 4);
    FUN_803c9288(auStack_44, 5);
    FUN_803c9288(auStack_44, 6);
    FUN_803c9288(auStack_44, 7);
    FUN_803ca4ec(0, 0, 0, 4);
    FUN_803ca4ec(1, 1, 1, 4);
    FUN_803ca4ec(2, 2, 2, 4);
    FUN_803ca4ec(3, 3, 3, 4);
    FUN_803ca4ec(4, 4, 4, 4);
    FUN_803ca4ec(5, 5, 5, 4);
    FUN_803ca4ec(6, 6, 6, 4);
    FUN_803ca4ec(7, 7, 7, 4);
    FUN_803ca4ec(8, 0xff, 0xff, 0xff);
    FUN_803ca4ec(9, 0xff, 0xff, 0xff);
    FUN_803ca4ec(10, 0xff, 0xff, 0xff);
    FUN_803ca4ec(0xb, 0xff, 0xff, 0xff);
    FUN_803ca4ec(0xc, 0xff, 0xff, 0xff);
    FUN_803ca4ec(0xd, 0xff, 0xff, 0xff);
    FUN_803ca4ec(0xe, 0xff, 0xff, 0xff);
    FUN_803ca4ec(0xf, 0xff, 0xff, 0xff);
    FUN_803ca688(1);
    FUN_803c9f90(0, 3);
    FUN_803ca41c(7, 0, 0, 7, 0);
    FUN_803ca460(0, 0x11, 0);
    uVar3 = 0;
    do {
        FUN_803ca29c(uVar3, 6);
        FUN_803ca2f8(uVar3, 0);
        FUN_803ca354(uVar3, 0, 0);
        uVar3 = uVar3 + 1;
    } while (uVar3 < 0x10);
    FUN_803ca39c(0, 0, 1, 2, 3);
    FUN_803ca39c(1, 0, 0, 0, 3);
    FUN_803ca39c(2, 1, 1, 1, 3);
    FUN_803ca39c(3, 2, 2, 2, 3);
    uVar3 = 0;
    do {
        FUN_803c9ef0(uVar3);
        uVar3 = uVar3 + 1;
    } while (uVar3 < 0x10);
    FUN_803c9ecc(0);
    FUN_803c9c9c(0, 0, 0);
    FUN_803c9c9c(1, 0, 0);
    FUN_803c9c9c(2, 0, 0);
    FUN_803c9c9c(3, 0, 0);
    local_98 = local_7c;
    FUN_803ca6b0((double)FLOAT_8061c050, (double)FLOAT_8061c060, (double)FLOAT_8061c064,
                 (double)FLOAT_8061c060, 0, &local_98);
    FUN_803caa74(0, 0, (ushort*)0x0);
    FUN_803cab98(0, 4, 5, 0);
    FUN_803cabec(1);
    FUN_803cac18(1);
    FUN_803cac44(1, 3, 1);
    FUN_803cac78(1);
    FUN_803cad80(1);
    FUN_803cadac(0, 0);
    FUN_803cacac(0, 0);
    FUN_803cade8(1, 1);
    FUN_803cae20((uint)(unsigned char)mode[0x18],
                 (uint)((uint)*(unsigned short*)(mode + 0x10) ==
                        (uint)*(unsigned short*)(mode + 8) << 1));
    FUN_803c784c(0, 0, (uint)*(unsigned short*)(mode + 4), (uint)*(unsigned short*)(mode + 6));
    FUN_803c7944((uint)*(unsigned short*)(mode + 4));
    uStack_1c = (uint)*(unsigned short*)(mode + 8);
    uStack_14 = (uint)*(unsigned short*)(mode + 6);
    local_20 = 0x43300000;
    local_18 = 0x43300000;
    FUN_803c7d5c((double)((float)((double)CONCAT44(0x43300000, uStack_1c) - DOUBLE_8061c068) /
                          (float)((double)CONCAT44(0x43300000, uStack_14) - DOUBLE_8061c068)));
    FUN_803c7acc(3);
    FUN_803c7ea0(mode[0x19], mode + 0x1a, '\x01', mode + 0x32);
    FUN_803c80a8(0);
    FUN_803c7aa8(0);
    FUN_803c83b0();
    FUN_803c7154(1);
    FUN_803c70d8(1);
    FUN_803c7190(0);
    FUN_803c70f0(0, 0, 1, 0xf);
    FUN_803c70a4(7, 0);
    FUN_803c70b8(1);
    FUN_803c716c(0, 0);
    FUN_803c71a8(1, 7, 1);
    FUN_803cb6a4(0x23, 0x16);
    FUN_803cbeec();
}

// Address: 0x803c514c | Name: gx_cp_interrupt_handler | Date: 2026-09-15T03:29:00Z
// Command-processor interrupt handler. Latches the live CP status from
// cc000000, then services three conditions gated by enable/pending bit pairs:
// FIFO under-watermark (resume the draw thread + arm the low-water line),
// FIFO over-watermark (suspend the draw thread + arm the high-water line), and
// the CP break/token interrupt (clear it and, if a break callback is set, run
// it under a scratch OS context, restoring the previous context afterwards).
void gx_cp_interrupt_handler(undefined4 arg, uint prev_ctx) {
    (void)arg;
    int st = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0xc) = (uint)*DAT_8061357c;

    if (((*(uint*)(st + 8) >> 3 & 1) != 0) && ((*(uint*)(st + 0xc) >> 1 & 1) != 0)) {
        os_resume_thread(DAT_806135a8);
        DAT_806135b0 = 0;
        FUN_803c5cb0(1, 1);
        FUN_803c5c80(1, 0);
    }
    if (((*(uint*)(DAT_8061c048 + 8) >> 2 & 1) != 0) &&
        ((*(uint*)(DAT_8061c048 + 0xc) & 1) != 0)) {
        DAT_806135b8 = DAT_806135b8 + 1;
        FUN_803c5c80(0, 1);
        FUN_803c5cb0(1, 0);
        DAT_806135b0 = 1;
        os_suspend_thread(DAT_806135a8);
    }
    st = DAT_8061c048;
    if (((*(uint*)(DAT_8061c048 + 8) >> 5 & 1) != 0) &&
        ((*(uint*)(DAT_8061c048 + 0xc) >> 4 & 1) != 0)) {
        *(uint*)(DAT_8061c048 + 8) = *(uint*)(DAT_8061c048 + 8) & 0xffffffdf;
        DAT_8061357c[1] = (int)(unsigned short)*(undefined4*)(st + 8);
        if (DAT_806135b4 != 0) {
            unsigned char ctx[716];
            os_clear_context((int)ctx);
            os_set_current_context((uint)ctx);
            ((void (*)(void))DAT_806135b4)();
            os_clear_context((int)ctx);
            os_set_current_context(prev_ctx);
        }
    }
}

// Address: 0x803c5280 | Name: gx_setup_fifo_base | Date: 2026-09-15T03:29:00Z
// Initialize a GX FIFO descriptor from a base pointer and size: store base,
// end (base + size - 4), size, clear the wrap flag, program the high/low
// watermark lines (size-16KB high, size/2 rounded down to a 32-byte multiple
// low), then set the read/write pointers to the base.
void gx_setup_fifo_base(int* fifo, int base, uint size) {
    fifo[0] = base;
    fifo[1] = base + (int)(size - 4);
    fifo[2] = (int)size;
    fifo[7] = 0;
    gx_fifo_set_limits((int)fifo, (int)(size - 0x4000), (int)(size >> 1 & 0x7fffffe0));
    gx_fifo_set_rw_ptrs((int)fifo, base, base);
}

// Address: 0x803c52ec | Name: gx_fifo_set_rw_ptrs | Date: 2026-09-15T03:29:00Z
// Set a FIFO descriptor's read/write pointers and recompute the outstanding
// byte count, wrapping by the FIFO size when the write pointer trails the read
// pointer. Runs with interrupts disabled.
void gx_fifo_set_rw_ptrs(int fifo, int read_ptr, int write_ptr) {
    os_disable_interrupts();
    *(int*)(fifo + 0x14) = read_ptr;
    *(int*)(fifo + 0x18) = write_ptr;
    *(int*)(fifo + 0x1c) = write_ptr - read_ptr;
    if (*(int*)(fifo + 0x1c) < 0) {
        *(int*)(fifo + 0x1c) = *(int*)(fifo + 0x1c) + *(int*)(fifo + 8);
    }
    os_get_interrupts_enabled();
}

// Address: 0x803c535c | Name: gx_fifo_set_limits | Date: 2026-09-15T03:29:00Z
// Store the high/low watermark byte limits into a FIFO descriptor.
void gx_fifo_set_limits(int fifo, undefined4 lo, undefined4 hi) {
    *(undefined4*)(fifo + 0xc) = lo;
    *(undefined4*)(fifo + 0x10) = hi;
}

// Address: 0x803c5368 | Name: gx_attach_cpu_fifo | Date: 2026-09-15T03:29:00Z
// Attach a FIFO as the CPU (write) FIFO. If it is also the current GP (read)
// FIFO, program the PI/GP FIFO base/top/write registers directly and mark the
// single-FIFO fast path; otherwise clear the fast path and program only the
// write-side registers. Runs with interrupts disabled and issues a syscall to
// flush the CP.
void gx_attach_cpu_fifo(uint* fifo) {
    os_disable_interrupts();
    DAT_806135a0 = fifo;
    if (fifo == DAT_806135a4) {
        *(uint*)(DAT_80613578 + 0xc) = *fifo & 0x3fffffff;
        *(uint*)(DAT_80613578 + 0x10) = fifo[1] & 0x3fffffff;
        *(uint*)(DAT_80613578 + 0x14) = fifo[6] & 0x3ffffe0;
        DAT_806135ac = 1;
        FUN_803c5cb0(1, 1);
        FUN_803c5c80(1, 0);
        FUN_803c5c4c('\x01');
    } else {
        if (DAT_806135ac != 0) {
            FUN_803c5c4c('\0');
            DAT_806135ac = 0;
        }
        FUN_803c5c80(0, 0);
        *(uint*)(DAT_80613578 + 0xc) = *fifo & 0x3fffffff;
        *(uint*)(DAT_80613578 + 0x10) = fifo[1] & 0x3fffffff;
        *(uint*)(DAT_80613578 + 0x14) = fifo[6] & 0x3ffffe0;
    }
    os_syscall();
    os_get_interrupts_enabled();
}

// Address: 0x803c5490 | Name: gx_attach_gp_fifo | Date: 2026-09-15T07:55:00Z
// Attach a FIFO as the GP (read/graphics-processor) FIFO. Flushes any pending
// CP work, records the descriptor as the current GP FIFO, then programs the CP
// FIFO base/top/high-watermark/low-watermark/read-write pointer register pairs
// in the cc000000 CP register mirror (each split into a 16-bit low half and a
// 14-bit high half). If the same descriptor is also the current CPU FIFO the
// single-FIFO fast path is set; otherwise it is cleared. Finally clears the CP
// under/over-watermark status bits and re-enables the CP FIFO. Interrupts
// disabled with a CP-flush syscall.
void gx_attach_gp_fifo(undefined4* fifo) {
    int state;

    os_disable_interrupts();
    FUN_803c5c28();
    FUN_803c5c80(0, 0);
    DAT_806135a4 = fifo;
    *(short*)(DAT_8061357c + 0x20) = (short)fifo[0];
    *(short*)(DAT_8061357c + 0x24) = (short)fifo[1];
    *(short*)(DAT_8061357c + 0x30) = (short)fifo[7];
    *(short*)(DAT_8061357c + 0x34) = (short)fifo[6];
    *(short*)(DAT_8061357c + 0x38) = (short)fifo[5];
    *(short*)(DAT_8061357c + 0x28) = (short)fifo[3];
    *(short*)(DAT_8061357c + 0x2c) = (short)fifo[4];
    *(ushort*)(DAT_8061357c + 0x22) = (ushort)((uint)fifo[0] >> 0x10) & 0x3fff;
    *(ushort*)(DAT_8061357c + 0x26) = (ushort)((uint)fifo[1] >> 0x10) & 0x3fff;
    *(short*)(DAT_8061357c + 0x32) = (short)((uint)fifo[7] >> 0x10);
    *(ushort*)(DAT_8061357c + 0x36) = (ushort)((uint)fifo[6] >> 0x10) & 0x3fff;
    *(ushort*)(DAT_8061357c + 0x3a) = (ushort)((uint)fifo[5] >> 0x10) & 0x3fff;
    *(short*)(DAT_8061357c + 0x2a) = (short)((uint)fifo[3] >> 0x10);
    *(short*)(DAT_8061357c + 0x2e) = (short)((uint)fifo[4] >> 0x10);
    os_syscall();
    if (DAT_806135a0 == (uint*)DAT_806135a4) {
        DAT_806135ac = 1;
        FUN_803c5c80(1, 0);
        FUN_803c5c4c('\x01');
    } else {
        DAT_806135ac = 0;
        FUN_803c5c80(0, 0);
        FUN_803c5c4c('\0');
    }
    state = DAT_8061c048;
    *(ushort*)(DAT_8061357c + 2) = (ushort)*(undefined4*)(DAT_8061c048 + 8) & 0xffdd;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
    FUN_803c5cb0(1, 1);
    FUN_803c5c04();
    os_get_interrupts_enabled();
}

// Address: 0x803c5630 | Name: gx_save_gp_fifo | Date: 2026-09-15T07:55:00Z
// Flush pending CP work (gx draw-sync) then snapshot the GP FIFO pointers back
// into the descriptor. Thin wrapper: draw-sync + gx_read_gp_fifo_ptrs.
void gx_save_gp_fifo(int* fifo) {
    FUN_803c6b7c();
    gx_read_gp_fifo_ptrs(fifo);
}

// Address: 0x803c5664 | Name: gx_read_gp_fifo_ptrs | Date: 2026-09-15T07:55:00Z
// Read the current GP FIFO base/top/write-pointer registers back out of the CP
// mirror into the descriptor (undoing the +0x80000000 physical-address bias and
// masking off the wrap bit 0x04000000). When in single-FIFO mode the read
// pointer/count come from the CP read-pointer register pair instead; otherwise
// the outstanding count is derived from write-read (wrapping by FIFO size).
void gx_read_gp_fifo_ptrs(int* fifo) {
    os_disable_interrupts();
    fifo[0] = *(int*)(DAT_80613578 + 0xc) + -0x80000000;
    fifo[1] = *(int*)(DAT_80613578 + 0x10) + -0x80000000;
    fifo[6] = (*(uint*)(DAT_80613578 + 0x14) & 0xfbffffff) + 0x80000000;
    if (DAT_806135ac == '\0') {
        fifo[7] = fifo[6] - fifo[5];
        if (fifo[7] < 0) {
            fifo[7] = fifo[7] + fifo[2];
        }
    } else {
        fifo[5] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x3a),
                           *(undefined2*)(DAT_8061357c + 0x38)) + -0x80000000;
        fifo[7] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x32),
                           *(undefined2*)(DAT_8061357c + 0x30));
    }
    os_get_interrupts_enabled();
}

// Address: 0x803c572c | Name: gx_get_gp_read_ptr | Date: 2026-09-15T07:55:00Z
// Read just the GP FIFO read pointer + outstanding count from the CP read-ptr
// register pair into the descriptor. In single-FIFO mode also refresh the
// base/top/write registers from the PI/GP FIFO regs.
void gx_get_gp_read_ptr(int* fifo) {
    os_disable_interrupts();
    fifo[5] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x3a),
                       *(undefined2*)(DAT_8061357c + 0x38)) + -0x80000000;
    fifo[7] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x32),
                       *(undefined2*)(DAT_8061357c + 0x30));
    if (DAT_806135ac != '\0') {
        fifo[0] = *(int*)(DAT_80613578 + 0xc) + -0x80000000;
        fifo[1] = *(int*)(DAT_80613578 + 0x10) + -0x80000000;
        fifo[6] = (*(uint*)(DAT_80613578 + 0x14) & 0xfbffffff) + 0x80000000;
    }
    os_get_interrupts_enabled();
}

// Address: 0x803c57c8 | Name: gx_get_cp_status | Date: 2026-09-15T07:55:00Z
// Latch the CP status register (cc000000 mirror at state+0xc) and unpack the
// five low status bits into the caller's byte outputs: read-idle, command-idle,
// FIFO-read-idle, CP-break, and FIFO-underflow.
void gx_get_cp_status(byte* rdIdle, byte* cmdIdle, byte* readIdle, byte* brk, byte* underflow) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0xc) = (uint)*DAT_8061357c;
    *rdIdle    = (byte)*(undefined4*)(state + 0xc) & 1;
    *cmdIdle   = (byte)(*(uint*)(state + 0xc) >> 1) & 1;
    *readIdle  = (byte)(*(uint*)(state + 0xc) >> 2) & 1;
    *brk       = (byte)(*(uint*)(state + 0xc) >> 3) & 1;
    *underflow = (byte)(*(uint*)(state + 0xc) >> 4) & 1;
}

// Address: 0x803c5818 | Name: gx_get_fifo_status | Date: 2026-09-15T07:55:00Z
// Full FIFO status query for a descriptor. Refreshes the read pointer/count from
// the CP registers when the descriptor is the current CPU FIFO, and the whole
// base/top/write set when it is the current GP FIFO (with a draw-sync flush).
// Then reports: over-high-watermark, under-low-watermark, current byte count,
// whether the descriptor is the GP FIFO, whether it is the CPU FIFO, and the CP
// FIFO wrap bit. The is-GP / is-CPU booleans are computed from a pointer
// difference via countLeadingZeros>>5 (nonzero difference -> 0, equal -> 1).
void gx_get_fifo_status(int* fifo, undefined1* overHi, undefined1* underLo, int* count,
                        undefined1* isGp, undefined1* isCpu, byte* wrap) {
    uint clz;

    *underLo = 0;
    *overHi = 0;
    *count = 0;
    *wrap = 0;
    if (fifo == (int*)DAT_806135a4) {
        fifo[5] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x3a),
                           *(undefined2*)(DAT_8061357c + 0x38)) + -0x80000000;
        fifo[7] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x32),
                           *(undefined2*)(DAT_8061357c + 0x30));
    }
    if (fifo == (int*)DAT_806135a0) {
        FUN_803c6b7c();
        os_disable_interrupts();
        fifo[0] = *(int*)(DAT_80613578 + 0xc) + -0x80000000;
        fifo[1] = *(int*)(DAT_80613578 + 0x10) + -0x80000000;
        fifo[6] = (*(uint*)(DAT_80613578 + 0x14) & 0xfbffffff) + 0x80000000;
        if (DAT_806135ac == '\0') {
            fifo[7] = fifo[6] - fifo[5];
            if (fifo[7] < 0) {
                fifo[7] = fifo[7] + fifo[2];
            }
        } else {
            fifo[5] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x3a),
                               *(undefined2*)(DAT_8061357c + 0x38)) + -0x80000000;
            fifo[7] = CONCAT22(*(undefined2*)(DAT_8061357c + 0x32),
                               *(undefined2*)(DAT_8061357c + 0x30));
        }
        os_get_interrupts_enabled();
        *wrap = (byte)((uint)*(undefined4*)(DAT_80613578 + 0x14) >> 0x1a) & 1;
    }
    *overHi  = (undefined1)((uint)fifo[3] < (uint)fifo[7]);
    *underLo = (undefined1)((uint)fifo[7] < (uint)fifo[4]);
    *count = fifo[7];
    clz = countLeadingZeros((int)fifo - (int)DAT_806135a0);
    *isGp = (undefined1)(char)(clz >> 5);
    clz = countLeadingZeros((int)fifo - (int)DAT_806135a4);
    *isCpu = (undefined1)(char)(clz >> 5);
}

// Address: 0x803c59c4 | Name: gx_sync_fifo_ptrs | Date: 2026-09-15T07:55:00Z
// Synchronize a FIFO descriptor's read/write pointers from the live CP hardware
// registers, then hand both pointers back to the caller. The write pointer is
// refreshed from the GP write register when this is the current GP FIFO; the
// read pointer/count from the CP read-ptr register pair when this is the current
// CPU FIFO, else count is derived from write-read (wrapping by FIFO size).
void gx_sync_fifo_ptrs(int fifo, undefined4* readPtrOut, undefined4* writePtrOut) {
    if (fifo == (int)DAT_806135a0) {
        *(uint*)(fifo + 0x18) = (*(uint*)(DAT_80613578 + 0x14) & 0xfbffffff) + 0x80000000;
    }
    if (fifo == (int)DAT_806135a4) {
        *(int*)(fifo + 0x14) =
            CONCAT22(*(undefined2*)(DAT_8061357c + 0x3a),
                     *(undefined2*)(DAT_8061357c + 0x38)) + -0x80000000;
        *(uint*)(fifo + 0x1c) =
            CONCAT22(*(undefined2*)(DAT_8061357c + 0x32),
                     *(undefined2*)(DAT_8061357c + 0x30));
    } else {
        *(int*)(fifo + 0x1c) = *(int*)(fifo + 0x18) - *(int*)(fifo + 0x14);
        if (*(int*)(fifo + 0x1c) < 0) {
            *(int*)(fifo + 0x1c) = *(int*)(fifo + 0x1c) + *(int*)(fifo + 8);
        }
    }
    *readPtrOut = *(undefined4*)(fifo + 0x14);
    *writePtrOut = *(undefined4*)(fifo + 0x18);
}

// Address: 0x803c5a5c | Name: gx_fifo_get_base | Date: 2026-09-15T07:55:00Z
// Accessor: return a FIFO descriptor's base address (word 0).
undefined4 gx_fifo_get_base(undefined4* fifo) {
    return *fifo;
}

// Address: 0x803c5a64 | Name: gx_fifo_get_size | Date: 2026-09-15T07:55:00Z
// Accessor: return a FIFO descriptor's byte size (offset +8).
undefined4 gx_fifo_get_size(int fifo) {
    return *(undefined4*)(fifo + 8);
}

// Address: 0x803c5a6c | Name: gx_set_draw_done_callback | Date: 2026-09-15T07:55:00Z
// Install the CP break/draw-done user callback (invoked by gx_cp_interrupt_handler
// on the CP break/token interrupt) and return the previously installed one.
// Interrupts disabled around the swap.
undefined4 gx_set_draw_done_callback(undefined4 cb) {
    undefined4 prev = DAT_806135b4;
    os_disable_interrupts();
    DAT_806135b4 = cb;
    os_get_interrupts_enabled();
    return prev;
}

// Address: 0x803c5ab0 | Name: gx_set_cp_break | Date: 2026-09-15T08:05:00Z
// Arm a CP FIFO breakpoint at a write-gather address. Disables the FIFO read,
// programs the break-pointer register pair in the CP mirror (16-bit low half +
// 14-bit high half), clears then re-sets the CP FIFO read-enable (bit1) and
// break-interrupt-enable (bit5) status bits, records the break address, and
// re-enables the FIFO. Interrupts disabled throughout.
void gx_set_cp_break(undefined4 breakAddr) {
    int state;

    os_disable_interrupts();
    FUN_803c5c28();
    *(short*)(DAT_8061357c + 0x3c) = (short)breakAddr;
    *(ushort*)(DAT_8061357c + 0x3e) = (ushort)((uint)breakAddr >> 0x10) & 0x3fff;
    state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 8) = *(uint*)(DAT_8061c048 + 8) & 0xfffffffd;
    *(uint*)(state + 8) = *(uint*)(state + 8) & 0xffffffdf;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
    *(uint*)(state + 8) = *(uint*)(state + 8) & 0xfffffffd | 2;
    *(uint*)(state + 8) = *(uint*)(state + 8) & 0xffffffdf | 0x20;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
    DAT_806135bc = breakAddr;
    gx_cp_fifo_enable();
    os_get_interrupts_enabled();
}

// Address: 0x803c5b64 | Name: gx_clear_cp_break | Date: 2026-09-15T08:05:00Z
// Disarm the CP FIFO breakpoint: clear the FIFO read-enable (bit1) and
// break-interrupt-enable (bit5) bits in the CP control register and forget the
// recorded break address. Interrupts disabled.
void gx_clear_cp_break(void) {
    int state;

    os_disable_interrupts();
    state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 8) = *(uint*)(DAT_8061c048 + 8) & 0xfffffffd;
    *(uint*)(state + 8) = *(uint*)(state + 8) & 0xffffffdf;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
    DAT_806135bc = 0;
    os_get_interrupts_enabled();
}

// Address: 0x803c5bb8 | Name: gx_cp_init | Date: 2026-09-15T08:05:00Z
// One-time CP interrupt wiring: register gx_cp_interrupt_handler on interrupt
// 0x11, unmask the CP interrupt bit 0x4000, capture the calling thread as the
// GX draw-done wait thread, and clear the draw-suspended flag and the GP/CPU
// FIFO descriptor pointers. Called from gx_init.
void gx_cp_init(void) {
    os_set_interrupt_handler(0x11, (void*)gx_cp_interrupt_handler);
    os_unmask_interrupts(0x4000);
    DAT_806135a8 = os_get_current_thread();
    DAT_806135b0 = 0;
    DAT_806135a0 = 0;
    DAT_806135a4 = 0;
}

// Address: 0x803c5c04 | Name: gx_cp_fifo_enable | Date: 2026-09-15T08:05:00Z
// Set the CP FIFO read-enable bit (bit0 of the CP control register at state+8)
// and mirror the low 16 bits into the CP status register.
void gx_cp_fifo_enable(void) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 8) = *(uint*)(DAT_8061c048 + 8) & 0xfffffffe | 1;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
}

// Address: 0x803c5c28 | Name: gx_cp_fifo_disable | Date: 2026-09-15T08:05:00Z
// Clear the CP FIFO read-enable bit (bit0) and mirror to the CP status register.
void gx_cp_fifo_disable(void) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 8) = *(uint*)(DAT_8061c048 + 8) & 0xfffffffe;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
}

// Address: 0x803c5c4c | Name: gx_cp_set_underflow_irq | Date: 2026-09-15T08:05:00Z
// Enable (nonzero) or disable the CP FIFO underflow interrupt (bit4 of the CP
// control register) and mirror to the CP status register.
void gx_cp_set_underflow_irq(char enable) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 8) =
        (uint)(enable != '\0') << 4 | *(uint*)(DAT_8061c048 + 8) & 0xffffffef;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
}

// Address: 0x803c5c80 | Name: gx_cp_set_watermark_irqs | Date: 2026-09-15T08:05:00Z
// Set the CP FIFO under-watermark (bit2) and over-watermark (bit3) interrupt
// enable bits in the CP control register and mirror to the CP status register.
void gx_cp_set_watermark_irqs(uint underEn, uint overEn) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 8) = (underEn & 1) << 2 | *(uint*)(DAT_8061c048 + 8) & 0xfffffffb;
    *(uint*)(state + 8) = (overEn & 1) << 3 | *(uint*)(state + 8) & 0xfffffff7;
    *(short*)(DAT_8061357c + 2) = (short)*(undefined4*)(state + 8);
}

// Address: 0x803c5cb0 | Name: gx_cp_set_watermark_irq_status | Date: 2026-09-15T08:05:00Z
// Set/clear the CP FIFO under-watermark (bit0) and over-watermark (bit1)
// interrupt STATUS/clear bits in the second CP control register (state+0x10) and
// mirror to the CP status register at +4.
void gx_cp_set_watermark_irq_status(uint underClr, uint overClr) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x10) = underClr & 1 | *(uint*)(DAT_8061c048 + 0x10) & 0xfffffffe;
    *(uint*)(state + 0x10) = (overClr & 1) << 1 | *(uint*)(state + 0x10) & 0xfffffffd;
    *(short*)(DAT_8061357c + 4) = (short)*(undefined4*)(state + 0x10);
}

// Address: 0x803c5ce0 | Name: gx_cp_fifo_reset | Date: 2026-09-15T08:05:00Z
// Reset the current CPU FIFO to an empty state and re-attach it. Copies the 16
// FIFO-descriptor words (from -2..+0x1c relative to the CPU FIFO ptr) into a
// scratch descriptor whose read/write pointers are collapsed to the base, then
// gx_attach_gp_fifo(+optionally gx_attach_cpu_fifo) with the empty descriptor,
// zeroes the real descriptor's read/write pointers and count (wrapping by size),
// and re-attaches the real FIFO. No-op when no CPU FIFO is attached.
void gx_cp_fifo_reset(void) {
    uint uVar1;
    uint* cpuFifo;
    uint* curGp;
    uint* src;
    uint* dst;
    uint base;
    int i;
    uint scratch[2];      // auStack_9c: 2 lead words consumed by the -2 offset copy
    uint desc[5];         // local_94: the attachable scratch descriptor
    uint desc_lo;         // local_80
    uint desc_hi;         // local_7c
    undefined4 desc_cnt;  // local_78

    cpuFifo = (uint*)gx_get_current_cpu_fifo();
    if (cpuFifo != (uint*)0) {
        curGp = (uint*)gx_get_current_gp_fifo();
        base = *cpuFifo;
        i = 0x10;
        src = cpuFifo + -2;
        dst = scratch;
        do {
            uVar1 = src[3];
            dst[2] = src[2];
            dst[3] = uVar1;
            i = i + -1;
            src = src + 2;
            dst = dst + 2;
        } while (i != 0);
        os_disable_interrupts();
        desc_cnt = 0;
        desc_lo = base;
        desc_hi = base;
        os_get_interrupts_enabled();
        gx_attach_gp_fifo(desc);
        if (curGp == cpuFifo) {
            gx_attach_cpu_fifo(desc);
        }
        os_disable_interrupts();
        cpuFifo[5] = base;
        cpuFifo[6] = base;
        cpuFifo[7] = 0;
        if ((int)cpuFifo[7] < 0) {
            cpuFifo[7] = cpuFifo[7] + cpuFifo[2];
        }
        os_get_interrupts_enabled();
        gx_attach_gp_fifo(cpuFifo);
        if (curGp == cpuFifo) {
            gx_attach_cpu_fifo(curGp);
        }
    }
}

// Address: 0x803c5de0 | Name: gx_get_current_gp_fifo | Date: 2026-09-15T08:05:00Z
// Accessor: return the current GP (graphics-processor read) FIFO descriptor.
undefined4 gx_get_current_gp_fifo(void) {
    return (undefined4)DAT_806135a0;
}

// Address: 0x803c5de8 | Name: gx_get_current_cpu_fifo | Date: 2026-09-15T08:12:00Z
// Accessor: return the current CPU (write) FIFO descriptor (DAT_806135a4).
undefined4 gx_get_current_cpu_fifo(void) {
    return (undefined4)DAT_806135a4;
}

// Address: 0x803c5df0 | Name: gx_set_vcd_attr | Date: 2026-09-15T08:12:00Z
// Set one field of the vertex-descriptor (VCD) enable words. attr selects the
// bitfield: 0..8 single-bit enables and 9/0xb/0xc two-bit modes in VCD word 0
// (state+0x14), 0xd..0x14 two-bit texcoord enables in VCD word 1 (state+0x18),
// 0xa/0x19 select the two "index base" latches (state+0x4d4/+0x4d5, sharing the
// value at +0x4d0 folded into VCD-word-0 bits 11-12). Marks VCD dirty (state
// +0x5ac bit3).
void gx_set_vcd_attr(undefined4 attr, uint value) {
    int state = DAT_8061c048;
    switch ((int)attr) {
    case 0:
        *(uint*)(DAT_8061c048 + 0x14) = value & 1 | *(uint*)(DAT_8061c048 + 0x14) & 0xfffffffe;
        break;
    case 1:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 1 | *(uint*)(DAT_8061c048 + 0x14) & 0xfffffffd;
        break;
    case 2:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 2 | *(uint*)(DAT_8061c048 + 0x14) & 0xfffffffb;
        break;
    case 3:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 3 | *(uint*)(DAT_8061c048 + 0x14) & 0xfffffff7;
        break;
    case 4:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 4 | *(uint*)(DAT_8061c048 + 0x14) & 0xffffffef;
        break;
    case 5:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 5 | *(uint*)(DAT_8061c048 + 0x14) & 0xffffffdf;
        break;
    case 6:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 6 | *(uint*)(DAT_8061c048 + 0x14) & 0xffffffbf;
        break;
    case 7:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 7 | *(uint*)(DAT_8061c048 + 0x14) & 0xffffff7f;
        break;
    case 8:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 1) << 8 | *(uint*)(DAT_8061c048 + 0x14) & 0xfffffeff;
        break;
    case 9:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 3) << 9 | *(uint*)(DAT_8061c048 + 0x14) & 0xfffff9ff;
        break;
    case 10:
        if (value == 0) {
            *(undefined1*)(DAT_8061c048 + 0x4d4) = 0;
        } else {
            *(undefined1*)(DAT_8061c048 + 0x4d4) = 1;
            *(undefined1*)(state + 0x4d5) = 0;
            *(uint*)(state + 0x4d0) = value;
        }
        break;
    case 0xb:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 3) << 0xd | *(uint*)(DAT_8061c048 + 0x14) & 0xffff9fff;
        break;
    case 0xc:
        *(uint*)(DAT_8061c048 + 0x14) = (value & 3) << 0xf | *(uint*)(DAT_8061c048 + 0x14) & 0xfffe7fff;
        break;
    case 0xd:
        *(uint*)(DAT_8061c048 + 0x18) = value & 3 | *(uint*)(DAT_8061c048 + 0x18) & 0xfffffffc;
        break;
    case 0xe:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 2 | *(uint*)(DAT_8061c048 + 0x18) & 0xfffffff3;
        break;
    case 0xf:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 4 | *(uint*)(DAT_8061c048 + 0x18) & 0xffffffcf;
        break;
    case 0x10:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 6 | *(uint*)(DAT_8061c048 + 0x18) & 0xffffff3f;
        break;
    case 0x11:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 8 | *(uint*)(DAT_8061c048 + 0x18) & 0xfffffcff;
        break;
    case 0x12:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 10 | *(uint*)(DAT_8061c048 + 0x18) & 0xfffff3ff;
        break;
    case 0x13:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 0xc | *(uint*)(DAT_8061c048 + 0x18) & 0xffffcfff;
        break;
    case 0x14:
        *(uint*)(DAT_8061c048 + 0x18) = (value & 3) << 0xe | *(uint*)(DAT_8061c048 + 0x18) & 0xffff3fff;
        break;
    case 0x19:
        if (value == 0) {
            *(undefined1*)(DAT_8061c048 + 0x4d5) = 0;
        } else {
            *(undefined1*)(DAT_8061c048 + 0x4d5) = 1;
            *(undefined1*)(state + 0x4d4) = 0;
            *(uint*)(state + 0x4d0) = value;
        }
        break;
    default:
        break;
    }
    if ((*(char*)(DAT_8061c048 + 0x4d4) == '\0') && (*(char*)(DAT_8061c048 + 0x4d5) == '\0')) {
        *(uint*)(DAT_8061c048 + 0x14) = *(uint*)(DAT_8061c048 + 0x14) & 0xffffe7ff;
    } else {
        *(uint*)(DAT_8061c048 + 0x14) =
            (*(uint*)(DAT_8061c048 + 0x4d0) & 3) << 0xb | *(uint*)(DAT_8061c048 + 0x14) & 0xffffe7ff;
    }
    *(uint*)(DAT_8061c048 + 0x5ac) = *(uint*)(DAT_8061c048 + 0x5ac) | 8;
}

// Address: 0x803c605c | Name: gx_send_vcd | Date: 2026-09-15T08:12:00Z
// Stream the two VCD enable words (state+0x14 / +0x18) to the CP as load-CP-reg
// 0x50 / 0x60, then compute and emit the matrix-index count register (0x1008):
// packs the index-base mode (0/1/2), the position-count (from the VCD-word-0
// bits 13-16 population via a countLeadingZeros trick) and the texcoord-count
// (from the VCD-word-1 low 16 bits). Marks CP dirty (state+2 = 1).
void gx_send_vcd(void) {
    int mode;
    int posShift;
    int texShift;

    kGxFifoByte[0] = 8;
    kGxFifoByte[0] = 0x50;
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x14);
    kGxFifoByte[0] = 8;
    kGxFifoByte[0] = 0x60;
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x18);
    if (*(char*)(DAT_8061c048 + 0x4d5) == '\0') {
        if (*(char*)(DAT_8061c048 + 0x4d4) == '\0') {
            mode = 0;
        } else {
            mode = 1;
        }
    } else {
        mode = 2;
    }
    texShift = countLeadingZeros(*(uint*)(DAT_8061c048 + 0x14) >> 0xd & 0xf);
    kGxFifoByte[0] = 0x10;
    posShift = countLeadingZeros(*(uint*)(DAT_8061c048 + 0x18) & 0xffff);
    DAT_cc008000 = 0x1008;
    DAT_cc008000 = (0x21 - posShift) * 8 & 0xfffffff0U | 0x21U - texShift >> 1 | mode << 2;
    *(undefined2*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803c6118 | Name: gx_calc_vertex_size | Date: 2026-09-15T08:12:00Z
// Recompute the current vertex byte size (state+6) from the enabled VCD fields:
// sum the nine single-bit position/normal/color-present bits, plus the
// per-field component sizes looked up from the DAT_806114d8/dc/e0 size tables
// (color x2 fields, texcoord x8 fields, normal doubled to 3 when the normal-BT
// bit VCD-word-1 bit9 is set). No-op when the "dirty" flag state+4 is 0.
void gx_calc_vertex_size(void) {
    uint w1;
    short nrmMul;
    uint w0;

    if (*(short*)(DAT_8061c048 + 4) == 0) {
        return;
    }
    w0 = *(uint*)(DAT_8061c048 + 0x14);
    w1 = *(uint*)(DAT_8061c048 + 0x18);
    if ((*(uint*)(DAT_8061c048 + 0x1c) >> 9 & 1) == 1) {
        nrmMul = 3;
    } else {
        nrmMul = 1;
    }
    *(ushort*)(DAT_8061c048 + 6) =
        ((ushort)w0 & 1) + ((ushort)(w0 >> 1) & 1) + ((ushort)(w0 >> 2) & 1) +
        ((ushort)(w0 >> 3) & 1) + ((ushort)(w0 >> 4) & 1) + ((ushort)(w0 >> 5) & 1) +
        ((ushort)(w0 >> 6) & 1) + ((ushort)(w0 >> 7) & 1) + ((ushort)(w0 >> 8) & 1) +
        (ushort)(byte)(&DAT_806114e0)[w0 >> 9 & 3] +
        (ushort)(byte)(&DAT_806114e0)[w0 >> 0xb & 3] * nrmMul +
        (ushort)(byte)(&DAT_806114d8)[w0 >> 0xd & 3] +
        (ushort)(byte)(&DAT_806114d8)[w0 >> 0xf & 3] + (ushort)(byte)(&DAT_806114dc)[w1 & 3] +
        (ushort)(byte)(&DAT_806114dc)[w1 >> 2 & 3] + (ushort)(byte)(&DAT_806114dc)[w1 >> 4 & 3] +
        (ushort)(byte)(&DAT_806114dc)[w1 >> 6 & 3] + (ushort)(byte)(&DAT_806114dc)[w1 >> 8 & 3] +
        (ushort)(byte)(&DAT_806114dc)[w1 >> 10 & 3] +
        (ushort)(byte)(&DAT_806114dc)[w1 >> 0xc & 3] +
        (ushort)(byte)(&DAT_806114dc)[w1 >> 0xe & 3];
}

// Address: 0x803c623c | Name: gx_clear_vcd | Date: 2026-09-15T08:12:00Z
// Reset the vertex-descriptor state to defaults: zero VCD word 0, set its
// index-base mode field (bits 9-10) to 1, zero VCD word 1, clear the two
// index-base latches (+0x4d4/+0x4d5), and mark VCD dirty (state+0x5ac bit3).
void gx_clear_vcd(void) {
    int state = DAT_8061c048;
    *(undefined4*)(DAT_8061c048 + 0x14) = 0;
    *(uint*)(state + 0x14) = *(uint*)(state + 0x14) & 0xfffff9ff | 0x200;
    *(undefined4*)(state + 0x18) = 0;
    *(undefined1*)(state + 0x4d4) = 0;
    *(undefined1*)(state + 0x4d5) = 0;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 8;
}

// Address: 0x803c6274 | Name: gx_set_vat_fmt | Date: 2026-09-15T08:12:00Z
// Set the vertex-attribute-table (VAT) format fields for one format index from
// explicit (cnt,type,frac) arguments. attr selects the field (9..0x14/0x19);
// the three VAT sub-words live at fmtIdx*4 + 0x1c / +0x3c / +0x5c in the state
// block. Marks VAT dirty (state+0x5ac bit4) and flags this format index in the
// pending-send mask (state+0x5ab).
void gx_set_vat_fmt(uint fmtIdx, undefined4 attr, uint cnt, uint type, uint frac) {
    uint* w0;
    int state = DAT_8061c048 + fmtIdx * 4;
    uint* w1;
    uint* w2;

    w0 = (uint*)(state + 0x1c);
    w1 = (uint*)(state + 0x3c);
    w2 = (uint*)(state + 0x5c);
    switch ((int)attr) {
    case 9:
        *w0 = cnt & 1 | *w0 & 0xfffffffe;
        *w0 = (type & 7) << 1 | *w0 & 0xfffffff1;
        *w0 = (frac & 0x1f) << 4 | *w0 & 0xfffffe0f;
        break;
    case 10:
    case 0x19:
        *w0 = (type & 7) << 10 | *w0 & 0xffffe3ff;
        if (cnt == 2) {
            *w0 = *w0 & 0xfffffdff | 0x200;
            *w0 = *w0 & 0x7fffffff | 0x80000000;
        } else {
            *w0 = (cnt & 1) << 9 | *w0 & 0xfffffdff;
            *w0 = *w0 & 0x7fffffff;
        }
        break;
    case 0xb:
        *w0 = (cnt & 1) << 0xd | *w0 & 0xffffdfff;
        *w0 = (type & 7) << 0xe | *w0 & 0xfffe3fff;
        break;
    case 0xc:
        *w0 = (cnt & 1) << 0x11 | *w0 & 0xfffdffff;
        *w0 = (type & 7) << 0x12 | *w0 & 0xffe3ffff;
        break;
    case 0xd:
        *w0 = (cnt & 1) << 0x15 | *w0 & 0xffdfffff;
        *w0 = (type & 7) << 0x16 | *w0 & 0xfe3fffff;
        *w0 = (frac & 0x1f) << 0x19 | *w0 & 0xc1ffffff;
        break;
    case 0xe:
        *w1 = cnt & 1 | *w1 & 0xfffffffe;
        *w1 = (type & 7) << 1 | *w1 & 0xfffffff1;
        *w1 = (frac & 0x1f) << 4 | *w1 & 0xfffffe0f;
        break;
    case 0xf:
        *w1 = (cnt & 1) << 9 | *w1 & 0xfffffdff;
        *w1 = (type & 7) << 10 | *w1 & 0xffffe3ff;
        *w1 = (frac & 0x1f) << 0xd | *w1 & 0xfffc1fff;
        break;
    case 0x10:
        *w1 = (cnt & 1) << 0x12 | *w1 & 0xfffbffff;
        *w1 = (type & 7) << 0x13 | *w1 & 0xffc7ffff;
        *w1 = (frac & 0x1f) << 0x16 | *w1 & 0xf83fffff;
        break;
    case 0x11:
        *w1 = (cnt & 1) << 0x1b | *w1 & 0xf7ffffff;
        *w1 = (type & 7) << 0x1c | *w1 & 0x8fffffff;
        *w2 = frac & 0x1f | *w2 & 0xffffffe0;
        break;
    case 0x12:
        *w2 = (cnt & 1) << 5 | *w2 & 0xffffffdf;
        *w2 = (type & 7) << 6 | *w2 & 0xfffffe3f;
        *w2 = (frac & 0x1f) << 9 | *w2 & 0xffffc1ff;
        break;
    case 0x13:
        *w2 = (cnt & 1) << 0xe | *w2 & 0xffffbfff;
        *w2 = (type & 7) << 0xf | *w2 & 0xfffc7fff;
        *w2 = (frac & 0x1f) << 0x12 | *w2 & 0xff83ffff;
        break;
    case 0x14:
        *w2 = (cnt & 1) << 0x17 | *w2 & 0xff7fffff;
        *w2 = (type & 7) << 0x18 | *w2 & 0xf8ffffff;
        *w2 = frac << 0x1b | *w2 & 0x7ffffff;
        break;
    default:
        break;
    }
    state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x5ac) = *(uint*)(DAT_8061c048 + 0x5ac) | 0x10;
    *(byte*)(state + 0x5ab) = *(byte*)(state + 0x5ab) | (byte)(1 << (fmtIdx & 0xff));
}

// Address: 0x803c64d0 | Name: gx_set_vat_fmt_array | Date: 2026-09-15T08:12:00Z
// Bulk VAT-format setter: walk an {attr,cnt,type,frac} record array (stride 4
// ints, terminated by attr==0xff) applying each field to the format index's
// three VAT sub-words exactly like gx_set_vat_fmt. Marks VAT dirty and flags the
// format index in the pending-send mask.
void gx_set_vat_fmt_array(uint fmtIdx, int* attrList) {
    byte fracB;
    uint type;
    int state;
    uint frac;
    uint* w0;
    uint* w1;
    uint* w2;
    uint cnt;

    state = DAT_8061c048 + fmtIdx * 4;
    w0 = (uint*)(state + 0x1c);
    w1 = (uint*)(state + 0x3c);
    w2 = (uint*)(state + 0x5c);
    for (; state = DAT_8061c048, *attrList != 0xff; attrList = attrList + 4) {
        fracB = *(byte*)(attrList + 3);
        frac = (uint)fracB;
        type = attrList[2];
        cnt = attrList[1];
        switch (*attrList) {
        case 9:
            *w0 = cnt & 1 | *w0 & 0xfffffffe;
            *w0 = (type & 7) << 1 | *w0 & 0xfffffff1;
            *w0 = (frac & 0x1f) << 4 | *w0 & 0xfffffe0f;
            break;
        case 10:
        case 0x19:
            *w0 = (type & 7) << 10 | *w0 & 0xffffe3ff;
            if (cnt == 2) {
                *w0 = *w0 & 0xfffffdff | 0x200;
                *w0 = *w0 & 0x7fffffff | 0x80000000;
            } else {
                *w0 = (cnt & 1) << 9 | *w0 & 0xfffffdff;
                *w0 = *w0 & 0x7fffffff;
            }
            break;
        case 0xb:
            *w0 = (cnt & 1) << 0xd | *w0 & 0xffffdfff;
            *w0 = (type & 7) << 0xe | *w0 & 0xfffe3fff;
            break;
        case 0xc:
            *w0 = (cnt & 1) << 0x11 | *w0 & 0xfffdffff;
            *w0 = (type & 7) << 0x12 | *w0 & 0xffe3ffff;
            break;
        case 0xd:
            *w0 = (cnt & 1) << 0x15 | *w0 & 0xffdfffff;
            *w0 = (type & 7) << 0x16 | *w0 & 0xfe3fffff;
            *w0 = (frac & 0x1f) << 0x19 | *w0 & 0xc1ffffff;
            break;
        case 0xe:
            *w1 = cnt & 1 | *w1 & 0xfffffffe;
            *w1 = (type & 7) << 1 | *w1 & 0xfffffff1;
            *w1 = (frac & 0x1f) << 4 | *w1 & 0xfffffe0f;
            break;
        case 0xf:
            *w1 = (cnt & 1) << 9 | *w1 & 0xfffffdff;
            *w1 = (type & 7) << 10 | *w1 & 0xffffe3ff;
            *w1 = (frac & 0x1f) << 0xd | *w1 & 0xfffc1fff;
            break;
        case 0x10:
            *w1 = (cnt & 1) << 0x12 | *w1 & 0xfffbffff;
            *w1 = (type & 7) << 0x13 | *w1 & 0xffc7ffff;
            *w1 = (frac & 0x1f) << 0x16 | *w1 & 0xf83fffff;
            break;
        case 0x11:
            *w1 = (cnt & 1) << 0x1b | *w1 & 0xf7ffffff;
            *w1 = (type & 7) << 0x1c | *w1 & 0x8fffffff;
            *w2 = fracB & 0x1f | *w2 & 0xffffffe0;
            break;
        case 0x12:
            *w2 = (cnt & 1) << 5 | *w2 & 0xffffffdf;
            *w2 = (type & 7) << 6 | *w2 & 0xfffffe3f;
            *w2 = (frac & 0x1f) << 9 | *w2 & 0xffffc1ff;
            break;
        case 0x13:
            *w2 = (cnt & 1) << 0xe | *w2 & 0xffffbfff;
            *w2 = (type & 7) << 0xf | *w2 & 0xfffc7fff;
            *w2 = (frac & 0x1f) << 0x12 | *w2 & 0xff83ffff;
            break;
        case 0x14:
            *w2 = (cnt & 1) << 0x17 | *w2 & 0xff7fffff;
            *w2 = (type & 7) << 0x18 | *w2 & 0xf8ffffff;
            *w2 = frac << 0x1b | *w2 & 0x7ffffff;
            break;
        default:
            break;
        }
    }
    *(uint*)(DAT_8061c048 + 0x5ac) = *(uint*)(DAT_8061c048 + 0x5ac) | 0x10;
    *(byte*)(state + 0x5ab) = *(byte*)(state + 0x5ab) | (byte)(1 << (fmtIdx & 0xff));
}

// Address: 0x803c6750 | Name: gx_send_vat | Date: 2026-09-15T08:12:00Z
// Stream every dirty VAT format row (flagged in the state+0x5ab pending mask) to
// the CP as load-CP-reg 0x70/0x80/0x90 register triples (the three VAT sub-words
// at row*4 + 0x1c/+0x3c/+0x5c), then clear the pending mask.
void gx_send_vat(void) {
    int off;
    byte reg;
    uint mask;

    reg = 0;
    off = 0;
    mask = (uint)*(byte*)(DAT_8061c048 + 0x5ab);
    do {
        if ((mask & 1) != 0) {
            kGxFifoByte[0] = 8;
            kGxFifoByte[0] = reg | 0x70;
            DAT_cc008000 = *(undefined4*)(DAT_8061c048 + off + 0x1c);
            kGxFifoByte[0] = 8;
            kGxFifoByte[0] = reg | 0x80;
            DAT_cc008000 = *(undefined4*)(DAT_8061c048 + off + 0x3c);
            kGxFifoByte[0] = 8;
            kGxFifoByte[0] = reg | 0x90;
            DAT_cc008000 = *(undefined4*)(DAT_8061c048 + off + 0x5c);
        }
        mask = mask >> 1;
        off = off + 4;
        reg = reg + 1;
    } while (mask != 0);
    *(undefined1*)(DAT_8061c048 + 0x5ab) = 0;
}

// Address: 0x803c67d8 | Name: gx_set_array_ptr | Date: 2026-09-15T08:12:00Z
// Set a vertex-attribute array's base address and stride: emit the base as
// load-CP-reg 0xa0+idx (masked to a 30-bit physical address) and the stride as
// load-CP-reg 0xb0+idx (byte value). attr 0x19 aliases to the 0xa attribute
// slot; the register index is attr-9.
void gx_set_array_ptr(int attr, uint base, uint stride) {
    byte reg;

    if (attr == 0x19) {
        attr = 10;
    }
    reg = (char)attr - 9;
    kGxFifoByte[0] = 8;
    kGxFifoByte[0] = reg | 0xa0;
    DAT_cc008000 = base & 0x3fffffff;
    kGxFifoByte[0] = 8;
    kGxFifoByte[0] = reg | 0xb0;
    DAT_cc008000 = stride & 0xff;
}

// Address: 0x803c681c | Name: gx_invalidate_vertex_cache | Date: 2026-09-15T08:12:00Z
// Emit the CP invalidate-vertex-cache command (single opcode byte 0x48).
void gx_invalidate_vertex_cache(void) {
    kGxFifoByte[0] = 0x48;
}

// ===========================================================================
// batch_1163: GX draw-sync / VI-token wait + CP FIFO abort + matrix-index
// register layer (main.dol 0x803c682c .. 0x803c7000).
// ---------------------------------------------------------------------------
// These finish the command-processor immediate layer sitting on top of the
// batch_1159-1162 GX bring-up: the per-attribute matrix-index register write
// (gx_set_matrix_index_reg), the two draw-sync token spin-waits that stall the
// CPU until the pixel engine has drained pending work (gx_draw_sync /
// gx_draw_done_reset), the CP FIFO flush/abort primitives, and the small
// wpar/token accessors. The draw-sync loops read the PE token count register
// (DAT_80613584 + 0x4e) and then spin on the 64-bit timebase for a fixed number
// of bus ticks; rendered here as a plain elapsed-tick busy-wait (non-matching).
// ---------------------------------------------------------------------------

// Per-file externs new to this layer.
extern volatile unsigned int  DAT_cc003018;   // PE finish/token-poke register
extern char  DAT_806135c8;                     // draw-done signalled flag
extern int   DAT_806135cc;                     // draw-done wait queue head
extern uint  os_get_wpar(void);                // read write-pipe-address reg
extern void  os_sleep_thread(int* queue);      // block current thread on queue
extern void  gx_load_default_bp_regs(void);    // (defined earlier in this file)
extern void  FUN_803c73cc(void);               // flush pending BP/VAT dirty regs
void  gx_commit_matrix_index(int matrixIndexSel);  // reprogram matrix-index-A/B reg (0x803cb620; defined below)

// Small fixed-tick busy-wait on the PPC timebase. The original inlines a
// 64-bit mftb compare against a tick budget; os_read_time_base is declared
// int-returning at file scope, so we express the identical "spin until N ticks
// elapsed" intent with the low-word delta (non-matching timing wait).
static void gx_spin_ticks(uint ticks) {
    int start = os_read_time_base();
    while ((uint)(os_read_time_base() - start) < ticks) {
        // busy-wait
    }
}

// Stall until the PE token count register stops advancing (two consecutive
// stable reads) with a short timebase settle between samples. Used to be sure
// the graphics pipe has quiesced before poking the PE finish register.
static void gx_wait_pe_token_stable(void) {
    if ((*(char*)(DAT_8061c048 + 0x5aa) != '\0') && (gx_get_current_cpu_fifo() != 0)) {
        short prev = *(short*)(DAT_80613584 + 0x4e);
        int   lastCnt;
        bool  changed;
        do {
            short cur = *(short*)(DAT_80613584 + 0x4e);
            lastCnt = *(int*)(DAT_80613584 + 0x4e);
            changed = cur != prev;
            prev = cur;
        } while (changed);
        do {
            gx_spin_ticks(8);
            prev = *(short*)(DAT_80613584 + 0x4e);
            int cnt;
            do {
                short cur = *(short*)(DAT_80613584 + 0x4e);
                cnt = *(int*)(DAT_80613584 + 0x4e);
                changed = cur != prev;
                prev = cur;
            } while (changed);
            changed = cnt != lastCnt;
            lastCnt = cnt;
        } while (changed);
    }
    // Pulse the PE finish/token-poke register: assert ~0x32 ticks, then release
    // ~5 ticks.
    DAT_cc003018 = 1;
    gx_spin_ticks(0x32);
    DAT_cc003018 = 0;
    gx_spin_ticks(5);
}

// Address: 0x803c6aac | Name: gx_set_matrix_index_reg | Date: 2026-09-15T08:26:00Z
// Program the low matrix-index selector (bits 0..3) into GX state+0x204 and
// emit it as load-CP-reg 0x103f, then flag the VAT/BP-dirty bit (state+0x5ac
// bit2) so the next draw-sync flushes it.
void gx_set_matrix_index_reg(uint sel) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x204) = sel & 0xf | *(uint*)(DAT_8061c048 + 0x204) & 0xfffffff0;
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = 0x103f;
    DAT_cc008000 = sel & 0xff;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 4;
}

// Address: 0x803c6ae8 | Name: gx_set_scissor_or_flag | Date: 2026-09-15T08:26:00Z
// Set one of three small GX state booleans/shorts selected by which:
//   which==1 : set state word[2] to a token count, recompute its top bit via
//              countLeadingZeros and mark the token-dirty bit (state+0x2d6 bit3);
//   which==2 : set the state byte at +0x5a9 to (value != 0);
//   which==3 : set the state byte at +0x2d5 to (value != 0).
// which==0 / out-of-range values are ignored.
void gx_set_scissor_or_flag(int which, int value) {
    undefined2* state = (undefined2*)DAT_8061c048;
    if (which == 2) {
        *(char*)((int)DAT_8061c048 + 0x5a9) = '\x01' - (value == 0);
        return;
    }
    if (1 < which) {
        if (3 < which) {
            return;
        }
        *(char*)(DAT_8061c048 + 0x2d5) = '\x01' - (value == 0);
        return;
    }
    if (which == 0) {
        return;
    }
    if (which < 0) {
        return;
    }
    state[2] = (short)value;
    uint clz = countLeadingZeros((uint)(ushort)state[2]);
    *state = (short)(clz >> 5);
    state[1] = 1;
    if (state[2] == 0) {
        return;
    }
    *(uint*)(state + 0x2d6) = *(uint*)(state + 0x2d6) | 8;
}

// Address: 0x803c682c | Name: gx_set_vtx_attr_fmt_reg | Date: 2026-09-15T08:26:00Z
// Build the per-attribute VAT format word from (attr, cnt, type, frac) and emit
// it to the CP as load-CP-reg (fmtIdx+0x1040) plus its fog/AA companion at
// (fmtIdx+0x1050), then splice the format index into the matrix-index-B nibble
// field of GX state (+0x80/+0x84) and reprogram the matrix-index register.
// NON-MATCHING: the flat type->code switch and the packed bitfield masks are
// kept verbatim.
void gx_set_vtx_attr_fmt_reg(int fmtIdx, int cnt, int type, uint attr,
                             uint frac, int shift) {
    uint word = 0;
    int  isPos = 0;
    int  code = 5;
    switch (type) {
    case 0:  code = 0; isPos = 1; break;
    case 1:  code = 1; isPos = 1; break;
    case 2:  code = 3; isPos = 1; break;
    case 3:  code = 4; isPos = 1; break;
    case 4:  code = 5;  break;
    case 5:  code = 6;  break;
    case 6:  code = 7;  break;
    case 7:  code = 8;  break;
    case 8:  code = 9;  break;
    case 9:  code = 10; break;
    case 10: code = 0xb; break;
    case 0xb: code = 0xc; break;
    case 0x13: code = 2; break;
    case 0x14: code = 2; break;
    default: break;
    }
    if (cnt == 1) {
        word = code << 7 | isPos << 2;
    } else if (cnt < 1) {
        if (-1 < cnt) {
            word = code << 7 | isPos << 2 | 2U;
        }
    } else if (cnt == 10) {
        if (type == 0x13) {
            word = 0x20;
        } else {
            word = 0x30;
        }
        word = word | isPos << 2 | 0x100;
    } else if (cnt < 10) {
        word = (cnt + -2) * 0x8000 & 0x38000U |
               (type + -0xc) * 0x1000 & 0x7000U | code << 7 | isPos << 2 | 0x10U;
    }
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = fmtIdx + 0x1040;
    DAT_cc008000 = word;
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = fmtIdx + 0x1050;
    DAT_cc008000 = (frac & 1) << 8 | shift - 0x40U & 0x3f;
    switch (fmtIdx) {
    case 0:
        *(uint*)(DAT_8061c048 + 0x80) =
            (attr & 0x3f) << 6 | *(uint*)(DAT_8061c048 + 0x80) & 0xfffff03f;
        break;
    case 1:
        *(uint*)(DAT_8061c048 + 0x80) =
            (attr & 0x3f) << 0xc | *(uint*)(DAT_8061c048 + 0x80) & 0xfffc0fff;
        break;
    case 2:
        *(uint*)(DAT_8061c048 + 0x80) =
            (attr & 0x3f) << 0x12 | *(uint*)(DAT_8061c048 + 0x80) & 0xff03ffff;
        break;
    case 3:
        *(uint*)(DAT_8061c048 + 0x80) =
            (attr & 0x3f) << 0x18 | *(uint*)(DAT_8061c048 + 0x80) & 0xc0ffffff;
        break;
    case 4:
        *(uint*)(DAT_8061c048 + 0x84) = attr & 0x3f | *(uint*)(DAT_8061c048 + 0x84) & 0xffffffc0;
        break;
    case 5:
        *(uint*)(DAT_8061c048 + 0x84) =
            (attr & 0x3f) << 6 | *(uint*)(DAT_8061c048 + 0x84) & 0xfffff03f;
        break;
    case 6:
        *(uint*)(DAT_8061c048 + 0x84) =
            (attr & 0x3f) << 0xc | *(uint*)(DAT_8061c048 + 0x84) & 0xfffc0fff;
        break;
    default:
        *(uint*)(DAT_8061c048 + 0x84) =
            (attr & 0x3f) << 0x12 | *(uint*)(DAT_8061c048 + 0x84) & 0xff03ffff;
    }
    gx_commit_matrix_index(fmtIdx + 1);
}

// Address: 0x803c6b7c | Name: gx_flush_fifo | Date: 2026-09-15T08:26:00Z
// Flush any pending BP/VAT dirty registers (via FUN_803c73cc when state+0x5ac
// is set), then emit eight zero words to pad/flush the CP write-gather FIFO and
// issue the CP flush syscall.
void gx_flush_fifo(void) {
    if (*(int*)(DAT_8061c048 + 0x5ac) != 0) {
        FUN_803c73cc();
    }
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    os_syscall();
}

// Address: 0x803c6bd8 | Name: gx_flush_write_pipe | Date: 2026-09-15T08:26:00Z
// Spin until the write-pipe-address (WPAR) register drains (bit0 clear), then
// point the write pipe at the CP FIFO immediate port 0x0c008000.
void gx_flush_write_pipe(void) {
    uint wpar;
    do {
        wpar = os_get_wpar();
    } while ((wpar & 1) != 0);
    os_set_wpar(0xc008000);
}

// Address: 0x803c6c0c | Name: gx_draw_sync | Date: 2026-09-15T08:26:00Z
// Draw-sync barrier: wait for the PE token count to stabilise then pulse the PE
// finish register (shared body with gx_draw_done_reset, minus the FIFO reset).
void gx_draw_sync(void) {
    gx_wait_pe_token_stable();
}

// Address: 0x803c6d78 | Name: gx_draw_done_reset | Date: 2026-09-15T08:26:00Z
// Full draw-done + CP FIFO reset: quiesce the pipe (gx_wait_pe_token_stable),
// then if a CPU FIFO is attached reset it, reload the default BP registers,
// clear the BP/VAT dirty word, and flush the FIFO.
void gx_draw_done_reset(void) {
    gx_wait_pe_token_stable();
    if (gx_get_current_cpu_fifo() != 0) {
        gx_cp_fifo_reset();
        gx_load_default_bp_regs();
        int state = DAT_8061c048;
        *(undefined4*)(DAT_8061c048 + 0x5ac) = 0;
        if (*(int*)(state + 0x5ac) != 0) {
            FUN_803c73cc();
        }
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        DAT_cc008000 = 0;
        os_syscall();
    }
}

// Address: 0x803c6f40 | Name: gx_set_draw_done_token | Date: 2026-09-15T08:26:00Z
// Emit a PE draw-done token pair (interrupt-on-token then no-interrupt token)
// carrying a 16-bit id, flush pending dirty regs + the FIFO, and clear the CP
// dirty flag (state+2). Interrupts are disabled around the token writes.
void gx_set_draw_done_token(uint tokenId) {
    os_disable_interrupts();
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = tokenId & 0xffff | 0x48000000;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = tokenId & 0xffff | 0x47000000;
    if (*(int*)(DAT_8061c048 + 0x5ac) != 0) {
        FUN_803c73cc();
    }
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    os_syscall();
    os_get_interrupts_enabled();
    *(undefined2*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c6ff4 | Name: gx_get_pe_token | Date: 2026-09-15T08:26:00Z
// Accessor: return the current PE token value (PE regs + 0xe).
undefined2 gx_get_pe_token(void) {
    return *(undefined2*)(DAT_80613580 + 0xe);
}

// Address: 0x803c7000 | Name: gx_wait_draw_done | Date: 2026-09-15T08:26:00Z
// Block the caller until the PE signals draw-done: emit the finish token
// (0x45000002), flush the FIFO, clear the signalled flag, then sleep on the
// draw-done wait queue until gx_cp_interrupt_handler sets DAT_806135c8.
void gx_wait_draw_done(void) {
    os_disable_interrupts();
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = 0x45000002;
    gx_flush_fifo();
    DAT_806135c8 = '\0';
    os_get_interrupts_enabled();
    os_disable_interrupts();
    while (DAT_806135c8 == '\0') {
        os_sleep_thread(&DAT_806135cc);
    }
    os_get_interrupts_enabled();
}

// ===========================================================================
// batch_1164: GX pixel-engine (PE) mode-register setters + draw-done token
// callback (main.dol 0x803c7080 .. 0x803c71c8).
// ---------------------------------------------------------------------------
// This is the GXSet*-style layer that pokes the PE control block (DAT_80613580
// = cc001000): the Z-compare / Z-update / color-update / alpha-update / dither
// mode word (PE +2), the destination-alpha register (PE +6), the blend/logic
// mode word (PE +0), the copy-filter enable (PE +8), the framebuffer clip
// enable (PE +4), plus the token-interrupt id setter (PE +2 low bits). Also the
// XF-abort token re-emit (gx_reemit_last_token) and the draw-done callback
// swap (gx_set_token_callback -> DAT_806135c0).
// ---------------------------------------------------------------------------

extern undefined4  DAT_806135c0;   // draw-done/token user callback (code*)

// Address: 0x803c7080 | Name: gx_reemit_last_token | Date: 2026-09-15T08:40:00Z
// Re-emit the last XF/other immediate word saved at GX state+0x1dc through the
// CP write-gather FIFO (opcode 0x61) and clear the CP dirty flag (state+2).
void gx_reemit_last_token(void) {
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x1dc);
    *(undefined2*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c70a4 | Name: gx_pe_set_dst_alpha | Date: 2026-09-15T08:40:00Z
// Set the PE destination-alpha register (PE +6): enable flag in the high byte,
// 8-bit alpha value in the low byte.
void gx_pe_set_dst_alpha(short enable, ushort alpha) {
    *(ushort*)(DAT_80613580 + 6) = enable << 8 | alpha & 0xff;
}

// Address: 0x803c70b8 | Name: gx_pe_set_copy_filter | Date: 2026-09-15T08:40:00Z
// Set the PE copy-filter mode (PE +8): 2-bit mode with bit2 (enable) forced on.
void gx_pe_set_copy_filter(ushort mode) {
    *(ushort*)(DAT_80613580 + 8) = mode & 3 | 4;
}

// Address: 0x803c70d8 | Name: gx_pe_set_color_update | Date: 2026-09-15T08:40:00Z
// Set the PE color-update enable bit (PE +2 bit4) leaving the rest of the mode
// word intact.
void gx_pe_set_color_update(ushort enable) {
    *(ushort*)(DAT_80613580 + 2) = (enable & 1) << 4 | *(ushort*)(DAT_80613580 + 2) & 0xffef;
}

// Address: 0x803c70f0 | Name: gx_pe_set_z_mode | Date: 2026-09-15T08:40:00Z
// Program the PE Z / peek-poke mode word (PE +2): the compare-enable bit, the
// pixel-format and z-format selectors (derived from a small mode index via
// countLeadingZeros), the source/dest factor fields, and the operation nibble,
// while preserving the color/alpha-update bits (mask 0x1c).
void gx_pe_set_z_mode(int mode, ushort srcFactor, ushort dstFactor, short op) {
    ushort cmpEnable = 1;
    if ((mode != 1) && (mode != 3)) {
        cmpEnable = 0;
    }
    uint clz3 = countLeadingZeros(3 - mode);
    uint clz2 = countLeadingZeros(2 - mode);
    *(ushort*)(DAT_80613580 + 2) =
        (dstFactor & 7) << 5 |
        (srcFactor & 7) << 8 |
        op << 0xc |
        (ushort)(clz2 >> 4) & 2 |
        (ushort)((clz3 >> 5) << 0xb) & 0x800 | cmpEnable | *(ushort*)(DAT_80613580 + 2) & 0x1c;
}

// Address: 0x803c7154 | Name: gx_pe_set_alpha_update | Date: 2026-09-15T08:40:00Z
// Set the PE alpha-update enable bit (PE +2 bit3), preserving the rest.
void gx_pe_set_alpha_update(ushort enable) {
    *(ushort*)(DAT_80613580 + 2) = (enable & 1) << 3 | *(ushort*)(DAT_80613580 + 2) & 0xfff7;
}

// Address: 0x803c716c | Name: gx_pe_set_dither | Date: 2026-09-15T08:40:00Z
// Set the PE dither register (PE +4): enable flag in bit8, 8-bit pattern id in
// the low byte.
void gx_pe_set_dither(ushort enable, ushort pattern) {
    *(ushort*)(DAT_80613580 + 4) = (enable & 1) << 8 | pattern & 0xff;
}

// Address: 0x803c7190 | Name: gx_pe_set_z_compare | Date: 2026-09-15T08:40:00Z
// Set the PE z-compare-before-texture bit (PE +2 bit2), preserving the rest.
void gx_pe_set_z_compare(ushort enable) {
    *(ushort*)(DAT_80613580 + 2) = (enable & 1) << 2 | *(ushort*)(DAT_80613580 + 2) & 0xfffb;
}

// Address: 0x803c71a8 | Name: gx_pe_set_blend_mode | Date: 2026-09-15T08:40:00Z
// Program the PE blend/logic-op mode word (PE +0): blend-enable bit0, the 3-bit
// blend/logic op field (bits 1..3), and the subtract/logic select bit4.
void gx_pe_set_blend_mode(ushort blendEnable, ushort op, ushort logicSelect) {
    *(ushort*)(DAT_80613580) = (logicSelect & 1) << 4 | (op & 7) << 1 | blendEnable & 1;
}

// Address: 0x803c71c8 | Name: gx_set_token_callback | Date: 2026-09-15T08:40:00Z
// Install the PE token/draw-done user callback DAT_806135c0 (invoked by
// gx_cp_interrupt_handler) and return the previous one; interrupts disabled
// around the swap.
undefined4 gx_set_token_callback(undefined4 callback) {
    undefined4 prev = DAT_806135c0;
    os_disable_interrupts();
    DAT_806135c0 = callback;
    os_get_interrupts_enabled();
    return prev;
}

// ===========================================================================
// batch_1165: GX pixel-engine interrupt/token handlers + BP/VAT dirty flush +
// begin-draw framebuffer clear + fog/blend PE-reg setters
// (main.dol 0x803c720c .. 0x803c7624).
// ---------------------------------------------------------------------------
// This is the interrupt-driven half of the PE layer plus the deferred-state
// flush that the earlier batches referenced as the extern FUN_803c73cc. The two
// interrupt handlers (token IRQ 0x12 and finish IRQ 0x13) run a saved user
// callback on a scratch OS context then ack the PE interrupt-status word
// (cc001000 +0xa); gx_pe_init_interrupts wires them up. gx_flush_dirty_regs and
// its inline twin in gx_begin_draw drain the six deferred GX dirty bits
// (state+0x5ac / state[0x16b]) by re-sending the alpha-compare, blend, texture,
// VCD, VAT and vertex-size register groups. gx_begin_draw also lazily clears the
// EFB via the CP 0x98 draw-rect opcode the first time it is entered.
// ---------------------------------------------------------------------------

extern undefined4  DAT_806135c4;                 // finish-IRQ user callback (code*)
extern void  os_init_thread_queue(undefined4* q);
extern void  os_wakeup_thread(int* queue);
// Cross-module deferred-state senders (defined elsewhere in the GX tree).
extern void  FUN_803c95e8(void);                 // resend alpha-compare / TEV env
extern void  FUN_803c9f38(void);                 // resend blend / cmode regs
extern void  FUN_803c76e4(void);                 // resend fog / range-adjust regs
// Forward decls for the in-batch begin-draw helper referenced by the handlers.
void gx_begin_draw(byte opcode, byte primType, undefined2 vtxCount);
void gx_clear_efb(void);

// Address: 0x803c720c | Name: gx_pe_token_isr | Date: 2026-09-15T08:54:00Z
// PE token interrupt (0x12) service routine: read the current PE token
// (cc001000 +0xe), run the installed token callback DAT_806135c0 on a scratch
// OS context if present, then ack the token interrupt (PE +0xa bit2).
void gx_pe_token_isr(undefined4 param_1, uint prevContext) {
    undefined2 token;
    undefined1 scratchCtx[712];
    token = *(undefined2*)(DAT_80613580 + 0xe);
    if (DAT_806135c0 != 0) {
        os_clear_context((int)scratchCtx);
        os_set_current_context((uint)scratchCtx);
        ((void(*)(undefined2))DAT_806135c0)(token);
        os_clear_context((int)scratchCtx);
        os_set_current_context(prevContext);
    }
    *(ushort*)(DAT_80613580 + 10) = *(ushort*)(DAT_80613580 + 10) & 0xfffb | 4;
}

// Address: 0x803c7294 | Name: gx_set_finish_callback | Date: 2026-09-15T08:54:00Z
// Install the PE finish-IRQ callback DAT_806135c4 (the one run by gx_pe_finish_isr)
// and return the previous one; interrupts disabled around the swap.
undefined4 gx_set_finish_callback(undefined4 callback) {
    undefined4 prev = DAT_806135c4;
    os_disable_interrupts();
    DAT_806135c4 = callback;
    os_get_interrupts_enabled();
    return prev;
}

// Address: 0x803c72d8 | Name: gx_pe_finish_isr | Date: 2026-09-15T08:54:00Z
// PE finish interrupt (0x13) service routine: ack the finish interrupt (PE +0xa
// bit3), set the draw-done signalled flag, run the finish callback DAT_806135c4
// on a scratch OS context if present, then wake any thread blocked in
// gx_wait_draw_done on the DAT_806135cc queue.
void gx_pe_finish_isr(undefined4 param_1, uint prevContext) {
    undefined1 scratchCtx[716];
    *(ushort*)(DAT_80613580 + 10) = *(ushort*)(DAT_80613580 + 10) & 0xfff7 | 8;
    DAT_806135c8 = 1;
    if (DAT_806135c4 != 0) {
        os_clear_context((int)scratchCtx);
        os_set_current_context((uint)scratchCtx);
        ((void(*)(void))DAT_806135c4)();
        os_clear_context((int)scratchCtx);
        os_set_current_context(prevContext);
    }
    os_wakeup_thread((int*)&DAT_806135cc);
}

// Address: 0x803c7358 | Name: gx_pe_init_interrupts | Date: 2026-09-15T08:54:00Z
// Wire up the PE interrupts: register the token ISR on interrupt 0x12 and the
// finish ISR on 0x13, init the draw-done wait queue, unmask the PE token
// (0x2000) and finish (0x1000) interrupt bits, and enable both PE interrupt
// enables (cc001000 +0xa low nibble = 0xf).
void gx_pe_init_interrupts(void) {
    os_set_interrupt_handler(0x12, gx_pe_token_isr);
    os_set_interrupt_handler(0x13, gx_pe_finish_isr);
    os_init_thread_queue((undefined4*)&DAT_806135cc);
    os_unmask_interrupts(0x2000);
    os_unmask_interrupts(0x1000);
    *(ushort*)(DAT_80613580 + 10) = *(ushort*)(DAT_80613580 + 10) & 0xfff0 | 0xf;
}

// Address: 0x803c73cc | Name: FUN_803c73cc | Date: 2026-09-15T08:54:00Z
// gx_flush_dirty_regs: drain the six deferred GX dirty bits in state+0x5ac by
// re-sending each pending register group (alpha-compare, blend, fog, VCD, VAT,
// vertex-size), then clear the dirty word. This is the routine the earlier
// batches (gx_flush_fifo / gx_set_draw_done_token / gx_draw_done_reset) call as
// the extern FUN_803c73cc; the name is retained so those call sites resolve.
void FUN_803c73cc(void) {
    uint dirty = *(uint*)(DAT_8061c048 + 0x5ac);
    if ((dirty & 1) != 0) {
        FUN_803c95e8();
    }
    if ((dirty & 2) != 0) {
        FUN_803c9f38();
    }
    if ((dirty & 4) != 0) {
        FUN_803c76e4();
    }
    if ((dirty & 8) != 0) {
        gx_send_vcd();
    }
    if ((dirty & 0x10) != 0) {
        gx_send_vat();
    }
    if ((dirty & 0x18) != 0) {
        gx_calc_vertex_size();
    }
    *(undefined4*)(DAT_8061c048 + 0x5ac) = 0;
}

// Address: 0x803c744c | Name: gx_begin_draw | Date: 2026-09-15T08:54:00Z
// Begin a primitive draw: first drain the per-draw deferred dirty bits held in
// GX state byte[0x16b] (the same six register groups as gx_flush_dirty_regs but
// gated on the per-draw mask), lazily clear the EFB via gx_clear_efb the first
// time (state word[0] == 0), then emit the primitive-begin opcode (primType |
// opcode) and the 16-bit vertex count into the CP write-gather FIFO.
void gx_begin_draw(byte opcode, byte primType, undefined2 vtxCount) {
    uint dirty = ((uint*)DAT_8061c048)[0x16b];
    if (dirty != 0) {
        if ((dirty & 1) != 0) {
            FUN_803c95e8();
        }
        if ((dirty & 2) != 0) {
            FUN_803c9f38();
        }
        if ((dirty & 4) != 0) {
            FUN_803c76e4();
        }
        if ((dirty & 8) != 0) {
            gx_send_vcd();
        }
        if ((dirty & 0x10) != 0) {
            gx_send_vat();
        }
        if ((dirty & 0x18) != 0) {
            gx_calc_vertex_size();
        }
        ((uint*)DAT_8061c048)[0x16b] = 0;
    }
    if (*(int*)DAT_8061c048 == 0) {
        gx_clear_efb();
    }
    kGxFifoByte[0] = primType | opcode;
    *(volatile unsigned short*)&DAT_cc008000 = vtxCount;
}

// Address: 0x803c751c | Name: gx_clear_efb | Date: 2026-09-15T08:54:00Z
// Clear the embedded framebuffer by streaming (w*h) zero pixels through the CP
// draw-rectangle opcode 0x98: emit the opcode + width, then push w*h rounded up
// zero words in blocks of eight then singly, and mark the CP FIFO dirty
// (state+2 = 1).
void gx_clear_efb(void) {
    int pixels = (uint)*(ushort*)(DAT_8061c048 + 4) * (uint)*(ushort*)(DAT_8061c048 + 6);
    kGxFifoByte[0] = 0x98;
    *(volatile unsigned short*)&DAT_cc008000 = *(ushort*)(DAT_8061c048 + 4);
    uint words = pixels + 3;
    uint singles = words >> 2;
    if (pixels != 0) {
        uint blocks = words >> 5;
        if (blocks != 0) {
            do {
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                DAT_cc008000 = 0;
                blocks = blocks - 1;
            } while (blocks != 0);
            singles = singles & 7;
            if (singles == 0) {
                goto done;
            }
        }
        do {
            DAT_cc008000 = 0;
            singles = singles - 1;
        } while (singles != 0);
    }
done:
    *(undefined2*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803c75a4 | Name: gx_set_blend_factors | Date: 2026-09-15T08:54:00Z
// Set the low blend-factor fields of GX cmode0 (state+0x7c): the 8-bit
// destination/constant factor (bits 0..7) and the 3-bit source-factor select
// (bits 16..18), then re-send cmode0 as an XF/BP immediate (opcode 0x61) and
// clear the CP dirty flag (state+2).
void gx_set_blend_factors(uint factorLo, uint srcSel) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x7c) = factorLo & 0xff | *(uint*)(DAT_8061c048 + 0x7c) & 0xffffff00;
    *(uint*)(state + 0x7c) = (srcSel & 7) << 0x10 | *(uint*)(state + 0x7c) & 0xfff8ffff;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(undefined4*)(state + 0x7c);
    *(undefined2*)(state + 2) = 0;
}

// Address: 0x803c75e4 | Name: gx_set_blend_factors_hi | Date: 2026-09-15T08:54:00Z
// Set the high blend-factor fields of GX cmode0 (state+0x7c): the 8-bit
// source/constant factor (bits 8..15) and the 3-bit dest-factor select
// (bits 19..21), re-send cmode0 (opcode 0x61) and clear the CP dirty flag.
void gx_set_blend_factors_hi(uint factorHi, uint dstSel) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x7c) = (factorHi & 0xff) << 8 | *(uint*)(DAT_8061c048 + 0x7c) & 0xffff00ff;
    *(uint*)(state + 0x7c) = (dstSel & 7) << 0x13 | *(uint*)(state + 0x7c) & 0xffc7ffff;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(undefined4*)(state + 0x7c);
    *(undefined2*)(state + 2) = 0;
}

// Address: 0x803c7624 | Name: gx_set_tev_clamp | Date: 2026-09-15T08:54:00Z
// Set the clamp/compare flags for TEV stage 'stage' in the per-stage cmode
// register (state + stage*4 + 0xb8): the color-clamp bit (bit18) and the
// alpha-clamp bit (bit19), then re-send the stage register (opcode 0x61) and
// clear the CP dirty flag.
void gx_set_tev_clamp(int stage, uint colorClamp, uint alphaClamp) {
    int state = DAT_8061c048;
    int reg = DAT_8061c048 + stage * 4;
    *(uint*)(reg + 0xb8) = (colorClamp & 1) << 0x12 | *(uint*)(reg + 0xb8) & 0xfffbffff;
    *(uint*)(reg + 0xb8) = (alphaClamp & 1) << 0x13 | *(uint*)(reg + 0xb8) & 0xfff7ffff;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(undefined4*)(reg + 0xb8);
    *(undefined2*)(state + 2) = 0;
}

// ===========================================================================
// batch_1166: GX cull-mode / texture-copy-region / EFB-copy setup layer
// (main.dol 0x803c766c .. 0x803c7aa8).
// ---------------------------------------------------------------------------
// This is the GXSetCullMode / GXSetCoPlanar / GXSetTexCopySrc/Dst /
// GXSetCopyClear-style layer that programs the geometry cull field and the EFB
// texture-copy source/destination rectangles in GX state (offsets 0x1e0-0x204),
// plus the deferred cmode resend (FUN_803c76e4) the earlier batches referenced.
// The copy-region words are packed with a BP-register opcode in the top byte
// (0x49/0x4a/0x4d) and 10-bit x/y or width/height fields, matching the GameCube
// copy engine register layout. NON-MATCHING: the flat DAT_8061c048 fixed-offset
// arithmetic and the packed bitfield masks are kept verbatim.
// ---------------------------------------------------------------------------

// Cross-module texture-format geometry lookup (defined elsewhere in the GX tree):
// maps a copy format + width/height to the block-packed byte counts.
extern void  FUN_803c8abc(uint fmt, ushort w, ushort h, int* outA, int* outB, int* outC);

// Address: 0x803c766c | Name: gx_set_cull_mode | Date: 2026-09-15T09:08:00Z
// Set the geometry cull mode field (GX state+0x204 bits 14..15): the two-bit
// mode is bit-swapped (front/back swap) before insertion, then the VAT/BP-dirty
// bit (state+0x5ac bit2) is flagged for the next flush.
void gx_set_cull_mode(uint mode) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x204) =
        ((mode & 1) << 1 | mode >> 1 & 1) << 0xe | *(uint*)(DAT_8061c048 + 0x204) & 0xffff3fff;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 4;
}

// Address: 0x803c7694 | Name: gx_get_cull_mode | Date: 2026-09-15T09:08:00Z
// Read back the cull mode from state+0x204 bits 14..15, undoing the front/back
// bit-swap performed by gx_set_cull_mode, into *out.
void gx_get_cull_mode(uint* out) {
    *out = *(uint*)(DAT_8061c048 + 0x204) >> 0xd & 2 |
           (int)(*(uint*)(DAT_8061c048 + 0x204) >> 0xe & 2) >> 1;
}

// Address: 0x803c76b0 | Name: gx_set_coplanar | Date: 2026-09-15T09:08:00Z
// Set the co-planar / z-offset-enable bit (state+0x204 bit19), then emit the
// genmode BP register (0xfe080000 mask word followed by the updated state+0x204)
// through the CP write-gather FIFO.
void gx_set_coplanar(uint enable) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x204) =
        (enable & 1) << 0x13 | *(uint*)(DAT_8061c048 + 0x204) & 0xfff7ffff;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = 0xfe080000;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(undefined4*)(state + 0x204);
}

// Address: 0x803c76e4 | Name: FUN_803c76e4 | Date: 2026-09-15T09:08:00Z
// gx_resend_genmode: re-send the genmode/cull register (state+0x204) as an
// XF/BP immediate (opcode 0x61) and clear the CP dirty flag (state+2). This is
// the routine gx_flush_dirty_regs / gx_begin_draw call as the extern
// FUN_803c76e4; the source symbol name is retained so those call sites resolve.
void FUN_803c76e4(void) {
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x204);
    *(undefined2*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c7708 | Name: gx_adjust_copy_rect | Date: 2026-09-15T09:08:00Z
// Copy a 0x3c-byte EFB-copy descriptor from src to dst (15 words), then adjust
// the destination rectangle for a border/scale pass: shift the origin by
// (border*2), scale the height by (scale*2 / srcHeight), and pick one of two
// width deltas based on the low format bits. Used to derive the shrunk copy
// region for mip/border generation.
void gx_adjust_copy_rect(uint* src, uint* dst, ushort border, uint scale) {
    int delta = (scale & 0x7fff) * 2;
    if (src != dst) {
        uint t;
        t = src[1];  *dst = *src;         dst[1] = t;
        t = src[3];  dst[2] = src[2];     dst[3] = t;
        t = src[5];  dst[4] = src[4];     dst[5] = t;
        t = src[7];  dst[6] = src[6];     dst[7] = t;
        t = src[9];  dst[8] = src[8];     dst[9] = t;
        t = src[0xb]; dst[10] = src[10];  dst[0xb] = t;
        t = src[0xd]; dst[0xc] = src[0xc]; dst[0xd] = t;
        dst[0xe] = src[0xe];
    }
    uint fmt = *src;
    *(ushort*)(dst + 1) = *(short*)(src + 1) + border * -2;
    *(ushort*)((int)dst + 6) =
        *(ushort*)((int)src + 6) -
        (short)((delta * (uint)*(ushort*)((int)src + 6)) / (uint)*(ushort*)(src + 2));
    if ((src[5] == 0) && ((fmt & 3) == 0)) {
        *(short*)(dst + 2) = *(short*)(src + 2) - (short)(delta >> 1);
    } else {
        *(short*)(dst + 2) = *(short*)(src + 2) - (short)delta;
    }
    *(ushort*)((int)dst + 0xe) = *(short*)((int)src + 0xe) + border * -2;
    if ((fmt & 3) == 1) {
        *(short*)(dst + 4) = *(short*)(src + 4) - (short)((scale & 0x7fff) << 2);
    } else {
        *(short*)(dst + 4) = *(short*)(src + 4) - (short)delta;
    }
    *(ushort*)((int)dst + 10) = *(short*)((int)src + 10) + border;
    *(short*)(dst + 3) = *(short*)(src + 3) + (short)scale;
}

// Address: 0x803c784c | Name: gx_set_tex_copy_src | Date: 2026-09-15T09:08:00Z
// Program the EFB texture-copy source rectangle: origin (x,y) into the copy
// register at state+0x1e0 (10-bit x + 10-bit y, BP opcode 0x49 in the top byte)
// and the (width-1,height-1) into state+0x1e4 (BP opcode 0x4a).
void gx_set_tex_copy_src(uint x, uint y, uint w, uint h) {
    int state = DAT_8061c048;
    *(undefined4*)(DAT_8061c048 + 0x1e0) = 0;
    *(uint*)(state + 0x1e0) = x & 0x3ff | *(uint*)(state + 0x1e0) & 0xfffffc00;
    *(uint*)(state + 0x1e0) = (y & 0x3ff) << 10 | *(uint*)(state + 0x1e0) & 0xfff003ff;
    *(uint*)(state + 0x1e0) = *(uint*)(state + 0x1e0) & 0xffffff | 0x49000000;
    *(undefined4*)(state + 0x1e4) = 0;
    *(uint*)(state + 0x1e4) = (w & 0xffff) - 1 & 0x3ff | *(uint*)(state + 0x1e4) & 0xfffffc00;
    *(uint*)(state + 0x1e4) = ((h & 0xffff) - 1) * 0x400 & 0xffc00 | *(uint*)(state + 0x1e4) & 0xfff003ff;
    *(uint*)(state + 0x1e4) = *(uint*)(state + 0x1e4) & 0xffffff | 0x4a000000;
}

// Address: 0x803c78c8 | Name: gx_set_disp_copy_src | Date: 2026-09-15T09:08:00Z
// Program the display-copy source rectangle: origin (x,y) into the copy register
// at state+0x1f0 (BP opcode 0x49) and (width-1,height-1) into state+0x1f4 (BP
// opcode 0x4a). Same layout as gx_set_tex_copy_src for the display path.
void gx_set_disp_copy_src(uint x, uint y, uint w, uint h) {
    int state = DAT_8061c048;
    *(undefined4*)(DAT_8061c048 + 0x1f0) = 0;
    *(uint*)(state + 0x1f0) = x & 0x3ff | *(uint*)(state + 0x1f0) & 0xfffffc00;
    *(uint*)(state + 0x1f0) = (y & 0x3ff) << 10 | *(uint*)(state + 0x1f0) & 0xfff003ff;
    *(uint*)(state + 0x1f0) = *(uint*)(state + 0x1f0) & 0xffffff | 0x49000000;
    *(undefined4*)(state + 500) = 0;
    *(uint*)(state + 500) = (w & 0xffff) - 1 & 0x3ff | *(uint*)(state + 500) & 0xfffffc00;
    *(uint*)(state + 500) = ((h & 0xffff) - 1) * 0x400 & 0xffc00 | *(uint*)(state + 500) & 0xfff003ff;
    *(uint*)(state + 500) = *(uint*)(state + 500) & 0xffffff | 0x4a000000;
}

// Address: 0x803c7944 | Name: gx_set_tex_copy_stride | Date: 2026-09-15T09:08:00Z
// Program the texture-copy destination stride (state+0x1e8): the (width*2)>>5
// row stride in 32-byte units (10-bit field, BP opcode 0x4d).
void gx_set_tex_copy_stride(uint width) {
    int state = DAT_8061c048;
    *(undefined4*)(DAT_8061c048 + 0x1e8) = 0;
    *(uint*)(state + 0x1e8) =
        (int)((width & 0x7fff) << 1) >> 5 & 0x3ffU | *(uint*)(state + 0x1e8) & 0xfffffc00;
    *(uint*)(state + 0x1e8) = *(uint*)(state + 0x1e8) & 0xffffff | 0x4d000000;
}

// Address: 0x803c7978 | Name: gx_set_tex_copy_fmt | Date: 2026-09-15T09:08:00Z
// Program the texture-copy destination format: normalise the format index
// (0x13 -> 0xb), select the intensity/YUV path vs the plain path in the copy
// control word (state+0x1fc bit15..17), stash the format nibble and its
// even/odd bit (via countLeadingZeros) into state+0x200 and +0x1fc, then compute
// the destination stride (block count * count) via the FUN_803c8abc geometry
// lookup and pack the mip/clamp flags.
void gx_set_tex_copy_fmt(ushort w, ushort h, uint fmt, uint mipEnable) {
    int a, b, c;
    uint fmtLow = fmt & 0xf;
    *(undefined1*)(DAT_8061c048 + 0x200) = 0;
    if (fmt == 0x13) {
        fmtLow = 0xb;
    }
    if ((fmt == 0x26) || ((((int)fmt < 0x26 && ((int)fmt < 4)) && (-1 < (int)fmt)))) {
        *(uint*)(DAT_8061c048 + 0x1fc) = *(uint*)(DAT_8061c048 + 0x1fc) & 0xfffe7fff | 0x18000;
    } else {
        *(uint*)(DAT_8061c048 + 0x1fc) = *(uint*)(DAT_8061c048 + 0x1fc) & 0xfffe7fff | 0x10000;
    }
    int state = DAT_8061c048;
    uint clz = countLeadingZeros((fmt & 0x10) - 0x10);
    *(char*)(state + 0x200) = (char)(clz >> 5);
    *(uint*)(state + 0x1fc) = fmtLow & 8 | *(uint*)(state + 0x1fc) & 0xfffffff7;
    FUN_803c8abc(fmt, w, h, &c, &b, &a);
    state = DAT_8061c048;
    *(undefined4*)(state + 0x1f8) = 0;
    *(uint*)(state + 0x1f8) = c * a & 0x3ffU | *(uint*)(state + 0x1f8) & 0xfffffc00;
    *(uint*)(state + 0x1f8) = *(uint*)(state + 0x1f8) & 0xffffff | 0x4d000000;
    *(uint*)(state + 0x1fc) = (mipEnable & 1) << 9 | *(uint*)(state + 0x1fc) & 0xfffffdff;
    *(uint*)(state + 0x1fc) = (fmtLow & 7) << 4 | *(uint*)(state + 0x1fc) & 0xffffff8f;
}

// Address: 0x803c7aa8 | Name: gx_set_copy_gamma | Date: 2026-09-15T09:08:00Z
// Set the copy gamma / pixel-format select (state+0x1ec bits 12..13) and clear
// the matching bits in the display-copy control word (state+0x1fc).
void gx_set_copy_gamma(uint gamma) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x1ec) =
        (gamma & 3) << 0xc | *(uint*)(DAT_8061c048 + 0x1ec) & 0xffffcfff;
    *(uint*)(state + 0x1fc) = *(uint*)(state + 0x1fc) & 0xffffcfff;
}

// ===========================================================================
// batch_1167: GX EFB copy-execute layer + copy Y-scale/filter/clear setup
// (main.dol 0x803c7acc .. 0x803c83e8), appended after the batch_1159-1166 GX
// bring-up + PE/copy-region layers. These are the GXCopyTex / GXCopyDisp
// execute paths plus the copy Y-scale factor solver, the copy-filter tap
// programming, the copy clear color/z, and the small clamp/format-bit setters.
//
// Additional per-file externs for this layer's copy-scale constants and the
// double->uint truncation the decompiler emitted as rt_double_to_uint.
extern float        FLOAT_8061c070;    // copy Y-scale numerator constant (256.0f)
extern double       DOUBLE_8061c078;   // 2^52 int->double bias constant (copy path)
// double->uint truncating convert (decompiler intrinsic rt_double_to_uint).
static inline uint gx_d2u(double v) { return (uint)v; }

// Address: 0x803c7acc | Name: gx_set_copy_clear_flags | Date: 2026-09-15T09:20:00Z
// Set two independent enable bits (bit0, bit1) of both the display-copy control
// word (state+0x1ec) and the texture-copy control word (state+0x1fc) from the
// low two bits of the argument, using the decompiler's countLeadingZeros
// (x-1 == 0 ? ...) predicate to extract each bit.
void gx_set_copy_clear_flags(uint flags) {
    int state = DAT_8061c048;
    uint b0 = countLeadingZeros((flags & 1) - 1) >> 5 & 1;
    *(uint*)(DAT_8061c048 + 0x1ec) = b0 | *(uint*)(DAT_8061c048 + 0x1ec) & 0xfffffffe;
    uint b1 = countLeadingZeros((flags & 2) - 2) >> 5 & 1;
    *(uint*)(state + 0x1ec) = (b1 << 1) | *(uint*)(state + 0x1ec) & 0xfffffffd;
    *(uint*)(state + 0x1fc) = b0 | *(uint*)(state + 0x1fc) & 0xfffffffe;
    *(uint*)(state + 0x1fc) = (b1 << 1) | *(uint*)(state + 0x1fc) & 0xfffffffd;
}

// Address: 0x803c7b24 | Name: gx_calc_yscale | Date: 2026-09-15T09:20:00Z
// Solve the EFB-copy vertical scale factor: given source height (srcH, low 16
// bits) and the requested destination line count (dstLines, low 16 bits),
// iterate the fractional Y-scale (256/scale, clamped to the 0x1ff scale field)
// so the produced line count matches dstLines, walking dstLines up or down
// until the scale converges (max 0x400 lines). Returns the converged fractional
// scale as a double (fed to gx_set_yscale by the caller).
double gx_calc_yscale(uint srcH, uint dstLines) {
    uint dst = dstLines & 0xffff;
    uint src = srcH & 0xffff;
    double scaleFrac =
        (double)((float)((double)CONCAT44(0x43300000, dst) - DOUBLE_8061c078) /
                 (float)((double)CONCAT44(0x43300000, src) - DOUBLE_8061c078));
    uint sc = gx_d2u((double)(float)((double)FLOAT_8061c070 / scaleFrac)) & 0x1ff;
    uint span = (src - 1) * 0x100;
    uint lines = span / sc;
    uint got = lines + 1;
    if ((0x80 < sc) && (sc < 0x100)) {
        for (; (sc & 1) == 0; sc = sc >> 1) { }
        if (src == (src / sc) * sc) { got = lines + 2; }
    }
    if (0x400 < got) { got = 0x400; }

    double numer = (double)FLOAT_8061c070;
    double bias = DOUBLE_8061c078;
    while ((dstLines & 0xffff) < got) {
        dst = dst - 1;
        scaleFrac =
            (double)((float)((double)CONCAT44(0x43300000, dst) - bias) /
                     (float)((double)CONCAT44(0x43300000, srcH & 0xffff) - bias));
        sc = gx_d2u((double)(float)(numer / scaleFrac)) & 0x1ff;
        lines = span / sc;
        got = lines + 1;
        if ((0x80 < sc) && (sc < 0x100)) {
            for (; (sc & 1) == 0; sc = sc >> 1) { }
            if (src == (src / sc) * sc) { got = lines + 2; }
        }
        if (0x400 < got) { got = 0x400; }
    }

    double numer2 = (double)FLOAT_8061c070;
    double prevFrac = scaleFrac;
    double bias2 = DOUBLE_8061c078;
    while (got < (dstLines & 0xffff)) {
        dst = dst + 1;
        double nextFrac =
            (double)((float)((double)CONCAT44(0x43300000, dst) - bias2) /
                     (float)((double)CONCAT44(0x43300000, srcH & 0xffff) - bias2));
        scaleFrac = prevFrac;
        sc = gx_d2u((double)(float)(numer2 / nextFrac)) & 0x1ff;
        lines = span / sc;
        got = lines + 1;
        if ((0x80 < sc) && (sc < 0x100)) {
            for (; (sc & 1) == 0; sc = sc >> 1) { }
            if (src == (src / sc) * sc) { got = lines + 2; }
        }
        prevFrac = nextFrac;
        if (0x400 < got) { got = 0x400; }
    }
    return scaleFrac;
}

// Address: 0x803c7d5c | Name: gx_set_yscale | Date: 2026-09-15T09:20:00Z
// Program the EFB-copy vertical scale register from a fractional scale value:
// convert to the 0x1ff scale field, emit it to the CP as BP register 0x4e,
// set the "scale != 1" bit10 of the display-copy control word (state+0x1ec),
// then compute and return the resulting destination line count.
uint gx_set_yscale(double scaleFrac) {
    uint sc = gx_d2u((double)(float)((double)FLOAT_8061c070 / scaleFrac)) & 0x1ff;
    int state = DAT_8061c048;
    int inv = -sc + 0x100;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = sc | 0x4e000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
    *(uint*)(state + 0x1ec) =
        (inv - ((uint)(inv == 0) + -sc + 0xff)) * 0x400 & 0x400 |
        *(uint*)(state + 0x1ec) & 0xfffffbff;
    uint lines = (*(uint*)(state + 0x1e4) >> 2 & 0x3ff00) / sc;
    uint dstH = (*(uint*)(state + 0x1e4) >> 10 & 0x3ff) + 1;
    uint got = lines + 1;
    if ((0x80 < sc) && (sc < 0x100)) {
        for (; (sc & 1) == 0; sc = sc >> 1) { }
        if (dstH == (dstH / sc) * sc) { got = lines + 2; }
    }
    if (0x400 < got) { got = 0x400; }
    return got;
}

// Address: 0x803c7e28 | Name: gx_set_copy_clear | Date: 2026-09-15T09:20:00Z
// Program the EFB-copy clear color and clear-Z: emit the clear AR/GB color
// bytes as BP registers 0x4f/0x50 (packed A|R then G|B) and the 24-bit clear-Z
// as BP register 0x51, then mark the CP FIFO dirty.
void gx_set_copy_clear(unsigned char* clearColor, uint clearZ) {
    kGxFifoByte[0] = 0x61;
    // CONCAT11(clearColor[3], clearColor[0]): pack A (byte3) high, R (byte0) low.
    DAT_cc008000 = ((uint)clearColor[3] << 8 | clearColor[0]) | 0x4f000000;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = *(unsigned short*)(clearColor + 1) | 0x50000000;
    kGxFifoByte[0] = 0x61;
    DAT_cc008000 = (clearZ & 0xffffff) | 0x51000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c7ea0 | Name: gx_set_copy_filter_coeffs | Date: 2026-09-15T09:20:00Z
// Program the EFB-copy anti-alias filter taps: 24 sample-pattern nibbles packed
// four-per BP register (0x1..0x4) when useSamples is set (else the default
// 0x_666666 pattern), followed by the 7 vertical filter coefficients packed as
// two BP registers 0x53/0x54 (else the SDK default 0x53595000 / 0x54000015).
void gx_set_copy_filter_coeffs(char useSamples, unsigned char* samples,
                               char useVertical, unsigned char* vertical) {
    uint w0, w1, w2, w3;
    if (useSamples == '\0') {
        w0 = 0x1666666; w1 = 0x2666666; w2 = 0x3666666; w3 = 0x4666666;
    } else {
        w0 = (samples[5] & 0xf) << 0x14 | (samples[4] & 0xf) << 0x10 |
             (samples[3] & 0xf) << 0xc  | (samples[2] & 0xf) << 8 |
             (samples[1] & 0xf) << 4    | (samples[0] & 0xf) | 0x1000000;
        w1 = (samples[0xb] & 0xf) << 0x14 | (samples[10] & 0xf) << 0x10 |
             (samples[9] & 0xf) << 0xc   | (samples[8] & 0xf) << 8 |
             (samples[7] & 0xf) << 4     | (samples[6] & 0xf) | 0x2000000;
        w2 = (samples[0x11] & 0xf) << 0x14 | (samples[0x10] & 0xf) << 0x10 |
             (samples[0xf] & 0xf) << 0xc  | (samples[0xe] & 0xf) << 8 |
             (samples[0xd] & 0xf) << 4    | (samples[0xc] & 0xf) | 0x3000000;
        w3 = (samples[0x17] & 0xf) << 0x14 | (samples[0x16] & 0xf) << 0x10 |
             (samples[0x15] & 0xf) << 0xc  | (samples[0x14] & 0xf) << 8 |
             (samples[0x13] & 0xf) << 4    | (samples[0x12] & 0xf) | 0x4000000;
    }
    kGxFifoByte[0] = 0x61; DAT_cc008000 = w0;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = w1;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = w2;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = w3;

    uint v0, v1;
    if (useVertical == '\0') {
        v0 = 0x54000015; v1 = 0x53595000;
    } else {
        v1 = (vertical[3] & 0x3f) << 0x12 | (vertical[2] & 0x3f) << 0xc |
             (vertical[1] & 0x3f) << 6    | (vertical[0] & 0x3f) | 0x53000000;
        v0 = (vertical[6] & 0x3f) << 0xc  | (vertical[5] & 0x3f) << 6 |
             (vertical[4] & 0x3f)         | 0x54000000;
    }
    kGxFifoByte[0] = 0x61; DAT_cc008000 = v1;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = v0;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c80a8 | Name: gx_set_copy_clamp | Date: 2026-09-15T09:20:00Z
// Set the copy vertical clamp-mode field (bits 7..8) of the display-copy
// control word (state+0x1ec) from the low two bits of the argument.
void gx_set_copy_clamp(uint clampMode) {
    *(uint*)(DAT_8061c048 + 0x1ec) =
        (clampMode & 3) << 7 | *(uint*)(DAT_8061c048 + 0x1ec) & 0xfffffe7f;
}

// Address: 0x803c80bc | Name: gx_do_tex_copy | Date: 2026-09-15T09:20:00Z
// Execute an EFB->texture copy to destAddr: if clear is requested, force the
// z-config/mask (state+0x1d8/+0x1d0) to allow the clear, temporarily patch the
// pixel-format (state+0x1dc) to a copy-compatible value, then emit the copy
// source rect (state+0x1e0..+0x1e8), the 30-bit destination address as BP 0x4b,
// the copy-execute control word (state+0x1ec) as BP 0x52, and restore the
// patched z-config / pixel-format afterwards.
void gx_do_tex_copy(uint destAddr, uint clear) {
    int state = DAT_8061c048;
    if ((clear & 0xff) != 0) {
        kGxFifoByte[0] = 0x61;
        DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1d8) & 0xfffffff0 | 0xf;
        kGxFifoByte[0] = 0x61;
        DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1d0) & 0xfffffffc;
    }
    bool restoreZ = false;
    if ((clear & 0xff) == 0) {
        if ((*(uint*)(DAT_8061c048 + 0x1dc) & 7) != 3) goto emit;
    }
    if ((*(uint*)(DAT_8061c048 + 0x1dc) >> 6 & 1) == 1) {
        kGxFifoByte[0] = 0x61;
        DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1dc) & 0xffffffbf;
        restoreZ = true;
    }
emit:
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1e0);
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1e4);
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1e8);
    kGxFifoByte[0] = 0x61; DAT_cc008000 = destAddr >> 5 & 0x1fffff | 0x4b000000;
    *(uint*)(DAT_8061c048 + 0x1ec) =
        (clear & 1) << 0xb | *(uint*)(DAT_8061c048 + 0x1ec) & 0xfffff7ff;
    *(uint*)(state + 0x1ec) = *(uint*)(state + 0x1ec) & 0xffffbfff | 0x4000;
    *(uint*)(state + 0x1ec) = *(uint*)(state + 0x1ec) & 0xffffff | 0x52000000;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(state + 0x1ec);
    if ((clear & 0xff) != 0) {
        kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(state + 0x1d8);
        kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(state + 0x1d0);
    }
    if (restoreZ) {
        kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1dc);
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c8224 | Name: gx_do_disp_copy | Date: 2026-09-15T09:20:00Z
// Execute an EFB->XFB display copy to destAddr: same structure as gx_do_tex_copy
// but drives the display-copy source rect (state+0x1f0..+0x1f8) and the
// display-copy control word (state+0x1fc), additionally forcing pixel-format 3
// when the copy-YUV flag (state+0x200) is set. Restores any patched z-config /
// pixel-format after the copy-execute BP 0x52.
void gx_do_disp_copy(uint destAddr, uint clear) {
    int state = DAT_8061c048;
    if ((clear & 0xff) != 0) {
        kGxFifoByte[0] = 0x61;
        DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1d8) & 0xfffffff0 | 0xf;
        kGxFifoByte[0] = 0x61;
        DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1d0) & 0xfffffffc;
    }
    bool restoreZ = false;
    uint zcfg = *(uint*)(DAT_8061c048 + 0x1dc);
    if (*(char*)(DAT_8061c048 + 0x200) != '\0') {
        if ((zcfg & 7) != 3) { zcfg = zcfg & 0xfffffff8 | 3; restoreZ = true; }
    }
    if ((clear & 0xff) == 0) {
        if ((zcfg & 7) != 3) goto emit;
    }
    if ((zcfg >> 6 & 1) == 1) { zcfg = zcfg & 0xffffffbf; restoreZ = true; }
emit:
    if (restoreZ) { kGxFifoByte[0] = 0x61; DAT_cc008000 = zcfg; }
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1f0);
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 500);
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1f8);
    kGxFifoByte[0] = 0x61; DAT_cc008000 = destAddr >> 5 & 0x1fffff | 0x4b000000;
    *(uint*)(DAT_8061c048 + 0x1fc) =
        (clear & 1) << 0xb | *(uint*)(DAT_8061c048 + 0x1fc) & 0xfffff7ff;
    *(uint*)(state + 0x1fc) = *(uint*)(state + 0x1fc) & 0xffffbfff;
    *(uint*)(state + 0x1fc) = *(uint*)(state + 0x1fc) & 0xffffff | 0x52000000;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(state + 0x1fc);
    if ((clear & 0xff) != 0) {
        kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(state + 0x1d8);
        kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(state + 0x1d0);
    }
    if (restoreZ) {
        kGxFifoByte[0] = 0x61; DAT_cc008000 = *(uint*)(DAT_8061c048 + 0x1dc);
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c83b0 | Name: gx_reset_copy_filter | Date: 2026-09-15T09:20:00Z
// Reset the EFB-copy filter to the SDK default: emit the two vertical filter
// registers 0x55/0x56 with the full 0x3ff coefficient, mark the CP FIFO dirty.
void gx_reset_copy_filter(void) {
    kGxFifoByte[0] = 0x61; DAT_cc008000 = 0x550003ff;
    kGxFifoByte[0] = 0x61; DAT_cc008000 = 0x560003ff;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c83e8 | Name: gx_store_6f | Date: 2026-09-15T09:20:00Z
// Store six single-precision floats into a caller descriptor at obj+0x10..+0x24
// (a 6-float record, e.g. a copy scale/offset or viewport tuple).
void gx_store_6f(float a, float b, float c, float d, float e, float f, int obj) {
    *(float*)(obj + 0x10) = a;
    *(float*)(obj + 0x14) = b;
    *(float*)(obj + 0x18) = c;
    *(float*)(obj + 0x1c) = d;
    *(float*)(obj + 0x20) = e;
    *(float*)(obj + 0x24) = f;
}

// ===========================================================================
// batch_1168: GX light-object build + XF light load + channel color layer
// (main.dol 0x803c8404 .. 0x803c86a4), appended after the batch_1167 EFB-copy
// execute layer. These are the GXInitLightPos / GXInitLightDir / GXInitLightAttn
// / GXInitSpecularDir builders that fill a light-object descriptor, plus the
// GXLoadLightObjImm path that streams the descriptor into XF light memory and
// the GXSetChanAmbColor / GXSetChanMatColor channel-color register setters.
//
// Additional per-file externs for this layer's math constants and the game's
// square-root helper the decompiler emitted as the SQRT intrinsic.
extern float        FLOAT_8061c080;    // 0.0f
extern float        FLOAT_8061c084;    // 1.0f
extern float        FLOAT_8061c088;    // distance-attenuation scale const
extern double       DOUBLE_8061c090;   // rsqrt Newton refine const (0.5)
extern double       DOUBLE_8061c098;   // rsqrt Newton refine const (3.0)
extern float        FLOAT_8061c0a0;    // spotlight direction scale const
extern double       math_sqrt(double x);   // game sqrt helper (SQRT intrinsic)
#ifndef SQRT
#define SQRT(x) math_sqrt((double)(x))
#endif
extern volatile unsigned int   DAT_cc008004;   // CP write-gather second port

// Address: 0x803c8404 | Name: gx_lightobj_set_pos | Date: 2026-09-15T09:40:00Z
// Store the light-object world position (3 floats) at obj+0x10..+0x18.
void gx_lightobj_set_pos(float x, float y, float z, int obj) {
    *(float*)(obj + 0x10) = x;
    *(float*)(obj + 0x14) = y;
    *(float*)(obj + 0x18) = z;
}

// Address: 0x803c8414 | Name: gx_lightobj_set_distattn | Date: 2026-09-15T09:40:00Z
// Compute and store the light-object distance-attenuation coefficients
// (k0/k1/k2 at obj+0x1c/+0x20/+0x24) from a reference distance (refDist) and
// brightness (brite) for one of the attenuation function modes (mode): mode 2
// is the full quadratic falloff, modes 1/3 are the linear/quadratic halves,
// else a flat unity. Out-of-range refDist/brite force the flat mode.
void gx_lightobj_set_distattn(double refDist, double brite, int obj, int mode) {
    float k2;
    float k1;
    double t;
    if (refDist < (double)FLOAT_8061c080) { mode = 0; }
    if ((brite <= (double)FLOAT_8061c080) || ((double)FLOAT_8061c084 <= brite)) { mode = 0; }
    if (mode == 2) {
        t = (double)(FLOAT_8061c088 * (float)((double)FLOAT_8061c084 - brite));
        k2 = (float)(t / (double)(float)(refDist * (double)(float)(brite * refDist)));
        k1 = (float)(t / (double)(float)(brite * refDist));
    } else {
        k2 = FLOAT_8061c080;
        k1 = FLOAT_8061c080;
        if (mode < 2) {
            if ((mode != 0) && (-1 < mode)) {
                k1 = (float)((double)FLOAT_8061c084 - brite) / (float)(brite * refDist);
            }
        } else if (mode < 4) {
            k2 = (float)((double)FLOAT_8061c084 - brite) /
                 (float)(refDist * (double)(float)(brite * refDist));
        }
    }
    *(float*)(obj + 0x1c) = FLOAT_8061c084;
    *(float*)(obj + 0x20) = k1;
    *(float*)(obj + 0x24) = k2;
}

// Address: 0x803c84e4 | Name: gx_lightobj_set_dir | Date: 2026-09-15T09:40:00Z
// Store the light-object direction vector (3 floats) at obj+0x28..+0x30.
void gx_lightobj_set_dir(float x, float y, float z, int obj) {
    *(float*)(obj + 0x28) = x;
    *(float*)(obj + 0x2c) = y;
    *(float*)(obj + 0x30) = z;
}

// Address: 0x803c84f4 | Name: gx_lightobj_get_dir | Date: 2026-09-15T09:40:00Z
// Read back the light-object direction vector (3 words) from obj+0x28..+0x30.
void gx_lightobj_get_dir(int obj, undefined4* ox, undefined4* oy, undefined4* oz) {
    *ox = *(undefined4*)(obj + 0x28);
    *oy = *(undefined4*)(obj + 0x2c);
    *oz = *(undefined4*)(obj + 0x30);
}

// Address: 0x803c8510 | Name: gx_lightobj_set_halfangle | Date: 2026-09-15T09:40:00Z
// Store the negated light-object half-angle / spot vector (3 floats) at
// obj+0x34..+0x3c.
void gx_lightobj_set_halfangle(float x, float y, float z, int obj) {
    *(float*)(obj + 0x34) = -x;
    *(float*)(obj + 0x38) = -y;
    *(float*)(obj + 0x3c) = -z;
}

// Address: 0x803c852c | Name: gx_lightobj_set_specular_dir | Date: 2026-09-15T09:40:00Z
// Set up a specular light: normalize the half-vector between the (negated) light
// direction and the +Z eye vector using a two-step Newton-Raphson inverse-sqrt
// refine, store it at obj+0x34..+0x3c, and store the scaled light direction at
// obj+0x28..+0x30.
void gx_lightobj_set_specular_dir(double dx, double dy, double dz, int obj) {
    double nx = -dx;
    double ny = -dy;
    double hz = (double)(float)((double)FLOAT_8061c084 - dz);
    double lenSq = (double)((float)(hz * hz) + (float)(nx * nx) + (float)(ny * ny));
    if ((double)FLOAT_8061c080 != lenSq) {
        if ((double)FLOAT_8061c080 < lenSq) {
            double r = 1.0 / SQRT(lenSq);
            r = DOUBLE_8061c090 * r * (DOUBLE_8061c098 - lenSq * r * r);
            r = DOUBLE_8061c090 * r * (DOUBLE_8061c098 - lenSq * r * r);
            lenSq = (double)(float)(lenSq * DOUBLE_8061c090 * r *
                                    (DOUBLE_8061c098 - lenSq * r * r));
        }
        lenSq = (double)(float)((double)FLOAT_8061c084 / lenSq);
    }
    *(float*)(obj + 0x34) = (float)(nx * lenSq);
    *(float*)(obj + 0x38) = (float)(ny * lenSq);
    *(float*)(obj + 0x3c) = (float)(hz * lenSq);
    double scale = (double)FLOAT_8061c0a0;
    *(float*)(obj + 0x28) = (float)(scale * dx);
    *(float*)(obj + 0x2c) = (float)(scale * dy);
    *(float*)(obj + 0x30) = (float)(scale * dz);
}

// Address: 0x803c8610 | Name: gx_lightobj_set_color | Date: 2026-09-15T09:40:00Z
// Store the light-object color word at obj+0xc.
void gx_lightobj_set_color(int obj, undefined4* color) {
    *(undefined4*)(obj + 0xc) = *color;
}

// Address: 0x803c861c | Name: gx_lightobj_get_color | Date: 2026-09-15T09:40:00Z
// Read back the light-object color word from obj+0xc.
void gx_lightobj_get_color(int obj, undefined4* out) {
    *out = *(undefined4*)(obj + 0xc);
}

// Address: 0x803c8628 | Name: gx_load_light_obj | Date: 2026-09-15T09:40:00Z
// Stream a light-object descriptor into XF light memory: emit the CP load-XF
// (opcode 0x10) header selecting the light slot (lightId -> slot via
// countLeadingZeros, base XF address 0x600, 16-word transfer 0xf0000), three
// reserved zero words, the color word (obj+0xc), then the 13 attenuation /
// position / direction floats (obj+0x10..+0x3c) alternating the two write-gather
// ports; mark the CP FIFO dirty.
void gx_load_light_obj(int obj, undefined4 lightId) {
    int clz = countLeadingZeros(lightId);
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = ((0x1f - clz) * 0x10 & 0x70U) + 0x600 | 0xf0000;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = 0;
    DAT_cc008000 = *(undefined4*)(obj + 0xc);
    DAT_cc008000 = *(undefined4*)(obj + 0x10);
    DAT_cc008004 = *(undefined4*)(obj + 0x14);
    DAT_cc008000 = *(undefined4*)(obj + 0x18);
    DAT_cc008004 = *(undefined4*)(obj + 0x1c);
    DAT_cc008000 = *(undefined4*)(obj + 0x20);
    DAT_cc008004 = *(undefined4*)(obj + 0x24);
    DAT_cc008000 = *(undefined4*)(obj + 0x28);
    DAT_cc008004 = *(undefined4*)(obj + 0x2c);
    DAT_cc008000 = *(undefined4*)(obj + 0x30);
    DAT_cc008004 = *(undefined4*)(obj + 0x34);
    DAT_cc008000 = *(undefined4*)(obj + 0x38);
    DAT_cc008004 = *(undefined4*)(obj + 0x3c);
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803c86a4 | Name: gx_set_channel_color | Date: 2026-09-15T09:40:00Z
// Program a lighting channel color register (which selects ambient=slot0 or
// material=slot1, and one of the sub-byte / full-word merge modes): build the
// merged color word from the source, emit the CP load-XF (opcode 0x10) targeting
// register 0x100a+slot, and cache it in GX state (+0xa8 ambient / +0xac material).
void gx_set_channel_color(int which, uint* src) {
    uint color;
    int slot;
    if (which == 3) {
        slot = 1;
        color = (uint)(byte)*src | *(uint*)(DAT_8061c048 + 0xac) & 0xffffff00;
    } else if (which < 3) {
        if (which == 1) {
            slot = 1;
            color = *src & 0xffffff00 | *(uint*)(DAT_8061c048 + 0xac) & 0xff;
        } else if (which < 1) {
            if (which < 0) { return; }
            slot = 0;
            color = *src & 0xffffff00 | *(uint*)(DAT_8061c048 + 0xa8) & 0xff;
        } else {
            slot = 0;
            color = (uint)(byte)*src | *(uint*)(DAT_8061c048 + 0xa8) & 0xffffff00;
        }
    } else if (which == 5) {
        color = *src;
        slot = 1;
    } else {
        if (4 < which) { return; }
        color = *src;
        slot = 0;
    }
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = slot + 0x100a;
    int off = DAT_8061c048 + slot * 4;
    DAT_cc008000 = color;
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
    *(uint*)(off + 0xa8) = color;
}

// ===========================================================================
// batch_1169: GX texture-object (GXTexObj) init/LOD layer + alpha-channel color
// + channel-control (main.dol 0x803c878c .. 0x803c8f8c), appended after the
// batch_1168 light-object layer. These are the GXInitTexObj / GXInitTexObjCI /
// GXInitTexObjLOD builders, the GXGetTexBufferSize image-size helpers, the
// texture tile-dimension lookup, and the GXSetChanCtrl / GXSetNumChans /
// second-pair channel-color setters.
//
// Additional per-file externs for this layer's LOD/bias math constants, the
// max-anisotropy code table, and the game memset helper.
extern float        FLOAT_8061c0a8;    // 16.0f  (LOD 4.4 fixed-point scale)
extern double       DOUBLE_8061c0b0;   // 2^52 int->double bias constant
extern float        FLOAT_8061c0b8;    // LOD bias clamp low
extern float        FLOAT_8061c0bc;    // LOD bias clamp mid
extern float        FLOAT_8061c0c0;    // LOD bias clamp high
extern float        FLOAT_8061c0c4;    // LOD 4.4 scale (16.0f)
extern float        FLOAT_8061c0c8;    // min-LOD clamp low (0.0f)
extern float        FLOAT_8061c0cc;    // max-LOD clamp high (10.0f)
extern unsigned char DAT_80611520[];   // filter -> max-anisotropy code table
extern void         memset_wrapper(int dst, int val, int len);

// Address: 0x803c878c | Name: gx_set_channel_color2 | Date: 2026-09-15T09:55:00Z
// Program the second pair of lighting channel color registers (state+0xb0/+0xb4,
// CP load-XF reg 0x100c/0x100d) from the source, mirroring gx_set_channel_color
// for channels 2/3.
void gx_set_channel_color2(int which, uint* src) {
    uint color;
    int slot;
    if (which == 3) {
        slot = 1;
        color = (uint)(byte)*src | *(uint*)(DAT_8061c048 + 0xb4) & 0xffffff00;
    } else if (which < 3) {
        if (which == 1) {
            slot = 1;
            color = *src & 0xffffff00 | *(uint*)(DAT_8061c048 + 0xb4) & 0xff;
        } else if (which < 1) {
            if (which < 0) { return; }
            slot = 0;
            color = *src & 0xffffff00 | *(uint*)(DAT_8061c048 + 0xb0) & 0xff;
        } else {
            slot = 0;
            color = (uint)(byte)*src | *(uint*)(DAT_8061c048 + 0xb0) & 0xffffff00;
        }
    } else if (which == 5) {
        color = *src;
        slot = 1;
    } else {
        if (4 < which) { return; }
        color = *src;
        slot = 0;
    }
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = slot + 0x100c;
    int off = DAT_8061c048 + slot * 4;
    DAT_cc008000 = color;
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
    *(uint*)(off + 0xb0) = color;
}

// Address: 0x803c8874 | Name: gx_set_num_chans | Date: 2026-09-15T09:55:00Z
// Set the number of active color channels (low 3 bits) into GX state+0x204
// bits 4..6, emit the CP load-XF num-chans register 0x1009, and flag the
// dirty bit state+0x5ac bit2.
void gx_set_num_chans(uint numChans) {
    int state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x204) =
        (numChans & 7) << 4 | *(uint*)(DAT_8061c048 + 0x204) & 0xffffff8f;
    kGxFifoByte[0] = 0x10;
    DAT_cc008000 = 0x1009;
    DAT_cc008000 = numChans & 0xff;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 4;
}

// Address: 0x803c88b0 | Name: gx_set_chan_ctrl | Date: 2026-09-15T09:55:00Z
// Program a color-channel control register (chan selects 0..3, with 4/5 also
// updating the alpha companion 0x1010/0x1011): pack the lighting-enable, ambient
// and material source-select, the attenuation-enable + diffuse-function fields,
// and the 8-bit light mask, then emit CP load-XF reg 0x100e+chan.
void gx_set_chan_ctrl(uint chan, uint matSrc, uint ambSrc, uint diffFn,
                      uint lightMask, uint attnFn, int attnEnable) {
    if (attnEnable == 0) { attnFn = 0; }
    int a = -attnEnable + 2;
    kGxFifoByte[0] = 0x10;
    uint word =
        (lightMask & 0xf0) << 7 |
        (lightMask & 0xf) << 2 |
        (uint)(attnEnable != 0) * 0x400 |
        (a - ((uint)(a == 0) + -attnEnable + 1)) * 0x200 & 0x200 |
        (attnFn & 3) << 7 | (ambSrc & 1) << 6 | diffFn & 1 | (matSrc & 1) << 1;
    DAT_cc008000 = (chan & 3) + 0x100e;
    DAT_cc008000 = word;
    if (chan == 4) {
        kGxFifoByte[0] = 0x10;
        DAT_cc008000 = 0x1010;
        DAT_cc008000 = word;
    } else if (chan == 5) {
        kGxFifoByte[0] = 0x10;
        DAT_cc008000 = 0x1011;
        DAT_cc008000 = word;
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803c8960 | Name: gx_calc_tex_size | Date: 2026-09-15T09:55:00Z
// Compute the byte size of a texture image (GXGetTexBufferSize): pick the tile
// width/height shifts and per-tile byte count (0x20 or 0x40 for the RGBA8/compat
// formats) from the format, then either sum the whole mip chain (mipmap != 0,
// mipCount levels halving each dimension) or a single level.
int gx_calc_tex_size(uint width, uint height, int format, char mipmap, uint mipCount) {
    int tileW;
    int tileH;
    switch (format) {
    case 0: case 8: case 0xe: case 0x20: case 0x30:
        tileW = 3; tileH = 3; break;
    case 1: case 2: case 9: case 0x11: case 0x22: case 0x27:
    case 0x28: case 0x29: case 0x2a: case 0x39: case 0x3a:
        tileW = 3; tileH = 2; break;
    case 3: case 4: case 5: case 6: case 10: case 0x13:
    case 0x16: case 0x23: case 0x2b: case 0x2c: case 0x3c:
        tileW = 2; tileH = 2; break;
    default:
        tileH = 0; tileW = 0; break;
    }
    int tileBytes = ((format == 6) || (format == 0x16)) ? 0x40 : 0x20;
    int total;
    if (mipmap == '\x01') {
        total = 0;
        for (uint n = mipCount & 0xff; n != 0; n = n - 1) {
            uint w = width & 0xffff;
            uint h = height & 0xffff;
            total += tileBytes * ((int)(w + (1 << tileW) - 1) >> tileW) *
                                 ((int)(h + (1 << tileH) - 1) >> tileH);
            if ((w == 1) && (h == 1)) { return total; }
            width  = ((width  & 0xffff) < 2) ? 1 : (int)w >> 1;
            height = ((height & 0xffff) < 2) ? 1 : (int)h >> 1;
        }
    } else {
        total = tileBytes * ((int)((width  & 0xffff) + (1 << tileW) - 1) >> tileW) *
                            ((int)((height & 0xffff) + (1 << tileH) - 1) >> tileH);
    }
    return total;
}

// Address: 0x803c8abc | Name: gx_get_tex_tile_dims | Date: 2026-09-15T09:55:00Z
// Texture tile-dimension lookup used by the EFB copy-format path (referenced as
// an extern by batch_1160 gx_set_tex_copy_fmt): pick the tile width/height
// shifts from the format, output the tiles-across (*tilesW) and tiles-down
// (*tilesH) for a width x height image, and the per-tile card count (*cards,
// 2 for the 0x40-byte formats else 1).
void gx_get_tex_tile_dims(int format, unsigned short width, unsigned short height,
                          int* tilesW, int* tilesH, undefined4* cards) {
    uint w = (uint)width;
    uint h = (uint)height;
    int shW;
    int shH;
    switch (format) {
    case 0: case 8: case 0xe: case 0x20: case 0x30:
        shW = 3; shH = 3; break;
    case 1: case 2: case 9: case 0x11: case 0x22: case 0x27:
    case 0x28: case 0x29: case 0x2a: case 0x39: case 0x3a:
        shW = 3; shH = 2; break;
    case 3: case 4: case 5: case 6: case 10: case 0x13:
    case 0x16: case 0x23: case 0x2b: case 0x2c: case 0x3c:
        shW = 2; shH = 2; break;
    default:
        shH = 0; shW = 0; break;
    }
    if (width == 0)  { w = 1; }
    if (height == 0) { h = 1; }
    *tilesW = (int)(w + (1 << shW) - 1) >> shW;
    *tilesH = (int)(h + (1 << shH) - 1) >> shH;
    bool wide = (format == 6) || (format == 0x16);
    *cards = wide ? 2 : 1;
}

// Address: 0x803c8b84 | Name: gx_texobj_init | Date: 2026-09-15T09:55:00Z
// Initialize a GXTexObj (0x20-byte descriptor): zero it, pack the wrap S/T modes
// (word0 bits 0..3), the mip-enable + edge-LOD flags, the format nibble and
// data pointer, the (w-1)/(h-1) dimensions, the tile-size, and (for mipmapped
// textures) the max-LOD from a countLeadingZeros of the larger dimension. Sets
// the "initialized" bits in the trailing flag byte (obj+0x1f).
void gx_texobj_init(uint* obj, uint dataAddr, uint width, uint height, uint format,
                    uint wrapS, uint wrapT, char mipmap) {
    int shW;
    int shH;
    memset_wrapper((int)obj, 0, 0x20);
    obj[0] = wrapS & 3 | obj[0] & 0xfffffffc;
    obj[0] = (wrapT & 3) << 2 | obj[0] & 0xfffffff3;
    obj[0] = obj[0] & 0xffffffef | 0x10;
    if (mipmap == '\0') {
        obj[0] = obj[0] & 0xffffff1f | 0x80;
    } else {
        *(byte*)((int)obj + 0x1f) = *(byte*)((int)obj + 0x1f) | 1;
        if (format - 8 < 3) {
            obj[0] = obj[0] & 0xffffff1f | 0xa0;
        } else {
            obj[0] = obj[0] & 0xffffff1f | 0xc0;
        }
        int clz;
        if ((height & 0xffff) < (width & 0xffff)) {
            clz = countLeadingZeros(width & 0xffff);
        } else {
            clz = countLeadingZeros(height & 0xffff);
        }
        obj[1] = ((int)(FLOAT_8061c0a8 *
                        (float)((double)CONCAT44(0x43300000, 0x1f - clz) - DOUBLE_8061c0b0)) & 0xffU)
                 << 8 | obj[1] & 0xffff00ff;
    }
    obj[5] = format;
    obj[2] = (width & 0xffff) - 1 & 0x3ff | obj[2] & 0xfffffc00;
    obj[2] = ((height & 0xffff) - 1) * 0x400 & 0xffc00 | obj[2] & 0xfff003ff;
    obj[2] = (format & 0xf) << 0x14 | obj[2] & 0xff0fffff;
    obj[3] = dataAddr >> 5 & 0x1fffff | obj[3] & 0xffe00000;
    switch (format & 0xf) {
    case 0: case 8:
        *(unsigned char*)((int)obj + 0x1e) = 1; shW = 3; shH = 3; break;
    case 1: case 2: case 9:
        *(unsigned char*)((int)obj + 0x1e) = 2; shW = 3; shH = 2; break;
    case 3: case 4: case 5: case 10:
        *(unsigned char*)((int)obj + 0x1e) = 2; shW = 2; shH = 2; break;
    case 6:
        *(unsigned char*)((int)obj + 0x1e) = 3; shW = 2; shH = 2; break;
    default:
        *(unsigned char*)((int)obj + 0x1e) = 2; shW = 2; shH = 2; break;
    case 0xe:
        *(unsigned char*)((int)obj + 0x1e) = 0; shW = 3; shH = 3; break;
    }
    *(unsigned short*)(obj + 7) =
        (short)((int)((width & 0xffff) + (1 << shW) - 1) >> shW) *
        (short)((int)((height & 0xffff) + (1 << shH) - 1) >> shH) & 0x7fff;
    *(byte*)((int)obj + 0x1f) = *(byte*)((int)obj + 0x1f) | 2;
}

// Address: 0x803c8dd0 | Name: gx_texobj_init_ci | Date: 2026-09-15T09:55:00Z
// Initialize a color-indexed GXTexObj (GXInitTexObjCI): delegate to
// gx_texobj_init, then clear the "not-CI" flag bit and stash the TLUT name at
// obj[6].
void gx_texobj_init_ci(uint* obj, uint dataAddr, uint width, uint height, uint format,
                       uint wrapS, uint wrapT, char mipmap, uint tlutName) {
    gx_texobj_init(obj, dataAddr, width, height, format, wrapS, wrapT, mipmap);
    *(byte*)((int)obj + 0x1f) = *(byte*)((int)obj + 0x1f) & 0xfd;
    obj[6] = tlutName;
}

// Address: 0x803c8e18 | Name: gx_texobj_init_lod | Date: 2026-09-15T09:55:00Z
// Program the LOD/filter fields of a GXTexObj (GXInitTexObjLOD): pack the max
// anisotropy (from maxAniso via the DAT_80611520 code table), the mag/min filter
// selects, the edge-LOD + bias-clamp + do-edge-LOD flags, the LOD bias (clamped),
// and the min/max LOD (each clamped) as 4.4 fixed-point in obj[1].
void gx_texobj_init_lod(double lodBias, double minLod, double maxLod, uint* obj,
                        int minFilter, int magFilter, uint biasClamp, char edgeLod,
                        uint maxAniso) {
    double b = (double)FLOAT_8061c0b8;
    if ((b <= maxLod) && (b = maxLod, (double)FLOAT_8061c0bc <= maxLod)) {
        b = (double)FLOAT_8061c0c0;
    }
    obj[0] = ((int)((double)FLOAT_8061c0c4 * b) & 0xffU) << 9 | obj[0] & 0xfffe01ff;
    obj[0] = (uint)(magFilter == 1) << 4 | obj[0] & 0xffffffef;
    obj[0] = ((byte)DAT_80611520[minFilter] & 7) << 5 | obj[0] & 0xffffff1f;
    obj[0] = (uint)(edgeLod == '\0') << 8 | obj[0] & 0xfffffeff;
    obj[0] = obj[0] & 0xfffdffff;
    obj[0] = obj[0] & 0xfffbffff;
    obj[0] = (maxAniso & 3) << 0x13 | obj[0] & 0xffe7ffff;
    obj[0] = (biasClamp & 1) << 0x15 | obj[0] & 0xffdfffff;
    double lo = (double)FLOAT_8061c0c8;
    if ((lo <= lodBias) && (lo = lodBias, (double)FLOAT_8061c0cc < lodBias)) {
        lo = (double)FLOAT_8061c0cc;
    }
    double hi = (double)FLOAT_8061c0c8;
    if ((hi <= minLod) && (hi = minLod, (double)FLOAT_8061c0cc < minLod)) {
        hi = (double)FLOAT_8061c0cc;
    }
    double scale = (double)FLOAT_8061c0a8;
    obj[1] = (int)((double)FLOAT_8061c0a8 * lo) & 0xffU | obj[1] & 0xffffff00;
    obj[1] = ((int)(scale * hi) & 0xffU) << 8 | obj[1] & 0xffff00ff;
}

// Address: 0x803c8f7c | Name: gx_texobj_set_mipmap_ptr | Date: 2026-09-15T09:55:00Z
// Set the texture-object image data pointer (30-bit physical address >> 5) in
// obj+0xc, preserving the high format bits.
void gx_texobj_set_mipmap_ptr(int obj, uint dataAddr) {
    *(uint*)(obj + 0xc) = dataAddr >> 5 & 0x1fffff | *(uint*)(obj + 0xc) & 0xffe00000;
}

// Address: 0x803c8f8c | Name: gx_texobj_set_wrap | Date: 2026-09-15T09:55:00Z
// Set the texture-object wrap S (bits 0..1) and wrap T (bits 2..3) modes in
// word0.
void gx_texobj_set_wrap(uint* obj, uint wrapS, uint wrapT) {
    obj[0] = wrapS & 3 | obj[0] & 0xfffffffc;
    obj[0] = (wrapT & 3) << 2 | obj[0] & 0xfffffff3;
}

// ===========================================================================
// batch_1170: GX texture-object filter/LOD setters + GXTexObj accessors
// (main.dol 0x803c8fa8 .. 0x803c9080), appended after the batch_1169 texobj
// init/LOD layer. These are the GXInitTexObjFilterMode / GXInitTexObjMaxLOD /
// GXInitTexObjUserData setters and the GXGetTexObjData / GXGetTexObjWidth /
// Height / Fmt / WrapS / WrapT accessor readers. No new externs (reuses the
// batch_1169 LOD constants and the DAT_80611520 anisotropy table).

// Address: 0x803c8fa8 | Name: gx_texobj_set_filter | Date: 2026-09-15T10:10:00Z
// Set the texture-object mag filter (word0 bit4) and min filter (word0 bits
// 5..7 via the DAT_80611520 max-anisotropy code table).
void gx_texobj_set_filter(uint* obj, int minFilter, int magFilter) {
    obj[0] = (uint)(magFilter == 1) << 4 | obj[0] & 0xffffffef;
    obj[0] = ((byte)DAT_80611520[minFilter] & 7) << 5 | obj[0] & 0xffffff1f;
}

// Address: 0x803c8fe0 | Name: gx_texobj_set_max_lod | Date: 2026-09-15T10:10:00Z
// Set the texture-object max LOD (clamped to [0, 10]) as 4.4 fixed-point in the
// low byte of obj+4.
void gx_texobj_set_max_lod(double maxLod, int obj) {
    double v = (double)FLOAT_8061c0c8;
    if ((v <= maxLod) && (v = maxLod, (double)FLOAT_8061c0cc < maxLod)) {
        v = (double)FLOAT_8061c0cc;
    }
    *(uint*)(obj + 4) =
        (int)((double)FLOAT_8061c0a8 * v) & 0xffU | *(uint*)(obj + 4) & 0xffffff00;
}

// Address: 0x803c9030 | Name: gx_texobj_set_userdata | Date: 2026-09-15T10:10:00Z
// Store the texture-object user-data word at obj+0x10.
void gx_texobj_set_userdata(int obj, undefined4 userData) {
    *(undefined4*)(obj + 0x10) = userData;
}

// Address: 0x803c9038 | Name: gx_texobj_get_userdata | Date: 2026-09-15T10:10:00Z
// Read back the texture-object user-data word from obj+0x10.
undefined4 gx_texobj_get_userdata(int obj) {
    return *(undefined4*)(obj + 0x10);
}

// Address: 0x803c9040 | Name: gx_texobj_get_image_ptr | Date: 2026-09-15T10:10:00Z
// Decode the texture-object image data pointer: the 21-bit field in obj+0xc
// scaled back up by 32 (<< 5).
int gx_texobj_get_image_ptr(int obj) {
    return (*(uint*)(obj + 0xc) & 0x1fffff) << 5;
}

// Address: 0x803c904c | Name: gx_texobj_get_width | Date: 2026-09-15T10:10:00Z
// Get the texture width: the low 10-bit (w-1) field in obj+8, plus 1.
int gx_texobj_get_width(int obj) {
    return (*(uint*)(obj + 8) & 0x3ff) + 1;
}

// Address: 0x803c905c | Name: gx_texobj_get_height | Date: 2026-09-15T10:10:00Z
// Get the texture height: the next 10-bit (h-1) field in obj+8, plus 1.
int gx_texobj_get_height(int obj) {
    return (*(uint*)(obj + 8) >> 10 & 0x3ff) + 1;
}

// Address: 0x803c906c | Name: gx_texobj_get_format | Date: 2026-09-15T10:10:00Z
// Read the texture-object format word from obj+0x14.
undefined4 gx_texobj_get_format(int obj) {
    return *(undefined4*)(obj + 0x14);
}

// Address: 0x803c9074 | Name: gx_texobj_get_wrap_s | Date: 2026-09-15T10:10:00Z
// Get the texture wrap-S mode (word0 bits 0..1).
uint gx_texobj_get_wrap_s(uint* obj) {
    return obj[0] & 3;
}

// Address: 0x803c9080 | Name: gx_texobj_get_wrap_t | Date: 2026-09-15T10:10:00Z
// Get the texture wrap-T mode (word0 bits 2..3).
uint gx_texobj_get_wrap_t(uint* obj) {
    return obj[0] >> 2 & 3;
}

// ===========================================================================
// batch_1171: GXTexObj mip/LOD getters + channel-color-load layer + TLUT/tex
// region init (GXInitTlutObj / GXLoadTlut / GXInitTexCacheRegion /
// GXInitTlutRegion) + GXInvalidateTexAll. Appended after the batch_1170
// texobj accessor readers.
// ---------------------------------------------------------------------------
// Additional per-file externs for this batch:
extern float         FLOAT_8061c0d0;    // LOD 4.4 fixed-point -> float scale (1/16)
extern unsigned char DAT_806114e8;      // channel-color reg-A high-byte code table
extern unsigned char DAT_806114f0;      // channel-color reg-B high-byte code table
extern unsigned char DAT_806114f8;      // channel-color reg-C high-byte code table
extern unsigned char DAT_80611500;      // channel-color reg-D high-byte code table
extern unsigned char DAT_80611508;      // channel-color reg-E high-byte code table
extern unsigned char DAT_80611510;      // channel-color reg-F high-byte code table
extern unsigned char DAT_80611518;      // channel-color alpha companion code table

// Address: 0x803c908c | Name: gx_texobj_get_mipmap | Date: 2026-09-15T10:35:00Z
// GXGetTexObjMipMap: return the mip-enable flag. The decompiler renders the
// (bit0 - 1) countLeadingZeros trick that maps {bit set -> clz(0)=32 -> 1,
// bit clear -> clz(-1)=0 -> 0}; kept verbatim (NON-MATCHING intrinsic form).
uint gx_texobj_get_mipmap(int obj) {
    uint n;
    n = countLeadingZeros((*(byte*)(obj + 0x1f) & 1) - 1);
    return n >> 5 & 0xff;
}

// Address: 0x803c90a4 | Name: gx_texobj_get_min_lod | Date: 2026-09-15T10:35:00Z
// GXGetTexObjMinLOD: decode the min-LOD from the low byte of obj+4 as a 4.4
// fixed-point value converted to float (int->double CONCAT44 bias, then *1/16).
double gx_texobj_get_min_lod(int obj) {
    return (double)((float)((double)CONCAT44(0x43300000, *(uint*)(obj + 4) & 0xff) -
                            DOUBLE_8061c0b0) * FLOAT_8061c0d0);
}

// Address: 0x803c90d8 | Name: gx_texobj_get_max_lod | Date: 2026-09-15T10:35:00Z
// GXGetTexObjMaxLOD: decode the max-LOD from the second byte of obj+4 as a
// 4.4 fixed-point value converted to float.
double gx_texobj_get_max_lod(int obj) {
    return (double)((float)((double)CONCAT44(0x43300000, *(uint*)(obj + 4) >> 8 & 0xff) -
                            DOUBLE_8061c0b0) * FLOAT_8061c0d0);
}

// Address: 0x803c910c | Name: gx_load_texobj_regs | Date: 2026-09-15T10:35:00Z
// GXLoadTexObj preload path: stamp the per-region BP opcode high-bytes (from
// the DAT_806114e8..DAT_80611518 code tables indexed by the TMEM region id)
// into the six descriptor words, stream them out through the CP write-gather
// port with the 0x61 BP-load opcode, resolve the CI/TLUT companion word for
// non-CI textures, then cache the SETMODE0/SETIMAGE words in the GX state
// block and flag the CP FIFO dirty. NON-MATCHING: flat state-block offsets and
// write-gather packing preserved verbatim.
void gx_load_texobj_regs(uint* obj, uint* companion, int region) {
    int p;
    int state;

    obj[0] = (uint)(byte)(&DAT_806114e8)[region] << 0x18 | obj[0] & 0xffffff;
    obj[1] = (uint)(byte)(&DAT_806114f0)[region] << 0x18 | obj[1] & 0xffffff;
    obj[2] = (uint)(byte)(&DAT_806114f8)[region] << 0x18 | obj[2] & 0xffffff;
    companion[0] = (uint)(byte)(&DAT_80611500)[region] << 0x18 | companion[0] & 0xffffff;
    companion[1] = (uint)(byte)(&DAT_80611508)[region] << 0x18 | companion[1] & 0xffffff;
    obj[3] = (uint)(byte)(&DAT_80611510)[region] << 0x18 | obj[3] & 0xffffff;

    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = obj[0];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = obj[1];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = obj[2];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = companion[0];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = companion[1];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = obj[3];

    if ((*(byte*)((int)obj + 0x1f) & 2) == 0) {
        p = (*(int (**)(uint))(DAT_8061c048 + 0x4cc))(obj[6]);
        *(uint*)(p + 4) = (uint)(byte)(&DAT_80611518)[region] << 0x18 | *(uint*)(p + 4) & 0xffffff;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(p + 4);
    }
    state = DAT_8061c048;
    p = DAT_8061c048 + region * 4;
    *(uint*)(p + 0x514) = obj[2];
    *(uint*)(p + 0x534) = obj[0];
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 1;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803c9288 | Name: gx_load_texobj | Date: 2026-09-15T10:35:00Z
// GXLoadTexObj: resolve the second (companion) descriptor pointer from the GX
// state block, then delegate to gx_load_texobj_regs.
void gx_load_texobj(uint* obj, int region) {
    uint* companion;
    companion = (uint*)(*(uint* (**)(void))(DAT_8061c048 + 0x4c8))();
    gx_load_texobj_regs(obj, companion, region);
}

// Address: 0x803c92dc | Name: gx_tlutobj_init | Date: 2026-09-15T10:35:00Z
// GXInitTlutObj: pack a TLUT descriptor - the TLUT format (word0 bits 10..11),
// the load address >>5 (word1 21-bit field), the fixed 0x64 SETIMAGE opcode in
// the high byte of word1, and the entry count in the halfword at obj+8.
void gx_tlutobj_init(uint* obj, uint load_addr, uint fmt, unsigned short count) {
    obj[0] = 0;
    obj[0] = (fmt & 3) << 10 | obj[0] & 0xfffff3ff;
    obj[1] = load_addr >> 5 & 0x1fffff | obj[1] & 0xffe00000;
    obj[1] = obj[1] & 0xffffff | 0x64000000;
    *(unsigned short*)(obj + 2) = count;
}

// Address: 0x803c9314 | Name: gx_load_tlut | Date: 2026-09-15T10:35:00Z
// GXLoadTlut: resolve the TMEM region companion, wait for CP idle, stream the
// TLUT load command (SETIMAGE + region base) via the write-gather port, then
// merge the region base into the descriptor and mirror the four descriptor
// words back into the region record.
void gx_load_tlut(uint* obj, undefined4 region_id) {
    uint w1;
    uint* region;

    region = (uint*)(*(uint* (**)(undefined4))(DAT_8061c048 + 0x4cc))(region_id);
    gx_flush_indbump_reg();
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = obj[1];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = region[0];
    gx_flush_indbump_reg();
    obj[0] = region[0] & 0x3ff | obj[0] & 0xfffffc00;
    w1 = obj[1];
    region[1] = obj[0];
    region[2] = w1;
    region[3] = obj[2];
}

// Address: 0x803c93ac | Name: gx_texcache_region_init | Date: 2026-09-15T10:35:00Z
// GXInitTexCacheRegion: pack a texture-cache TMEM region descriptor. Both the
// even/odd cache-bank size selects are mapped through the same size->code
// switch (param_4 for word0, param_6 for word1), each producing the cache
// size-code in bits 15..17 / 18..20 alongside the TMEM base (>>5, 15-bit) and
// the format bits; obj+0xc/obj+0xd mark the region as an initialized cache.
void gx_texcache_region_init(uint* obj, unsigned char fmt, uint tmem_even, int size_even,
                             uint tmem_odd, int size_odd, uint code) {
    if (size_even == 1) {
        code = 4;
    } else if (size_even < 1) {
        if (-1 < size_even) {
            code = 3;
        }
    } else if (size_even < 3) {
        code = 5;
    }
    obj[0] = 0;
    obj[0] = tmem_even >> 5 & 0x7fff | obj[0] & 0xffff8000;
    obj[0] = (code & 7) << 0xf | obj[0] & 0xfffc7fff;
    obj[0] = (code & 7) << 0x12 | obj[0] & 0xffe3ffff;
    obj[0] = obj[0] & 0xffdfffff;
    if (size_odd == 2) {
        code = 5;
    } else if (size_odd < 2) {
        if (size_odd == 0) {
            code = 3;
        } else if (-1 < size_odd) {
            code = 4;
        }
    } else if (size_odd < 4) {
        code = 0;
    }
    obj[1] = 0;
    obj[1] = tmem_odd >> 5 & 0x7fff | obj[1] & 0xffff8000;
    obj[1] = (code & 7) << 0xf | obj[1] & 0xfffc7fff;
    obj[1] = (code & 7) << 0x12 | obj[1] & 0xffe3ffff;
    *(unsigned char*)(obj + 3) = fmt;
    *(unsigned char*)((int)obj + 0xd) = 1;
}

// Address: 0x803c94a0 | Name: gx_tlut_region_init | Date: 2026-09-15T10:35:00Z
// GXInitTlutRegion: pack a TLUT TMEM region descriptor - the TMEM base biased
// by 0x80000 then >>9 (10-bit field), the entry-count size (11-bit field), and
// the fixed 0x65 SETIMAGE opcode in the high byte of word0.
void gx_tlut_region_init(uint* obj, int tmem_addr, uint size) {
    obj[0] = 0;
    obj[0] = tmem_addr - 0x80000U >> 9 & 0x3ff | obj[0] & 0xfffffc00;
    obj[0] = (size & 0x7ff) << 10 | obj[0] & 0xffe003ff;
    obj[0] = obj[0] & 0xffffff | 0x65000000;
}

// Address: 0x803c94d8 | Name: gx_invalidate_tex_all | Date: 2026-09-15T10:35:00Z
// GXInvalidateTexAll: wait for CP idle, emit the two BP texture-cache
// invalidate commands (0x66001000 / 0x66001100) through the write-gather port,
// then wait again. Called from the frame present / FIFO-recover / material
// bind paths.
void gx_invalidate_tex_all(void) {
    gx_flush_indbump_reg();
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x66001000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x66001100;
    gx_flush_indbump_reg();
}

// ===========================================================================
// batch_1172: GX texture-region callbacks + texcoord-scale commit + indirect-
// texture layer (GXSetTexRegionCallback / GXSetTlutRegionCallback /
// GXSetTexCoordScaleManually loop / GXSetTevIndirect / GXSetIndTexMtx /
// GXSetTevDirect-order / GXSetNumIndStages). Appended after the batch_1171
// texobj/TLUT region-load layer.
// ---------------------------------------------------------------------------
// Additional per-file extern for this batch:
extern float FLOAT_8061c0d8;   // indirect-matrix element quantize scale (1024.0f)

// Address: 0x803c9520 | Name: gx_set_texregion_callback | Date: 2026-09-15T11:05:00Z
// GXSetTexRegionCallback: install the texture-region allocation callback in the
// GX state block (+0x4c8) and return the previous one.
undefined4 gx_set_texregion_callback(undefined4 cb) {
    undefined4 prev;
    prev = *(undefined4*)(DAT_8061c048 + 0x4c8);
    *(undefined4*)(DAT_8061c048 + 0x4c8) = cb;
    return prev;
}

// Address: 0x803c9534 | Name: gx_set_tlutregion_callback | Date: 2026-09-15T11:05:00Z
// GXSetTlutRegionCallback: install the TLUT-region allocation callback in the
// GX state block (+0x4cc) and return the previous one.
undefined4 gx_set_tlutregion_callback(undefined4 cb) {
    undefined4 prev;
    prev = *(undefined4*)(DAT_8061c048 + 0x4cc);
    *(undefined4*)(DAT_8061c048 + 0x4cc) = cb;
    return prev;
}

// Address: 0x803c9548 | Name: gx_commit_texcoord_scale | Date: 2026-09-15T11:05:00Z
// Commit one texture-coordinate scale pair: copy the cached (w-1)/(h-1) dims and
// the wrap-mode bilinear-enable bits from the source texmap region record into
// the destination texcoord's SU_SSIZE/SU_TSIZE registers (state +0xb8/+0xd8),
// then stream both via the CP write-gather 0x61 BP-load opcode. NON-MATCHING:
// the countLeadingZeros(1-wrap) trick maps wrap==REPEAT/MIRROR -> bilinear bit.
void gx_commit_texcoord_scale(int src_texmap, int dst_coord) {
    int state;
    int src;
    uint dims;
    int dst;
    uint word0;

    state = DAT_8061c048;
    src = DAT_8061c048 + src_texmap * 4;
    dims = *(uint*)(src + 0x514);
    dst = DAT_8061c048 + dst_coord * 4;
    *(uint*)(dst + 0xb8) = dims & 0x3ff | *(uint*)(dst + 0xb8) & 0xffff0000;
    *(uint*)(dst + 0xd8) = dims >> 10 & 0x3ff | *(uint*)(dst + 0xd8) & 0xffff0000;
    word0 = *(uint*)(src + 0x534);
    dims = countLeadingZeros(1 - (word0 & 3));
    *(uint*)(dst + 0xb8) = (dims >> 5 & 1) << 0x10 | *(uint*)(dst + 0xb8) & 0xfffeffff;
    dims = countLeadingZeros(1 - (word0 >> 2 & 3));
    *(uint*)(dst + 0xd8) = (dims >> 5 & 1) << 0x10 | *(uint*)(dst + 0xd8) & 0xfffeffff;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(dst + 0xb8);
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(dst + 0xd8);
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803c95e8 | Name: gx_refresh_texcoord_scales | Date: 2026-09-15T11:05:00Z
// GXSetTexCoordScaleManually refresh pass: walk every active texcoord-gen
// (count in state+0x204 bits 16..18), map each to its source texmap via the
// RAS1_TREF-style triples in state+0x120, and commit the scale for any texmap
// not flagged in the state+0x594 override mask; then walk the direct texmap set
// (count in state+0x204 bits 10..13) and commit scales for the entries enabled
// in state+0x598. NON-MATCHING: register bit extraction preserved verbatim.
void gx_refresh_texcoord_scales(void) {
    uint word0;
    uint texmap;
    uint chans;
    uint* pref;
    int off;
    uint src = 0;
    uint dst = 0;
    uint i;

    if (*(int*)(DAT_8061c048 + 0x594) != 0xff) {
        chans = *(uint*)(DAT_8061c048 + 0x204);
        for (i = 0; i < (chans >> 0x10 & 7); i = i + 1) {
            if (i == 2) {
                src = *(uint*)(DAT_8061c048 + 0x120) >> 0xc & 7;
                dst = *(uint*)(DAT_8061c048 + 0x120) >> 0xf & 7;
            } else if ((int)i < 2) {
                if (i == 0) {
                    src = *(uint*)(DAT_8061c048 + 0x120) & 7;
                    dst = *(uint*)(DAT_8061c048 + 0x120) >> 3 & 7;
                } else if (-1 < (int)i) {
                    src = *(uint*)(DAT_8061c048 + 0x120) >> 6 & 7;
                    dst = *(uint*)(DAT_8061c048 + 0x120) >> 9 & 7;
                }
            } else if ((int)i < 4) {
                src = *(uint*)(DAT_8061c048 + 0x120) >> 0x12 & 7;
                dst = *(uint*)(DAT_8061c048 + 0x120) >> 0x15 & 7;
            }
            if ((*(uint*)(DAT_8061c048 + 0x594) & 1 << dst) == 0) {
                gx_commit_texcoord_scale(src, dst);
            }
        }
        off = 0;
        for (i = 0; i < (chans >> 10 & 0xf) + 1; i = i + 1) {
            pref = (uint*)(DAT_8061c048 + (i & 0x7ffffffe) * 2 + 0x100);
            word0 = *(uint*)(DAT_8061c048 + off + 0x554) & 0xfffffeff;
            if ((i & 1) == 0) {
                texmap = *pref >> 3;
            } else {
                texmap = *pref >> 0xf;
            }
            if (((word0 != 0xff) && ((*(uint*)(DAT_8061c048 + 0x594) & 1 << (texmap & 7)) == 0)) &&
                ((*(uint*)(DAT_8061c048 + 0x598) & 1 << i) != 0)) {
                gx_commit_texcoord_scale(word0, texmap & 7);
            }
            off = off + 4;
        }
    }
}

// Address: 0x803c9764 | Name: gx_load_ind_matrix_preset | Date: 2026-09-15T11:05:00Z
// Stream one of three indirect-texture-matrix presets to the BP registers
// (IND_MTXA/B/C banks 0x8c-0x93 and 0xac-0xb3) via the write-gather port. The
// preset index selects the identity/half/quarter scaling layouts. NON-MATCHING:
// the raw 32-bit BP command words are emitted verbatim.
void gx_load_ind_matrix_preset(int preset) {
    if (preset == 1) {
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8c0d8000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x900dc000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8d0d8800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x910dc800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8e0d9000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x920dd000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8f0d9800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x930dd800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xac0da000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb00de000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xad0da800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb10de800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xae0db000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb20df000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xaf0db800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb30df800;
    } else if ((preset < 1) || (2 < preset)) {
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8c0d8000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x900dc000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8d0d8400;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x910dc400;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8e0d8800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x920dc800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8f0d8c00;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x930dcc00;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xac0d9000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb00dd000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xad0d9400;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb10dd400;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xae0d9800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb20dd800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xaf0d9c00;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb30ddc00;
    } else {
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8c0d8000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x900dc000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8d0d8800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x910dc800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8e0d9000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x920dd000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x8f0d9800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x930dd800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xac0da000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb00dc400;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xad0da800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb10dcc00;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xae0db000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb20dd400;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xaf0db800;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0xb30ddc00;
    }
}

// Address: 0x803c9ab8 | Name: gx_set_tev_indirect | Date: 2026-09-15T11:05:00Z
// GXSetTevIndirect: pack a TEV-stage indirect-texture control BP register
// (opcode base 0x10 + stage) from the indirect stage/format/bias/matrix-select/
// wrap/add-prev/utc-lod/alpha-select fields, then flag the CP FIFO dirty.
void gx_set_tev_indirect(int stage, uint ind_stage, uint fmt, uint bias, uint mtx_sel,
                         uint wrap_s, uint wrap_t, uint add_prev, byte utc_lod, uint alpha_sel) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (stage + 0x10) * 0x1000000 |
                   (add_prev & 1) << 0x14 |
                   (utc_lod & 1) << 0x13 |
                   (wrap_t & 7) << 0x10 |
                   (wrap_s & 7) << 0xd |
                   (mtx_sel & 0xf) << 9 |
                   (alpha_sel & 3) << 7 | (bias & 7) << 4 | (fmt & 3) << 2 | ind_stage & 3;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c9b24 | Name: gx_set_ind_tex_matrix | Date: 2026-09-15T11:05:00Z
// GXSetIndTexMtx: program a 2x3 indirect-texture matrix. The matrix id selects
// one of the IND_MTXA/B/C register triples (base index computed from the id),
// each element quantized to 11 bits (* FLOAT_8061c0d8 == 1024.0f) with the
// two-bit scale exponent (id offset +0x11) spread across the three registers.
void gx_set_ind_tex_matrix(int mtx_id, float* m, char scale_exp) {
    int base;
    uint exp;

    if (mtx_id != 8) {
        if (mtx_id < 8) {
            if (mtx_id != 4) {
                if (3 < mtx_id) { base = mtx_id + -5; goto have_base; }
                if (0 < mtx_id) { base = mtx_id + -1; goto have_base; }
            }
        } else if (mtx_id < 0xc) {
            base = mtx_id + -9; goto have_base;
        }
    }
    base = 0;
have_base:
    base = base * 3;
    exp = (uint)(char)(scale_exp + '\x11');
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (base + 6) * 0x1000000 |
                   (exp & 3) << 0x16 |
                   ((int)(FLOAT_8061c0d8 * m[3]) & 0x7ffU) << 0xb |
                   (int)(FLOAT_8061c0d8 * m[0]) & 0x7ffU;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (base + 7) * 0x1000000 |
                   (exp >> 2 & 3) << 0x16 |
                   ((int)(FLOAT_8061c0d8 * m[4]) & 0x7ffU) << 0xb |
                   (int)(FLOAT_8061c0d8 * m[1]) & 0x7ffU;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (base + 8) * 0x1000000 |
                   (exp >> 4 & 3) << 0x16 |
                   ((int)(FLOAT_8061c0d8 * m[5]) & 0x7ffU) << 0xb |
                   (int)(FLOAT_8061c0d8 * m[2]) & 0x7ffU;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c9c9c | Name: gx_set_ind_tex_coord_scale | Date: 2026-09-15T11:05:00Z
// GXSetIndTexCoordScale: program the indirect-texture coordinate scale (S/T
// wrap-shift) fields for one of the four indirect stages into the RAS1_SS0/SS1
// BP registers (0x25/0x26) held in state +0x128/+300, then stream the updated
// word. Even/odd stage index selects the low/high nibble pair.
void gx_set_ind_tex_coord_scale(int stage, uint scale_s, uint scale_t) {
    int state;

    state = DAT_8061c048;
    if (stage == 2) {
        *(uint*)(DAT_8061c048 + 300) = scale_s & 0xf | *(uint*)(DAT_8061c048 + 300) & 0xfffffff0;
        *(uint*)(state + 300) = (scale_t & 0xf) << 4 | *(uint*)(state + 300) & 0xffffff0f;
        *(uint*)(state + 300) = *(uint*)(state + 300) & 0xffffff | 0x26000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(state + 300);
    } else if (stage < 2) {
        if (stage == 0) {
            *(uint*)(DAT_8061c048 + 0x128) = scale_s & 0xf | *(uint*)(DAT_8061c048 + 0x128) & 0xfffffff0;
            *(uint*)(state + 0x128) = (scale_t & 0xf) << 4 | *(uint*)(state + 0x128) & 0xffffff0f;
            *(uint*)(state + 0x128) = *(uint*)(state + 0x128) & 0xffffff | 0x25000000;
            *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(state + 0x128);
        } else if (-1 < stage) {
            *(uint*)(DAT_8061c048 + 0x128) = (scale_s & 0xf) << 8 | *(uint*)(DAT_8061c048 + 0x128) & 0xfffff0ff;
            *(uint*)(state + 0x128) = (scale_t & 0xf) << 0xc | *(uint*)(state + 0x128) & 0xffff0fff;
            *(uint*)(state + 0x128) = *(uint*)(state + 0x128) & 0xffffff | 0x25000000;
            *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(state + 0x128);
        }
    } else if (stage < 4) {
        *(uint*)(DAT_8061c048 + 300) = (scale_s & 0xf) << 8 | *(uint*)(DAT_8061c048 + 300) & 0xfffff0ff;
        *(uint*)(state + 300) = (scale_t & 0xf) << 0xc | *(uint*)(state + 300) & 0xffff0fff;
        *(uint*)(state + 300) = *(uint*)(state + 300) & 0xffffff | 0x26000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(state + 300);
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c9de0 | Name: gx_set_tev_ind_tex_ref | Date: 2026-09-15T11:05:00Z
// GXSetIndTexOrder-style: assign the indirect texmap + texcoord for one of the
// four indirect stages into the RAS1_IREF register (state+0x120, three bits
// each per stage). 0xff (GX_TEXMAP/COORD_NULL) is normalized to 0. Marks the
// state dirty (bits 0/1) and flags the CP FIFO.
void gx_set_tev_ind_tex_ref(int stage, uint texmap, uint texcoord) {
    int state;

    state = DAT_8061c048;
    if (texcoord == 0xff) { texcoord = 0; }
    if (texmap == 0xff) { texmap = 0; }
    if (stage == 2) {
        *(uint*)(DAT_8061c048 + 0x120) = (texcoord & 7) << 0xc | *(uint*)(DAT_8061c048 + 0x120) & 0xffff8fff;
        *(uint*)(state + 0x120) = (texmap & 7) << 0xf | *(uint*)(state + 0x120) & 0xfffc7fff;
    } else if (stage < 2) {
        if (stage == 0) {
            *(uint*)(DAT_8061c048 + 0x120) = texcoord & 7 | *(uint*)(DAT_8061c048 + 0x120) & 0xfffffff8;
            *(uint*)(state + 0x120) = (texmap & 7) << 3 | *(uint*)(state + 0x120) & 0xffffffc7;
        } else if (-1 < stage) {
            *(uint*)(DAT_8061c048 + 0x120) = (texcoord & 7) << 6 | *(uint*)(DAT_8061c048 + 0x120) & 0xfffffe3f;
            *(uint*)(state + 0x120) = (texmap & 7) << 9 | *(uint*)(state + 0x120) & 0xfffff1ff;
        }
    } else if (stage < 4) {
        *(uint*)(DAT_8061c048 + 0x120) = (texcoord & 7) << 0x12 | *(uint*)(DAT_8061c048 + 0x120) & 0xffe3ffff;
        *(uint*)(state + 0x120) = (texmap & 7) << 0x15 | *(uint*)(state + 0x120) & 0xff1fffff;
    }
    state = DAT_8061c048;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(DAT_8061c048 + 0x120);
    *(uint*)(DAT_8061c048 + 0x5ac) = *(uint*)(DAT_8061c048 + 0x5ac) | 3;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803c9ecc | Name: gx_set_num_ind_stages | Date: 2026-09-15T11:05:00Z
// GXSetNumIndStages: set the active indirect-stage count (state+0x204 bits
// 16..18) and flag the dependent state dirty (bits 1|2).
void gx_set_num_ind_stages(uint num) {
    int state;
    state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x204) = (num & 7) << 0x10 | *(uint*)(DAT_8061c048 + 0x204) & 0xfff8ffff;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 6;
}

// ===========================================================================
// batch_1173: GX TEV combiner setup layer - direct-mode helper, RAS1_SS
// indirect-bump flush, TEV color/alpha input/op setters + presets, and the
// TEV register-color loader. Appended after the batch_1172 indirect-texture
// layer.
// ---------------------------------------------------------------------------
// Additional per-file externs for this batch (TEV env-register default tables):
extern unsigned char DAT_8049feb8;   // TEV color-input reg default preset table
extern unsigned char DAT_8049fecc;   // TEV color-input reg default preset table (variant)
extern unsigned char DAT_8049fee0;   // TEV alpha-input reg default preset table
extern unsigned char DAT_8049fef4;   // TEV alpha-input reg default preset table (variant)

// Address: 0x803c9ef0 | Name: gx_set_tev_direct | Date: 2026-09-15T11:40:00Z
// GXSetTevDirect: disable indirect texturing for a TEV stage by programming an
// all-zero indirect control word (delegates to gx_set_tev_indirect).
void gx_set_tev_direct(int stage) {
    gx_set_tev_indirect(stage, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

// Address: 0x803c9f38 | Name: gx_tev_state_nop | Date: 2026-09-15T11:40:00Z
// Empty GX TEV state hook (no-op).
void gx_tev_state_nop(void) {
}

// Address: 0x803c9f3c | Name: gx_set_indbump_mask | Date: 2026-09-15T11:40:00Z
// Set the RAS1_SS indirect-bump stage mask (low byte of state+0x124), stream it
// via the CP write-gather 0x61 BP-load, and flag the CP FIFO dirty.
void gx_set_indbump_mask(uint mask) {
    int state;
    state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x124) = mask & 0xff | *(uint*)(DAT_8061c048 + 0x124) & 0xffffff00;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *(unsigned int*)(state + 0x124);
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803c9f90 | Name: gx_load_tev_env_preset | Date: 2026-09-15T11:40:00Z
// Load default TEV color/alpha env words for a stage from the DAT_8049feb8..
// DAT_8049fef4 preset tables (selected by the mode flag), merging the preset
// bits into the cached RAS color-env (+0x130) / alpha-env (+0x170) registers,
// streaming both and caching them back. NON-MATCHING: table base+stride verbatim.
void gx_load_tev_env_preset(int stage, int mode) {
    int state;
    uint w;
    uint* color_tbl;
    int reg;
    uint* alpha_tbl;

    state = DAT_8061c048;
    if (stage == 0) {
        color_tbl = (uint*)(&DAT_8049feb8 + mode * 4);
        alpha_tbl = (uint*)(&DAT_8049fee0 + mode * 4);
    } else {
        color_tbl = (uint*)(&DAT_8049fecc + mode * 4);
        alpha_tbl = (uint*)(&DAT_8049fef4 + mode * 4);
    }
    reg = DAT_8061c048 + stage * 4;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = *color_tbl & 0xffffff | *(uint*)(reg + 0x130) & 0xff000000;
    DAT_cc008000 = w;
    *(uint*)(reg + 0x130) = w;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = *alpha_tbl & 0xfffff0 | *(uint*)(reg + 0x170) & 0xff00000f;
    DAT_cc008000 = w;
    *(uint*)(reg + 0x170) = w;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca01c | Name: gx_set_tev_color_in | Date: 2026-09-15T11:40:00Z
// GXSetTevColorIn: pack the four TEV color-combiner input selects (a/b/c/d, four
// bits each) into the low half of the color-env register (state+0x130 per stage)
// and stream it.
void gx_set_tev_color_in(int stage, uint a, uint b, uint c, uint d) {
    int state;
    uint w;
    int reg;

    state = DAT_8061c048;
    reg = DAT_8061c048 + stage * 4;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = d & 0xf | (c & 0xf) << 4 | (b & 0xf) << 8 | (a & 0xf) << 0xc |
        *(uint*)(reg + 0x130) & 0xffff0000;
    DAT_cc008000 = w;
    *(uint*)(reg + 0x130) = w;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca060 | Name: gx_set_tev_alpha_in | Date: 2026-09-15T11:40:00Z
// GXSetTevAlphaIn: pack the four TEV alpha-combiner input selects (a/b/c/d,
// three bits each) into the alpha-env register (state+0x170 per stage) and
// stream it.
void gx_set_tev_alpha_in(int stage, uint a, uint b, uint c, uint d) {
    int state;
    uint w;
    int reg;

    state = DAT_8061c048;
    reg = DAT_8061c048 + stage * 4;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = (d & 7) << 4 | (c & 7) << 7 | (b & 7) << 10 | (a & 7) << 0xd |
        *(uint*)(reg + 0x170) & 0xffff000f;
    DAT_cc008000 = w;
    *(uint*)(reg + 0x170) = w;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca0a4 | Name: gx_set_tev_color_op | Date: 2026-09-15T11:40:00Z
// GXSetTevColorOp: pack the TEV color-combiner op/bias/scale/clamp/out-reg
// fields into the color-env register (state+0x130 per stage). The op value < 2
// selects the normal add/sub path (explicit bias+scale) while >= 2 selects the
// compare path (scale bits form the compare op, fixed bias). Streams the word.
void gx_set_tev_color_op(int stage, uint op, uint bias, uint scale, uint clamp, uint out_reg) {
    int state;
    uint prev;
    uint w;

    state = DAT_8061c048;
    prev = *(uint*)(DAT_8061c048 + stage * 4 + 0x130);
    w = (op & 1) << 0x12;
    if ((int)op < 2) {
        w = (bias & 3) << 0x10 | (scale & 3) << 0x14 | w | prev & 0xffc8ffff;
    } else {
        w = (op & 6) << 0x13 | w | prev & 0xffc8ffff | 0x30000;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = (out_reg & 3) << 0x16 | (clamp & 1) << 0x13 | w & 0xff37ffff;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + stage * 4 + 0x130) = w;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca10c | Name: gx_set_tev_alpha_op | Date: 2026-09-15T11:40:00Z
// GXSetTevAlphaOp: identical packing to gx_set_tev_color_op but targeting the
// alpha-env register (state+0x170 per stage).
void gx_set_tev_alpha_op(int stage, uint op, uint bias, uint scale, uint clamp, uint out_reg) {
    int state;
    uint prev;
    uint w;

    state = DAT_8061c048;
    prev = *(uint*)(DAT_8061c048 + stage * 4 + 0x170);
    w = (op & 1) << 0x12;
    if ((int)op < 2) {
        w = (bias & 3) << 0x10 | (scale & 3) << 0x14 | w | prev & 0xffc8ffff;
    } else {
        w = (op & 6) << 0x13 | w | prev & 0xffc8ffff | 0x30000;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = (out_reg & 3) << 0x16 | (clamp & 1) << 0x13 | w & 0xff37ffff;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + stage * 4 + 0x170) = w;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca174 | Name: gx_set_tev_reg_color | Date: 2026-09-15T11:40:00Z
// GXSetTevColorS10 / GXSetTevKColor: load a TEV register color. The RGBA source
// word is re-packed into the two RAS TEV register-color BP words (base 0xe0 +
// 2*id) - RA into the first, GB into the second - with the GB word emitted three
// times (the SDK's stall-avoidance triple write). Streams via write-gather.
void gx_set_tev_reg_color(int reg_id, uint* rgba) {
    uint c;

    c = rgba[0];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (c & 0xff) << 0xc | c >> 0x18 | (reg_id * 2 + 0xe0) * 0x1000000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    c = c >> 4 & 0xff000 | c >> 8 & 0xff | (reg_id * 2 + 0xe1) * 0x1000000;
    DAT_cc008000 = c;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = c;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = c;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803c9f6c | Name: gx_flush_indbump_reg | Date: 2026-09-15T11:40:00Z
// Re-emit the cached RAS1_SS indirect-bump register (state+0x124) via the CP
// write-gather 0x61 BP-load and clear the pending-command halfword. Used as a
// pre/post barrier around TMEM/TLUT register loads throughout this module.
void gx_flush_indbump_reg(void) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = *(unsigned int*)(DAT_8061c048 + 0x124);
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// ===========================================================================
// batch_1174: GX TEV konst/swap/order + alpha-compare + z-texture layer -
// register-color S10 loader, konst-color loader, konst color/alpha selectors,
// swap-mode + swap-table, GXSetAlphaCompare, GXSetZTexture, GXSetTevOrder,
// GXSetNumTevStages. Appended after the batch_1173 TEV combiner setup layer.
// ---------------------------------------------------------------------------
// Additional per-file extern for this batch:
extern unsigned char DAT_8049ff08;   // TEV color-channel (GX_COLOR*) -> RAS channel remap table

// Address: 0x803ca1d4 | Name: gx_set_tev_reg_color_s10 | Date: 2026-09-15T12:10:00Z
// GXSetTevColorS10: load a signed-10-bit TEV register color from two packed
// 11-bit component words (word0 = R|B halves, word1 = G|A halves), emitting the
// RA BP word (base 0xe0+2*id) then the GB word triple-written for stall
// avoidance. NON-MATCHING: the 11-bit field shuffles are kept verbatim.
void gx_set_tev_reg_color_s10(int reg_id, uint* rgba) {
    uint gb;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (rgba[1] & 0x7ff) << 0xc | rgba[0] >> 0x10 & 0x7ff |
                   (reg_id * 2 + 0xe0) * 0x1000000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    gb = (rgba[0] & 0x7ff) << 0xc | rgba[1] >> 0x10 & 0x7ff | (reg_id * 2 + 0xe1) * 0x1000000;
    DAT_cc008000 = gb;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = gb;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = gb;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca238 | Name: gx_set_tev_konst_color | Date: 2026-09-15T12:10:00Z
// GXSetTevKColor: load a TEV konstant register color (like gx_set_tev_reg_color
// but with the 0x800000 konstant-bank select bit set in both BP words; no GB
// triple-write because konst regs do not stall).
void gx_set_tev_konst_color(int reg_id, uint* rgba) {
    uint c;
    c = rgba[0];
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (c & 0xff) << 0xc | c >> 0x18 | (reg_id * 2 + 0xe0) * 0x1000000 | 0x800000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = c >> 4 & 0xff000 | c >> 8 & 0xff | (reg_id * 2 + 0xe1) * 0x1000000 | 0x800000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca29c | Name: gx_set_tev_kcolor_sel | Date: 2026-09-15T12:10:00Z
// GXSetTevKColorSel: pack the konstant-color selector (5 bits) for a TEV stage
// into the even/odd nibble of the shared RAS1_TREF-adjacent register at
// state+0x1b0 (two stages per 32-bit word), then stream it.
void gx_set_tev_kcolor_sel(uint stage, uint sel) {
    uint* reg;
    reg = (uint*)(DAT_8061c048 + ((int)stage >> 1) * 4 + 0x1b0);
    if ((stage & 1) == 0) {
        *reg = (sel & 0x1f) << 4 | *reg & 0xfffffe0f;
    } else {
        *reg = (sel & 0x1f) << 0xe | *reg & 0xfff83fff;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *reg;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca2f8 | Name: gx_set_tev_kalpha_sel | Date: 2026-09-15T12:10:00Z
// GXSetTevKAlphaSel: pack the konstant-alpha selector (5 bits) for a TEV stage
// into the alpha portion of the shared register at state+0x1b0.
void gx_set_tev_kalpha_sel(uint stage, uint sel) {
    uint* reg;
    reg = (uint*)(DAT_8061c048 + ((int)stage >> 1) * 4 + 0x1b0);
    if ((stage & 1) == 0) {
        *reg = (sel & 0x1f) << 9 | *reg & 0xffffc1ff;
    } else {
        *reg = (sel & 0x1f) << 0x13 | *reg & 0xff07ffff;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *reg;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca354 | Name: gx_set_tev_swap_mode | Date: 2026-09-15T12:10:00Z
// GXSetTevSwapMode: set the ras-swap (bits 0-1) and tex-swap (bits 2-3) table
// selects for a TEV stage into the alpha-env register (state+0x170 per stage).
void gx_set_tev_swap_mode(int stage, uint ras_sel, uint tex_sel) {
    int state;
    uint* reg;
    state = DAT_8061c048;
    reg = (uint*)(DAT_8061c048 + stage * 4 + 0x170);
    *reg = ras_sel & 3 | *reg & 0xfffffffc;
    *reg = (tex_sel & 3) << 2 | *reg & 0xfffffff3;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *reg;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca39c | Name: gx_set_tev_swap_table_entry | Date: 2026-09-15T12:10:00Z
// GXSetTevSwapModeTable: program one swap-table entry (two register words at
// state+0x1b0): the R/G component remaps in the first word and the B/A remaps in
// the second, each streamed individually.
void gx_set_tev_swap_table_entry(int entry, uint r, uint g, uint b, uint a) {
    int state;
    uint* w1;
    uint* w0;
    state = DAT_8061c048;
    w0 = (uint*)(DAT_8061c048 + entry * 8 + 0x1b0);
    *w0 = r & 3 | *w0 & 0xfffffffc;
    *w0 = (g & 3) << 2 | *w0 & 0xfffffff3;
    w1 = (uint*)(state + (entry * 2 + 1) * 4 + 0x1b0);
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *w0;
    *w1 = b & 3 | *w1 & 0xfffffffc;
    *w1 = (a & 3) << 2 | *w1 & 0xfffffff3;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *w1;
    *(unsigned short*)(state + 2) = 0;
}

// Address: 0x803ca41c | Name: gx_set_alpha_compare | Date: 2026-09-15T12:10:00Z
// GXSetAlphaCompare: pack the two alpha-compare functions, references, and the
// logic op into the alpha-compare BP register (opcode 0xf3). NON-MATCHING: the
// field ordering (comp0 bits 16-18, comp1 bits 22-23, op bits 19-21, refs in the
// low two bytes) is preserved verbatim.
void gx_set_alpha_compare(uint comp0, uint ref0, uint comp1, uint op, uint ref1) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (comp1 & 3) << 0x16 |
                   (op & 7) << 0x13 |
                   (comp0 & 7) << 0x10 | (ref1 & 0xff) << 8 | ref0 & 0xff | 0xf3000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca460 | Name: gx_set_z_texture | Date: 2026-09-15T12:10:00Z
// GXSetZTexture: program the z-texture op/format (BP reg 0xf5) and the z-bias
// (BP reg 0xf4). The z-texture format code maps GX_TF_Z8/16/24 (0x11/0x13/0x16)
// to the 2-bit size select {0,1,2}. NON-MATCHING: goto preserves the switch
// fall-through of the decompiler.
void gx_set_z_texture(uint op, int fmt, uint bias) {
    uint size;
    if (fmt == 0x13) {
        size = 1;
    } else {
        if (fmt < 0x13) {
            if (fmt == 0x11) { size = 0; goto have_size; }
        } else if (fmt == 0x16) {
            size = 2; goto have_size;
        }
        size = 2;
    }
have_size:
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = bias & 0xffffff | 0xf4000000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (op & 3) << 2 | size | 0xf5000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca4ec | Name: gx_set_tev_order | Date: 2026-09-15T12:10:00Z
// GXSetTevOrder: assign the texcoord, texmap, and color-channel used by a TEV
// stage. Caches the texmap in state+0x554, updates the texcoord-active mask in
// state+0x598, then packs the texcoord (3 bits), color-channel (3 bits, mapped
// via DAT_8049ff08), and the texmap-enable bit into the even/odd half of the
// RAS1_TREF register (state+0x100, two stages per word); flags the CP FIFO
// dirty. NON-MATCHING: the packed RAS1_TREF bitfields and the signed-shift stage
// index arithmetic are kept verbatim.
void gx_set_tev_order(uint stage, uint color_chan, uint tref_word, int texmap) {
    int state;
    uint* reg;
    uint coord;

    state = DAT_8061c048;
    coord = tref_word & 0xfffffeff;
    *(uint*)(DAT_8061c048 + stage * 4 + 0x554) = tref_word;
    reg = (uint*)(state + (((int)stage >> 1) + (uint)((int)stage < 0 && (stage & 1) != 0)) * 4 + 0x100);
    if (7 < coord) { coord = 0; }
    if ((int)color_chan < 8) {
        *(uint*)(DAT_8061c048 + 0x598) = *(uint*)(DAT_8061c048 + 0x598) | 1 << stage;
    } else {
        color_chan = 0;
        *(uint*)(DAT_8061c048 + 0x598) = *(uint*)(DAT_8061c048 + 0x598) & ~(1 << stage);
    }
    if ((stage & 1) == 0) {
        *reg = coord & 7 | *reg & 0xfffffff8;
        *reg = (color_chan & 7) << 3 | *reg & 0xffffffc7;
        if (texmap == 0xff) { coord = 7; }
        else { coord = *(uint*)(&DAT_8049ff08 + texmap * 4); }
        *reg = (coord & 7) << 7 | *reg & 0xfffffc7f;
        state = 0;
        if ((tref_word != 0xff) && ((tref_word & 0x100) == 0)) { state = 1; }
        *reg = state << 6 | *reg & 0xffffffbf;
    } else {
        *reg = (coord & 7) << 0xc | *reg & 0xffff8fff;
        *reg = (color_chan & 7) << 0xf | *reg & 0xfffc7fff;
        if (texmap == 0xff) { coord = 7; }
        else { coord = *(uint*)(&DAT_8049ff08 + texmap * 4); }
        *reg = (coord & 7) << 0x13 | *reg & 0xffc7ffff;
        state = 0;
        if ((tref_word != 0xff) && ((tref_word & 0x100) == 0)) { state = 1; }
        *reg = state << 0x12 | *reg & 0xfffbffff;
    }
    state = DAT_8061c048;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = *reg;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 1;
}

// Address: 0x803ca688 | Name: gx_set_num_tev_stages | Date: 2026-09-15T12:10:00Z
// GXSetNumTevStages: set the active TEV stage count (state+0x204 bits 10-13,
// stored as count-1) and flag the dependent state dirty (bit 2).
void gx_set_num_tev_stages(uint num) {
    int state;
    state = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x204) =
         ((num & 0xff) - 1) * 0x400 & 0x3c00 | *(uint*)(DAT_8061c048 + 0x204) & 0xffffc3ff;
    *(uint*)(state + 0x5ac) = *(uint*)(state + 0x5ac) | 4;
}

// ---------------------------------------------------------------------------
// batch_1175: GX fog + PE blend/z-mode setup layer (appended to gx_init.cpp,
// continuing the batch_1174 TEV konst/swap/order + alpha-compare layer).
// Fog float/double constants live in the GX constant pool at 0x8061c0e0..138;
// DAT_8049ff30 is the GX_TF_Z8/16/24 -> z-format size-select remap table.
// NON-MATCHING: the flat DAT_8061c048 state offsets, the DAT_cc008000
// write-gather 0x61 BP-load packing, the 20e8-bias int->double CONCAT44
// conversions, the exponent-normalization loop in gx_set_fog, and the packed
// fog/blend/z-mode BP register bitfields are kept verbatim.
// ---------------------------------------------------------------------------
extern float        FLOAT_8061c0e0;    // fog: 0.0f
extern float        FLOAT_8061c0e4;    // fog: 1.0f
extern float        FLOAT_8061c0e8;    // fog: exponent step multiplier (0.5f)
extern float        FLOAT_8061c0f8;    // fog: exponent step multiplier (2.0f)
extern float        FLOAT_8061c108;    // fog: mantissa->fixed scale constant
extern float        FLOAT_8061c120;    // fog: perspective A-param scale
extern float        FLOAT_8061c130;    // fog: range-adjust k-value scale
extern double       DOUBLE_8061c0f0;   // fog: exponent normalize high bound
extern double       DOUBLE_8061c100;   // fog: exponent normalize low bound
extern double       DOUBLE_8061c110;   // fog: 2^52 int->double bias constant
extern double       DOUBLE_8061c118;   // fog: projection-mode select sentinel
extern double       DOUBLE_8061c128;   // fog: rsqrt Newton refine const (3.0)
extern double       DOUBLE_8061c138;   // fog: 2^52 int->double bias constant
extern unsigned int DAT_8049ff30;      // GX_TF_Z8/16/24 -> z-texture size select remap table

// Address: 0x803ca6b0 | Name: gx_set_fog | Date: 2026-09-15T12:18:00Z
// GXSetFog: derive the fog A/B/C parameters from the projection near/far/end
// planes (or the ortho path when bit 3 of type is set), normalizing the
// exponent by repeatedly halving/doubling A until it lands in [lo,hi) while
// tracking the shift count, then stream the packed FOG_PARAM0..3 + color-index
// words (opcodes 0xee..0xf2) via the write-gather 0x61 BP-load.
void gx_set_fog(double near, double far, double endz, double startz, uint type,
                uint* colorPtr) {
    float aParam;
    int iVar2;
    uint fogA;
    uint fogExp;
    uint proj;
    double bParam;
    double cParam;
    double aDbl;
    double t;
    float local_40;
    float local_3c;

    fogA = 0;
    proj = type >> 3 & 1;
    fogExp = 0;
    if (proj == 0) {
        if ((startz == endz) || (far == near)) {
            bParam = (double)FLOAT_8061c0e0;
            aDbl = bParam;
            aParam = FLOAT_8061c0e8;
        } else {
            aParam = (float)(startz / (double)(float)(startz - endz));
            aDbl = (double)(float)(near / (double)(float)(far - near));
            bParam = (double)((float)(startz * endz) /
                             (float)((double)(float)(startz - endz) *
                                    (double)(float)(far - near)));
        }
        iVar2 = 0;
        while (t = (double)aParam, DOUBLE_8061c0f0 < t) {
            iVar2 = iVar2 + 1;
            aParam = (float)(t * (double)FLOAT_8061c0e8);
        }
        for (; ((double)FLOAT_8061c0e0 < t && (t < DOUBLE_8061c100));
             t = (double)(float)(t * (double)FLOAT_8061c0f8)) {
            iVar2 = iVar2 + -1;
        }
        local_3c = (float)(bParam / (double)(float)((double)CONCAT44(0x43300000,
                                                        1 << iVar2 + 1U ^ 0x80000000) -
                                                    DOUBLE_8061c110));
        fogA = gx_d2u((double)(float)((double)FLOAT_8061c108 * t));
        local_40 = (float)aDbl;
        fogA = fogA & 0xffffff | 0xef000000;
        fogExp = iVar2 + 1U & 0x1f | 0xf0000000;
    } else if ((startz == endz) || (far == near)) {
        local_3c = FLOAT_8061c0e0;
        local_40 = FLOAT_8061c0e0;
    } else {
        local_40 = FLOAT_8061c0e4 / (float)(far - near);
        local_3c = local_40 * (float)(startz - endz);
        local_40 = local_40 * (float)(near - endz);
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (uint)local_3c >> 0xc & 0x80000 |
                   (uint)local_3c >> 0xc & 0x7f800 | (uint)local_3c >> 0xc & 0x7ff | 0xee000000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = fogA;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = fogExp;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (type & 7) << 0x15 |
                   proj << 0x14 |
                   (uint)local_40 >> 0xc & 0x80000 |
                   (uint)local_40 >> 0xc & 0x7f800 | (uint)local_40 >> 0xc & 0x7ff | 0xf1000000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = *colorPtr >> 8 | 0xf2000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803ca8c4 | Name: gx_init_fog_adj_table | Date: 2026-09-15T12:18:00Z
// GXInitFogAdjTable: fill a 10-entry (12-bit) fog range-adjustment table from
// the viewport width and the projection matrix, applying the rsqrt-based
// perspective correction 1/sqrt(1 + (x/A)^2) at 10 sample columns.
void gx_init_fog_adj_table(ushort* table, uint viewportWidth, float* projMtx) {
    float aParam;
    uint uVar2;
    uint idx;
    double invA;
    double sample;
    double refined;
    double denom;
    double kScale;
    double stepScale;
    double biasA;
    double loBound;
    double refine3;

    if (DOUBLE_8061c118 == (double)projMtx[0xf]) {
        aParam = projMtx[0xb] / (projMtx[10] - FLOAT_8061c0e4);
        invA = (double)(aParam / *projMtx);
    } else {
        invA = (double)(FLOAT_8061c0e4 / *projMtx);
        aParam = (float)((double)FLOAT_8061c120 * invA);
    }
    denom = (double)(aParam * aParam);
    biasA = (double)FLOAT_8061c0e4;
    loBound = (double)FLOAT_8061c0e0;
    idx = 0;
    kScale = (double)FLOAT_8061c130;
    stepScale = (double)(FLOAT_8061c0f8 /
                        (float)((double)CONCAT44(0x43300000, viewportWidth & 0xffff) -
                               DOUBLE_8061c138));
    double biasW = DOUBLE_8061c138;
    double refineHi = DOUBLE_8061c100;
    refine3 = DOUBLE_8061c128;
    do {
        aParam = (float)((double)(float)((double)(float)((double)CONCAT44(0x43300000, (idx + 1) * 0x20)
                                                        - biasW) * stepScale) * invA);
        sample = (double)(float)(biasA + (double)(float)((double)(aParam * aParam) / denom));
        if (loBound < sample) {
            refined = 1.0 / SQRT(sample);
            refined = refineHi * refined * (refine3 - sample * refined * refined);
            refined = refineHi * refined * (refine3 - sample * refined * refined);
            sample = (double)(float)(sample * refineHi * refined * (refine3 - sample * refined * refined));
        }
        uVar2 = gx_d2u((double)(float)(kScale * sample));
        idx = idx + 1;
        *table = (ushort)uVar2 & 0xfff;
        table = table + 1;
    } while (idx < 10);
}

// Address: 0x803caa74 | Name: gx_set_fog_range_adj | Date: 2026-09-15T12:18:00Z
// GXSetFogRangeAdj: when enabled (low byte of enable != 0), stream the five
// packed FOG_RANGE_ADJ k-value register words (opcodes 0xe9..0xed, two 12-bit
// entries each) then always stream the FOG_RANGE center/enable word (0xe8).
void gx_set_fog_range_adj(uint enable, uint center, ushort* kTable) {
    if ((enable & 0xff) != 0) {
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = (kTable[1] & 0xfff) << 0xc | *kTable & 0xfff | 0xe9000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = (kTable[3] & 0xfff) << 0xc | kTable[2] & 0xfff | 0xea000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = (kTable[5] & 0xfff) << 0xc | kTable[4] & 0xfff | 0xeb000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = (kTable[7] & 0xfff) << 0xc | kTable[6] & 0xfff | 0xec000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = (kTable[9] & 0xfff) << 0xc | kTable[8] & 0xfff | 0xed000000;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (enable & 1) << 10 | (center & 0xffff) + 0x156 & 0x3ff | 0xe8000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803cab98 | Name: gx_set_blend_mode | Date: 2026-09-15T12:18:00Z
// GXSetBlendMode: pack the blend type, src factor, dst factor and logic op into
// the PE control register at state+0x1d0. countLeadingZeros(3-type)/(2-type)
// derives the subtract/blend-enable bits without a branch. Streams the word.
void gx_set_blend_mode(uint type, uint srcFactor, uint dstFactor, uint logicOp) {
    int iVar1;
    uint w;
    uint clzBlend;

    iVar1 = DAT_8061c048;
    clzBlend = countLeadingZeros(3 - type);
    w = countLeadingZeros(2 - type);
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = (dstFactor & 7) << 5 |
        (srcFactor & 7) << 8 |
        (logicOp & 0xf) << 0xc |
        w >> 4 & 2 |
        type & 1 | (clzBlend & 0x20) << 6 | *(uint*)(DAT_8061c048 + 0x1d0) & 0xffff001c;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + 0x1d0) = w;
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cabec | Name: gx_set_color_update | Date: 2026-09-15T12:18:00Z
// GXSetColorUpdate: set bit 3 (color write enable) of the PE control register.
void gx_set_color_update(uint enable) {
    int iVar1;
    uint w;

    iVar1 = DAT_8061c048;
    w = (enable & 1) << 3 | *(uint*)(DAT_8061c048 + 0x1d0) & 0xfffffff7;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + 0x1d0) = w;
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cac18 | Name: gx_set_alpha_update | Date: 2026-09-15T12:18:00Z
// GXSetAlphaUpdate: set bit 4 (alpha write enable) of the PE control register.
void gx_set_alpha_update(uint enable) {
    int iVar1;
    uint w;

    iVar1 = DAT_8061c048;
    w = (enable & 1) << 4 | *(uint*)(DAT_8061c048 + 0x1d0) & 0xffffffef;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + 0x1d0) = w;
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cac44 | Name: gx_set_dst_alpha | Date: 2026-09-15T12:18:00Z
// GXSetDstAlpha: pack the enable bit, the 8-bit constant destination alpha and
// its high bit into the PE destination-alpha register at state+0x1d8.
void gx_set_dst_alpha(uint enable, uint alpha, uint enableHi) {
    int iVar1;
    uint w;

    iVar1 = DAT_8061c048;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = (enableHi & 1) << 4 |
        (alpha & 7) << 1 | enable & 1 | *(uint*)(DAT_8061c048 + 0x1d8) & 0xffffffe0;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + 0x1d8) = w;
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cac78 | Name: gx_set_dither | Date: 2026-09-15T12:18:00Z
// GXSetDither: set bit 6 (dither enable) of the PE mode register at state+0x1dc.
void gx_set_dither(uint enable) {
    int iVar1;

    iVar1 = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x1dc) =
         (enable & 1) << 6 | *(uint*)(DAT_8061c048 + 0x1dc) & 0xffffffbf;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = *(unsigned int*)(iVar1 + 0x1dc);
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cacac | Name: gx_set_z_mode | Date: 2026-09-15T12:18:00Z
// GXSetZMode: program the z-compare-enable, the 3-bit compare func (remapped via
// DAT_8049ff30) and the z-update-enable bits into the PE z-mode register
// state+0x1dc, streaming it only when the packed word changed and updating the
// z-compare-before-texture dependent bit at state+0x204. When the remapped
// compare func is GX_ALWAYS(4) it also programs the alpha-func BP register
// state+0x1d4 (opcode 0x42) so the copy pipeline sees an always-pass.
void gx_set_z_mode(int compareFunc, uint updateEnable) {
    int iVar1;
    int iVar2;
    uint prev;

    iVar1 = DAT_8061c048;
    prev = *(uint*)(DAT_8061c048 + 0x1dc);
    *(uint*)(DAT_8061c048 + 0x1dc) = *(uint*)(&DAT_8049ff30 + compareFunc * 4) & 7 | prev & 0xfffffff8;
    *(uint*)(iVar1 + 0x1dc) = (updateEnable & 7) << 3 | *(uint*)(iVar1 + 0x1dc) & 0xffffffc7;
    iVar2 = DAT_8061c048;
    if (prev != *(uint*)(iVar1 + 0x1dc)) {
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = *(uint*)(iVar1 + 0x1dc);
        *(uint*)(DAT_8061c048 + 0x204) =
             (uint)(compareFunc == 2) << 9 | *(uint*)(DAT_8061c048 + 0x204) & 0xfffffdff;
        *(uint*)(iVar2 + 0x5ac) = *(uint*)(iVar2 + 0x5ac) | 4;
    }
    iVar1 = DAT_8061c048;
    if (*(uint*)(&DAT_8049ff30 + compareFunc * 4) == 4) {
        *(uint*)(DAT_8061c048 + 0x1d4) =
             (compareFunc + -4) * 0x200 & 0x600U | *(uint*)(DAT_8061c048 + 0x1d4) & 0xfffff9ff;
        *(uint*)(iVar1 + 0x1d4) = *(uint*)(iVar1 + 0x1d4) & 0xffffff | 0x42000000;
        *(volatile unsigned char*)&DAT_cc008000 = 0x61;
        DAT_cc008000 = *(unsigned int*)(iVar1 + 0x1d4);
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803cad80 | Name: gx_set_z_comp_loc | Date: 2026-09-15T12:18:00Z
// GXSetZCompLoc: set bit 2 (z-compare-before-texture) of the PE mode register
// at state+0x1dc and stream it.
void gx_set_z_comp_loc(uint beforeTex) {
    int iVar1;
    uint w;

    iVar1 = DAT_8061c048;
    w = (beforeTex & 1) << 2 | *(uint*)(DAT_8061c048 + 0x1d0) & 0xfffffffb;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + 0x1d0) = w;
    *(unsigned short*)(iVar1 + 2) = 0;
}

// ---------------------------------------------------------------------------
// batch_1176: GX alpha-compare/field-mode + display-list capture + XF
// projection/matrix load layer (appended to gx_init.cpp, continuing the
// batch_1175 fog + PE blend/z-mode layer).
// DAT_8060f360..384 is the display-list capture CPU-FIFO scratch descriptor;
// DAT_806135d8 caches the saved GP FIFO across a display-list capture.
// NON-MATCHING: the flat DAT_8061c048 state offsets, the DAT_cc008000 /
// DAT_cc008004 write-gather port packing (0x10 CP-load, 0x20/0x40 XF loads,
// 0x44/0x61/0x68 BP-loads), and the packed register bitfields are kept verbatim.
// ---------------------------------------------------------------------------
extern int          DAT_8060f360;      // display-list capture: CPU-FIFO base ptr
extern int          DAT_8060f364;      // display-list capture: CPU-FIFO end ptr
extern int          DAT_8060f368;      // display-list capture: CPU-FIFO size
extern int          DAT_8060f374;      // display-list capture: read ptr
extern int          DAT_8060f378;      // display-list capture: write ptr
extern int          DAT_8060f37c;      // display-list capture: byte count
extern int          DAT_8060f384;      // display-list capture: saved BP-state snapshot
extern uint*        DAT_806135d8;      // saved GP FIFO descriptor across capture
extern float        FLOAT_8061c140;    // GXSetProjection: PERSPECTIVE mode sentinel (0.0f)
extern void         leaf_31e8(uint dst, uint src, int len);   // block copy (memcpy-like)
extern void         FUN_803c73cc(void);                       // gx_flush_dirty_regs (see 0x803c73cc)

// Address: 0x803cadac | Name: gx_set_alpha_compare_ref | Date: 2026-09-15T12:40:00Z
// Program the alpha-compare enable bit (bit 8) plus the 8-bit reference value
// into the PE alpha-func register at state+0x1d4 and stream it.
void gx_set_alpha_compare_ref(uint enable, uint ref) {
    int iVar1;
    uint w;

    iVar1 = DAT_8061c048;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    w = (enable & 1) << 8 | ref & 0xff | *(uint*)(DAT_8061c048 + 0x1d4) & 0xfffffe00;
    DAT_cc008000 = w;
    *(uint*)(DAT_8061c048 + 0x1d4) = w;
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cade8 | Name: gx_set_field_mode | Date: 2026-09-15T12:40:00Z
// GXSetFieldMode: stream the field-mode BP register (opcode 0x44) packing the
// field-output-enable (bit 1) and half-aspect-ratio (bit 0) flags.
void gx_set_field_mode(uint fieldOutput, uint halfAspect) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (fieldOutput & 1) << 1 | halfAspect & 1 | 0x44000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803cae20 | Name: gx_set_copy_clamp | Date: 2026-09-15T12:40:00Z
// Set the copy-clamp/gamma enable bit (bit 22) in the blend/logic-op register
// state+0x7c, stream it, then stream a copy-execute BP command (opcode 0x68)
// carrying an 8-bit mode byte; each stream is preceded by a pending-cmd flush.
void gx_set_copy_clamp(uint mode, uint enable) {
    int iVar1;

    iVar1 = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x7c) =
         (enable & 1) << 0x16 | *(uint*)(DAT_8061c048 + 0x7c) & 0xffbfffff;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = *(unsigned int*)(iVar1 + 0x7c);
    gx_flush_indbump_reg();
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = mode & 0xff | 0x68000000;
    gx_flush_indbump_reg();
}

// Address: 0x803cae98 | Name: gx_begin_display_list | Date: 2026-09-15T12:40:00Z
// GXBeginDisplayList: flush any pending dirty registers and (if a BP-state
// snapshot is armed) capture it, then redirect the CPU FIFO to the supplied
// display-list buffer [ptr, ptr+size) so subsequent GX commands are recorded
// instead of executed, saving the live GP FIFO for restore in end.
void gx_begin_display_list(int listPtr, int listSize) {
    uint* savedFifo;

    savedFifo = (uint*)gx_get_current_gp_fifo();
    if (*(int*)(DAT_8061c048 + 0x5ac) != 0) {
        FUN_803c73cc();
    }
    if (*(char*)(DAT_8061c048 + 0x5a9) != '\0') {
        leaf_31e8(0x8060f384, DAT_8061c048, 0x5b0);
    }
    DAT_8060f364 = listPtr + listSize + -4;
    DAT_8060f37c = 0;
    DAT_8060f360 = listPtr;
    DAT_8060f368 = listSize;
    DAT_8060f374 = listPtr;
    DAT_8060f378 = listPtr;
    *(unsigned char*)(DAT_8061c048 + 0x5a8) = 1;
    gx_save_gp_fifo((int*)savedFifo);
    DAT_806135d8 = savedFifo;
    gx_attach_cpu_fifo((uint*)&DAT_8060f360);
    gx_flush_write_pipe();
}

// Address: 0x803caf64 | Name: gx_end_display_list | Date: 2026-09-15T12:40:00Z
// GXEndDisplayList: flush the capture FIFO, read back the GP FIFO pointers,
// re-attach the saved live GP FIFO, optionally restore the BP-state snapshot
// under an interrupt lock, and return the captured byte count (0 if the FIFO
// overflowed, indicated by bit 26 of the PI/GP status register).
undefined4 gx_end_display_list(void) {
    uint status;
    undefined4 result;

    gx_flush_fifo();
    status = *(uint*)(DAT_80613578 + 0x14);
    gx_read_gp_fifo_ptrs(&DAT_8060f360);
    gx_attach_cpu_fifo(DAT_806135d8);
    if (*(char*)(DAT_8061c048 + 0x5a9) != '\0') {
        os_disable_interrupts();
        result = *(undefined4*)(DAT_8061c048 + 8);
        leaf_31e8(DAT_8061c048, 0x8060f384, 0x5b0);
        *(undefined4*)(DAT_8061c048 + 8) = result;
        os_get_interrupts_enabled();
    }
    *(unsigned char*)(DAT_8061c048 + 0x5a8) = 0;
    result = DAT_8060f37c;
    if ((status >> 0x1a & 1) != 0) {
        result = 0;
    }
    return result;
}

// Address: 0x803cb028 | Name: gx_load_matrix_index | Date: 2026-09-15T12:40:00Z
// Stream a two-word XF matrix-index register load (write-gather opcode 0x40)
// after flushing dirty regs / clearing the EFB when required.
void gx_load_matrix_index(undefined4 indexA, undefined4 indexB) {
    if (((uint*)DAT_8061c048)[0x16b] != 0) {
        FUN_803c73cc();
    }
    if (*(int*)DAT_8061c048 == 0) {
        gx_clear_efb();
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x40;
    DAT_cc008000 = indexA;
    DAT_cc008000 = indexB;
}

// Address: 0x803cb098 | Name: gx_set_projection | Date: 2026-09-15T12:40:00Z
// GXSetProjection: cache the projection type + six coefficients from the caller
// matrix into state+0x4d8.., choosing the perspective (type==1) or ortho column
// pair, then load the six-word XF projection register block (write-gather 0x10
// CP-load, register 0x61020) via alternating cc008000/cc008004 ports.
void gx_set_projection(undefined4* mtx, int type) {
    int iVar1;

    iVar1 = DAT_8061c048;
    *(int*)(DAT_8061c048 + 0x4d8) = type;
    *(undefined4*)(iVar1 + 0x4dc) = *mtx;
    *(undefined4*)(iVar1 + 0x4e4) = mtx[5];
    *(undefined4*)(iVar1 + 0x4ec) = mtx[10];
    *(undefined4*)(iVar1 + 0x4f0) = mtx[0xb];
    if (type == 1) {
        *(undefined4*)(iVar1 + 0x4e0) = mtx[3];
        *(undefined4*)(iVar1 + 0x4e8) = mtx[7];
    } else {
        *(undefined4*)(iVar1 + 0x4e0) = mtx[2];
        *(undefined4*)(iVar1 + 0x4e8) = mtx[6];
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = 0x61020;
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x4dc);
    DAT_cc008004 = *(undefined4*)(DAT_8061c048 + 0x4e0);
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x4e4);
    DAT_cc008004 = *(undefined4*)(DAT_8061c048 + 0x4e8);
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x4ec);
    DAT_cc008004 = *(undefined4*)(DAT_8061c048 + 0x4f0);
    DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x4d8);
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803cb13c | Name: gx_set_projection_v | Date: 2026-09-15T12:40:00Z
// GXSetProjectionv: the packed-float-array variant of gx_set_projection; the
// leading element is the projection type (!= PERSPECTIVE sentinel FLOAT_8061c140
// yields ortho=1), the following six floats are the coefficients, streamed
// through the same XF projection register load.
void gx_set_projection_v(float* proj) {
    float c2;
    float c3;
    float c4;
    float c5;
    float c6;
    int iVar6;

    iVar6 = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0x4d8) = (uint)(FLOAT_8061c140 != *proj);
    c2 = proj[2];
    c3 = proj[3];
    c4 = proj[4];
    c5 = proj[5];
    c6 = proj[6];
    *(float*)(iVar6 + 0x4dc) = proj[1];
    *(float*)(iVar6 + 0x4e0) = c2;
    *(float*)(iVar6 + 0x4e4) = c3;
    *(float*)(iVar6 + 0x4e8) = c4;
    *(float*)(iVar6 + 0x4ec) = c5;
    *(float*)(iVar6 + 0x4f0) = c6;
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = 0x61020;
    DAT_cc008000 = *(unsigned int*)(iVar6 + 0x4dc);
    DAT_cc008004 = *(undefined4*)(iVar6 + 0x4e0);
    DAT_cc008000 = *(undefined4*)(iVar6 + 0x4e4);
    DAT_cc008004 = *(undefined4*)(iVar6 + 0x4e8);
    DAT_cc008000 = *(undefined4*)(iVar6 + 0x4ec);
    DAT_cc008004 = *(undefined4*)(iVar6 + 0x4f0);
    DAT_cc008000 = *(undefined4*)(iVar6 + 0x4d8);
    *(unsigned short*)(iVar6 + 2) = 1;
}

// Address: 0x803cb1c8 | Name: gx_load_xf_matrix_3x4 | Date: 2026-09-15T12:40:00Z
// Load a 3x4 (12-float) matrix into XF memory at (regBase<<2 | 0xb0000) via the
// write-gather 0x10 CP-load, streaming the twelve floats through the paired
// cc008000/cc008004 ports.
void gx_load_xf_matrix_3x4(float* mtx, int regBase) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = regBase << 2 | 0xb0000;
    DAT_cc008000 = *mtx;
    DAT_cc008004 = mtx[1];
    DAT_cc008000 = mtx[2];
    DAT_cc008004 = mtx[3];
    DAT_cc008000 = mtx[4];
    DAT_cc008004 = mtx[5];
    DAT_cc008000 = mtx[6];
    DAT_cc008004 = mtx[7];
    DAT_cc008000 = mtx[8];
    DAT_cc008004 = mtx[9];
    DAT_cc008000 = mtx[10];
    DAT_cc008004 = mtx[0xb];
}

// Address: 0x803cb218 | Name: gx_set_current_matrix | Date: 2026-09-15T12:40:00Z
// GXSetCurrentMtx: stream the matrix-index-A XF register (write-gather 0x20
// CP-load, register 0xb000) selecting the current geometry matrix slot.
void gx_set_current_matrix(int regSel, uint mtxIndex) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x20;
    DAT_cc008000 = regSel << 0x10 | (mtxIndex & 0x3ff) << 2 | 0xb000;
}

// ---------------------------------------------------------------------------
// batch_1177: GX normal/tex matrix load + viewport/scissor setup layer
// (appended to gx_init.cpp, continuing the batch_1176 projection/matrix layer).
// Viewport params are cached in state+0x4f4..0x510; the scissor/box-offset regs
// live at state+0xf8/0xfc. FUN_803cb620 (reprogram matrix-index-A/B reg) is
// already forward-declared earlier in this file.
// NON-MATCHING: the flat DAT_8061c048 state offsets, the DAT_cc008000 /
// DAT_cc008004 write-gather packing (0x08 BP-idx, 0x10 CP-load, 0x61 BP-load),
// the +0x156 scissor bias, and the packed register bitfields are kept verbatim.
// ---------------------------------------------------------------------------
extern float        FLOAT_8061c144;    // viewport z-scale bias (0.0f)
extern float        FLOAT_8061c148;    // viewport half-height/scale factor (0.5f)
extern float        FLOAT_8061c14c;    // viewport pixel-center offset (0.5f?)
extern float        FLOAT_8061c150;    // viewport depth-range scale factor

// Address: 0x803cb248 | Name: gx_load_nrm_matrix_imm | Date: 2026-09-15T12:56:00Z
// GXLoadNrmMtxImm: load a normal (3x3) matrix into XF memory at
// (idx*3+0x400 | 0x80000) via write-gather 0x10 CP-load, streaming the nine
// rotation elements (skipping the translation column of each 3x4 source row).
void gx_load_nrm_matrix_imm(float* mtx, int idx) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = idx * 3 + 0x400U | 0x80000;
    DAT_cc008000 = *mtx;
    DAT_cc008004 = mtx[1];
    DAT_cc008000 = mtx[2];
    DAT_cc008000 = mtx[4];
    DAT_cc008004 = mtx[5];
    DAT_cc008000 = mtx[6];
    DAT_cc008000 = mtx[8];
    DAT_cc008004 = mtx[9];
    DAT_cc008000 = mtx[10];
}

// Address: 0x803cb298 | Name: gx_set_pos_matrix_index | Date: 2026-09-15T12:56:00Z
// Set the 6-bit position/normal matrix index (PNMTXIDX) in the low bits of the
// matrix-index-A register state+0x80, then reprogram/commit it via FUN_803cb620.
void gx_set_pos_matrix_index(uint idx) {
    *(uint*)(DAT_8061c048 + 0x80) = idx & 0x3f | *(uint*)(DAT_8061c048 + 0x80) & 0xffffffc0;
    gx_commit_matrix_index(0);
}

// Address: 0x803cb2cc | Name: gx_load_matrix_imm | Date: 2026-09-15T12:56:00Z
// GXLoadPosMtxImm / GXLoadTexMtxImm: load a matrix into XF matrix memory at
// (idx<0x40 ? idx*4 : (idx-0x40)*4+0x500) sized 3x4 (12 floats, mode!=1) or 2x4
// (8 floats, mode==1) via write-gather 0x10 CP-load through paired ports.
void gx_load_matrix_imm(float* mtx, uint idx, int mode) {
    int iVar1;
    uint addr;

    if (idx < 0x40) {
        addr = idx << 2;
    } else {
        addr = (idx - 0x40) * 4 + 0x500;
    }
    if (mode == 1) {
        iVar1 = 8;
    } else {
        iVar1 = 0xc;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = addr | (iVar1 + -1) * 0x10000;
    if (mode != 0) {
        DAT_cc008000 = *mtx;
        DAT_cc008004 = mtx[1];
        DAT_cc008000 = mtx[2];
        DAT_cc008004 = mtx[3];
        DAT_cc008000 = mtx[4];
        DAT_cc008004 = mtx[5];
        DAT_cc008000 = mtx[6];
        DAT_cc008004 = mtx[7];
        return;
    }
    DAT_cc008000 = *mtx;
    DAT_cc008004 = mtx[1];
    DAT_cc008000 = mtx[2];
    DAT_cc008004 = mtx[3];
    DAT_cc008000 = mtx[4];
    DAT_cc008004 = mtx[5];
    DAT_cc008000 = mtx[6];
    DAT_cc008004 = mtx[7];
    DAT_cc008000 = mtx[8];
    DAT_cc008004 = mtx[9];
    DAT_cc008000 = mtx[10];
    DAT_cc008004 = mtx[0xb];
}

// Address: 0x803cb380 | Name: gx_commit_viewport_internal | Date: 2026-09-15T12:56:00Z
// Internal: recompute and stream the six viewport XF registers (0x5101a base:
// scale/offset x,y and the pixel-center biases) from the cached viewport params
// state+0x4f4..0x510 via write-gather 0x10 CP-load.
void gx_commit_viewport_internal(void) {
    float halfW;
    float depth;

    halfW = *(float*)(DAT_8061c048 + 0x4fc) * FLOAT_8061c148;
    depth = *(float*)(DAT_8061c048 + 0x508) * *(float*)(DAT_8061c048 + 0x510);
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = 0x5101a;
    DAT_cc008000 = halfW;
    DAT_cc008000 = -*(float*)(DAT_8061c048 + 0x500) * FLOAT_8061c148;
    DAT_cc008000 = depth - *(float*)(DAT_8061c048 + 0x504) * *(float*)(DAT_8061c048 + 0x510);
    DAT_cc008000 = FLOAT_8061c14c + *(float*)(DAT_8061c048 + 0x4f4) + halfW;
    DAT_cc008000 = FLOAT_8061c14c +
                   *(float*)(DAT_8061c048 + 0x4f8) +
                   *(float*)(DAT_8061c048 + 0x500) * FLOAT_8061c148;
    DAT_cc008000 = depth + *(float*)(DAT_8061c048 + 0x50c);
}

// Address: 0x803cb410 | Name: gx_set_viewport_jitter | Date: 2026-09-15T12:56:00Z
// GXSetViewportJitter: cache the six viewport params (x,y,w,h,near,far) into
// state+0x4f4.., adjusting y by -half-pixel when the field parity arg is 0
// (interlaced jitter), then commit via gx_commit_viewport_internal.
void gx_set_viewport_jitter(double x, double y, double w, double h, double zNear, double zFar,
                            int field) {
    int iVar1;

    iVar1 = DAT_8061c048;
    if (field == 0) {
        y = (double)(float)(y - (double)FLOAT_8061c148);
    }
    *(float*)(DAT_8061c048 + 0x4f4) = (float)x;
    *(float*)(iVar1 + 0x4f8) = (float)y;
    *(float*)(iVar1 + 0x4fc) = (float)w;
    *(float*)(iVar1 + 0x500) = (float)h;
    *(float*)(iVar1 + 0x504) = (float)zNear;
    *(float*)(iVar1 + 0x508) = (float)zFar;
    gx_commit_viewport_internal();
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803cb468 | Name: gx_set_viewport | Date: 2026-09-15T12:56:00Z
// GXSetViewport: cache the six viewport params (x,y,w,h,near,far) into
// state+0x4f4.. and commit via gx_commit_viewport_internal.
void gx_set_viewport(double x, double y, double w, double h, double zNear, double zFar) {
    int iVar1;

    iVar1 = DAT_8061c048;
    *(float*)(DAT_8061c048 + 0x4f4) = (float)x;
    *(float*)(iVar1 + 0x4f8) = (float)y;
    *(float*)(iVar1 + 0x4fc) = (float)w;
    *(float*)(iVar1 + 0x500) = (float)h;
    *(float*)(iVar1 + 0x504) = (float)zNear;
    *(float*)(iVar1 + 0x508) = (float)zFar;
    gx_commit_viewport_internal();
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803cb4b0 | Name: gx_get_viewport_v | Date: 2026-09-15T12:56:00Z
// GXGetViewportv: read back the six cached viewport params (x,y,w,h,near,far)
// from state+0x4f4.. into the caller float[6].
void gx_get_viewport_v(float* out) {
    float y;
    float w;
    float h;
    float zNear;
    float zFar;

    y = *(float*)(DAT_8061c048 + 0x4f8);
    w = *(float*)(DAT_8061c048 + 0x4fc);
    h = *(float*)(DAT_8061c048 + 0x500);
    zNear = *(float*)(DAT_8061c048 + 0x504);
    zFar = *(float*)(DAT_8061c048 + 0x508);
    *out = *(float*)(DAT_8061c048 + 0x4f4);
    out[1] = y;
    out[2] = w;
    out[3] = h;
    out[4] = zNear;
    out[5] = zFar;
}

// Address: 0x803cb4d4 | Name: gx_set_z_scale_offset | Date: 2026-09-15T12:56:00Z
// Program the viewport depth-range scale/offset: cache the near/far z into
// state+0x50c/0x510 (scaled by FLOAT_8061c150, offset by FLOAT_8061c144), then
// stream the two XF depth registers 0x101c (scale) and 0x101f (offset).
void gx_set_z_scale_offset(double zNear, double zFar) {
    float zOff;
    float zScale;
    float depth;
    int iVar4;
    double factor;

    iVar4 = DAT_8061c048;
    factor = (double)FLOAT_8061c150;
    zOff = (float)(factor * zFar);
    *(float*)(DAT_8061c048 + 0x50c) = zOff;
    zScale = FLOAT_8061c144 + (float)(factor * zNear);
    *(float*)(iVar4 + 0x510) = zScale;
    depth = *(float*)(iVar4 + 0x508) * zScale;
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = 0x101c;
    DAT_cc008000 = depth - *(float*)(iVar4 + 0x504) * zScale;
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = 0x101f;
    DAT_cc008000 = zOff + depth;
    *(unsigned short*)(iVar4 + 2) = 1;
}

// Address: 0x803cb540 | Name: gx_set_scissor | Date: 2026-09-15T12:56:00Z
// GXSetScissor: pack the top-left (x,y) and bottom-right (x+w-1,y+h-1) corners,
// each biased by +0x156, into the scissor BP registers state+0xf8/0xfc and
// stream both (write-gather 0x61 BP-load).
void gx_set_scissor(int x, int y, int w, int h) {
    int iVar1;

    iVar1 = DAT_8061c048;
    *(uint*)(DAT_8061c048 + 0xf8) =
         y + 0x156U & 0x7ff | *(uint*)(DAT_8061c048 + 0xf8) & 0xfffff800;
    *(uint*)(iVar1 + 0xf8) =
         (x + 0x156) * 0x1000 & 0x7ff000U | *(uint*)(iVar1 + 0xf8) & 0xff800fff;
    *(uint*)(iVar1 + 0xfc) =
         y + 0x156U + h + -1 & 0x7ff | *(uint*)(iVar1 + 0xfc) & 0xfffff800;
    *(uint*)(iVar1 + 0xfc) =
         (x + 0x156 + w + -1) * 0x1000 & 0x7ff000U | *(uint*)(iVar1 + 0xfc) & 0xff800fff;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = *(unsigned int*)(iVar1 + 0xf8);
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = *(unsigned int*)(iVar1 + 0xfc);
    *(unsigned short*)(iVar1 + 2) = 0;
}

// Address: 0x803cb5b8 | Name: gx_set_scissor_box_offset | Date: 2026-09-15T12:56:00Z
// GXSetScissorBoxOffset: pack the +0x156-biased box offset (x>>1, y) into the
// scissor-box-offset BP register (opcode 0x59) and stream it.
void gx_set_scissor_box_offset(int x, int y) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = (y + 0x156) * 0x200 & 0xffc00U | x + 0x156U >> 1 & 0x3ff | 0x59000000;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// ---------------------------------------------------------------------------
// batch_1178: GX clip/pixel-format + matrix-index commit + GP performance-metric
// layer (appended to gx_init.cpp, completing the FUN_* backlog in this module).
// This resolves FUN_803cb620 to its real name gx_commit_matrix_index; the
// earlier forward decl (line ~1783) and its two in-file call sites are renamed
// to match in the same edit.
// NON-MATCHING: the flat DAT_8061c048 state offsets, the CP register mirror
// DAT_8061357c / PE register mirror DAT_80613580 accesses, the DAT_cc008000
// write-gather packing (0x08 BP-idx, 0x10 CP-load, 0x61 BP-load), the pixel-fmt
// switch's raw BP command words, and the double-read metric consistency loops
// are kept verbatim.
// ---------------------------------------------------------------------------

// Address: 0x803cb5f8 | Name: gx_set_clip_mode | Date: 2026-09-15T13:12:00Z
// Stream the XF clip-control register 0x1005 (clip-disable / co-planar mode).
void gx_set_clip_mode(undefined4 mode) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x10;
    DAT_cc008000 = 0x1005;
    DAT_cc008000 = mode;
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803cb620 | Name: gx_commit_matrix_index | Date: 2026-09-15T13:12:00Z
// Reprogram the matrix-index-A (sel<5) or matrix-index-B (sel>=5) register:
// stream the cached word from state+0x80 / +0x84 as both a BP-index update
// (write-gather 0x08, register 0x30 / 0x40) and an XF register load (0x1018 /
// 0x1019) so both the CP and XF views stay coherent.
void gx_commit_matrix_index(int sel) {
    if (sel < 5) {
        *(volatile unsigned char*)&DAT_cc008000 = 8;
        *(volatile unsigned char*)&DAT_cc008000 = 0x30;
        DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x80);
        *(volatile unsigned char*)&DAT_cc008000 = 0x10;
        DAT_cc008000 = 0x1018;
        DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x80);
    } else {
        *(volatile unsigned char*)&DAT_cc008000 = 8;
        *(volatile unsigned char*)&DAT_cc008000 = 0x40;
        DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x84);
        *(volatile unsigned char*)&DAT_cc008000 = 0x10;
        DAT_cc008000 = 0x1019;
        DAT_cc008000 = *(undefined4*)(DAT_8061c048 + 0x84);
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 1;
}

// Address: 0x803cb6a4 | Name: gx_set_pixel_fmt | Date: 2026-09-15T13:12:00Z
// GXSetPixelFmt: reprogram the EFB pixel format (state+0x59c) and z-compression
// format (state+0x5a0). It first undoes the register state implied by the old
// formats (clip-scale XF 0x1006, SU line-width BP 0x23/0x24, PE control 0x67,
// pixel-engine mask), then applies the new formats through a large switch that
// selects the correct XF clip-scale value / BP GEN_MODE word / z-format nibble
// for each of the ~0x23 pixel formats and ~0x16 z formats.
void gx_set_pixel_fmt(undefined4 pixelFmt, undefined4 zFmt) {
    int iVar1;
    int iVar2;

    iVar1 = DAT_8061c048;
    iVar2 = *(int*)(DAT_8061c048 + 0x59c);
    if (iVar2 == 0x22) {
LAB_803cb6dc:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10;
        DAT_cc008000 = 0x1006;
        DAT_cc008000 = 0;
    } else if (iVar2 < 0x22) {
        if (iVar2 < 0xb) {
            if (-1 < iVar2) goto LAB_803cb6dc;
        } else if (iVar2 < 0x1b) {
            *(volatile unsigned char*)&DAT_cc008000 = 0x61;
            DAT_cc008000 = 0x23000000;
        } else {
            *(volatile unsigned char*)&DAT_cc008000 = 0x61;
            DAT_cc008000 = 0x24000000;
        }
    }
    iVar2 = *(int*)(DAT_8061c048 + 0x5a0);
    if (iVar2 != 0x15) {
        if (0x14 < iVar2) goto LAB_803cb7b4;
        if (8 < iVar2) {
            if (iVar2 < 0x11) {
                *(uint*)(DAT_8061c048 + 0x5a4) = *(uint*)(DAT_8061c048 + 0x5a4) & 0xffffff0f;
                *(volatile unsigned char*)&DAT_cc008000 = 8;
                *(volatile unsigned char*)&DAT_cc008000 = 0x20;
                DAT_cc008000 = *(undefined4*)(iVar1 + 0x5a4);
            } else {
                *(unsigned short*)(DAT_8061357c + 6) = 0;
            }
            goto LAB_803cb7b4;
        }
        if (iVar2 < 0) goto LAB_803cb7b4;
    }
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = 0x67000000;
LAB_803cb7b4:
    iVar1 = DAT_8061c048;
    *(undefined4*)(DAT_8061c048 + 0x59c) = pixelFmt;
    iVar2 = DAT_8061c048;
    switch (*(int*)(iVar1 + 0x59c)) {
    case 0:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x273; break;
    case 1:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x14a; break;
    case 2:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x16b; break;
    case 3:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x84; break;
    case 4:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0xc6; break;
    case 5:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x210; break;
    case 6:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x252; break;
    case 7:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x231; break;
    case 8:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x1ad; break;
    case 9:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x1ce; break;
    case 10:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x153; break;
    case 0xb:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300ae7f; break;
    case 0xc:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x23008e7f; break;
    case 0xd:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x23009e7f; break;
    case 0xe:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x23001e7f; break;
    case 0xf:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300ac3f; break;
    case 0x10:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300ac7f; break;
    case 0x11:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300acbf; break;
    case 0x12:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300acff; break;
    case 0x13:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300ad3f; break;
    case 0x14:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300ad7f; break;
    case 0x15:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300adbf; break;
    case 0x16:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300adff; break;
    case 0x17:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300ae3f; break;
    case 0x18:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300a27f; break;
    case 0x19:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300a67f; break;
    case 0x1a:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2300aa7f; break;
    case 0x1b:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c0c6; break;
    case 0x1c:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c16b; break;
    case 0x1d:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c0e7; break;
    case 0x1e:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c108; break;
    case 0x1f:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c129; break;
    case 0x20:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c14a; break;
    case 0x21:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x2402c1ad; break;
    case 0x22:
        *(volatile unsigned char*)&DAT_cc008000 = 0x10; DAT_cc008000 = 0x1006; DAT_cc008000 = 0x21;
    }
    *(undefined4*)(DAT_8061c048 + 0x5a0) = zFmt;
    switch (*(int*)(iVar2 + 0x5a0)) {
    case 0:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000042; break;
    case 1:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000084; break;
    case 2:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000063; break;
    case 3:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000129; break;
    case 4:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x6700014b; break;
    case 5:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x6700018d; break;
    case 6:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x670001cf; break;
    case 7:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000211; break;
    case 8:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000252; break;
    case 9:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x20;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 10:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x30;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0xb:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x40;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0xc:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x50;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0xd:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x60;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0xe:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x70;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0xf:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x90;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0x10:
        *(uint*)(iVar2 + 0x5a4) = *(uint*)(iVar2 + 0x5a4) & 0xffffff0f | 0x80;
        *(volatile unsigned char*)&DAT_cc008000 = 8; *(volatile unsigned char*)&DAT_cc008000 = 0x20;
        DAT_cc008000 = *(undefined4*)(iVar2 + 0x5a4); break;
    case 0x11:
        *(unsigned short*)(DAT_8061357c + 6) = 2; break;
    case 0x12:
        *(unsigned short*)(DAT_8061357c + 6) = 3; break;
    case 0x13:
        *(unsigned short*)(DAT_8061357c + 6) = 4; break;
    case 0x14:
        *(unsigned short*)(DAT_8061357c + 6) = 5; break;
    case 0x15:
        *(volatile unsigned char*)&DAT_cc008000 = 0x61; DAT_cc008000 = 0x67000021;
    }
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}

// Address: 0x803cbeec | Name: gx_set_gp_metric_control | Date: 2026-09-15T13:12:00Z
// Poke the CP performance-metric select register (mirror DAT_8061357c + 4) to
// mode 4, arming the GP pixel/quad metric counters used by the occlusion query.
void gx_set_gp_metric_control(void) {
    *(unsigned short*)(DAT_8061357c + 4) = 4;
}

// Address: 0x803cbefc | Name: gx_read_gp_metrics | Date: 2026-09-15T13:12:00Z
// Read six 48-bit GP performance counters from the PE register mirror
// (DAT_80613580 +0x18..+0x2e). Each counter's high halfword is re-read until it
// reads stable (the hardware latches lo then hi), then the {hi,lo} pair is
// combined; the first five are shifted left by 2 (quad->pixel scale), the sixth
// (input-primitive count) is returned raw.
void gx_read_gp_metrics(int* out0, int* out1, int* out2, int* out3, int* out4, undefined4* out5) {
    bool changed;
    short hi;
    short prevHi;

    prevHi = *(short*)(DAT_80613580 + 0x1a);
    do {
        hi = *(short*)(DAT_80613580 + 0x1a);
        changed = hi != prevHi;
        prevHi = hi;
    } while (changed);
    *out0 = CONCAT22(hi, *(undefined2*)(DAT_80613580 + 0x18)) << 2;
    prevHi = *(short*)(DAT_80613580 + 0x1e);
    do {
        hi = *(short*)(DAT_80613580 + 0x1e);
        changed = hi != prevHi;
        prevHi = hi;
    } while (changed);
    *out1 = CONCAT22(hi, *(undefined2*)(DAT_80613580 + 0x1c)) << 2;
    prevHi = *(short*)(DAT_80613580 + 0x22);
    do {
        hi = *(short*)(DAT_80613580 + 0x22);
        changed = hi != prevHi;
        prevHi = hi;
    } while (changed);
    *out2 = CONCAT22(hi, *(undefined2*)(DAT_80613580 + 0x20)) << 2;
    prevHi = *(short*)(DAT_80613580 + 0x26);
    do {
        hi = *(short*)(DAT_80613580 + 0x26);
        changed = hi != prevHi;
        prevHi = hi;
    } while (changed);
    *out3 = CONCAT22(hi, *(undefined2*)(DAT_80613580 + 0x24)) << 2;
    prevHi = *(short*)(DAT_80613580 + 0x2a);
    do {
        hi = *(short*)(DAT_80613580 + 0x2a);
        changed = hi != prevHi;
        prevHi = hi;
    } while (changed);
    *out4 = CONCAT22(hi, *(undefined2*)(DAT_80613580 + 0x28)) << 2;
    prevHi = *(short*)(DAT_80613580 + 0x2e);
    do {
        hi = *(short*)(DAT_80613580 + 0x2e);
        changed = hi != prevHi;
        prevHi = hi;
    } while (changed);
    *out5 = CONCAT22(hi, *(undefined2*)(DAT_80613580 + 0x2c));
}

// Address: 0x803cc034 | Name: gx_clear_gp_metric | Date: 2026-09-15T13:12:00Z
// Clear/reset the GP performance metrics by streaming the SU perf-metric BP
// register (opcode 0x57) twice: a zeroing word then the enable/clear pattern
// 0xaaa, arming a fresh occlusion/pixel-count measurement.
void gx_clear_gp_metric(void) {
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = 0x57000000;
    *(volatile unsigned char*)&DAT_cc008000 = 0x61;
    DAT_cc008000 = 0x57000aaa;
    *(unsigned short*)(DAT_8061c048 + 2) = 0;
}
