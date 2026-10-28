// anim_codec.cpp
// Bit-packed keyframe / channel-animation decode codec bound to the message /
// replay context. The compressed stream is introduced by the ASCII magic
// "VCB" (0x564342) and carries a codebook (per-symbol bit widths, optionally
// RLE-encoded), a set of channel descriptors dispatched through two builder
// vtables (PTR_DAT_80610b08 / PTR_DAT_80425f40), and a payload table. The
// decoder unpacks channels frame-by-frame, blending against a weight table
// (FUN_801a10b0) with 64-bit fixed-point accumulators.
//
// Object layout notes (offsets are byte offsets into the decoder state):
//   state+0x04 : owning context (holds codebook at +0x1c, counts at +4)
//   state+0x08 : channel-scratch pointer array
//   state+0x0c : output pointer array
//   state+0x14 : window end (frames available)
//   state+0x18 : current read position
//   state+0x68 : decode sub-context (per-channel objects, ranges at +0xb8/+0xbc)
//   state+0x54 : head of scratch-chunk free list (next at [1])
//
// The FUN_803826xx helpers are the compiler's callee-saved-register
// save/restore prologue/epilogue thunks; the value Ghidra models as their
// "return" is really the incoming r3:r4 pair (the state / context pointers).
// They are reconstructed here as explicit locals seeded from the parameters.
//
// Address range: 0x80193258 - 0x801998f0
// Batches: batch_509 (0x80193258-0x801944f8), batch_510 (0x8019455c-0x80194dfc:
//   channel bit-write, Huffman symbol decode, the vtable-dispatched per-type
//   channel decode handlers, and sub-context/buffer teardown),
//   batch_511 (0x80194dbc-0x8019568c: curve-header parse, the double-precision
//   falloff/attenuation table builder, ramp decode, channel eval, lookup-node
//   create, a few sized free helpers, and two bit-width leaves)
#include "common.h"

extern "C" {
// --- raw runtime allocator / memory helpers ---
extern void        *FUN_801a13e0(unsigned int size);         // buffer alloc (word array)
extern int          FUN_801a1498(unsigned int count, unsigned int elemSize); // calloc-like
extern int          FUN_801a1528(void);                      // scratch grow (new base)
extern void         FUN_801a14dc(int buf);                   // buffer free
unsigned char *anim_weight_table_lookup(int index);         // weight-table lookup (defined batch_523 below)
void                anim_fft_plan_init(int *plan, int n);          // FFT plan build (defined batch_523 below)
void                anim_fft_plan_free(int plan);                  // FFT plan free  (defined batch_523 below)
extern void         memset_wrapper(int dst, int val, int len);

// --- spill / register-save prologue helpers (context-pair carriers) ---
extern void FUN_8038273c(void);   // spill restore (paired with FUN_803826f0)
extern void FUN_80382724(void);   // restore (paired with FUN_803826d8)
extern void FUN_80382700(void);   // restore (paired with FUN_803826b4)
extern void FUN_80382734(void);   // restore (paired with FUN_803826e8)

// --- codebook / channel builder + teardown callees ---
extern void FUN_801995cc(void);                 // codebook table build step
void FUN_8019b380(int obj, int *desc, unsigned int *cb,
                  int mid, unsigned int flags);         // FUN_8019b380 channel descriptor build (defined batch_517 below)
extern void FUN_8019f434(int obj);              // channel object free
extern void FUN_8019f4fc(unsigned int *dst, unsigned int *src); // channel object copy
extern void FUN_8019f64c(void);                 // channel object init
// FFT sub-buffer teardowns are defined in this file (batch_516) below.
void anim_fft_free_pair(int desc);              // FUN_8019a120 (twiddle-pair teardown, 0x14)
void anim_fft_free_0x24(int obj);               // FUN_8019a858 (sub-decoder teardown, 0x24)
void FUN_8019bbfc(void);                        // per-entry teardown step (defined batch_517 below)
extern int  FUN_8019ed34(int n);                // bit width for remaining count
extern int  FUN_8019efac(unsigned int *cb);     // payload element count (type 1)
extern void FUN_8019f348(int obj);              // codebook parse cleanup

// --- 64-bit fixed-point divide/shift helper (returns r3:r4) ---
extern unsigned long long FUN_80382ba8(unsigned int hi, unsigned int lo, unsigned int shift);

// --- libm-style scalar math (falloff/attenuation curve) ---
extern double FUN_8038fecc(double x);   // exp-like
extern double FUN_80390324(double x);   // pow/round-like
// FUN_80197f58 (weighted eval) is defined in this file (batch_514) below.
void FUN_80197f58(double a, double b, int out, int table, int count,
                  unsigned int dim, float *weights, unsigned int idx);

// --- math constants used by the falloff table builder (0x80616918..0x80616944) ---
extern double DOUBLE_80616918;   // int->double bias (2^52 + 2^31 magic)
extern double DOUBLE_80616928;
extern double DOUBLE_80616938;
extern float  FLOAT_80616920;
extern float  FLOAT_80616924;
extern float  FLOAT_80616930;
extern float  FLOAT_80616940;
extern float  FLOAT_80616944;

// --- builder/teardown dispatch vtables (each entry: [+8]=build, [+0x10]=free) ---
extern int PTR_DAT_80610b08[];
extern int PTR_DAT_80425f40[];
} // extern "C"

// Named cross-module helpers (C++ linkage) resolve through the shared headers.
extern unsigned int bitreader_peek_bits(int *br, int count);
extern unsigned int bitreader_read_bits(int *br, int count);
extern void         bitreader_advance(int *br, int count);
extern void         bitwriter_write_bits(int bw, unsigned int value, int count);
extern void         bitwriter_free(int bw);
extern void         msg_ctx_free(void);
extern int          bits_for_count(unsigned int n);
extern int          msg_arena_alloc(int arena, int size);

// Forward decls for functions defined later in this file but referenced earlier.
int  anim_huffman_decode_symbol(void);
unsigned int bit_reverse32(unsigned int v);
void anim_subctx_free_buffers(void);   // (FUN_80194cf8) called by anim_ctx_destroy
int  anim_decode_channels_copy(int count);      // (FUN_80194b64) used by anim_ramp_decode
void anim_build_falloff_table(unsigned int arg0, unsigned int arg1, unsigned int *desc);
void anim_buffer_free(int obj);        // (FUN_80194d7c) used by anim_curve_header_parse

// The spill accessors below return the active context pair (r3:r4). We model
// each call site by fetching the two live pointers directly; the exact runtime
// object is provided by the caller's saved registers.
extern "C" {
extern unsigned long long FUN_803826f0(void);   // -> active scratch object (r3)
extern unsigned long long FUN_803826d8(void);   // -> (stateHi:ctxLo) build pair
extern unsigned long long FUN_803826b4(void);   // -> (dataHi:stateLo) step pair
extern unsigned long long FUN_803826e8(void);   // -> (ctxHi:outLo) codebook pair
extern unsigned long long FUN_803826e4(void);   // -> (stateHi:dstLo) decode pair
extern unsigned long long FUN_803826dc(void);   // -> (stateHi:frameLo) falloff pair
extern int                __save_gpr_25(void);   // -> active decoder state (r3)
extern void               __restore_gpr_25(void);
extern void               FUN_80382730(void);    // restore (paired with FUN_803826e4)
extern void               FUN_80382728(void);    // restore (paired with FUN_803826dc)
}

// ---------------------------------------------------------------------------
// anim_ctx_free_scratch_chunks (FUN_80193258)
// Walk the ctx+0x54 singly-linked list of scratch chunks, freeing each chunk's
// backing buffer and the chunk node. Then, if a pending scratch delta exists at
// ctx+0x50, grow the spill scratch (FUN_801a1528), fold the delta into the used
// size (ctx+0x4c) and clear it. Finally reset the head/cursor and restore spill.
// ---------------------------------------------------------------------------
void anim_ctx_free_scratch_chunks(void) {
    int ctx = (int)FUN_803826f0();
    int *node = *(int **)(ctx + 0x54);
    while (node != (int *)0) {
        int *next = (int *)node[1];
        FUN_801a14dc(*node);
        memset_wrapper((int)node, 0, 8);
        FUN_801a14dc((int)node);
        node = next;
    }
    if (*(int *)(ctx + 0x50) != 0) {
        int newBase = FUN_801a1528();
        *(int *)(ctx + 0x44) = newBase;
        *(int *)(ctx + 0x4c) = *(int *)(ctx + 0x4c) + *(int *)(ctx + 0x50);
        *(int *)(ctx + 0x50) = 0;
    }
    *(int *)(ctx + 0x48) = 0;
    *(int *)(ctx + 0x54) = 0;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_decoder_free (FUN_80193304)
// Tear down a decoder object: release its bitwriter (word[1], if the header
// pointer is non-null and points at a live buffer), flush the shared scratch
// chunks, free the working codebook buffer (+0x44) and the decode sub-context
// (+0x68), then zero the 0x70-byte object. Always returns 0.
// ---------------------------------------------------------------------------
unsigned int anim_decoder_free(int dec) {
    if ((*(int **)(dec + 0x40) != (int *)0) && (**(int **)(dec + 0x40) != 0)) {
        bitwriter_free(dec + 4);
    }
    anim_ctx_free_scratch_chunks();
    if (*(int *)(dec + 0x44) != 0) {
        FUN_801a14dc(*(int *)(dec + 0x44));
    }
    if (*(int *)(dec + 0x68) != 0) {
        FUN_801a14dc(*(int *)(dec + 0x68));
    }
    memset_wrapper(dec, 0, 0x70);
    return 0;
}

// ---------------------------------------------------------------------------
// anim_decoder_build_tables (FUN_80193388)
// Allocate and populate the 0xC0-byte decode-state block from the codebook
// (ctx+0x1c). Computes per-symbol bit widths (bits_for_count), builds the two
// channel-descriptor tables via the PTR_DAT_80610b08 / PTR_DAT_80425f40 builder
// vtables, and allocates the per-frame scratch and output pointer arrays.
// `mode` selects the full build (mode!=0, includes range init + descriptor
// copies + the third descriptor table at state+0x38) vs. the lightweight build.
// The state/context pointer pair arrives via the register-save helper.
// ---------------------------------------------------------------------------
int anim_decoder_build_tables(int decState, unsigned int /*unused*/, int mode) {
    unsigned long long pair = FUN_803826d8();
    unsigned int *state = (unsigned int *)decState; // hi32 of pair == state ptr
    int ctx = (int)pair;                            // lo32 of pair == ctx ptr
    unsigned int *cb = *(unsigned int **)(ctx + 0x1c);
    int result = 0;

    if (cb != (unsigned int *)0) {
        memset_wrapper((int)state, 0, 0x70);
        int st = FUN_801a1498(1, 0xc0);
        state[0x1a] = (unsigned int)st;
        state[1] = (unsigned int)ctx;

        int w = bits_for_count(cb[2]);
        *(int *)(st + 0x2c) = w;
        *(int *)(st + 0xc)  = FUN_801a1498(1, 4);
        *(int *)(st + 0x10) = FUN_801a1498(1, 4);
        **(int **)(st + 0xc)  = FUN_801a1498(1, 0x14);
        **(int **)(st + 0x10) = FUN_801a1498(1, 0x14);
        FUN_801995cc();
        FUN_801995cc();
        *(int *)(st + 4) = bits_for_count(cb[0]) - 6;
        *(int *)(st + 8) = bits_for_count(cb[1]) - 6;

        if (mode == 0) {
            if (cb[0x2c8] == 0) {
                cb[0x2c8] = (unsigned int)FUN_801a1498(cb[6], 0x2c);
                int off = 0;
                for (int i = 0; i < (int)cb[6]; i = i + 1) {
                    FUN_8019f64c();
                    FUN_8019f434(*(int *)((int)cb + off + 0x720));
                    *(int *)((int)cb + off + 0x720) = 0;
                    off = off + 4;
                }
            }
        } else {
            FUN_801a0e38((int *)(st + 0x14), cb[0]);
            FUN_801a0e38((int *)(st + 0x20), cb[1]);
            if (cb[0x2c8] == 0) {
                cb[0x2c8] = (unsigned int)FUN_801a1498(cb[6], 0x2c);
                int off = 0;
                int dstOff = 0;
                for (int i = 0; i < (int)cb[6]; i = i + 1) {
                    FUN_8019f4fc((unsigned int *)(cb[0x2c8] + dstOff),
                                 *(unsigned int **)((int)cb + off + 0x720));
                    off = off + 4;
                    dstOff = dstOff + 0x2c;
                }
            }
            *(int *)(st + 0x38) = FUN_801a1498(cb[7], 0x30);
            int off = 0;
            int srcOff = 0;
            for (int i = 0; i < (int)cb[7]; i = i + 1) {
                int *desc = *(int **)((int)cb + srcOff + 0xb24);
                unsigned int v = cb[*desc];
                int mid = ((int)v >> 1) + (((int)v < 0 && (v & 1) != 0) ? 1 : 0);
                FUN_8019b380(*(int *)(st + 0x38) + off, desc, cb + 0x2cd, mid,
                             *(unsigned int *)(ctx + 8));
                off = off + 0x30;
                srcOff = srcOff + 4;
            }
            state[0] = 1;
        }

        state[4] = cb[1];
        state[2] = (unsigned int)FUN_801a13e0(*(int *)(ctx + 4) << 2);
        state[3] = (unsigned int)FUN_801a13e0(*(int *)(ctx + 4) << 2);
        int off = 0;
        for (int i = 0; i < *(int *)(ctx + 4); i = i + 1) {
            *(int *)(state[2] + off) = FUN_801a1498(state[4], 4);
            off = off + 4;
        }
        state[9]  = 0;
        state[10] = 0;
        {
            unsigned int v = cb[1];
            state[0xc] = ((int)v >> 1) + (((int)v < 0 && (v & 1) != 0) ? 1 : 0);
        }
        state[5] = state[0xc];
        *(int *)(st + 0x30) = FUN_801a1498(cb[4], 4);
        *(int *)(st + 0x34) = FUN_801a1498(cb[5], 4);

        int idx = 0;
        for (int i = 0; i < (int)cb[4]; i = i + 1) {
            int slot = *(int *)((int)cb + idx + 800);
            typedef int (*BuildFn)(unsigned int *, int);
            BuildFn fn = *(BuildFn *)(PTR_DAT_80610b08[slot] + 8);
            int out = fn(state, *(int *)((int)cb + idx + 0x420));
            *(int *)(*(int *)(st + 0x30) + idx) = out;
            idx = idx + 4;
        }
        idx = 0;
        for (int i = 0; i < (int)cb[5]; i = i + 1) {
            int slot = *(int *)((int)cb + idx + 0x520);
            typedef int (*BuildFn)(unsigned int *, int);
            BuildFn fn = *(BuildFn *)(PTR_DAT_80425f40[slot] + 8);
            int out = fn(state, *(int *)((int)cb + idx + 0x620));
            *(int *)(*(int *)(st + 0x34) + idx) = out;
            idx = idx + 4;
        }
    }
    FUN_80382724();
    return result;
}

// ---------------------------------------------------------------------------
// anim_ctx_destroy (FUN_80193738)
// Full teardown of the decode sub-context (state+0x68): free the codebook
// working copy, the two range lists (via FUN_8019a120), every per-channel
// object through the builder-vtable free slot (+0x10 of PTR_DAT_80610b08 /
// PTR_DAT_80425f40), the descriptor/entry tables, then the message context
// (msg_ctx_free) and the two embedded ranges. Also frees the state's own index
// arrays. Finally zero the 0x70-byte object. No-op when no state is active.
// The active decoder state arrives via the register-save helper (r3).
// ---------------------------------------------------------------------------
void anim_ctx_destroy(void) {
    int state = __save_gpr_25();
    if (state != 0) {
        int ctx = *(int *)(state + 4);
        int cb = (ctx == 0) ? 0 : *(int *)(ctx + 0x1c);
        int *sub = *(int **)(state + 0x68);
        if (sub != (int *)0) {
            if (*sub != 0) {
                anim_subctx_free_buffers();
                FUN_801a14dc(*sub);
            }
            if ((int *)sub[3] != (int *)0) {
                anim_fft_free_pair(*(int *)sub[3]);
                FUN_801a14dc(*(int *)sub[3]);
                FUN_801a14dc(sub[3]);
            }
            if ((int *)sub[4] != (int *)0) {
                anim_fft_free_pair(*(int *)sub[4]);
                FUN_801a14dc(*(int *)sub[4]);
                FUN_801a14dc(sub[4]);
            }
            if (sub[0xc] != 0) {
                int idx = 0;
                for (int i = 0; i < *(int *)(cb + 0x10); i = i + 1) {
                    int slot = *(int *)(cb + idx + 800);
                    typedef void (*FreeFn)(int);
                    FreeFn fn = *(FreeFn *)(PTR_DAT_80610b08[slot] + 0x10);
                    fn(*(int *)(sub[0xc] + idx));
                    idx = idx + 4;
                }
                FUN_801a14dc(sub[0xc]);
            }
            if (sub[0xd] != 0) {
                int idx = 0;
                for (int i = 0; i < *(int *)(cb + 0x14); i = i + 1) {
                    int slot = *(int *)(cb + idx + 0x520);
                    typedef void (*FreeFn)(int);
                    FreeFn fn = *(FreeFn *)(PTR_DAT_80425f40[slot] + 0x10);
                    fn(*(int *)(sub[0xd] + idx));
                    idx = idx + 4;
                }
                FUN_801a14dc(sub[0xd]);
            }
            if (sub[0xe] != 0) {
                for (int i = 0; i < *(int *)(cb + 0x1c); i = i + 1) {
                    FUN_8019bbfc();
                }
                FUN_801a14dc(sub[0xe]);
            }
            if (sub[0xf] != 0) {
                anim_fft_free_0x24(sub[0xf]);
            }
            msg_ctx_free();
            FUN_801a0ea0((int)(sub + 5));
            FUN_801a0ea0((int)(sub + 8));
        }
        if (*(int *)(state + 8) != 0) {
            int idx = 0;
            for (int i = 0; i < *(int *)(ctx + 4); i = i + 1) {
                int p = *(int *)(*(int *)(state + 8) + idx);
                if (p != 0) {
                    FUN_801a14dc(p);
                }
                idx = idx + 4;
            }
            FUN_801a14dc(*(int *)(state + 8));
            if (*(int *)(state + 0xc) != 0) {
                FUN_801a14dc(*(int *)(state + 0xc));
            }
        }
        if (sub != (int *)0) {
            if (sub[0x10] != 0) FUN_801a14dc(sub[0x10]);
            if (sub[0x11] != 0) FUN_801a14dc(sub[0x11]);
            if (sub[0x12] != 0) FUN_801a14dc(sub[0x12]);
            FUN_801a14dc((int)sub);
        }
        memset_wrapper(state, 0, 0x70);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_decoder_reset (FUN_801939c8)
// Reset the decode window and channel positions from the codebook shift stored
// at codebook+0xe78. Returns -1 (0xffffffff) if the sub-context (+0x68), the
// owning context (+4), or the codebook are missing; 0 on success.
// ---------------------------------------------------------------------------
unsigned int anim_decoder_reset(int dec) {
    if (*(int *)(dec + 0x68) == 0)  return 0xffffffff;
    if (*(int *)(dec + 4) == 0)     return 0xffffffff;
    int cb = *(int *)(*(int *)(dec + 4) + 0x1c);
    if (cb == 0)                    return 0xffffffff;

    unsigned int shift = *(unsigned int *)(cb + 0xe78);
    *(int *)(dec + 0x30) = *(int *)(cb + 4) >> ((shift + 1) & 0x3f);
    *(int *)(dec + 0x14) = *(int *)(dec + 0x30) >> (shift & 0x3f);
    *(int *)(dec + 0x18) = -1;
    *(int *)(dec + 0x3c) = -1;
    *(int *)(dec + 0x38) = -1;
    *(int *)(dec + 0x44) = -1;
    *(int *)(dec + 0x40) = -1;
    *(int *)(dec + 0x20) = 0;
    int sub = *(int *)(dec + 0x68);
    *(int *)(sub + 0xbc) = -1;
    *(int *)(sub + 0xb8) = -1;
    return 0;
}

// ---------------------------------------------------------------------------
// anim_decoder_begin (FUN_80193a58)
// Build the lightweight decode tables (mode 0) then reset the decoder. Returns
// true when the table build failed (non-zero), matching the original's
// (result != 0) return; on success it performs the reset.
// ---------------------------------------------------------------------------
bool anim_decoder_begin(int dec, unsigned int arg) {
    int rc = anim_decoder_build_tables(dec, arg, 0);
    if (rc == 0) {
        anim_decoder_reset(dec);
    }
    return rc != 0;
}

// ---------------------------------------------------------------------------
// anim_decode_step (FUN_80193aa4)
// Decode one frame of channel data. Advances the read window, blends each
// channel against the weight table (FUN_801a10b0) using paired-float LERP loops,
// maintains four 64-bit accumulators (byte/sample counters) with carry
// propagation, and updates the presentation timestamps. The data/state pointer
// pair arrives via the register-save helper.
// ---------------------------------------------------------------------------
void anim_decode_step(void) {
    unsigned long long pair = FUN_803826b4();
    int state = (int)((unsigned long long)pair >> 0x20);
    int *frame = (int *)pair;
    int ctx = *(int *)(state + 4);
    int sub = *(int *)(state + 0x68);
    int *cb = *(int **)(ctx + 0x1c);
    unsigned int shift = (unsigned int)cb[0x39e];

    if ((frame != (int *)0) &&
        ((*(int *)(state + 0x14) <= *(int *)(state + 0x18)) ||
         (*(int *)(state + 0x18) == -1))) {
        *(int *)(state + 0x24) = *(int *)(state + 0x28);
        *(int *)(state + 0x28) = frame[7];
        *(int *)(state + 0x2c) = -1;
        unsigned int win = *(unsigned int *)(state + 0x44);
        if ((win == 0xffffffff && *(int *)(state + 0x40) == -1) ||
            (win + 1 != (unsigned int)frame[0xf] ||
             *(int *)(state + 0x40) + (unsigned int)(0xfffffffe < win) != frame[0xe])) {
            *(int *)(state + 0x3c) = -1;
            *(int *)(state + 0x38) = -1;
            *(int *)(sub + 0xbc) = -1;
            *(int *)(sub + 0xb8) = -1;
        }
        *(int *)(state + 0x44) = frame[0xf];
        *(int *)(state + 0x40) = frame[0xe];

        if (*frame != 0) {
            unsigned int shBase = shift + 1;
            unsigned int lenA = (unsigned int)frame[0x16];
            int prevCount = cb[*(int *)(state + 0x28)] >> (shBase & 0x3f);
            unsigned int accLo = *(unsigned int *)(state + 0x4c);
            unsigned int cur0 = (unsigned int)cb[0] >> (shBase & 0x3f);
            unsigned int cur1 = (unsigned int)cb[1] >> (shBase & 0x3f);

            *(unsigned int *)(state + 0x4c) = accLo + lenA;
            *(int *)(state + 0x48) = *(int *)(state + 0x48) + ((int)lenA >> 0x1f) +
                                     (CARRY4(accLo, lenA) ? 1 : 0);
            {
                unsigned int v = (unsigned int)frame[0x17];
                unsigned int lo = *(unsigned int *)(state + 0x54);
                *(unsigned int *)(state + 0x54) = lo + v;
                *(int *)(state + 0x50) = *(int *)(state + 0x50) + ((int)v >> 0x1f) +
                                         (CARRY4(lo, v) ? 1 : 0);
            }
            {
                unsigned int v = (unsigned int)frame[0x18];
                unsigned int lo = *(unsigned int *)(state + 0x5c);
                *(unsigned int *)(state + 0x5c) = lo + v;
                *(int *)(state + 0x58) = *(int *)(state + 0x58) + ((int)v >> 0x1f) +
                                         (CARRY4(lo, v) ? 1 : 0);
            }
            {
                unsigned int v = (unsigned int)frame[0x19];
                unsigned int lo = *(unsigned int *)(state + 100);
                *(unsigned int *)(state + 100) = lo + v;
                *(int *)(state + 0x60) = *(int *)(state + 0x60) + ((int)v >> 0x1f) +
                                         (CARRY4(lo, v) ? 1 : 0);
            }

            unsigned int outCount;
            unsigned int carryCount;
            if (*(int *)(state + 0x30) == 0) {
                carryCount = 0;
                outCount = cur1;
            } else {
                outCount = 0;
                carryCount = cur1;
            }
            int baseOff = outCount * 4;
            int half1 = ((int)cur1 >> 1) + (((int)cur1 < 0 && (cur1 & 1) != 0) ? 1 : 0);
            int half0 = ((int)cur0 >> 1) + (((int)cur0 < 0 && (cur0 & 1) != 0) ? 1 : 0);
            int half1Off = half1 * 4;
            int chan = 0;
            int total = half1 + half0;

            for (int c = 0; c < *(int *)(ctx + 4); c = c + 1) {
                if (*(int *)(state + 0x24) == 0) {
                    if (*(int *)(state + 0x28) == 0) {
                        unsigned char *w = FUN_801a10b0(*(int *)(sub + 4) - shift);
                        int k = 0;
                        int off = 0;
                        int srcA = *(int *)(*frame + chan);
                        int dst = *(int *)(*(int *)(state + 8) + chan) + baseOff;
                        unsigned int rem = cur0;
                        if (0 < (int)cur0) {
                            do {
                                int wi = cur0 - k;
                                k = k + 1;
                                *(float *)(dst + off) =
                                    *(float *)(dst + off) * *(float *)(w + wi * 4 - 4) +
                                    *(float *)(srcA + off) * *(float *)(w + off);
                                off = off + 4;
                                rem = rem - 1;
                            } while (rem != 0);
                        }
                    } else {
                        unsigned char *w = FUN_801a10b0(*(int *)(sub + 4) - shift);
                        int k = 0;
                        int off = 0;
                        int dst = *(int *)(*(int *)(state + 8) + chan) + baseOff;
                        int srcA = *(int *)(*frame + chan) + half1Off + half0 * -4;
                        unsigned int rem = cur0;
                        if (0 < (int)cur0) {
                            do {
                                int wi = cur0 - k;
                                k = k + 1;
                                *(float *)(dst + off) =
                                    *(float *)(dst + off) * *(float *)(w + wi * 4 - 4) +
                                    *(float *)(srcA + off) * *(float *)(w + off);
                                off = off + 4;
                                rem = rem - 1;
                            } while (rem != 0);
                        }
                        int tail = total - k;
                        int toff = k << 2;
                        if (k < total) {
                            do {
                                *(int *)(dst + toff) = *(int *)(srcA + toff);
                                toff = toff + 4;
                                tail = tail - 1;
                            } while (tail != 0);
                        }
                    }
                } else if (*(int *)(state + 0x28) == 0) {
                    unsigned char *w = FUN_801a10b0(*(int *)(sub + 4) - shift);
                    int k = 0;
                    int off = 0;
                    int srcA = *(int *)(*frame + chan);
                    int dst = *(int *)(*(int *)(state + 8) + chan) + baseOff + half1Off + half0 * -4;
                    unsigned int rem = cur0;
                    if (0 < (int)cur0) {
                        do {
                            int wi = cur0 - k;
                            k = k + 1;
                            *(float *)(dst + off) =
                                *(float *)(dst + off) * *(float *)(w + wi * 4 - 4) +
                                *(float *)(srcA + off) * *(float *)(w + off);
                            off = off + 4;
                            rem = rem - 1;
                        } while (rem != 0);
                    }
                } else {
                    unsigned char *w = FUN_801a10b0(*(int *)(sub + 8) - shift);
                    int k = 0;
                    int off = 0;
                    int srcA = *(int *)(*frame + chan);
                    int dst = *(int *)(*(int *)(state + 8) + chan) + baseOff;
                    unsigned int rem = cur1;
                    if (0 < (int)cur1) {
                        do {
                            int wi = cur1 - k;
                            k = k + 1;
                            *(float *)(dst + off) =
                                *(float *)(dst + off) * *(float *)(w + wi * 4 - 4) +
                                *(float *)(srcA + off) * *(float *)(w + off);
                            off = off + 4;
                            rem = rem - 1;
                        } while (rem != 0);
                    }
                }
                {
                    int off = 0;
                    int dst = *(int *)(*(int *)(state + 8) + chan);
                    int srcA = *(int *)(*frame + chan);
                    int rem = prevCount;
                    if (0 < prevCount) {
                        do {
                            *(int *)(dst + carryCount * 4 + off) =
                                *(int *)(srcA + prevCount * 4 + off);
                            off = off + 4;
                            rem = rem - 1;
                        } while (rem != 0);
                    }
                }
                chan = chan + 4;
            }

            if (*(int *)(state + 0x30) == 0) {
                *(unsigned int *)(state + 0x30) = cur1;
            } else {
                *(int *)(state + 0x30) = 0;
            }
            if (*(int *)(state + 0x18) == -1) {
                *(unsigned int *)(state + 0x18) = carryCount;
                *(unsigned int *)(state + 0x14) = carryCount;
            } else {
                *(unsigned int *)(state + 0x18) = outCount;
                unsigned int a = (unsigned int)cb[*(int *)(state + 0x24)];
                unsigned int b = (unsigned int)cb[*(int *)(state + 0x28)];
                *(unsigned int *)(state + 0x14) =
                    outCount + ((int)(((int)a >> 2) + (((int)a < 0 && (a & 3) != 0) ? 1 : 0) +
                                       ((int)b >> 2) + (((int)b < 0 && (b & 3) != 0) ? 1 : 0)) >>
                                (shift & 0x3f));
            }
        }

        // presentation-time accumulator for the sub-context
        unsigned int sHi = *(unsigned int *)(sub + 0xbc);
        if (sHi == 0xffffffff && *(int *)(sub + 0xb8) == -1) {
            *(int *)(sub + 0xbc) = 0;
            *(int *)(sub + 0xb8) = 0;
        } else {
            unsigned int a = (unsigned int)cb[*(int *)(state + 0x24)];
            unsigned int b = (unsigned int)cb[*(int *)(state + 0x28)];
            unsigned int d = ((int)a >> 2) + (((int)a < 0 && (a & 3) != 0) ? 1 : 0) +
                             ((int)b >> 2) + (((int)b < 0 && (b & 3) != 0) ? 1 : 0);
            *(unsigned int *)(sub + 0xbc) = sHi + d;
            *(int *)(sub + 0xb8) = *(int *)(sub + 0xb8) + ((int)d >> 0x1f) +
                                   (CARRY4(sHi, d) ? 1 : 0);
        }

        // keyframe-time accumulator for the decoder state
        unsigned int kHi = *(unsigned int *)(state + 0x3c);
        if (kHi == 0xffffffff && *(int *)(state + 0x38) == -1) {
            int kf = frame[0xc];
            if (frame[0xd] != -1 || kf != -1) {
                *(int *)(state + 0x3c) = frame[0xd];
                *(int *)(state + 0x38) = kf;
                unsigned int hiRef = *(unsigned int *)(sub + 0xb8);
                unsigned int loState = *(unsigned int *)(state + 0x38);
                unsigned int loRef = *(unsigned int *)(sub + 0xbc);
                unsigned int hiState = *(unsigned int *)(state + 0x3c);
                if ((loState ^ 0x80000000) < (unsigned int)(hiState < loRef) + (hiRef ^ 0x80000000)) {
                    if (frame[0xb] == 0) {
                        unsigned long long q =
                            FUN_80382ba8(hiRef - ((loRef < hiState) + loState), loRef - hiState, shift);
                        *(int *)(state + 0x18) = (int)q + *(int *)(state + 0x18);
                        if (*(int *)(state + 0x14) < *(int *)(state + 0x18)) {
                            *(int *)(state + 0x18) = *(int *)(state + 0x14);
                        }
                    } else {
                        unsigned long long q =
                            FUN_80382ba8(hiRef - ((loRef < hiState) + loState), loRef - hiState, shift);
                        *(int *)(state + 0x14) = *(int *)(state + 0x14) - (int)q;
                    }
                }
            }
        } else {
            unsigned int a = (unsigned int)cb[*(int *)(state + 0x24)];
            unsigned int b = (unsigned int)cb[*(int *)(state + 0x28)];
            unsigned int d = ((int)a >> 2) + (((int)a < 0 && (a & 3) != 0) ? 1 : 0) +
                             ((int)b >> 2) + (((int)b < 0 && (b & 3) != 0) ? 1 : 0);
            *(unsigned int *)(state + 0x3c) = kHi + d;
            *(int *)(state + 0x38) = *(int *)(state + 0x38) + ((int)d >> 0x1f) +
                                     (CARRY4(kHi, d) ? 1 : 0);
            unsigned int tLo = (unsigned int)frame[0xc];
            unsigned int tHi = (unsigned int)frame[0xd];
            if (tHi != 0xffffffff || tLo != 0xffffffff) {
                unsigned int curHi = *(unsigned int *)(state + 0x3c);
                if (curHi != tHi || *(unsigned int *)(state + 0x38) != tLo) {
                    if (((tLo ^ 0x80000000) <
                         (unsigned int)(tHi < curHi) + (*(unsigned int *)(state + 0x38) ^ 0x80000000)) &&
                        (curHi - tHi != 0) && (frame[0xb] != 0)) {
                        *(int *)(state + 0x14) =
                            *(int *)(state + 0x14) - ((int)(curHi - tHi) >> (shift & 0x3f));
                    }
                    int nkf = frame[0xc];
                    *(int *)(state + 0x3c) = frame[0xd];
                    *(int *)(state + 0x38) = nkf;
                }
            }
        }
        if (frame[0xb] != 0) {
            *(int *)(state + 0x20) = 1;
        }
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// anim_decode_get_output (FUN_80194178)
// If the read position (+0x18) is within [0, window end +0x14), point each
// channel's output pointer (+0xc array) at the decoded samples for the current
// position (base +8 plus pos*4), publish the pointer array through *out, and
// return the number of frames remaining in the window. Returns 0 otherwise.
// ---------------------------------------------------------------------------
int anim_decode_get_output(int dec, unsigned int *out) {
    int ctx = *(int *)(dec + 4);
    if ((-1 < *(int *)(dec + 0x18)) && (*(int *)(dec + 0x18) < *(int *)(dec + 0x14))) {
        if (out != (unsigned int *)0) {
            int off = 0;
            for (int i = 0; i < *(int *)(ctx + 4); i = i + 1) {
                *(int *)(*(int *)(dec + 0xc) + off) =
                    *(int *)(*(int *)(dec + 8) + off) + *(int *)(dec + 0x18) * 4;
                off = off + 4;
            }
            *out = *(unsigned int *)(dec + 0xc);
        }
        return *(int *)(dec + 0x14) - *(int *)(dec + 0x18);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// anim_decode_skip (FUN_801941f8)
// Advance the read position (+0x18) by `n` frames. Returns -131 (0xffffff7d)
// when the advance would run past the window end (+0x14); 0 on success.
// ---------------------------------------------------------------------------
unsigned int anim_decode_skip(int dec, int n) {
    if ((n != 0) && (*(int *)(dec + 0x14) < *(int *)(dec + 0x18) + n)) {
        return 0xffffff7d;
    }
    *(int *)(dec + 0x18) = *(int *)(dec + 0x18) + n;
    return 0;
}

// ---------------------------------------------------------------------------
// anim_codebook_parse (FUN_80194230)
// Parse a "VCB" (0x564342) codebook header from the bit stream:
//   word[0] : symbol count field (16 bits)
//   word[1] : entry count (24 bits); 0xffffffff = empty (skip)
//   word[2] : per-entry bit widths - either RLE runs (flag bit set) or a flat
//             list, optionally sparse (0 = absent)
//   word[3] : payload type (1..2 valid)
//   word[4],word[5] : two 32-bit parameters
//   word[6] : field width (+1)
//   word[7] : 1-bit flag
//   word[8] : payload table (element count from type), terminated by -1
// On any overrun / malformed run the partial object is freed (FUN_8019f348).
// The context/output pointer pair arrives via the register-save helper.
// ---------------------------------------------------------------------------
// Returns 0 on a clean parse (finish), 1 when the partial object was freed on a
// malformed/overrun stream (done); callers use nonzero as the failure signal.
int anim_codebook_parse(void) {
    unsigned long long pair = FUN_803826e8();
    int *br = (int *)((unsigned long long)pair >> 0x20);
    unsigned int *cb = (unsigned int *)pair;
    int failed = 0;

    memset_wrapper((int)cb, 0, 0x34);
    cb[0xc] = 1;

    unsigned int magic = bitreader_read_bits(br, 0x18);
    if (magic == 0x564342) {
        cb[0] = bitreader_read_bits(br, 0x10);
        cb[1] = bitreader_read_bits(br, 0x18);
        if (cb[1] != 0xffffffff) {
            unsigned int rle = bitreader_read_bits(br, 1);
            if (rle == 1) {
                unsigned int sym = bitreader_read_bits(br, 5);
                cb[2] = (unsigned int)FUN_801a13e0(cb[1] << 2);
                int idx = 0;
                int off = 0;
                while (true) {
                    sym = sym + 1;
                    if ((int)cb[1] <= idx) break;
                    int width = FUN_8019ed34(cb[1] - idx);
                    unsigned int run = bitreader_read_bits(br, width);
                    if (run == 0xffffffff) { goto done; }
                    int j = 0;
                    for (; (j < (int)run && (idx < (int)cb[1])); idx = idx + 1) {
                        j = j + 1;
                        *(unsigned int *)(cb[2] + off) = sym;
                        off = off + 4;
                    }
                }
            } else {
                cb[2] = (unsigned int)FUN_801a13e0(cb[1] << 2);
                unsigned int sparse = bitreader_read_bits(br, 1);
                if (sparse == 0) {
                    int off = 0;
                    for (int i = 0; i < (int)cb[1]; i = i + 1) {
                        unsigned int v = bitreader_read_bits(br, 5);
                        if (v == 0xffffffff) { goto done; }
                        *(unsigned int *)(cb[2] + off) = v + 1;
                        off = off + 4;
                    }
                } else {
                    int off = 0;
                    for (int i = 0; i < (int)cb[1]; i = i + 1) {
                        unsigned int present = bitreader_read_bits(br, 1);
                        if (present == 0) {
                            *(unsigned int *)(cb[2] + off) = 0;
                        } else {
                            unsigned int v = bitreader_read_bits(br, 5);
                            if (v == 0xffffffff) { goto done; }
                            *(unsigned int *)(cb[2] + off) = v + 1;
                        }
                        off = off + 4;
                    }
                }
            }

            cb[3] = bitreader_read_bits(br, 4);
            if (cb[3] == 0) goto finish;
            if ((-1 < (int)cb[3]) && ((int)cb[3] < 3)) {
                cb[4] = bitreader_read_bits(br, 0x20);
                cb[5] = bitreader_read_bits(br, 0x20);
                cb[6] = bitreader_read_bits(br, 4) + 1;
                cb[7] = bitreader_read_bits(br, 1);
                int elems = 0;
                unsigned int type = cb[3];
                if (type == 2) {
                    elems = cb[1] * cb[0];
                } else if (((int)type < 2) && (0 < (int)type)) {
                    elems = FUN_8019efac(cb);
                }
                cb[8] = (unsigned int)FUN_801a13e0(elems << 2);
                int off = 0;
                for (int i = 0; i < elems; i = i + 1) {
                    *(unsigned int *)(cb[8] + off) = bitreader_read_bits(br, cb[6]);
                    off = off + 4;
                }
                if ((elems == 0) || (*(int *)(cb[8] + elems * 4 - 4) != -1)) goto finish;
            }
        }
    }

done:
    // Reached on overrun/malformed run, OR by fall-through from the magic /
    // entry-count guards without a successful parse: release the partial object.
    FUN_8019f348((int)cb);
    failed = 1;
finish:
    FUN_80382734();
    return failed;
}


// ===========================================================================
// batch_510 - channel bit-write, Huffman symbol decode, vtable-dispatched
// per-type channel decode handlers, and sub-context / buffer teardown.
// ===========================================================================

// ---------------------------------------------------------------------------
// anim_channel_write_bits (FUN_8019455c)
// Emit one channel's value into the bit writer `bw`: the value is the per-symbol
// width word at (state+0x14)[idx], written with a bit count taken from the
// channel's count table ((*(state+0xc))+8)[idx]. Returns that bit count.
// ---------------------------------------------------------------------------
int anim_channel_write_bits(int state, int idx, int bw) {
    int off = idx * 4;
    int count = *(int *)(*(int *)(*(int *)(state + 0xc) + 8) + off);
    bitwriter_write_bits(bw, *(unsigned int *)(*(int *)(state + 0x14) + off), count);
    return count;
}

// ---------------------------------------------------------------------------
// bit_reverse32 (FUN_801945b8)
// Reverse the 32 bits of `v` (swap halves, then bytes, nibbles, pairs, bits).
// Used to turn an LSB-first peeked bit field into the MSB-first code value the
// Huffman code table is sorted by.
// ---------------------------------------------------------------------------
unsigned int bit_reverse32(unsigned int v) {
    v = v >> 0x10 | v << 0x10;
    v = (v >> 8 & 0x00ff00ff) | ((v & 0x00ff00ff) << 8);
    v = (v >> 4 & 0x0f0f0f0f) | ((v & 0x0f0f0f0f) << 4);
    v = (v >> 2 & 0x33333333) | ((v & 0x33333333) << 2);
    return (v >> 1 & 0x55555555) | ((v & 0x55555555) << 1);
}

// ---------------------------------------------------------------------------
// anim_huffman_decode_symbol (FUN_80194658)
// Decode the next Huffman symbol from the bit reader. Two-level table: a direct
// lookup (ctx+0x20) keyed by a peek of ctx+0x24 bits handles short codes (top
// bit clear -> immediate advance by the code length at ctx+0x1c); otherwise it
// falls back to a length-bounded binary search over the sorted code table
// (ctx+0x14) using the bit-reversed peek, then advances by the matched length.
// Returns the symbol index, or is a no-op (returns via ctx) when the peek
// overruns. The bit-reader / table pair arrives via the register-save helper.
// ---------------------------------------------------------------------------
int anim_huffman_decode_symbol(void) {
    unsigned long long pair = FUN_803826e8();
    int ctx = (int)((unsigned long long)pair >> 0x20);
    int *br = (int *)pair;
    int result = -1;

    int lenBits = *(int *)(ctx + 0x28);
    unsigned int idx = bitreader_peek_bits(br, *(int *)(ctx + 0x24));
    int hi;
    if ((int)idx < 0) {
        idx = 0;
        hi = *(int *)(ctx + 8);
    } else {
        unsigned int entry = *(unsigned int *)(*(int *)(ctx + 0x20) + idx * 4);
        if ((entry & 0x80000000) == 0) {
            bitreader_advance(br, (int)*(char *)(*(int *)(ctx + 0x1c) + entry - 1));
            return (int)entry - 1;
        }
        idx = entry >> 0xf & 0x7fff;
        hi = *(int *)(ctx + 8) - (entry & 0x7fff);
    }

    unsigned int code = bitreader_peek_bits(br, lenBits);
    while (((int)code < 0) && (1 < lenBits)) {
        lenBits = lenBits - 1;
        code = bitreader_peek_bits(br, lenBits);
    }
    if (-1 < (int)code) {
        code = bit_reverse32(code);
        while (1 < (int)(hi - idx)) {
            unsigned int halfSpan = (unsigned int)((int)(hi - idx) >> 1);
            unsigned int lt = (unsigned int)(code < *(unsigned int *)(*(int *)(ctx + 0x14) + (idx + halfSpan) * 4));
            idx = idx + (halfSpan & (lt - 1));
        }
        int codeLen = (int)*(char *)(*(int *)(ctx + 0x1c) + idx);
        if (lenBits < codeLen) {
            bitreader_advance(br, lenBits);
        } else {
            bitreader_advance(br, codeLen);
        }
        result = (int)idx;
    }
    return result;
}

// ---------------------------------------------------------------------------
// anim_huffman_decode_indexed (FUN_801947b8)
// Decode a symbol, then map it through the state's index table (state+0x18).
// Returns the mapped value, or the raw (negative) symbol on decode failure.
// ---------------------------------------------------------------------------
int anim_huffman_decode_indexed(int state) {
    int sym = anim_huffman_decode_symbol();
    if (-1 < sym) {
        sym = *(int *)(*(int *)(state + 0x18) + sym * 4);
    }
    return sym;
}

// ---------------------------------------------------------------------------
// anim_decode_channels_accum (FUN_801947f8)
// Vtable-dispatched channel decode handler (accumulate). Decodes `count` frame
// symbols (each selecting a source row `state[4] + state[0]*sym*dim`), then
// float-accumulates the per-dimension source rows into the destination row set.
// `dim` = state[0] (channel dimension), inner block = count/dim. dst/state pair
// arrives via the register-save helper; `count` is the incoming r6 argument.
// ---------------------------------------------------------------------------
void anim_decode_channels_accum(int count) {
    unsigned long long pair = FUN_803826d8();
    int *state = (int *)((unsigned long long)pair >> 0x20);
    int dst = (int)pair;
    int block = count / *state;

    // Decode one source-row pointer per inner-block element. On any failed
    // symbol the accumulation is skipped (matches the original's early exit).
    int rowPtrs[16];
    for (int i = 0; i < block; i = i + 1) {
        int sym = anim_huffman_decode_symbol();
        if (sym == -1) {
            goto done;
        }
        rowPtrs[i] = state[4] + *state * sym * 4;
    }

    {
        int dimOff = 0;   // per-dimension byte offset into each source row (d*4)
        int rowBase = 0;  // running destination element base (advances by block)
        for (int d = 0; d < *state; d = d + 1) {
            for (int j = 0; j < block; j = j + 1) {
                int dstIdx = (rowBase + j) * 4;
                *(float *)(dst + dstIdx) =
                    *(float *)(dst + dstIdx) + *(float *)(dimOff + rowPtrs[j]);
            }
            rowBase = rowBase + block;
            dimOff = dimOff + 4;
        }
    }
done:
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// anim_decode_channels_unrolled (FUN_80194930)
// Vtable-dispatched channel decode handler for small channel dimensions
// (state[0] < 9): an 8-way Duff's-device float accumulator that adds each
// decoded source row's components into the destination. For dimension >= 9 it
// falls back to the generic per-component loop. `count` is the incoming r6 arg.
// ---------------------------------------------------------------------------
void anim_decode_channels_unrolled(int count) {
    unsigned long long pair = FUN_803826e4();
    int *state = (int *)((unsigned long long)pair >> 0x20);
    int dst = (int)pair;

    if (*state < 9) {
        int written = 0;
        int dstOff = 0;
        int sym;
        while ((written < count) && ((sym = anim_huffman_decode_symbol()) != -1)) {
            int c = 0;
            float *row = (float *)(state[4] + sym * *state * 4);
            switch (*state) {
            case 8:
                c = 1; written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[0];
                dstOff = dstOff + 4;
                // fallthrough
            case 7:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c]; c = c + 1;
                dstOff = dstOff + 4;
                // fallthrough
            case 6:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c]; c = c + 1;
                dstOff = dstOff + 4;
                // fallthrough
            case 5:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c]; c = c + 1;
                dstOff = dstOff + 4;
                // fallthrough
            case 4:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c]; c = c + 1;
                dstOff = dstOff + 4;
                // fallthrough
            case 3:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c]; c = c + 1;
                dstOff = dstOff + 4;
                // fallthrough
            case 2:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c]; c = c + 1;
                dstOff = dstOff + 4;
                // fallthrough
            case 1:
                written = written + 1;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + row[c];
                dstOff = dstOff + 4;
                break;
            default:
                break;
            }
        }
    } else {
        int written = 0;
        int dstOff = 0;
        int sym;
        while ((written < count) && ((sym = anim_huffman_decode_symbol()) != -1)) {
            int dim = *state;
            int rowBase = state[4];
            int srcOff = 0;
            for (int j = 0; j < *state; j = j + 1) {
                float *row = (float *)(rowBase + sym * dim * 4 + srcOff);
                written = written + 1;
                srcOff = srcOff + 4;
                *(float *)(dst + dstOff) = *(float *)(dst + dstOff) + *row;
                dstOff = dstOff + 4;
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// anim_decode_channels_copy (FUN_80194b64)
// Vtable-dispatched channel decode handler that copies (rather than accumulates)
// each decoded source row's components straight into the destination buffer.
// `count` is the incoming r6 argument. Returns the terminal symbol left in r3
// (-1 at stream end), which callers such as anim_ramp_decode test against -1.
// ---------------------------------------------------------------------------
int anim_decode_channels_copy(int count) {
    unsigned long long pair = FUN_803826e4();
    int *state = (int *)((unsigned long long)pair >> 0x20);
    int dst = (int)pair;

    int written = 0;
    int dstOff = 0;
    int sym = 0;
    while ((written < count) && ((sym = anim_huffman_decode_symbol()) != -1)) {
        int dim = *state;
        int rowBase = state[4];
        int srcOff = 0;
        for (int j = 0; j < *state; j = j + 1) {
            unsigned int *src = (unsigned int *)(rowBase + sym * dim * 4 + srcOff);
            srcOff = srcOff + 4;
            written = written + 1;
            *(unsigned int *)(dst + dstOff) = *src;
            dstOff = dstOff + 4;
        }
    }
    FUN_80382730();
    return sym;
}

// ---------------------------------------------------------------------------
// anim_decode_channels_strided (FUN_80194c18)
// Vtable-dispatched channel decode handler that accumulates decoded rows into a
// set of per-channel destination buffers (pointer array carried by the
// register-save helper). `base`/`div`/`span` define the strided frame window:
// decode continues while the running frame index (base/div .. (base+span)/div)
// is in range, wrapping each `div` components to the next frame column.
// ---------------------------------------------------------------------------
void anim_decode_channels_strided(unsigned int arg0, unsigned int arg1,
                                  int base, int div, unsigned int arg4, int span) {
    unsigned long long pair = __save_gpr_25();
    int *state = (int *)((unsigned long long)pair >> 0x20);
    int dstPtrs = (int)pair;

    int frame = base / div;
    int col = 0;
    int sym;
    while ((frame < (base + span) / div) && ((sym = anim_huffman_decode_symbol()) != -1)) {
        int dim = *state;
        int frameOff = frame << 2;
        int rowBase = state[4];
        int srcOff = 0;
        for (int j = 0; j < *state; j = j + 1) {
            int chanBuf = *(int *)(dstPtrs + col * 4);
            col = col + 1;
            *(float *)(chanBuf + frameOff) =
                *(float *)(chanBuf + frameOff) + *(float *)(rowBase + sym * dim * 4 + srcOff);
            if (col == div) {
                col = 0;
                frame = frame + 1;
                frameOff = frameOff + 4;
            }
            srcOff = srcOff + 4;
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_subctx_free_buffers (FUN_80194cf8)
// Free the working buffers held by the decode sub-context: the range list at
// base+0x10 (FUN_8019a120), the 7 per-channel buffers at base+0x30 (0x10
// stride), and the three extra buffers at +0x24 / +0x98 / +0xa0, then zero the
// 0xB4-byte block. The sub-context pointer arrives via the spill helper.
// ---------------------------------------------------------------------------
void anim_subctx_free_buffers(void) {
    int base = (int)FUN_803826f0();
    anim_fft_free_pair(base + 0x10);
    int off = 0;
    for (int i = 0; i < 7; i = i + 1) {
        FUN_801a14dc(*(int *)(base + off + 0x30));
        off = off + 0x10;
    }
    FUN_801a14dc(*(int *)(base + 0x24));
    FUN_801a14dc(*(int *)(base + 0x98));
    FUN_801a14dc(*(int *)(base + 0xa0));
    memset_wrapper(base, 0, 0xb4);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_buffer_free (FUN_80194d7c)
// Zero a 0x60-byte object and release it. No-op on a null pointer.
// ---------------------------------------------------------------------------
void anim_buffer_free(int obj) {
    if (obj != 0) {
        memset_wrapper(obj, 0, 0x60);
        FUN_801a14dc(obj);
    }
}


// ===========================================================================
// batch_511 - curve-header parse, double-precision falloff/attenuation table
// builder, ramp decode, channel eval, lookup-node create, sized free helpers,
// and two bit-width leaves.
// ===========================================================================

// ---------------------------------------------------------------------------
// anim_channel_desc_free (FUN_80194dbc)
// Free a 0x20-byte channel descriptor: release its two sub-buffers (word[8][0]
// and word[8][1]) and the word[8] block itself, then zero and free the object.
// No-op on null.
// ---------------------------------------------------------------------------
void anim_channel_desc_free(int desc) {
    if (desc != 0) {
        if (*(int **)(desc + 8) != (int *)0) {
            int b0 = **(int **)(desc + 8);
            if (b0 != 0) FUN_801a14dc(b0);
            int b1 = *(int *)(*(int *)(desc + 8) + 4);
            if (b1 != 0) FUN_801a14dc(b1);
            FUN_801a14dc(*(int *)(desc + 8));
        }
        memset_wrapper(desc, 0, 0x20);
        FUN_801a14dc(desc);
    }
}

// ---------------------------------------------------------------------------
// anim_curve_header_parse (FUN_80194e38)
// Allocate a 0x60-byte curve descriptor and read its header from the bit stream:
//   [0] type (8b), [1] countA (16b), [2] countB (16b), [3] param (6b),
//   [4] flags (8b), [5] entryCount (4b)+1.
// Then read entryCount entry indices (8b each, stored at obj+0x18) validating
// each against the codebook bound (*(ctx->[0x1c] + 0x18)). Frees the partial
// object (anim_buffer_free) on any zero/invalid field. Reader/context pair
// arrives via the register-save helper FUN_803826e8.
// ---------------------------------------------------------------------------
void anim_curve_header_parse(void) {
    unsigned long long pair = FUN_803826e8();
    int *br = (int *)pair;
    int codebook = *(int *)((int)((unsigned long long)pair >> 0x20) + 0x1c);

    unsigned int *obj = (unsigned int *)FUN_801a13e0(0x60);
    obj[0] = bitreader_read_bits(br, 8);
    obj[1] = bitreader_read_bits(br, 0x10);
    obj[2] = bitreader_read_bits(br, 0x10);
    obj[3] = bitreader_read_bits(br, 6);
    obj[4] = bitreader_read_bits(br, 8);
    obj[5] = bitreader_read_bits(br, 4) + 1;

    if (((int)obj[0] < 1) || ((int)obj[1] < 1) || ((int)obj[2] < 1) || ((int)obj[5] < 1)) {
        anim_buffer_free((int)obj);
    } else {
        int off = 0;
        for (int i = 0; i < (int)obj[5]; i = i + 1) {
            *(unsigned int *)((int)obj + off + 0x18) = bitreader_read_bits(br, 8);
            int idx = *(int *)((int)obj + off + 0x18);
            if ((idx < 0) || (*(int *)(codebook + 0x18) <= idx)) {
                anim_buffer_free((int)obj);
                break;
            }
            off = off + 4;
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_build_falloff_table (FUN_80194f70)
// Build (once, lazily) the per-frame falloff/attenuation lookup table for the
// descriptor's channel slot desc[2][ctx->[0x1c]]. Table length is ceil(count/2)+1
// (terminated by -1). Each entry is a nonlinear map of the frame index through a
// scale derived from frame count (desc[0]) and the two curve params, evaluated
// with the exp-like FUN_8038fecc and the pow/round-like FUN_80390324, then
// clamped to [0, desc[0]-1]. The int->double conversions use the classic
// 0x43300000 magic-constant trick (CONCAT44 + DOUBLE_80616918 bias). state/frame
// pair arrives via the register-save helper FUN_803826dc.
// ---------------------------------------------------------------------------
void anim_build_falloff_table(unsigned int arg0, unsigned int arg1, unsigned int *desc) {
    (void)arg0; (void)arg1;
    unsigned long long pair = FUN_803826dc();
    int state = (int)((unsigned long long)pair >> 0x20);
    int frame = (int)pair;
    int slot = *(int *)(state + 0x1c);
    int slotOff = slot * 4;

    if (*(int *)(desc[2] + slotOff) == 0) {
        unsigned int refCount = *(unsigned int *)(*(int *)(*(int *)(*(int *)(state + 0x40) + 4) + 0x1c) + slotOff);
        int half = ((int)refCount >> 1) + (((int)refCount < 0 && (refCount & 1) != 0) ? 1 : 0);

        // scalar helpers reused across the loop
        double bias  = DOUBLE_80616918;
        double c928  = DOUBLE_80616928;
        double c938  = DOUBLE_80616938;
        double f920  = (double)FLOAT_80616920;
        double f924  = (double)FLOAT_80616924;
        double f930  = (double)FLOAT_80616930;
        double f940  = (double)FLOAT_80616940;

        float span = (float)((double)CONCAT44(0x43300000, *(unsigned int *)(frame + 4) ^ 0x80000000) - bias) * FLOAT_80616924;
        double e1 = c938 * FUN_8038fecc((double)(FLOAT_80616940 * span * span));
        double e2 = FUN_8038fecc((double)(FLOAT_80616930 * span));
        double denom = (double)(FLOAT_80616920 * span) + c928 * e2 + e1;
        double norm = (double)(float)(((double)CONCAT44(0x43300000, desc[0] ^ 0x80000000) - bias) / denom);

        unsigned int *outTable = (unsigned int *)FUN_801a13e0((half + 1) * 4);
        *(unsigned int *)(desc[2] + slotOff) = (unsigned int)outTable;

        int outOff = 0;
        unsigned int hiComp = (unsigned int)half ^ 0x80000000;
        unsigned int i;
        for (i = 0; (int)i < half; i = i + 1) {
            float fi = (float)((double)CONCAT44(0x43300000, i ^ 0x80000000) - bias) *
                       ((float)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(frame + 4) ^ 0x80000000) - bias) * f924) /
                        (float)((double)CONCAT44(0x43300000, hiComp) - bias));
            double t1 = c938 * FUN_8038fecc((double)(float)(f940 * (double)(fi * fi)));
            double t2 = FUN_8038fecc((double)(float)(f930 * (double)fi));
            double mapped = FUN_80390324(norm * ((double)(float)(f920 * (double)fi) + c928 * t2 + t1));
            int v = (int)mapped;
            if ((int)desc[0] <= v) {
                v = (int)desc[0] - 1;
            }
            *(int *)(*(int *)(slotOff + desc[2]) + outOff) = v;
            outOff = outOff + 4;
        }
        *(unsigned int *)(*(int *)(desc[2] + slotOff) + i * 4) = 0xffffffff;
        desc[slot + 3] = (unsigned int)half;
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// anim_lookup_node_create (FUN_801952d0)
// Allocate a 0x20-byte lookup node seeded from `src`: node[1]=src[0],
// node[0]=src[2], node[5]=src, and a 2-word child array at node[2]. Returns the
// node.
// ---------------------------------------------------------------------------
unsigned int *anim_lookup_node_create(unsigned int arg0, unsigned int *src) {
    (void)arg0;
    unsigned int *node = (unsigned int *)FUN_801a1498(1, 0x20);
    node[1] = src[0];
    node[0] = src[2];
    node[5] = (unsigned int)src;
    node[2] = (unsigned int)FUN_801a1498(2, 4);
    return node;
}

// ---------------------------------------------------------------------------
// anim_ramp_decode (FUN_80195338)
// Decode a ramp/keyframe run for one channel. Reads a magnitude (width from
// desc+0xc) and an entry index (width from FUN_8019ed34), validates the index,
// dereferences the codebook channel-descriptor stride (piVar8), allocates an
// arena run (msg_arena_alloc), decodes the channel samples via
// anim_decode_channels_copy, then integrates them into a monotone ramp adding a
// fixed step (FLOAT_80616944) per element, storing the final magnitude at the
// tail. Reader/context pair via the register-save helper FUN_803826e8.
// ---------------------------------------------------------------------------
void anim_ramp_decode(void) {
    unsigned long long pair = FUN_803826e8();
    int ctx = (int)((unsigned long long)pair >> 0x20);
    int stateBase = (int)pair;
    int desc = *(int *)(stateBase + 0x14);

    unsigned int mag = bitreader_read_bits((int *)(ctx + 4), *(int *)(desc + 0xc));
    if (0 < (int)mag) {
        double magNorm = (double)((float)((double)CONCAT44(0x43300000, *(unsigned int *)(desc + 0x10) ^ 0x80000000) - DOUBLE_80616918) *
                                  ((float)((double)CONCAT44(0x43300000, mag ^ 0x80000000) - DOUBLE_80616918) /
                                   (float)((double)CONCAT44(0x43300000, (unsigned int)((1 << *(int *)(desc + 0xc)) - 1) ^ 0x80000000) - DOUBLE_80616918)));

        int idxWidth = FUN_8019ed34(*(int *)(desc + 0x14));
        unsigned int idx = bitreader_read_bits((int *)(ctx + 4), idxWidth);
        if ((idx != 0xffffffff) && ((int)idx < *(int *)(desc + 0x14))) {
            float step = FLOAT_80616944;
            int *chanDesc = (int *)(*(int *)(*(int *)(*(int *)(*(int *)(ctx + 0x40) + 4) + 0x1c) + 0xb20) +
                                    *(int *)(desc + idx * 4 + 0x18) * 0x2c);
            int stride = *chanDesc;
            int run = msg_arena_alloc(ctx, (*(int *)(stateBase + 4) + stride + 1) * 4);

            bool decodeOk = true;
            for (int f = 0; f < *(int *)(stateBase + 4); f = f + stride) {
                if (anim_decode_channels_copy(0) == -1) {
                    decodeOk = false;
                    break;
                }
            }

            if (decodeOk) {
                int i = 0;
                int off = 0;
                int at = 0;
                double acc = (double)step;
                while (i < *(int *)(stateBase + 4)) {
                    for (int c = 0; c < stride; c = c + 1) {
                        i = i + 1;
                        off = off + 4;
                        *(float *)(run + at) = (float)((double)*(float *)(run + at) + acc);
                        at = at + 4;
                    }
                    acc = (double)*(float *)(run + off - 4);
                }
                *(float *)(run + *(int *)(stateBase + 4) * 4) = (float)magNorm;
            }
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_channel_eval (FUN_80195518)
// Evaluate one channel into `out`: first make sure the falloff table for the
// channel slot exists (anim_build_falloff_table). With no input weights, clear
// the output run; otherwise call the weighted sample-eval helper (FUN_80197f58)
// with the channel's falloff table, run length, and weight vector. state/desc
// pair via the register-save helper FUN_803826e8.
// ---------------------------------------------------------------------------
void anim_channel_eval(unsigned int arg0, unsigned int arg1, float *weights, int out) {
    (void)arg0; (void)arg1;
    unsigned long long pair = FUN_803826e8();
    int state = (int)((unsigned long long)pair >> 0x20);
    unsigned int *desc = (unsigned int *)pair;
    int slot = *(int *)(state + 0x1c);
    unsigned int owner = desc[5];

    anim_build_falloff_table(state, owner, desc);

    if (weights == (float *)0) {
        memset_wrapper(out, 0, desc[slot + 3] << 2);
    } else {
        float scale = (float)((double)CONCAT44(0x43300000, *(unsigned int *)(owner + 0x10) ^ 0x80000000) - DOUBLE_80616918);
        FUN_80197f58((double)weights[desc[1]], (double)scale, out,
                     *(int *)(desc[2] + slot * 4), (int)desc[slot + 3],
                     desc[0], weights, desc[1]);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_free_0x460 (FUN_801955ec)
// Zero a 0x460-byte object and release it. No-op on null. (vtable free slot)
// ---------------------------------------------------------------------------
void anim_free_0x460(int obj) {
    if (obj != 0) {
        memset_wrapper(obj, 0, 0x460);
        FUN_801a14dc(obj);
    }
}

// ---------------------------------------------------------------------------
// anim_free_0x520 (FUN_8019562c)
// Zero a 0x520-byte object and release it. No-op on null. (vtable free slot)
// ---------------------------------------------------------------------------
void anim_free_0x520(int obj) {
    if (obj != 0) {
        memset_wrapper(obj, 0, 0x520);
        FUN_801a14dc(obj);
    }
}

// ---------------------------------------------------------------------------
// bit_length (FUN_8019566c)
// Number of bits needed to represent `n` (bit length): count right-shifts until
// the value reaches zero. Returns 0 for n == 0.
// ---------------------------------------------------------------------------
int bit_length(unsigned int n) {
    int bits = 0;
    for (; n != 0; n >>= 1) {
        bits = bits + 1;
    }
    return bits;
}

// ---------------------------------------------------------------------------
// bits_for_count_2 (FUN_8019568c)
// Bits needed to index `n` values = ceil(log2(n)) = bit length of (n-1); 0 for
// n <= 1. A second, separately-emitted copy of the msgbuf bits_for_count leaf.
// ---------------------------------------------------------------------------
int bits_for_count_2(unsigned int n) {
    int bits = 0;
    if (n != 0) {
        n = n - 1;
    }
    for (; n != 0; n >>= 1) {
        bits = bits + 1;
    }
    return bits;
}

// ===========================================================================
// batch_512 (0x801956b8 - 0x8019637c): channel serialize/deserialize, keyframe
// sort/prepare, fixed-point interpolation & quantization, Bresenham-stepped
// ramp runs, and the double-precision least-squares linear-fit solver used to
// approximate a channel's quantized value trajectory.
// ===========================================================================
extern "C" {
// spill / register-save prologue helpers new to this batch
extern unsigned long long FUN_803826c8(void);   // -> (ctxHi:descLo) serialize pair
extern unsigned long long FUN_803826c0(void);   // -> (endHi:startLo) ramp-run pair
extern void               FUN_80382714(void);    // restore (paired with FUN_803826c8)
extern void               FUN_8038270c(void);    // restore (paired with FUN_803826c0)
extern int                __save_gpr_28(void);   // -> fit-solver state (r3:r4 pair)
extern void               __restore_gpr_28(void);

// generic quicksort: (base, count, elemSize, comparator)
extern void FUN_8038a0a4(int base, unsigned int count, int elemSize, void *cmp);
// round-to-nearest double->double (banker/half-up) used by the fit solver
extern double FUN_803907b8(double x);

// falloff/attenuation LUT (float[]) indexed by curve position
extern float DAT_8041f708;

// quantization + fit math constants
extern float  FLOAT_80616950;    // quantize bias (+0.5-style rounding)
extern float  FLOAT_80616954;    // quantize scale (value -> 0..0x3ff)
extern double DOUBLE_80616948;   // int->double magic (2^52+2^31) subtrahend
extern double DOUBLE_80616958;   // 1.0 (fit-solver determinant numerator)

// comparator target referenced by the keyframe sort (compares *int keys)
extern int LAB_80195ae8;
} // extern "C"

// Forward decls for the two quantize/interpolate leaves used across this batch.
unsigned int anim_quantize_value(float *pValue);

// ---------------------------------------------------------------------------
// anim_channel_write_all (FUN_801956b8)
// Serialize the whole channel table to the active bit-writer: symbol count (5b),
// per-channel symbol widths (4b each), then for every distinct symbol the run
// length (3b, biased -1), a 2-bit sub-table selector, an optional 8-bit base,
// and (1<<selector) 8-bit deltas (biased +1). Finally emits the payload:
// dimension-1 (2b), an index bit-width (4b), and every payload index packed at
// that width. Mirrors anim_channel_read_all exactly.
// ---------------------------------------------------------------------------
void anim_channel_write_all(void) {
    unsigned long long ctxPair = FUN_803826c8();
    int *pDesc = (int *)(unsigned int)(ctxPair >> 0x20); // descriptor block (r3)
    int   bw   = (int)ctxPair;                            // bit-writer (r4)

    unsigned int payloadCount = (unsigned int)pDesc[0xd2];
    int symCount = *pDesc;
    int maxSymbol = -1;
    int runningTotal = 0;

    bitwriter_write_bits(bw, symCount, 5);

    int off = 0;
    for (int i = 0; i < symCount; i = i + 1) {
        int width = *(int *)((int)pDesc + off + 4);
        bitwriter_write_bits(bw, (unsigned int)width, 4);
        if (maxSymbol < width) {
            maxSymbol = width;
        }
        off = off + 4;
    }

    int deltaBase = 0;
    int tblOff = 0;
    for (int s = 0; s < maxSymbol + 1; s = s + 1) {
        bitwriter_write_bits(bw, (unsigned int)(*(int *)((int)pDesc + tblOff + 0x80) - 1), 3);
        int selector = *(int *)((int)pDesc + tblOff + 0xc0);
        bitwriter_write_bits(bw, (unsigned int)selector, 2);
        if (selector != 0) {
            bitwriter_write_bits(bw, *(unsigned int *)((int)pDesc + tblOff + 0x100), 8);
        }
        int entryOff = 0;
        for (int e = 0; e < (1 << selector); e = e + 1) {
            bitwriter_write_bits(bw, (unsigned int)(*(int *)((int)pDesc + entryOff + 0x140 + deltaBase) + 1), 8);
            entryOff = entryOff + 4;
        }
        deltaBase = deltaBase + 0x20;
        tblOff = tblOff + 4;
    }

    bitwriter_write_bits(bw, (unsigned int)(pDesc[0xd0] - 1), 2);
    int idxWidth = bits_for_count_2(payloadCount);
    bitwriter_write_bits(bw, (unsigned int)idxWidth, 4);
    idxWidth = bits_for_count_2(payloadCount);

    int symOff = 0;
    int payOff = 0;
    int written = 0;
    for (int c = 0; c < symCount; c = c + 1) {
        runningTotal = runningTotal + pDesc[*(int *)((int)pDesc + symOff + 4) + 0x20];
        for (; written < runningTotal; written = written + 1) {
            bitwriter_write_bits(bw, *(unsigned int *)((int)pDesc + payOff + 0x34c), idxWidth);
            payOff = payOff + 4;
        }
        symOff = symOff + 4;
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// anim_channel_read_all (FUN_8019589c)
// Inverse of anim_channel_write_all. Allocates a 0x460 channel descriptor, reads
// the codebook + payload from the bit-reader, and validates every symbol index
// and payload value against the context's codebook bound (ctx+0x18). On any
// out-of-range value it frees the descriptor via anim_free_0x460 and aborts.
// Stores payload dimension at +0xd0, resets cursor +0xd1, sets slot count +0xd2.
// ---------------------------------------------------------------------------
void anim_channel_read_all(void) {
    unsigned long long ctxPair = FUN_803826c8();
    int *br  = (int *)(unsigned int)ctxPair;                       // bit-reader (r3)
    int  ctx = *(int *)((int)(unsigned int)(ctxPair >> 0x20) + 0x1c); // codebook ctx (r4+0x1c)

    unsigned int *pDesc = (unsigned int *)FUN_801a1498(1, 0x460);
    int runningTotal = 0;
    int maxSymbol = -1;

    unsigned int symCount = bitreader_read_bits(br, 5);
    *pDesc = symCount;

    int off = 0;
    for (int i = 0; i < (int)*pDesc; i = i + 1) {
        unsigned int width = bitreader_read_bits(br, 4);
        *(unsigned int *)((int)pDesc + off + 4) = width;
        if (maxSymbol < (int)width) {
            maxSymbol = (int)width;
        }
        off = off + 4;
    }

    int deltaBase = 0;
    int tblOff = 0;
    for (int s = 0; s < maxSymbol + 1; s = s + 1) {
        unsigned int runLen = bitreader_read_bits(br, 3);
        *(unsigned int *)((int)pDesc + tblOff + 0x80) = runLen + 1;
        unsigned int selector = bitreader_read_bits(br, 2);
        *(unsigned int *)((int)pDesc + tblOff + 0xc0) = selector;
        if ((int)selector < 0) {
            goto fail;
        }
        if (selector != 0) {
            unsigned int base = bitreader_read_bits(br, 8);
            *(unsigned int *)((int)pDesc + tblOff + 0x100) = base;
        }
        int baseVal = *(int *)((int)pDesc + tblOff + 0x100);
        if ((baseVal < 0) || (*(int *)(ctx + 0x18) <= baseVal)) {
            goto fail;
        }
        int entryOff = 0;
        for (int e = 0; e < (1 << (int)selector); e = e + 1) {
            unsigned int delta = bitreader_read_bits(br, 8);
            *(unsigned int *)((int)pDesc + entryOff + 0x140 + deltaBase) = delta - 1;
            int decoded = *(int *)((int)pDesc + entryOff + 0x140 + deltaBase);
            if ((decoded < -1) || (*(int *)(ctx + 0x18) <= decoded)) {
                goto fail;
            }
            entryOff = entryOff + 4;
        }
        deltaBase = deltaBase + 0x20;
        tblOff = tblOff + 4;
    }

    {
        unsigned int dim = bitreader_read_bits(br, 2);
        pDesc[0xd0] = dim + 1;
    }
    unsigned int idxWidth = bitreader_read_bits(br, 4);

    int symOff = 0;
    int written = 0;
    int payOff = 0;
    for (int c = 0; c < (int)*pDesc; c = c + 1) {
        runningTotal = runningTotal + (int)pDesc[*(int *)((int)pDesc + symOff + 4) + 0x20];
        for (; written < runningTotal; written = written + 1) {
            unsigned int idx = bitreader_read_bits(br, idxWidth);
            *(unsigned int *)((int)pDesc + payOff + 0x34c) = idx;
            if (((int)idx < 0) || ((1 << idxWidth) <= (int)idx)) {
                goto fail;
            }
            payOff = payOff + 4;
        }
        symOff = symOff + 4;
    }
    pDesc[0xd1] = 0;
    pDesc[0xd2] = 1 << idxWidth;

done:
    FUN_80382714();
    return;
fail:
    anim_free_0x460((int)pDesc);
    goto done;
}

// ---------------------------------------------------------------------------
// anim_curve_prepare_sort (FUN_80195b00)
// Given a decoded channel descriptor (r4), build the 0x520 evaluation context:
// gather the payload key pointers into a scratch array, sort them by key value
// (FUN_8038a0a4 + LAB_80195ae8 comparator), derive per-key sorted frame indices
// and an inverse permutation, then per interior key precompute the nearest
// lower/upper neighbour indices (+0x408 / +0x30c). Also selects a fixed-point
// scale (+0x50c) from the payload dimension (+0xd0): 1->0x100, 2->0x80,
// 3->0x56, 4->0x40.
// ---------------------------------------------------------------------------
void anim_curve_prepare_sort(int *pDesc) {
    FUN_803826f0();
    int eval = FUN_801a1498(1, 0x520);
    *(int **)(eval + 0x510) = pDesc;

    int keyCount = 0;
    int symOff = 0;
    *(int *)(eval + 0x508) = pDesc[0xd2];

    int symCount = *pDesc;
    if (0 < symCount) {
        int remaining = symCount;
        do {
            symOff = symOff + 4;
            keyCount = keyCount + pDesc[*(int *)((int)pDesc + symOff) + 0x20];
            remaining = remaining - 1;
        } while (remaining != 0);
    }

    unsigned int total = (unsigned int)(keyCount + 2);
    *(unsigned int *)(eval + 0x504) = total;

    int keyPtrs[70]; // scratch pointer array (max keyCount+2 entries)
    {
        int idx = 0;
        unsigned int n = total;
        if (0 < (int)total) {
            do {
                *(int *)((int)keyPtrs + idx) = (int)pDesc + idx + 0x344;
                idx = idx + 4;
                n = n - 1;
            } while (n != 0);
        }
    }
    FUN_8038a0a4((int)keyPtrs, total, 4, &LAB_80195ae8);

    // sorted key -> quantized sorted-index array at eval+0x104
    {
        int count = keyCount + 2;
        int idx = 0;
        if (0 < count) {
            do {
                int *pKey = (int *)((int)keyPtrs + idx);
                int outOff = idx + 0x104;
                idx = idx + 4;
                unsigned int diff = (unsigned int)(*pKey - (int)(pDesc + 0xd1));
                *(unsigned int *)(eval + outOff) =
                    ((int)diff >> 2) + (unsigned int)((int)diff < 0 && (diff & 3) != 0);
                count = count - 1;
            } while (count != 0);
        }
    }
    // inverse permutation at eval+0x208
    {
        int count = keyCount + 2;
        int rank = 0;
        int idx = 0;
        if (0 < count) {
            do {
                int outOff = idx + 0x104;
                idx = idx + 4;
                *(int *)(eval + *(int *)(eval + outOff) * 4 + 0x208) = rank;
                rank = rank + 1;
                count = count - 1;
            } while (count != 0);
        }
    }
    // resolve sorted keys back to payload values at eval+0x00
    {
        int count = keyCount + 2;
        int idx = 0;
        if (0 < count) {
            do {
                int *pSlot = (int *)(eval + idx);
                idx = idx + 4;
                *pSlot = pDesc[pSlot[0x41] + 0xd1];
                count = count - 1;
            } while (count != 0);
        }
    }

    int dim = pDesc[0xd0];
    if (dim == 3) {
        *(int *)(eval + 0x50c) = 0x56;
    } else if (dim < 3) {
        if (dim == 1) {
            *(int *)(eval + 0x50c) = 0x100;
        } else if (0 < dim) {
            *(int *)(eval + 0x50c) = 0x80;
        }
    } else if (dim < 5) {
        *(int *)(eval + 0x50c) = 0x40;
    }

    // per interior key: nearest lower (+0x408) and upper (+0x30c) neighbours
    int keyOff = 0;
    for (int k = 0; k < keyCount; k = k + 1) {
        int scan = k + 2;
        int target = *(int *)((int)pDesc + keyOff + 0x34c);
        int lowerIdx = 0;
        int upperVal = *(int *)(eval + 0x508);
        int upperIdx = 1;
        int lowerVal = 0;
        int rank = 0;
        int scanOff = 0;
        if (0 < scan) {
            do {
                int keyVal = *(int *)((int)pDesc + scanOff + 0x344);
                if ((lowerVal < keyVal) && (keyVal < target)) {
                    lowerIdx = rank;
                    lowerVal = keyVal;
                }
                if ((keyVal < upperVal) && (target < keyVal)) {
                    upperIdx = rank;
                    upperVal = keyVal;
                }
                rank = rank + 1;
                scanOff = scanOff + 4;
                scan = scan - 1;
            } while (scan != 0);
        }
        int slot = eval + keyOff;
        *(int *)(slot + 0x408) = lowerIdx;
        keyOff = keyOff + 4;
        *(int *)(slot + 0x30c) = upperIdx;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_lerp_clamped_15 (FUN_80195d54)
// Linear interpolate a 15-bit value: given endpoints v0/v1 (masked to 15 bits)
// and a fractional position (param_5-param_1)/(param_2-param_1), return
// v0 +/- |v1-v0| * frac. Sign follows the direction of (v1-v0).
// ---------------------------------------------------------------------------
int anim_lerp_clamped_15(int posLo, int posHi, unsigned int v0, unsigned int v1, int posAt) {
    unsigned int base = v0 & 0x7fff;
    unsigned int delta = (v1 & 0x7fff) - base;
    int scaled = (int)((((int)delta >> 0x1f ^ delta) - ((int)delta >> 0x1f)) * (posAt - posLo)) /
                 (posHi - posLo);
    if (-1 < (int)delta) {
        return base + scaled;
    }
    return base - scaled;
}

// ---------------------------------------------------------------------------
// anim_quantize_value (FUN_80195d8c)
// Quantize a float to a 10-bit code: round(value*scale + bias), clamp to
// [0, 0x3ff]. Negative results clamp to 0 via the sign-mask trick.
// ---------------------------------------------------------------------------
unsigned int anim_quantize_value(float *pValue) {
    unsigned int q = (unsigned int)(FLOAT_80616954 * *pValue + FLOAT_80616950);
    if ((int)q < 0x400) {
        q = q & ~((int)q >> 0x1f);
    } else {
        q = 0x3ff;
    }
    return q;
}

// ---------------------------------------------------------------------------
// anim_ramp_scale_run (FUN_80195dcc)
// Multiply a float run [start,end) in `pOut` by falloff-table weights, walking
// the table index from tabStart to tabEnd with a Bresenham step so the index
// advances proportionally across the run.
// ---------------------------------------------------------------------------
void anim_ramp_scale_run(int start, int end, int tabStart, int tabEnd, int pOut) {
    unsigned int span = (unsigned int)(tabEnd - tabStart);
    int len = end - start;
    int step = (int)span / len;
    int stepUp = step + 1;
    if ((int)span < 0) {
        stepUp = step - 1;
    }
    int outOff = start * 4;
    unsigned int rem = (unsigned int)((int)(step * len) >> 0x1f);
    int accum = 0;
    *(float *)(pOut + outOff) =
        *(float *)(pOut + outOff) * *(float *)(&DAT_8041f708 + tabStart * 4);
    while (true) {
        start = start + 1;
        outOff = outOff + 4;
        if (end <= start) {
            break;
        }
        accum = accum + ((((int)span >> 0x1f ^ span) - ((int)span >> 0x1f)) -
                         ((rem ^ (unsigned int)(step * len)) - rem));
        int inc = step;
        if (len <= accum) {
            accum = accum - len;
            inc = stepUp;
        }
        tabStart = tabStart + inc;
        *(float *)(pOut + outOff) =
            *(float *)(pOut + outOff) * *(float *)(&DAT_8041f708 + tabStart * 4);
    }
}

// ---------------------------------------------------------------------------
// anim_ramp_index_run (FUN_80195e88)
// Fill an int run [start,end) in `pOut` with table indices stepped from
// tabStart to tabEnd via the same Bresenham distribution as
// anim_ramp_scale_run (this variant stores the index instead of scaling).
// ---------------------------------------------------------------------------
void anim_ramp_index_run(int start, int end, int tabStart, int tabEnd, int pOut) {
    unsigned int span = (unsigned int)(tabEnd - tabStart);
    int len = end - start;
    int step = (int)span / len;
    int stepUp = step + 1;
    if ((int)span < 0) {
        stepUp = step - 1;
    }
    int outOff = start * 4;
    *(int *)(pOut + outOff) = tabStart;
    int accum = 0;
    unsigned int rem = (unsigned int)((int)(step * len) >> 0x1f);
    while (true) {
        start = start + 1;
        outOff = outOff + 4;
        if (end <= start) {
            break;
        }
        accum = accum + ((((int)span >> 0x1f ^ span) - ((int)span >> 0x1f)) -
                         ((rem ^ (unsigned int)(step * len)) - rem));
        int inc = step;
        if (len <= accum) {
            accum = accum - len;
            inc = stepUp;
        }
        tabStart = tabStart + inc;
        *(int *)(pOut + outOff) = tabStart;
    }
}

// ---------------------------------------------------------------------------
// anim_channel_accumulate_stats (FUN_80195f14)
// Sweep the frame window [frameLo, frameHi) for one channel and accumulate the
// least-squares sums (sum x, sum y, sum x^2, sum y^2, sum xy, count) into the
// 8-int stats block `pStats`, partitioned into two groups by whether the
// per-frame value exceeds a threshold (ctx+0x458 + reference). The group sizes
// are blended with a dimension-scaled weight (ctx+0x454) before summing.
// ---------------------------------------------------------------------------
// Returns the (weighted) total sample count written to pStats[7]; callers use it
// as a "segment has samples" predicate.
int anim_channel_accumulate_stats(unsigned int arg0, unsigned int arg1, int frameLo,
                                   int frameHi, int *pStats, int frameCap, int ctx) {
    unsigned long long pair = FUN_803826b4();
    int values = (int)(unsigned int)(pair >> 0x20); // per-frame quantized values (r3 hi)

    anim_quantize_value((float *)(values + frameLo * 4));

    int sumXa = 0, sumYa = 0, sumX2a = 0, sumY2a = 0, sumXYa = 0, cntA = 0;
    int sumXb = 0, sumYb = 0, sumX2b = 0, sumY2b = 0, sumXYb = 0, cntB = 0;

    memset_wrapper((int)pStats, 0, 0x20);
    pStats[0] = frameLo;
    pStats[1] = frameHi;
    if (frameCap <= frameHi) {
        frameHi = frameCap - 1;
    }

    int off = frameLo << 2;
    for (; frameLo <= frameHi; frameLo = frameLo + 1) {
        float *pVal = (float *)(values + off);
        unsigned int q = anim_quantize_value(pVal);
        if (q != 0) {
            if (*(float *)(ctx + 0x458) + *(float *)((int)(unsigned int)pair + off) < *pVal) {
                sumXb = sumXb + frameLo;
                sumYb = sumYb + (int)q;
                cntB = cntB + 1;
                sumX2b = sumX2b + frameLo * frameLo;
                sumY2b = sumY2b + (int)(q * q);
                sumXYb = sumXYb + frameLo * (int)q;
            } else {
                sumXa = sumXa + frameLo;
                sumYa = sumYa + (int)q;
                cntA = cntA + 1;
                sumX2a = sumX2a + frameLo * frameLo;
                sumY2a = sumY2a + (int)(q * q);
                sumXYa = sumXYa + frameLo * (int)q;
            }
        }
        off = off + 4;
    }

    int weight = (int)(((float)((double)CONCAT44(0x43300000, (unsigned int)((cntB + cntA) ^ 0x80000000)) - DOUBLE_80616948)
                        * *(float *)(ctx + 0x454)) /
                       (float)((double)CONCAT44(0x43300000, (unsigned int)((cntA + 1U) ^ 0x80000000)) - DOUBLE_80616948));
    pStats[2] = sumXb + sumXa + sumXa * weight;
    pStats[3] = sumYb + sumYa + sumYa * weight;
    pStats[4] = sumX2b + sumX2a + sumX2a * weight;
    pStats[5] = sumY2b + sumY2a + sumY2a * weight;
    pStats[6] = sumXYb + sumXYa + sumXYa * weight;
    pStats[7] = cntB + cntA + cntA * weight;
    FUN_80382700();
    return pStats[7];
}

// ---------------------------------------------------------------------------
// anim_solve_linear_fit (FUN_80196114)
// Solve a least-squares linear fit from the accumulated stats table (the r3:r4
// pair carries the base + entry count). Combines the per-entry sums with the two
// endpoint samples in *pV0 / *pV1, computes slope/intercept in double precision
// (determinant = n*sumX2 - sumX^2), evaluates the fit at the two endpoints,
// rounds, and clamps each result to [0, 0x3ff]. If there is no usable sample it
// zeroes both endpoints.
// ---------------------------------------------------------------------------
void anim_solve_linear_fit(unsigned int arg0, unsigned int arg1, int *pV0, int *pV1) {
    unsigned long long pair = (unsigned long long)__save_gpr_28();
    unsigned int *pStats = (unsigned int *)(unsigned int)(pair >> 0x20); // stats base (hi)
    int entryCount = (int)pair;                                          // entry count (lo)

    unsigned int x1 = pStats[entryCount * 8 + -7]; // last entry's x endpoint
    unsigned int sumX = 0;
    unsigned int x0 = *pStats;
    unsigned int sumY = 0;
    unsigned int sumX2 = 0;
    unsigned int sumXY = 0;
    unsigned int count = 0;

    int off = 0;
    int n = entryCount;
    if (0 < n) {
        do {
            sumX  = sumX  + *(int *)((int)pStats + off + 8);
            sumY  = sumY  + *(int *)((int)pStats + off + 0xc);
            sumX2 = sumX2 + *(int *)((int)pStats + off + 0x10);
            sumXY = sumXY + *(int *)((int)pStats + off + 0x18);
            count = count + *(int *)((int)pStats + off + 0x1c);
            n = n - 1;
            off = off + 0x20;
        } while (n != 0);
    }

    int v0 = *pV0;
    if (-1 < v0) {
        sumY  = sumY + v0;
        sumX  = sumX + x0;
        count = count + 1;
        sumX2 = sumX2 + x0 * x0;
        sumXY = sumXY + v0 * x0;
    }
    int v1 = *pV1;
    if (-1 < v1) {
        sumY  = sumY + v1;
        sumX  = sumX + x1;
        count = count + 1;
        sumX2 = sumX2 + x1 * x1;
        sumXY = sumXY + v1 * x1;
    }

    if (count == 0) {
        *pV0 = 0;
        *pV1 = 0;
    } else {
        double dSumX  = (double)CONCAT44(0x43300000, sumX  ^ 0x80000000) - DOUBLE_80616948;
        double dSumX2 = (double)CONCAT44(0x43300000, sumX2 ^ 0x80000000) - DOUBLE_80616948;
        double dSumY  = (double)CONCAT44(0x43300000, sumY  ^ 0x80000000) - DOUBLE_80616948;
        double invDet = DOUBLE_80616958 /
                        (((double)CONCAT44(0x43300000, count ^ 0x80000000) - DOUBLE_80616948) * dSumX2 -
                         dSumX * dSumX);
        double dSumXY = (double)CONCAT44(0x43300000, sumXY ^ 0x80000000) - DOUBLE_80616948;
        double intercept = invDet * (dSumY * dSumX2 - dSumXY * dSumX);
        double slope = invDet * (((double)CONCAT44(0x43300000, count ^ 0x80000000) - DOUBLE_80616948) * dSumXY
                                 - dSumX * dSumY);

        double fit0 = FUN_803907b8(slope * ((double)CONCAT44(0x43300000, x0 ^ 0x80000000) - DOUBLE_80616948) + intercept);
        *pV0 = (int)fit0;
        double fit1 = FUN_803907b8(slope * ((double)CONCAT44(0x43300000, x1 ^ 0x80000000) - DOUBLE_80616948) + intercept);
        *pV1 = (int)fit1;

        if (0x3ff < *pV0) { *pV0 = 0x3ff; }
        if (0x3ff < *pV1) { *pV1 = 0x3ff; }
        if (*pV0 < 0) { *pV0 = 0; }
        if (*pV1 < 0) { *pV1 = 0; }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// anim_channel_check_tolerance (FUN_8019637c)
// Walk a channel's frame run stepping a reference index (Bresenham) from
// startIdx to endIdx and, for each quantized frame value that is under the
// visibility threshold (ctx+0x458 + ref), verify it stays inside the tolerance
// band [ref - tol_lo(ctx+0x44c), ref + tol_hi(ctx+0x448)]. Returns as soon as a
// frame breaks the band (the caller re-splits the run there).
// ---------------------------------------------------------------------------
// Returns 0 while every sampled frame stays inside the tolerance band (the run
// can be represented by the straight line); returns the (nonzero) frame index at
// which a value first breaks the band, signalling the caller to split there.
int anim_channel_check_tolerance(unsigned int arg0, unsigned int arg1, unsigned int refIdx,
                                  int endIdx, int pValues, int pRef, int ctx) {
    unsigned long long pair = FUN_803826c0();
    int start = (int)(unsigned int)(pair >> 0x20); // window start (hi)
    int end   = (int)pair;                         // window end (lo)
    int result = 0;

    unsigned int span = (unsigned int)(endIdx - refIdx);
    int len = end - start;
    int step = (int)span / len;
    int stepUp = step + 1;
    if ((int)span < 0) {
        stepUp = step - 1;
    }

    float *pVal = (float *)(pValues + start * 4);
    int accum = 0;
    unsigned int q = anim_quantize_value(pVal);
    unsigned int rem = (unsigned int)((int)(step * len) >> 0x1f);

    if (*pVal <= *(float *)(ctx + 0x458) + *(float *)(pRef + start * 4)) {
        float ref = (float)((double)CONCAT44(0x43300000, refIdx ^ 0x80000000) - DOUBLE_80616948);
        float qf  = (float)((double)CONCAT44(0x43300000, q ^ 0x80000000) - DOUBLE_80616948);
        if ((ref + *(float *)(ctx + 0x448) < qf) ||
            (qf < ref - *(float *)(ctx + 0x44c))) {
            goto done;
        }
    }

    {
        int off = start << 2;
        while (true) {
            start = start + 1;
            off = off + 4;
            if (end <= start) {
                break;
            }
            accum = accum + ((((int)span >> 0x1f ^ span) - ((int)span >> 0x1f)) -
                             ((rem ^ (unsigned int)(step * len)) - rem));
            int inc = step;
            if (len <= accum) {
                accum = accum - len;
                inc = stepUp;
            }
            refIdx = refIdx + inc;
            q = anim_quantize_value((float *)(pValues + off));
            if ((*(float *)(pValues + off) <= *(float *)(ctx + 0x458) + *(float *)(pRef + off)) &&
                (q != 0)) {
                float ref = (float)((double)CONCAT44(0x43300000, refIdx ^ 0x80000000) - DOUBLE_80616948);
                float qf  = (float)((double)CONCAT44(0x43300000, q ^ 0x80000000) - DOUBLE_80616948);
                if ((ref + *(float *)(ctx + 0x448) < qf) ||
                    (qf < ref - *(float *)(ctx + 0x44c))) {
                    result = start;
                    break;
                }
            }
        }
    }
done:
    FUN_8038270c();
    return result;
}

// ===========================================================================
// batch_513 (0x80196630 - 0x801975d8): the delta-encode/decode driver layer on
// top of the least-squares primitives - recursive curve fitting, keyframe
// blending, the Huffman channel encode + decode paths, float-curve
// reconstruction, and the codec context init/teardown helpers.
// ===========================================================================
extern "C" {
extern void halt_stub_assert(void);   // assertion/abort stub (FUN_ panic)
extern int  DAT_806124f8;             // global encode-invocation counter
} // extern "C"

// ---------------------------------------------------------------------------
// anim_fit_pick_value (FUN_80196630)
// Reconcile two per-key candidate fit values (left/right approximations) at
// index i: if the left value is negative (unset) take the right; if the right
// is also set, average them; otherwise keep the left.
// ---------------------------------------------------------------------------
int anim_fit_pick_value(int pLeft, int pRight, int i) {
    int off = i * 4;
    int left = *(int *)(pLeft + off);
    if (left < 0) {
        return *(int *)(pRight + off);
    }
    if (-1 < *(int *)(pRight + off)) {
        return left + *(int *)(pRight + off) >> 1;
    }
    return left;
}

// ---------------------------------------------------------------------------
// anim_curve_fit_recursive (FUN_80196664)
// Top-level encoder fit: accumulate per-segment least-squares stats, solve the
// global endpoints, then iteratively split segments (walking the sorted key
// permutation) wherever the linear approximation exceeds tolerance
// (anim_channel_check_tolerance). Each accepted split re-solves the two child
// segments and marks the affected key spans. Finally materializes the chosen
// per-key values into a freshly arena-allocated output array, filling interior
// keys by clamped interpolation (anim_lerp_clamped_15) and tagging exact
// matches with the 0x8000 "keyframe present" bit.
// ---------------------------------------------------------------------------
int anim_curve_fit_recursive(unsigned int arg0, unsigned int arg1, int pRef, int pValues) {
    unsigned long long pair = FUN_803826b4();
    int eval = (int)pair; // 0x520 eval context (r4 lo)
    int result = 0;       // arena ptr of the fitted key array (0 if no samples)

    int keyCount = *(int *)(eval + 0x504);
    int refBlock = *(int *)(eval + 0x510);
    int total = 0;
    int frameCount = *(int *)(eval + 0x508);

    int leftVals[65];   // local_94c
    int rightVals[65];  // local_a50
    int upperKey[65];   // local_b54
    int lowerKey[65];   // local_c58
    int keyState[65];   // local_d5c
    int stats[530];     // aiStack_848 (65 * 8 + slack)

    int i, n;

    n = keyCount; i = 0;
    if (0 < keyCount) { do { *(int *)((int)leftVals + i)  = 0xffffff38; i += 4; n--; } while (n); }
    n = keyCount; i = 0;
    if (0 < keyCount) { do { *(int *)((int)rightVals + i) = 0xffffff38; i += 4; n--; } while (n); }
    n = keyCount; i = 0;
    if (0 < keyCount) { do { *(int *)((int)upperKey + i)  = 0;          i += 4; n--; } while (n); }
    n = keyCount; i = 0;
    if (0 < keyCount) { do { *(int *)((int)lowerKey + i)  = 1;          i += 4; n--; } while (n); }
    n = keyCount; i = 0;
    if (0 < keyCount) { do { *(int *)((int)keyState + i)  = 0xffffffff;  i += 4; n--; } while (n); }

    int nonzero = 0;
    if (keyCount == 0) {
        nonzero = anim_channel_accumulate_stats(arg1, arg0, 0, frameCount, stats, frameCount, refBlock);
    } else {
        int segOff = 0, segIdx = 0;
        for (int s = 0; s < keyCount + -1; s = s + 1) {
            int c = anim_channel_accumulate_stats(arg1, arg0,
                        *(int *)(eval + segIdx), ((int *)(eval + segIdx))[1],
                        (int *)((int)stats + segOff), frameCount, refBlock);
            nonzero = nonzero + c;
            segOff = segOff + 0x20;
            segIdx = segIdx + 4;
        }
    }

    if (nonzero != 0) {
        int v0 = -200, v1 = -200;
        anim_solve_linear_fit((unsigned int)(int)stats, (unsigned int)(keyCount + -1), &v0, &v1);
        int off = 8;
        leftVals[0] = v1;   // note: source stores local_d60 into [0]/[1] of both arrays
        rightVals[0] = v1;
        rightVals[1] = v0;  // local_d64
        leftVals[1] = v0;
        // (variable names above mirror the raw layout; v0/v1 seed both endpoints)
        leftVals[0]  = v0; leftVals[1]  = v1;
        rightVals[0] = v0; rightVals[1] = v1;

        for (int k = 2; k < keyCount; k = k + 1) {
            int permK = *(int *)(eval + off + 0x208);
            int hiK = upperKey[permK];
            int loK = lowerKey[permK];
            if (loK != keyState[hiK]) {
                keyState[hiK] = loK;
                int permHi = *(int *)(eval + hiK * 4 + 0x208);
                int keyHiVal = *(int *)(refBlock + hiK * 4 + 0x344);
                int permLo = *(int *)(eval + loK * 4 + 0x208);
                int keyLoVal = *(int *)(refBlock + loK * 4 + 0x344);
                unsigned int fitHi = (unsigned int)anim_fit_pick_value((int)leftVals, (int)rightVals, hiK);
                int fitLo = anim_fit_pick_value((int)leftVals, (int)rightVals, loK);
                if ((fitHi == 0xffffffff) || (fitLo == -1)) {
                    halt_stub_assert();
                }
                int ok = anim_channel_check_tolerance((unsigned int)keyHiVal, (unsigned int)keyLoVal,
                             fitHi, fitLo, arg1, arg0, refBlock);
                if (ok == 0) {
                    *(int *)((int)leftVals + off)  = 0xffffff38;
                    *(int *)((int)rightVals + off) = 0xffffff38;
                } else {
                    int a0 = -200, a1 = -200, b0 = -200, b1 = -200;
                    anim_solve_linear_fit((unsigned int)(int)(stats + permHi * 8),
                                          (unsigned int)(permK - permHi), &a0, &a1);
                    anim_solve_linear_fit((unsigned int)(int)(stats + permK * 8),
                                          (unsigned int)(permLo - permK), &b0, &b1);
                    rightVals[hiK] = a0;
                    if (hiK == 0) { leftVals[0] = a0; }
                    *(int *)((int)leftVals + off)  = a1;
                    *(int *)((int)rightVals + off) = b0;
                    leftVals[loK] = b1;
                    if (loK == 1) { rightVals[1] = b1; }
                    if ((-1 < a1) || (-1 < b0)) {
                        int off2 = (permK + -1) * 4;
                        int nn = permK;
                        if (-1 < permK + -1) {
                            do {
                                if (loK != *(int *)((int)lowerKey + off2)) break;
                                *(int *)((int)lowerKey + off2) = k;
                                off2 = off2 + -4;
                                nn = nn + -1;
                            } while (nn != 0);
                        }
                        permK = permK + 1;
                        nn = keyCount - permK;
                        int off3 = permK * 4;
                        if (permK < keyCount) {
                            do {
                                if (hiK != *(int *)((int)upperKey + off3)) break;
                                *(int *)((int)upperKey + off3) = k;
                                off3 = off3 + 4;
                                nn = nn + -1;
                            } while (nn != 0);
                        }
                    }
                }
            }
            off = off + 4;
        }

        int *out = (int *)msg_arena_alloc((int)(unsigned int)(pair >> 0x20), keyCount << 2);
        out[0] = anim_fit_pick_value((int)leftVals, (int)rightVals, 0);
        out[1] = anim_fit_pick_value((int)leftVals, (int)rightVals, 1);
        int off4 = 8;
        for (int j = 2; j < keyCount; j = j + 1) {
            int lo = *(int *)(eval + off4 + 0x400);
            int hi = *(int *)(eval + off4 + 0x304);
            unsigned int interp = anim_lerp_clamped_15(
                *(int *)(refBlock + lo * 4 + 0x344), *(int *)(refBlock + hi * 4 + 0x344),
                out[lo], out[hi], *(int *)(refBlock + off4 + 0x344));
            unsigned int chosen = (unsigned int)anim_fit_pick_value((int)leftVals, (int)rightVals, j);
            if (((int)chosen < 0) || (interp == chosen)) {
                *(unsigned int *)((int)out + off4) = interp | 0x8000;
            } else {
                *(unsigned int *)((int)out + off4) = chosen;
            }
            off4 = off4 + 4;
        }
        result = (int)out;
    }
    FUN_80382700();
    return result;
}

// ---------------------------------------------------------------------------
// anim_curve_blend_frames (FUN_80196af8)
// Blend two decoded keyframe sets frame-by-frame with a 0.16 fixed-point factor
// t (param_5): out = ((0x10000-t)*a + t*b + 0x8000) >> 16 over the low 15 bits;
// the 0x8000 "present" flag is set only when both inputs have it. Allocates the
// output from the message arena.
// ---------------------------------------------------------------------------
int anim_curve_blend_frames(unsigned int arg0, unsigned int arg1, int pA, int pB, int t) {
    unsigned long long pair = (unsigned long long)__save_gpr_28();
    int count = *(int *)((int)pair + 0x504);
    int result = 0;   // blended arena ptr (0 if either input null)
    if ((pA != 0) && (pB != 0)) {
        int out = msg_arena_alloc((int)(unsigned int)(pair >> 0x20), count << 2);
        result = out;
        int off = 0;
        if (0 < count) {
            do {
                *(int *)(out + off) =
                    (int)((0x10000 - t) * (*(unsigned int *)(pA + off) & 0x7fff) +
                          t * (*(unsigned int *)(pB + off) & 0x7fff) + 0x8000) >> 0x10;
                if (((*(unsigned int *)(pA + off) & 0x8000) != 0) &&
                    ((*(unsigned int *)(pB + off) & 0x8000) != 0)) {
                    *(unsigned int *)(out + off) = *(unsigned int *)(out + off) | 0x8000;
                }
                off = off + 4;
                count = count + -1;
            } while (count != 0);
        }
    }
    __restore_gpr_28();
    return result;
}

// ---------------------------------------------------------------------------
// anim_channel_encode (FUN_80196bc4)
// Encode one channel's fitted keyframes into the bit-writer. If the input is
// null, writes a 0-bit "no data" marker and zeroes the output. Otherwise:
// dimension-quantizes each key, delta+zigzag-encodes interior keys against the
// clamped interpolation, writes a 1-bit "present" marker, packs the two endpoint
// values, then for each codebook symbol builds the per-entry Huffman selector
// word (bit-packed) and emits value codes via anim_channel_write_bits. Also
// accumulates bit-usage statistics into the eval context (+0x514/0x518/0x51c)
// and finally rebuilds the per-frame ramp index table (anim_ramp_index_run).
// ---------------------------------------------------------------------------
int anim_channel_encode(unsigned int arg0, unsigned int arg1, int *pKeys, int pOut) {
    unsigned long long pair = FUN_803826b4();
    int br  = (int)(unsigned int)(pair >> 0x20);   // bit-writer holder (r3 hi)
    int eval = (int)pair;                          // eval context (r4 lo)
    int result = 0;   // 1 when a present (non-null) channel was encoded

    int *pDesc = *(int **)(eval + 0x510);
    int count = *(int *)(eval + 0x504);
    int ctx = *(int *)(*(int *)(*(int *)(br + 0x40) + 4) + 0x1c);
    int codebook = *(int *)(ctx + 0xb20);

    int local_198[7];
    unsigned int uStack_17c;
    int local_178[8];
    int local_158[86];

    if (pKeys == (int *)0) {
        bitwriter_write_bits(br + 4, 0, 1);
        unsigned int c = *(unsigned int *)(br + 0x24);
        memset_wrapper(pOut, 0, (((int)c >> 1) + (unsigned int)((int)c < 0 && (c & 1) != 0)) * 4);
    } else {
        result = 1;
        int off = 0, m = count;
        if (0 < count) {
            do {
                int dim = pDesc[0xd0];
                unsigned int q = *(unsigned int *)((int)pKeys + off) & 0x7fff;
                if (dim == 3) { q = q / 0xc; }
                else if (dim < 3) {
                    if (dim == 1) { q = (int)q >> 2; }
                    else if (0 < dim) { q = (int)q >> 3; }
                } else if (dim < 5) { q = (int)q >> 4; }
                *(unsigned int *)((int)pKeys + off) = q | *(unsigned int *)((int)pKeys + off) & 0x8000;
                off = off + 4;
                m = m + -1;
            } while (m != 0);
        }

        local_158[0] = *pKeys;
        local_158[1] = pKeys[1];
        int koff = 8;
        for (int k = 2; k < count; k = k + 1) {
            int loK = *(int *)(eval + koff + 0x400);
            int hiK = *(int *)(eval + koff + 0x304);
            unsigned int interp = anim_lerp_clamped_15(
                pDesc[loK + 0xd1], pDesc[hiK + 0xd1], pKeys[loK], pKeys[hiK],
                *(int *)((int)pDesc + koff + 0x344));
            unsigned int cur = *(unsigned int *)((int)pKeys + koff);
            if (((cur & 0x8000) == 0) && (interp != cur)) {
                unsigned int hiRange = *(int *)(eval + 0x50c) - interp;
                unsigned int band = interp;
                if ((int)hiRange < (int)interp) { band = hiRange; }
                int d = cur - interp;
                if (d < 0) {
                    if (d < (int)-band) { d = (band - d) + -1; }
                    else { d = d * -2 + -1; }
                } else if (d < (int)band) { d = d * 2; }
                else { d = d + band; }
                unsigned int save = pKeys[loK];
                *(int *)((int)local_158 + koff) = d;
                pKeys[loK] = save & 0x7fff;
                pKeys[hiK] = pKeys[hiK] & 0x7fff;
            } else {
                *(unsigned int *)((int)pKeys + koff) = interp | 0x8000;
                *(int *)((int)local_158 + koff) = 0;
            }
            koff = koff + 4;
        }

        bitwriter_write_bits(br + 4, 1, 1);
        *(int *)(eval + 0x51c) = *(int *)(eval + 0x51c) + 1;
        int w = bit_length(*(int *)(eval + 0x50c) - 1);
        *(int *)(eval + 0x518) = *(int *)(eval + 0x518) + w * 2;
        w = bit_length(*(int *)(eval + 0x50c) - 1);
        bitwriter_write_bits(br + 4, local_158[0], w);
        w = bit_length(*(int *)(eval + 0x50c) - 1);
        bitwriter_write_bits(br + 4, local_158[1], w);

        int symOff = 0, keyBase = 2;
        for (int s = 0; s < *pDesc; s = s + 1) {
            int sym = *(int *)((int)pDesc + symOff + 4);
            unsigned int *dst = &uStack_17c;
            unsigned int *srcTbl = (unsigned int *)0x803cd114;
            int selector = pDesc[sym + 0x30];
            int runLen = pDesc[sym + 0x20];
            int tblSize = 1 << selector;
            int cp = 4;
            do {
                unsigned int *a = srcTbl + 1;
                srcTbl = srcTbl + 2;
                unsigned int v = *srcTbl;
                dst[1] = *a;
                dst = dst + 2;
                *dst = v;
                cp = cp + -1;
            } while (cp != 0);

            unsigned int word = 0;
            int bitPos = 0;
            if (selector != 0) {
                int e = 0, ec = tblSize;
                if (0 < tblSize) {
                    do {
                        int decoded = *(int *)((int)pDesc + e + 0x140 + sym * 0x20);
                        if (decoded < 0) {
                            *(int *)((int)local_198 + e) = 1;
                        } else {
                            *(int *)((int)local_198 + e) =
                                *(int *)(*(int *)(ctx + 0x720 + decoded * 4) + 4);
                        }
                        e = e + 4;
                        ec = ec + -1;
                    } while (ec != 0);
                }
                int outOff = 0;
                for (int r = 0; r < runLen; r = r + 1) {
                    int idx = 0, scan = 0, sc = tblSize;
                    if (0 < tblSize) {
                        do {
                            if (local_158[keyBase + r] < *(int *)((int)local_198 + scan)) {
                                *(int *)((int)local_178 + outOff) = idx;
                                break;
                            }
                            idx = idx + 1;
                            scan = scan + 4;
                            sc = sc + -1;
                        } while (sc != 0);
                    }
                    int *slot = (int *)((int)local_178 + outOff);
                    outOff = outOff + 4;
                    unsigned int packed = *slot << bitPos;
                    bitPos = bitPos + selector;
                    word = word | packed;
                }
                int used = anim_channel_write_bits(codebook + pDesc[sym + 0x40] * 0x2c, word, br + 4);
                *(int *)(eval + 0x514) = *(int *)(eval + 0x514) + used;
            }
            int voff = 0;
            for (int r = 0; r < runLen; r = r + 1) {
                if (-1 < pDesc[sym * 8 + *(int *)((int)local_178 + voff) + 0x50]) {
                    int cbEntry = codebook + pDesc[sym * 8 + *(int *)((int)local_178 + voff) + 0x50] * 0x2c;
                    if (local_158[keyBase + r] < *(int *)(cbEntry + 4)) {
                        int used = anim_channel_write_bits(cbEntry, local_158[keyBase + r], br + 4);
                        *(int *)(eval + 0x518) = *(int *)(eval + 0x518) + used;
                    }
                }
                voff = voff + 4;
            }
            keyBase = keyBase + runLen;
            symOff = symOff + 4;
        }

        int lastIdx = 0, permOff = 4, prevIdx = 0;
        int prevRamp = *pKeys * pDesc[0xd0];
        for (int k = 1; k < *(int *)(eval + 0x504); k = k + 1) {
            int keyIdx = *(int *)(eval + permOff + 0x104);
            unsigned int q = pKeys[keyIdx] & 0x7fff;
            int curIdx = prevIdx;
            int curRamp = prevRamp;
            if (q == (unsigned int)pKeys[keyIdx]) {
                curIdx = pDesc[keyIdx + 0xd1];
                curRamp = q * pDesc[0xd0];
                anim_ramp_index_run(prevIdx, curIdx, prevRamp, curRamp, pOut);
                lastIdx = curIdx;
            }
            permOff = permOff + 4;
            prevIdx = curIdx;
            prevRamp = curRamp;
        }
        int fill = lastIdx << 2;
        for (;;) {
            unsigned int c = *(unsigned int *)(br + 0x24);
            if (lastIdx >= (int)(((int)c >> 1) + (unsigned int)((int)c < 0 && (c & 1) != 0))) break;
            *(int *)(pOut + fill) = prevRamp;
            fill = fill + 4;
            lastIdx = lastIdx + 1;
        }
    }
    DAT_806124f8 = DAT_806124f8 + 1;
    FUN_80382700();
    return result;
}

// ---------------------------------------------------------------------------
// anim_channel_decode (FUN_801970e8)
// Inverse of anim_channel_encode. Reads the 1-bit present marker; if set,
// arena-allocates the key array, reads the two endpoints, then per codebook
// symbol reads the packed selector word (anim_huffman_decode_indexed) and the
// per-entry value codes, reconstructing each interior key by inverse zigzag +
// clamped interpolation. Aborts cleanly on a 0xffffffff Huffman error.
// ---------------------------------------------------------------------------
void anim_channel_decode(void) {
    unsigned long long pair = FUN_803826c0();
    int br   = (int)(unsigned int)(pair >> 0x20);   // bit-reader holder (r3 hi)
    int eval = (int)pair;                           // eval context (r4 lo)
    int *pDesc = *(int **)(eval + 0x510);
    int codebook = *(int *)(*(int *)(*(int *)(*(int *)(br + 0x40) + 4) + 0x1c) + 0xb20);

    unsigned int present = bitreader_read_bits((int *)(br + 4), 1);
    if (present == 1) {
        unsigned int *out = (unsigned int *)msg_arena_alloc(br, *(int *)(eval + 0x504) << 2);
        int w = bit_length(*(int *)(eval + 0x50c) - 1);
        out[0] = bitreader_read_bits((int *)(br + 4), w);
        w = bit_length(*(int *)(eval + 0x50c) - 1);
        out[1] = bitreader_read_bits((int *)(br + 4), w);

        int symOff = 0, keyBase = 2;
        for (int s = 0; s < *pDesc; s = s + 1) {
            int sym = *(int *)((int)pDesc + symOff + 4);
            unsigned int word = 0;
            unsigned int selector = pDesc[sym + 0x30];
            int runLen = pDesc[sym + 0x20];
            if ((selector != 0) &&
                (word = anim_huffman_decode_indexed(codebook + pDesc[sym + 0x40] * 0x2c),
                 word == 0xffffffff)) {
                goto done;
            }
            for (int r = 0; r < runLen; r = r + 1) {
                unsigned int sel = word & (1U << selector) - 1U;
                word = (int)word >> (selector & 0x3f);
                if (pDesc[sym * 8 + sel + 0x50] < 0) {
                    out[keyBase + r] = 0;
                } else {
                    unsigned int val = anim_huffman_decode_indexed(codebook + pDesc[sym * 8 + sel + 0x50] * 0x2c);
                    out[keyBase + r] = val;
                    if (val == 0xffffffff) { goto done; }
                }
            }
            keyBase = keyBase + runLen;
            symOff = symOff + 4;
        }

        int koff = 8;
        for (int k = 2; k < *(int *)(eval + 0x504); k = k + 1) {
            int slot = eval + koff;
            unsigned int interp = anim_lerp_clamped_15(
                pDesc[*(int *)(slot + 0x400) + 0xd1], pDesc[*(int *)(slot + 0x304) + 0xd1],
                out[*(int *)(slot + 0x400)], out[*(int *)(slot + 0x304)],
                *(int *)((int)pDesc + koff + 0x344));
            unsigned int hiRange = *(int *)(eval + 0x50c) - interp;
            unsigned int band = interp;
            if ((int)hiRange < (int)interp) { band = hiRange; }
            unsigned int cur = *(unsigned int *)((int)out + koff);
            if (cur == 0) {
                *(unsigned int *)((int)out + koff) = interp | 0x8000;
            } else {
                int d;
                if ((int)cur < (int)(band << 1)) {
                    if ((cur & 1) == 0) { d = (int)cur >> 1; }
                    else { d = -((int)(cur + 1) >> 1); }
                } else if ((int)interp < (int)hiRange) {
                    d = cur - interp;
                } else {
                    d = -1 - (cur - hiRange);
                }
                *(unsigned int *)((int)out + koff) = d + interp;
                out[*(int *)(slot + 0x400)] = out[*(int *)(slot + 0x400)] & 0x7fff;
                out[*(int *)(slot + 0x304)] = out[*(int *)(slot + 0x304)] & 0x7fff;
            }
            koff = koff + 4;
        }
    }
done:
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// anim_channel_reconstruct (FUN_80197380)
// Rebuild the per-frame float curve from a decoded key array: for each key that
// is an exact frame (low 15 bits unchanged), scale the float run between the
// previous and current frame index by the falloff LUT (anim_ramp_scale_run);
// tail frames past the last key use the final weight. Null input clears output.
// ---------------------------------------------------------------------------
void anim_channel_reconstruct(unsigned int arg0, unsigned int arg1, int *pKeys, int pOut) {
    unsigned long long pair = FUN_803826d8();
    int ctxHi = (int)(unsigned int)(pair >> 0x20);  // context (r3 hi)
    int eval  = (int)pair;                          // eval context (r4 lo)
    int refBlock = *(int *)(eval + 0x510);
    unsigned int frames = *(unsigned int *)(*(int *)(*(int *)(*(int *)(ctxHi + 0x40) + 4) + 0x1c) +
                                            *(int *)(ctxHi + 0x1c) * 4);
    int frameCount = ((int)frames >> 1) + (unsigned int)((int)frames < 0 && (frames & 1) != 0);

    if (pKeys == (int *)0) {
        memset_wrapper(pOut, 0, frameCount * 4);
    } else {
        int lastIdx = 0, permOff = 4, prevIdx = 0;
        int prevRamp = *pKeys * *(int *)(refBlock + 0x340);
        for (int k = 1; k < *(int *)(eval + 0x504); k = k + 1) {
            int keyIdx = *(int *)(eval + permOff + 0x104);
            unsigned int q = pKeys[keyIdx] & 0x7fff;
            int curIdx = prevIdx;
            int curRamp = prevRamp;
            if (q == (unsigned int)pKeys[keyIdx]) {
                curIdx = *(int *)(refBlock + keyIdx * 4 + 0x344);
                curRamp = q * *(int *)(refBlock + 0x340);
                anim_ramp_scale_run(prevIdx, curIdx, prevRamp, curRamp, pOut);
                lastIdx = curIdx;
            }
            permOff = permOff + 4;
            prevIdx = curIdx;
            prevRamp = curRamp;
        }
        int tail = frameCount - lastIdx;
        int off = lastIdx << 2;
        if (lastIdx < frameCount) {
            do {
                *(float *)(pOut + off) = *(float *)(pOut + off) * *(float *)(&DAT_8041f708 + prevRamp * 4);
                off = off + 4;
                tail = tail + -1;
            } while (tail != 0);
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// anim_read_bytes (FUN_801974ac)
// Read `count` raw bytes (8 bits each) from the bit-reader `br` into `dst`. The
// register-save prologue (FUN_803826f0 / FUN_8038273c) carries the br/dst pair;
// callers pass them explicitly as (br, dst, count).
// ---------------------------------------------------------------------------
void anim_read_bytes(int *br, int dst, int count) {
    unsigned char *p = (unsigned char *)dst;
    bool more;
    while (more = count != 0, count = count + -1, more) {
        unsigned int b = bitreader_read_bits(br, 8);
        *p = (unsigned char)b;
        p = p + 1;
    }
}

// ---------------------------------------------------------------------------
// anim_ctx_zero_0x10 (FUN_80197508)
// Zero a 0x10-byte codec sub-structure.
// ---------------------------------------------------------------------------
void anim_ctx_zero_0x10(int pObj) {
    memset_wrapper(pObj, 0, 0x10);
}

// ---------------------------------------------------------------------------
// anim_ctx_free_scratch (FUN_80197530)
// Free the scratch-buffer array held by the spill-carried object: release each
// of the [2] entries, then the base/[1]/[3] pointers, then zero the 0x10 header.
// ---------------------------------------------------------------------------
void anim_ctx_free_scratch(void) {
    int *pObj = (int *)FUN_803826f0();
    if (pObj != (int *)0) {
        int off = 0;
        for (int i = 0; i < pObj[2]; i = i + 1) {
            if (*(int *)(*pObj + off) != 0) {
                FUN_801a14dc(*(int *)(*pObj + off));
            }
            off = off + 4;
        }
        if (*pObj != 0) { FUN_801a14dc(*pObj); }
        if (pObj[1] != 0) { FUN_801a14dc(pObj[1]); }
        if (pObj[3] != 0) { FUN_801a14dc(pObj[3]); }
    }
    memset_wrapper((int)pObj, 0, 0x10);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_ctx_init_0xe80 (FUN_801975d8)
// Zero a 0x20-byte context header then allocate its 0xe80 codebook sub-context
// (stored at +0x1c).
// ---------------------------------------------------------------------------
void anim_ctx_init_0xe80(int pObj) {
    memset_wrapper(pObj, 0, 0x20);
    int sub = FUN_801a1498(1, 0xe80);
    *(int *)(pObj + 0x1c) = sub;
}

// ===========================================================================
// batch_514 (0x8019761c - 0x80198294): the top-level stream parser (context
// destroy, header/scratch/codebook-table parse, block dispatch by type tag)
// plus the weighted polynomial sample-eval helper (FUN_80197f58) and its
// piecewise-linear LUT math primitives (round, two interpolators, one lookup).
// ===========================================================================
extern "C" {
// vtable base for the "processor" component list (each entry: [+4]=parse, [+8]=free)
extern int *PTR_PTR_80610b10[];
// additional teardown callees
extern void FUN_8019f474(int obj);     // codebook-entry teardown (per 0x2c entry)
void FUN_8019a898(int obj);            // sub-decoder teardown (0x210, defined batch_517 below)
// stream-dispatch helpers
extern void bitwriter_reset(int *br, unsigned int lo, unsigned int hi); // (re)seat bit cursor
extern int  FUN_80387b68(int a, int b, int n);   // memcmp-like (n bytes)
// weighted-eval math helpers
extern unsigned long long FUN_803826cc(void);    // -> (outHi:tableLo) eval pair
extern void   FUN_80382718(void);                // restore (paired with FUN_803826cc)
extern double FUN_8039046c(double x, int *pExp);  // frexp-like (mantissa + exponent out)
extern double FUN_801982ac(double x);             // exp2-like scalar (LUT-backed)

// weighted-eval interpolation LUT tables + constants (0x80616960..0x80616994,
// 0x8041fb28..0x8041fe30)
extern double DOUBLE_80616960;   // round bias (2^52 + 2^51-ish)
extern double DOUBLE_80616968;   // int->double magic subtrahend (2^52)
extern double DOUBLE_80616970;   // exp-LUT input scale
extern float  FLOAT_80616978;    // log-LUT input scale
extern float  FLOAT_8061697c;    // log-LUT input bias
extern float  FLOAT_80616988;    // 1.0 (polynomial constant)
extern float  FLOAT_80616990;    // reciprocal-count scale
extern float  FLOAT_80616994;    // polynomial seed (1.0)
extern float  DAT_8041fb28;      // exp interp LUT (lower)
extern float  DAT_8041fb2c;      // exp interp LUT (upper)
extern float  DAT_8041fd2c;      // log interp LUT (lower)
extern float  DAT_8041fd30;      // log interp LUT (upper)
extern float  DAT_8041fe30;      // exp2 lookup LUT
} // extern "C"

// Forward decls for the LUT primitives (referenced before their definitions).
int    anim_round_to_int(double x);
double anim_lut_interp_fb28(double x);
double anim_lut_interp_fd2c(double x);
double anim_lut_lookup_fe30(int i);

// ---------------------------------------------------------------------------
// anim_subctx_destroy (FUN_8019761c)
// Tear down the entire 0xe80 codebook sub-context (state+0x1c): free each raw
// scratch buffer (+0x20 array), invoke the per-entry free vtable slots for the
// "processor" (+0x120/+0x220), builder (+800/+0x420) and secondary builder
// (+0x520/+0x620) component lists, free each channel object (+0x720) and its
// 0x2c codebook entry (+0xb20), free the codebook block, tear down each
// sub-decoder (+0xb24), free the sub-context, and finally zero the 0x20 header.
// ---------------------------------------------------------------------------
void anim_subctx_destroy(void) {
    int state = (int)FUN_803826e8();
    int sub = *(int *)(state + 0x1c);
    if (sub != 0) {
        int off = 0;
        for (int i = 0; i < *(int *)(sub + 8); i = i + 1) {
            int buf = *(int *)(sub + off + 0x20);
            if (buf != 0) { FUN_801a14dc(buf); }
            off = off + 4;
        }
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0xc); i = i + 1) {
            (**(void (**)(int))((int)PTR_PTR_80610b10[*(int *)(sub + off + 0x120)] + 8))
                (*(int *)(sub + off + 0x220));
            off = off + 4;
        }
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0x10); i = i + 1) {
            (**(void (**)(int))((int)PTR_DAT_80610b08[*(int *)(sub + off + 800)] + 0xc))
                (*(int *)(sub + off + 0x420));
            off = off + 4;
        }
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0x14); i = i + 1) {
            (**(void (**)(int))((int)PTR_DAT_80425f40[*(int *)(sub + off + 0x520)] + 0xc))
                (*(int *)(sub + off + 0x620));
            off = off + 4;
        }
        int cbOff = 0;
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0x18); i = i + 1) {
            int ch = *(int *)(sub + off + 0x720);
            if (ch != 0) { FUN_8019f434(ch); }
            if (*(int *)(sub + 0xb20) != 0) {
                FUN_8019f474(*(int *)(sub + 0xb20) + cbOff);
            }
            cbOff = cbOff + 0x2c;
            off = off + 4;
        }
        if (*(int *)(sub + 0xb20) != 0) {
            FUN_801a14dc(*(int *)(sub + 0xb20));
        }
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0x1c); i = i + 1) {
            FUN_8019a898(*(int *)(sub + off + 0xb24));
            off = off + 4;
        }
        FUN_801a14dc(sub);
    }
    memset_wrapper(state, 0, 0x20);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_stream_parse_header (FUN_80197808)
// Parse the fixed header record (present only when word[7] holds a live pointer):
// a 32-bit tag; when zero it is a full header - read version(8), four 32-bit
// params, then two 4-bit log2 dimensions (stored as 1<<n). Validates the
// dimensions/params and a trailing 1-bit marker; on any failure destroys the
// context. Pair carried via the register-save helper.
// ---------------------------------------------------------------------------
void anim_stream_parse_header(void) {
    unsigned long long pair = FUN_803826f0();
    unsigned int *hdr = (unsigned int *)((unsigned long long)pair >> 0x20);
    int *br = (int *)pair;
    int *dims = (int *)hdr[7];
    if (dims != (int *)0) {
        hdr[0] = bitreader_read_bits(br, 0x20);
        if (hdr[0] == 0) {
            hdr[1] = bitreader_read_bits(br, 8);
            hdr[2] = bitreader_read_bits(br, 0x20);
            hdr[3] = bitreader_read_bits(br, 0x20);
            hdr[4] = bitreader_read_bits(br, 0x20);
            hdr[5] = bitreader_read_bits(br, 0x20);
            unsigned int w0 = bitreader_read_bits(br, 4);
            dims[0] = 1 << w0;
            unsigned int w1 = bitreader_read_bits(br, 4);
            dims[1] = 1 << w1;
            unsigned int flag;
            if (((int)hdr[2] < 1) || ((int)hdr[1] < 1) || (dims[0] < 8) ||
                (dims[1] < dims[0]) || (flag = bitreader_read_bits(br, 1), flag != 1)) {
                anim_subctx_destroy();
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_stream_parse_scratch (FUN_8019794c)
// Parse the opaque scratch/blob table: a length-prefixed primary blob, then a
// count-prefixed array of length-prefixed secondary blobs. Each blob is
// arena-allocated and filled via anim_read_bytes. Validates a trailing 1-bit
// marker; frees everything (anim_ctx_free_scratch) on malformation.
// ---------------------------------------------------------------------------
void anim_stream_parse_scratch(void) {
    unsigned long long pair = FUN_803826e8();
    int *obj = (int *)((unsigned long long)pair >> 0x20);
    int *br = (int *)pair;

    unsigned int n = bitreader_read_bits(br, 0x20);
    if (-1 < (int)n) {
        int buf = FUN_801a1498(n + 1, 1);
        obj[3] = buf;
        anim_read_bytes(br, obj[3], n);
        obj[2] = bitreader_read_bits(br, 0x20);
        if (-1 < obj[2]) {
            obj[0] = FUN_801a1498(obj[2] + 1, 4);
            obj[1] = FUN_801a1498(obj[2] + 1, 4);
            int off = 0;
            for (int i = 0; i < obj[2]; i = i + 1) {
                unsigned int len = bitreader_read_bits(br, 0x20);
                if ((int)len < 0) { goto fail; }
                *(unsigned int *)(obj[1] + off) = len;
                int b = FUN_801a1498(len + 1, 1);
                *(int *)(obj[0] + off) = b;
                anim_read_bytes(br, *(int *)(obj[0] + off), len);
                off = off + 4;
            }
            unsigned int marker = bitreader_read_bits(br, 1);
            if (marker == 1) { goto done; }
        }
    }
fail:
    anim_ctx_free_scratch();
done:
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_stream_parse_codebooks (FUN_80197a80)
// Parse the codebook/component tables into the sub-context: N codebooks (each a
// 0x34 object via anim_codebook_parse), a skipped 16-bit reserved list, then the
// builder (+800/+0x420), secondary-builder (+0x520/+0x620) and processor
// (+0x120/+0x220) component lists (each entry dispatched through its vtable
// parse slot), and finally the channel-slot table (+0x20, each a 0x10 record
// with flags/ranges validated against the processor count). Destroys the
// context on any malformed field. Uses the __save_gpr_25 frame.
// ---------------------------------------------------------------------------
void anim_stream_parse_codebooks(void) {
    unsigned long long pair = (unsigned long long)__save_gpr_25();
    int state = (int)((unsigned long long)pair >> 0x20);
    int *br = (int *)pair;
    int sub = *(int *)(state + 0x1c);
    if (sub != 0) {
        unsigned int cbCount = bitreader_read_bits(br, 8);
        *(unsigned int *)(sub + 0x18) = cbCount + 1;
        int off = 0;
        for (int i = 0; i < *(int *)(sub + 0x18); i = i + 1) {
            int cb = FUN_801a1498(1, 0x34);
            *(int *)(sub + off + 0x720) = cb;
            int failed = anim_codebook_parse();
            if (failed != 0) { goto fail; }
            off = off + 4;
        }
        unsigned int reserved = bitreader_read_bits(br, 6);
        for (int i = 0; i < (int)(reserved + 1); i = i + 1) {
            unsigned int v = bitreader_read_bits(br, 0x10);
            if (((int)v < 0) || (0 < (int)v)) { goto fail; }
        }
        unsigned int nBuild = bitreader_read_bits(br, 6);
        *(unsigned int *)(sub + 0x10) = nBuild + 1;
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0x10); i = i + 1) {
            unsigned int type = bitreader_read_bits(br, 0x10);
            int slot = sub + off;
            *(unsigned int *)(slot + 800) = type;
            int t = *(int *)(slot + 800);
            if ((t < 0) || (1 < t)) { goto fail; }
            int obj = (**(int (**)(int, int *))((int)PTR_DAT_80610b08[t] + 4))(state, br);
            *(int *)(slot + 0x420) = obj;
            if (*(int *)(slot + 0x420) == 0) { goto fail; }
            off = off + 4;
        }
        unsigned int nBuild2 = bitreader_read_bits(br, 6);
        *(unsigned int *)(sub + 0x14) = nBuild2 + 1;
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0x14); i = i + 1) {
            unsigned int type = bitreader_read_bits(br, 0x10);
            int slot = sub + off;
            *(unsigned int *)(slot + 0x520) = type;
            int t = *(int *)(slot + 0x520);
            if ((t < 0) || (2 < t)) { goto fail; }
            int obj = (**(int (**)(int, int *))((int)PTR_DAT_80425f40[t] + 4))(state, br);
            *(int *)(slot + 0x620) = obj;
            if (*(int *)(slot + 0x620) == 0) { goto fail; }
            off = off + 4;
        }
        unsigned int nProc = bitreader_read_bits(br, 6);
        *(unsigned int *)(sub + 0xc) = nProc + 1;
        off = 0;
        for (int i = 0; i < *(int *)(sub + 0xc); i = i + 1) {
            unsigned int type = bitreader_read_bits(br, 0x10);
            int slot = sub + off;
            *(unsigned int *)(slot + 0x120) = type;
            int t = *(int *)(slot + 0x120);
            if ((t < 0) || (0 < t)) { goto fail; }
            int obj = (**(int (**)(int, int *))((int)PTR_PTR_80610b10[t] + 4))(state, br);
            *(int *)(slot + 0x220) = obj;
            if (*(int *)(slot + 0x220) == 0) { goto fail; }
            off = off + 4;
        }
        unsigned int nSlot = bitreader_read_bits(br, 6);
        *(unsigned int *)(sub + 8) = nSlot + 1;
        int off2 = 0;
        for (int i = 0; i < *(int *)(sub + 8); i = i + 1) {
            int rec = FUN_801a1498(1, 0x10);
            int slotOff = off2 + 0x20;
            *(int *)(sub + slotOff) = rec;
            unsigned int f = bitreader_read_bits(br, 1);
            **(unsigned int **)(sub + slotOff) = f;
            unsigned int a = bitreader_read_bits(br, 0x10);
            *(unsigned int *)(*(int *)(sub + slotOff) + 4) = a;
            unsigned int b = bitreader_read_bits(br, 0x10);
            *(unsigned int *)(*(int *)(sub + slotOff) + 8) = b;
            unsigned int c = bitreader_read_bits(br, 8);
            *(unsigned int *)(*(int *)(sub + slotOff) + 0xc) = c;
            int r = *(int *)(sub + slotOff);
            if ((0 < *(int *)(r + 4)) || (0 < *(int *)(r + 8)) ||
                (*(int *)(sub + 0xc) <= *(int *)(r + 0xc))) { goto fail; }
            off2 = off2 + 4;
        }
        unsigned int endMarker = bitreader_read_bits(br, 1);
        if (endMarker != 1) {
fail:
            anim_subctx_destroy();
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_stream_dispatch_block (FUN_80197e00)
// Dispatch one stream block: reseat the bit cursor over the block bytes, read a
// 1-byte type tag, verify the 6-byte magic (compared against 0x8061...0b00 via
// FUN_80387b68), then route by tag: 3 -> codebook parse (if codebooks armed),
// 1 -> header parse (if a header slot exists and none armed yet), 5 -> component
// table parse (if codebooks + component present). Uses the __save_gpr_28 frame.
// ---------------------------------------------------------------------------
void anim_stream_dispatch_block(unsigned int arg0, unsigned int arg1, unsigned int *pBlock) {
    unsigned long long pair = (unsigned long long)__save_gpr_28();
    int state = (int)((unsigned long long)pair >> 0x20);
    unsigned char magic[8];
    int br[12];
    if (pBlock != (unsigned int *)0) {
        bitwriter_reset(br, pBlock[0], pBlock[1]);
        unsigned int tag = bitreader_read_bits(br, 8);
        memset_wrapper((int)magic, 0, 6);
        anim_read_bytes(br, (int)magic, 6);
        int cmp = FUN_80387b68((int)magic, -0x7f9ef500, 6);
        if (cmp == 0) {
            if (tag == 3) {
                if (*(int *)(state + 8) != 0) {
                    anim_stream_parse_scratch();
                }
            } else if ((int)tag < 3) {
                if ((tag == 1) && (pBlock[2] != 0) && (*(int *)(state + 8) == 0)) {
                    anim_stream_parse_header();
                }
            } else if ((tag == 5) && (*(int *)(state + 8) != 0) &&
                       (*(int *)((int)pair + 0xc) != 0)) {
                anim_stream_parse_codebooks();
            }
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// FUN_80197f58 (weighted polynomial sample eval)
// For a run of `count` samples selected out of the state's index array, evaluate
// a product-of-differences polynomial in the sample position (built from the
// per-frame weight vector), map it through the exp/log LUT primitives and an
// exp2 (anim_lut_lookup_fe30 + FUN_801982ac / anim_lut_interp_* / FUN_8039046c),
// scale by the incoming base weight, and multiply it into the output run in
// contiguous groups sharing the same index key. The in_f26..in_f31 / in_ps*
// values are callee-saved FP registers Ghidra could not source; they feed only
// the (decompiler-removed) unreachable spill blocks, so they are seeded to 0.
// ---------------------------------------------------------------------------
void FUN_80197f58(double a, double b, int out, int table, int count,
                  unsigned int dim, float *weights, unsigned int idx) {
    (void)a; (void)table; (void)weights; (void)idx;
    // callee-saved FP spill slots (unused in the reachable path)
    double in_f26 = 0, in_f27 = 0, in_f28 = 0, in_f29 = 0, in_f30 = 0, in_f31 = 0;
    double in_ps26_1 = 0, in_ps27_1 = 0, in_ps28_1 = 0, in_ps29_1 = 0, in_ps30_1 = 0, in_ps31_1 = 0;
    float local_8  = (float)in_f31,  fStack_4  = (float)in_ps31_1;
    float local_18 = (float)in_f30,  fStack_14 = (float)in_ps30_1;
    float local_28 = (float)in_f29,  fStack_24 = (float)in_ps29_1;
    float local_38 = (float)in_f28,  fStack_34 = (float)in_ps28_1;
    float local_48 = (float)in_f27,  fStack_44 = (float)in_ps27_1;
    float local_58 = (float)in_f26,  fStack_54 = (float)in_ps26_1;
    (void)local_8; (void)fStack_4; (void)local_18; (void)fStack_14;
    (void)local_28; (void)fStack_24; (void)local_38; (void)fStack_34;
    (void)local_48; (void)fStack_44; (void)local_58; (void)fStack_54;

    unsigned long long pair = FUN_803826cc();
    int idxArr = (int)((unsigned long long)pair >> 0x20);  // sample index array (r3 hi)
    int outState = (int)pair;                              // per-sample state (r4 lo)

    double dScale = (double)(FLOAT_80616990 /
                    (float)((double)CONCAT44(0x43300000, dim ^ 0x80000000) - DOUBLE_80616968));
    double dBase = b;   // incoming base weight (double arg)

    int off = 0;
    for (int s = 0; s < (int)idx; s = s + 1) {
        double interp = anim_lut_interp_fb28((double)*(float *)((int)weights + off));
        *(float *)((int)weights + off) = (float)interp;
        off = off + 4;
    }

    int done = 0;
    int keyOff = 0;
    int outOff = 0;
    double dMagic = DOUBLE_80616968;
    while (done < count) {
        unsigned int key = *(unsigned int *)((int)outState + keyOff);
        double acc0 = (double)FLOAT_80616994;
        double acc1 = acc0;
        double pos = anim_lut_interp_fb28(
            (double)(float)(dScale * (double)(float)((double)CONCAT44(0x43300000, key ^ 0x80000000) - dMagic)));

        float *pw = weights;
        int half = (int)idx >> 1;
        do {
            float w0 = *pw;
            half = half + -1;
            float *pw1 = pw + 1;
            pw = pw + 2;
            acc0 = (double)(float)(acc0 * (double)(float)((double)w0 - pos));
            acc1 = (double)(float)(acc1 * (double)(float)((double)*pw1 - pos));
        } while (half != 0);

        double tailD;
        float tailF;
        if ((idx & 1) == 0) {
            tailD = (double)(float)((double)FLOAT_80616988 - pos);
            tailF = (float)(acc0 * (double)(float)(acc0 * (double)(float)((double)FLOAT_80616988 + pos)));
        } else {
            tailD = -(double)(float)(pos * pos - (double)FLOAT_80616988);
            float t = (float)(acc0 * (double)(float)((double)*pw - pos));
            tailF = t * t;
        }

        int expOut;
        double mant = FUN_8039046c((double)((float)(acc1 * (double)(float)(acc1 * tailD)) + tailF), &expOut);
        double m = (double)(float)mant;
        double luk = anim_lut_lookup_fe30(expOut + (int)idx);
        m = anim_lut_interp_fd2c(m);
        double scaled = FUN_801982ac((double)(float)((double)(float)(dBase * m) * luk - a));
        do {
            done = done + 1;
            keyOff = keyOff + 4;
            *(float *)(idxArr + outOff) = (float)((double)*(float *)(idxArr + outOff) * scaled);
            outOff = outOff + 4;
        } while (key == *(unsigned int *)((int)outState + keyOff));
    }
    FUN_80382718();
}

// ---------------------------------------------------------------------------
// anim_round_to_int (FUN_80198168)
// Round a double to int by adding the magic bias (2^52-style) and truncating.
// ---------------------------------------------------------------------------
int anim_round_to_int(double x) {
    return (int)(DOUBLE_80616960 + x);
}

// ---------------------------------------------------------------------------
// anim_lut_interp_fb28 (FUN_80198188)
// Piecewise-linear interpolation over the exp-style LUT at 0x8041fb28: scale the
// input, split into integer bucket (anim_round_to_int) + fraction, and lerp
// between adjacent table entries.
// ---------------------------------------------------------------------------
double anim_lut_interp_fb28(double x) {
    double scaled = DOUBLE_80616970 * x;
    unsigned int i = (unsigned int)anim_round_to_int(scaled - DOUBLE_80616960);
    return (double)(float)((scaled - ((double)CONCAT44(0x43300000, i ^ 0x80000000) - DOUBLE_80616968)) *
                           (double)(float)((double)*(float *)(&DAT_8041fb2c + i * 4) -
                                           (double)*(float *)(&DAT_8041fb28 + i * 4)) +
                           (double)*(float *)(&DAT_8041fb28 + i * 4));
}

// ---------------------------------------------------------------------------
// anim_lut_interp_fd2c (FUN_8019820c)
// Piecewise-linear interpolation over the log-style LUT at 0x8041fd2c: affine
// input transform, integer bucket + fraction, lerp between adjacent entries.
// ---------------------------------------------------------------------------
double anim_lut_interp_fd2c(double x) {
    double scaled = (double)(float)((double)FLOAT_80616978 * x - (double)FLOAT_8061697c);
    unsigned int i = (unsigned int)anim_round_to_int(scaled - DOUBLE_80616960);
    return (double)(float)((scaled - ((double)CONCAT44(0x43300000, i ^ 0x80000000) - DOUBLE_80616968)) *
                           (double)(float)((double)*(float *)(&DAT_8041fd30 + i * 4) -
                                           (double)*(float *)(&DAT_8041fd2c + i * 4)) +
                           (double)*(float *)(&DAT_8041fd2c + i * 4));
}

// ---------------------------------------------------------------------------
// anim_lut_lookup_fe30 (FUN_80198294)
// Direct float lookup into the exp2 coefficient LUT at 0x8041fe30 by index.
// ---------------------------------------------------------------------------
double anim_lut_lookup_fe30(int i) {
    return (double)*(float *)(&DAT_8041fe30 + i * 4);
}

// ===========================================================================
// batch_515 (0x801982ac - 0x801998f0): the exp2 LUT tail, sub-context small
// helpers, the codebook-descriptor write/read pair, the two big per-block
// encode / decode drivers (FUN_80198760 / FUN_8019916c), an int->float dequant
// leaf, the FFT twiddle-table builder (FUN_801995cc) and a radix-2 butterfly.
// The encode/decode drivers allocate several per-channel scratch runs on the
// stack sized by the runtime channel count; the decompiler modelled these as
// raw stack-pointer arithmetic (&stack.. + n, register0x00000004). They are
// reconstructed here with _alloca, which is the faithful stack allocation.
// ===========================================================================
#include <malloc.h>   // _alloca for the runtime-sized per-channel scratch runs

extern "C" {
// exp2 LUT split-table + clamp constants
extern float  FLOAT_80616980;    // exp2 input scale
extern float  FLOAT_80616984;    // exp2 input bias
extern float  FLOAT_8061698c;    // exp2 overflow clamp (large)
extern float  DAT_8041feb4;      // exp2 coarse LUT (>>5 index)
extern float  DAT_8041ff40;      // exp2 fine LUT (&0x1f index)

// int->float dequant (FUN_8019958c) constants
extern float  FLOAT_80616998;    // dequant scale
extern float  FLOAT_8061699c;    // dequant bias
extern double DOUBLE_806169a0;   // int->double magic (2^52)

// encode-driver magnitude constants
extern float  FLOAT_806169a8;    // log-magnitude scale
extern float  FLOAT_806169ac;    // secondary magnitude scale
extern float  FLOAT_806169b0;    // magnitude ceiling clamp

// FFT twiddle-builder constants (FUN_801995cc)
extern double DOUBLE_806169b8;   // ln(2)-ish (log base for stage count)
extern double DOUBLE_806169c8;   // 1/sqrt(2)-ish stage scale
extern double DOUBLE_806169d8;   // int->double magic (2^52)
extern float  FLOAT_806169c0;    // -2*pi (angle step numerator)
extern float  FLOAT_806169d0;    // reciprocal-N scale

// scalar transcendentals used by the twiddle builder
extern double FUN_80390b04(double x);   // log
extern double FUN_80390250(double x);   // cos
extern double FUN_80390914(double x);   // sin

// per-channel encode/decode cross-module callees (component pipeline)
void anim_build_weight_window(int out, int table, int codes, unsigned int a, int gate, unsigned int b); // weight window (defined batch_523 below); // weight window (defined batch_523 below)
void          anim_rfft_forward(int a, float *w, int out);                   // FUN_8019a5dc (defined batch_516 below)
void          anim_fft_transform(int *plan, float *w);                             // FFT plan run (defined batch_523 below)efined batch_523 below)efined batch_523 below) batch_523)
void          FUN_8019c7b4(double d0, double d1, double d2, double d3, int *pipe, int in, int scratch); // spectral transform (defined batch_518 below)
void          FUN_8019c904(double d0, double d1, int *pipe, int keys, int scratch);                     // magnitude threshold (defined batch_518 below)
void          FUN_8019ca00(int *pipe, int a, int b, int mode, int keys);                                // channel select (defined batch_518 below)
void          FUN_8019c73c(int *pipe, int a, int enc, int keys, int cb);                                // encode commit (defined batch_518 below)
extern void   FUN_8019d14c(int *pipe, int a, int b, int out);                                            // reconstruct commit
extern int    FUN_8019cd7c(int *st, int a, int *pipe, int desc, int scratch);                           // aux-track fit
extern int    FUN_8019ced0(int *st, int *pipe, int desc, int prev);                                     // aux-track fit 2
extern void   FUN_8019d328(unsigned int idx, int a, int *pipe, int desc, int st, int f0, int f1, int e, int p2, int p3); // aux emit
extern void   FUN_8019d03c(int *pipe, int a, int out);                                                  // decode commit
void          anim_ifft_real(int a, int buf, float *w);                                                 // FUN_8019a2b4 (defined batch_516 below)
extern void   bitwriter_align_byte(int bw);
extern int    bitreader_byte_len(int *br);
extern bool   msg_ctx_has_pending(int st);
} // extern "C"

// Forward decls for this batch's own helpers used before their definitions.
double anim_dequant_i2f(unsigned int *pv);   // FUN_8019958c

// ---------------------------------------------------------------------------
// FUN_801982ac (exp2-style scalar via split coarse/fine LUT)
// exp2 approximation: bucket the scaled input, clamp to [1.0, big], and combine
// the coarse (>>5) and fine (&0x1f) LUT factors from 0x8041feb4 / 0x8041ff40.
// ---------------------------------------------------------------------------
double FUN_801982ac(double x) {
    unsigned int i = (unsigned int)anim_round_to_int(
        (double)(float)((double)FLOAT_80616980 * x - (double)FLOAT_80616984));
    double r;
    if ((int)i < 0) {
        r = (double)FLOAT_80616988;   // 1.0 underflow floor
    } else if ((int)i < 0x460) {
        r = (double)(*(float *)(&DAT_8041feb4 + ((int)i >> 5) * 4) *
                     *(float *)(&DAT_8041ff40 + (i & 0x1f) * 4));
    } else {
        r = (double)FLOAT_8061698c;   // overflow ceiling
    }
    return r;
}

// ---------------------------------------------------------------------------
// anim_desc_free_0xc88 (FUN_80198320)
// Zero and free a 0xc88-byte channel descriptor object.
// ---------------------------------------------------------------------------
void anim_desc_free_0xc88(int pObj) {
    if (pObj != 0) {
        memset_wrapper(pObj, 0, 0xc88);
        FUN_801a14dc(pObj);
    }
}

// ---------------------------------------------------------------------------
// anim_bits_for_index (FUN_80198360)
// Bits needed to index `n` values = ceil(log2(n)) = bit length of (n-1); 0 for
// n <= 1. (Another separately-emitted copy of the bits_for_count leaf.)
// ---------------------------------------------------------------------------
int anim_bits_for_index(unsigned int n) {
    int bits = 0;
    if (n != 0) { n = n - 1; }
    for (; n != 0; n = n >> 1) { bits = bits + 1; }
    return bits;
}

// ---------------------------------------------------------------------------
// anim_desc_write (FUN_8019838c)
// Serialize a channel descriptor to the bit-writer: dimension count (1b flag +
// optional 4b), a pair-list (1b flag + 8b count, then index pairs sized by
// bits_for_index of the channel count), a 2-bit reserved zero, per-channel
// symbol widths (4b), and per-channel two 8-bit builder/secondary selectors.
// state/desc pair via the register-save helper FUN_803826e4.
// ---------------------------------------------------------------------------
void anim_desc_write(unsigned int arg0, unsigned int arg1, int bw) {
    unsigned long long pair = FUN_803826e4();
    int ctx = (int)((unsigned long long)pair >> 0x20);
    int *desc = (int *)pair;

    if (*desc < 2) {
        bitwriter_write_bits(bw, 0, 1);
    } else {
        bitwriter_write_bits(bw, 1, 1);
        bitwriter_write_bits(bw, *desc + -1, 4);
    }

    if (desc[0x121] < 1) {
        bitwriter_write_bits(bw, 0, 1);
    } else {
        bitwriter_write_bits(bw, 1, 1);
        bitwriter_write_bits(bw, desc[0x121] + -1, 8);
        int off = 0;
        for (int i = 0; i < desc[0x121]; i = i + 1) {
            int w = anim_bits_for_index(*(unsigned int *)(ctx + 4));
            bitwriter_write_bits(bw, *(unsigned int *)((int)desc + off + 0x488), w);
            w = anim_bits_for_index(*(unsigned int *)(ctx + 4));
            bitwriter_write_bits(bw, *(unsigned int *)((int)desc + off + 0x888), w);
            off = off + 4;
        }
    }

    bitwriter_write_bits(bw, 0, 2);
    if (1 < *desc) {
        int off = 0;
        for (int i = 0; i < *(int *)(ctx + 4); i = i + 1) {
            bitwriter_write_bits(bw, *(unsigned int *)((int)desc + off + 4), 4);
            off = off + 4;
        }
    }
    int off = 0;
    for (int i = 0; i < *desc; i = i + 1) {
        bitwriter_write_bits(bw, 0, 8);
        bitwriter_write_bits(bw, *(unsigned int *)((int)desc + off + 0x404), 8);
        bitwriter_write_bits(bw, *(unsigned int *)((int)desc + off + 0x444), 8);
        off = off + 4;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// anim_desc_read (FUN_80198544)
// Inverse of anim_desc_write: allocate a 0xc88 descriptor, read dimension count,
// pair-list, per-channel symbol widths and builder/secondary selectors, each
// validated against the sub-context bounds; frees the descriptor
// (anim_desc_free_0xc88) on any malformation. Pair via FUN_803826dc.
// ---------------------------------------------------------------------------
void anim_desc_read(void) {
    unsigned long long pair = FUN_803826dc();
    int ctx = (int)((unsigned long long)pair >> 0x20);
    int *br = (int *)pair;
    int *desc = (int *)FUN_801a1498(1, 0xc88);
    int sub = *(int *)(ctx + 0x1c);
    memset_wrapper((int)desc, 0, 0xc88);

    unsigned int f = bitreader_read_bits(br, 1);
    if (f == 0) {
        *desc = 1;
    } else {
        unsigned int n = bitreader_read_bits(br, 4);
        *desc = n + 1;
    }

    f = bitreader_read_bits(br, 1);
    if (f != 0) {
        unsigned int cnt = bitreader_read_bits(br, 8);
        desc[0x121] = cnt + 1;
        int off = 0;
        for (int i = 0; i < desc[0x121]; i = i + 1) {
            int w = anim_bits_for_index(*(unsigned int *)(ctx + 4));
            unsigned int a = bitreader_read_bits(br, w);
            *(unsigned int *)((int)desc + off + 0x488) = a;
            w = anim_bits_for_index(*(unsigned int *)(ctx + 4));
            unsigned int b = bitreader_read_bits(br, w);
            *(unsigned int *)((int)desc + off + 0x888) = b;
            if (((int)a < 0) || ((int)b < 0) || (a == b) ||
                (*(int *)(ctx + 4) <= (int)a) || (*(int *)(ctx + 4) <= (int)b)) {
                goto fail;
            }
            off = off + 4;
        }
    }

    {
        unsigned int mode = bitreader_read_bits(br, 2);
        if ((int)mode < 1) {
            if (1 < *desc) {
                int off = 0;
                for (int i = 0; i < *(int *)(ctx + 4); i = i + 1) {
                    unsigned int w = bitreader_read_bits(br, 4);
                    *(unsigned int *)((int)desc + off + 4) = w;
                    if (*desc <= *(int *)((int)desc + off + 4)) { goto fail; }
                    off = off + 4;
                }
            }
            int off = 0;
            for (int i = 0; i < *desc; i = i + 1) {
                bitreader_read_bits(br, 8);
                unsigned int b = bitreader_read_bits(br, 8);
                *(unsigned int *)((int)desc + off + 0x404) = b;
                if (*(int *)(sub + 0x10) <= *(int *)((int)desc + off + 0x404)) { goto fail; }
                unsigned int s = bitreader_read_bits(br, 8);
                *(unsigned int *)((int)desc + off + 0x444) = s;
                if (*(int *)(sub + 0x14) <= *(int *)((int)desc + off + 0x444)) { goto fail; }
                off = off + 4;
            }
        } else {
fail:
            anim_desc_free_0xc88((int)desc);
        }
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// anim_block_encode (FUN_80198760)
// Per-block channel-encode driver. For every channel: build the spectral
// magnitude envelope (dequant + FUN_801a10c4/8019a5dc/801a0df8 + running max),
// fit the primary curve (anim_curve_fit_recursive); when the message context has
// a pending alternate, fit the two child curves and produce the 7-step +
// 7-step blended interpolation ladder (anim_curve_blend_frames). Then for each
// of the 7 (or 14, when pending) sub-frames, write the channel headers, encode
// each channel (anim_channel_encode) + its component contributions through the
// secondary-builder vtable, byte-align, and record the sub-frame byte length.
// The per-channel scratch runs are _alloca'd, sized by the runtime channel
// count (the decompiler's &stack.. pointer chain). in_f26..f31 are unsourced
// callee-saved FP registers, seeded to 0.
// ---------------------------------------------------------------------------
void anim_block_encode(unsigned long long param_1, double param_2, double param_3, double param_4) {
    (void)param_1;
    double in_f26 = 0, in_f27 = 0, in_f28 = 0, in_f29 = 0, in_f30 = 0, in_f31 = 0;
    double in_ps26_1 = 0, in_ps27_1 = 0, in_ps28_1 = 0, in_ps29_1 = 0, in_ps30_1 = 0, in_ps31_1 = 0;
    (void)in_f26; (void)in_f27; (void)in_f28; (void)in_f29; (void)in_f30; (void)in_f31;
    (void)in_ps26_1; (void)in_ps27_1; (void)in_ps28_1; (void)in_ps29_1; (void)in_ps30_1; (void)in_ps31_1;

    int *st = (int *)FUN_803826b4();
    int envMax = st[0x1a];
    int desc = *(int *)(st[0x10] + 4);        // per-block descriptor (channel count at +4)
    int pipeRoot = *(int *)(st[0x10] + 0x68);
    unsigned int frames = st[9];
    int sub = *(int *)(desc + 0x1c);
    int chCount = *(int *)(desc + 4);

    // per-channel run pointer arrays (runtime-sized; were raw stack pointers)
    float **maxRow = (float **)_alloca(((chCount * 4 + 7) & ~7));
    int   *envRuns = (int *)_alloca(chCount * 4);   // local_dc
    int   *encRuns = (int *)_alloca(chCount * 4);   // local_e0
    int   *fitRuns = (int *)_alloca(chCount * 4);   // iVar6
    (void)maxRow;

    int frameCount = ((int)frames >> 1) + (unsigned int)((int)frames < 0 && (frames & 1) != 0);
    int runBytes = frameCount * 4;

    double dMax = (double)*(float *)(envMax + 4);
    int layer = st[7];
    int *pipeCh = *(int **)(sub + layer * 4 + 0x220);
    st[10] = layer;
    int *pipe = (int *)(pipeRoot + *(int *)(pipeRoot + 0x38) + 0);  // resolved below per raw
    pipe = (int *)(*(int *)(pipeRoot + 0x38) + *(int *)(envMax + 8) * 0x30 +
                   (-(unsigned int)(layer != 0) & 2) * 0x30);

    double dScaleA = (double)FLOAT_806169ac;
    double dScaleB = (double)FLOAT_806169a8;
    double dCeil = (double)FLOAT_806169b0;
    unsigned int frBias = frames ^ 0x80000000;
    double dMagic = DOUBLE_806169a0;

    // --- pass 1: build per-channel magnitude envelope + running max ---
    int chOff = 0;
    for (int c = 0; c < chCount; c = c + 1) {
        float local_110[3];
        local_110[2] = 176.0f;
        local_110[1] = (float)(dScaleB / (double)(float)((double)CONCAT44(0x43300000, frBias) - dMagic));
        float *pw = *(float **)(*st + chOff);
        int row = msg_arena_alloc((int)st, runBytes);
        envRuns[c] = row;
        double base = anim_dequant_i2f((unsigned int *)(local_110 + 1));
        FUN_801a10c4((int)pw, pipeRoot + 4, sub, st[6], st[7], st[8]);
        anim_rfft_forward(**(int **)(pipeRoot + st[7] * 4 + 0xc), pw, envRuns[c]);
        FUN_801a0df8((int *)(pipeRoot + st[7] * 0xc + 0x14), pw);
        double first = anim_dequant_i2f((unsigned int *)pw);
        *pw = (float)(base + first);
        float rowMax = *pw;
        int idx = 4;
        for (int k = 1; k < (int)frames - 1; k = k + 2) {
            float re = *(float *)((int)pw + idx);
            float im = ((float *)((int)pw + idx))[1];
            float mag = re * re + im * im;
            double m = anim_dequant_i2f((unsigned int *)&mag);
            mag = (float)(dScaleA * m + base);
            pw[(k + 1) >> 1] = mag;
            if ((double)rowMax < (double)mag) { rowMax = mag; }
            idx = idx + 8;
        }
        if (dCeil < (double)rowMax) { rowMax = (float)dCeil; }
        if (dMax < (double)rowMax) { dMax = (double)rowMax; }
        chOff = chOff + 4;
    }

    // --- pass 2: per-channel curve fit + blend ladder ---
    int scratchA = msg_arena_alloc((int)st, runBytes);
    int scratchB = msg_arena_alloc((int)st, runBytes);
    chOff = 0;
    for (int c = 0; c < chCount; c = c + 1) {
        int chIdx = *(int *)((int)pipeCh + chOff + 4);
        int env = envRuns[c];
        int keys = *(int *)(*st + chOff);
        int keysMax = keys + runBytes;
        st[10] = layer;
        int fit = msg_arena_alloc((int)st, 0x3c);
        fitRuns[c] = fit;
        memset_wrapper(fitRuns[c], 0, 0x3c);
        int off = 0;
        double dq = 0;
        for (int f = 0; f < frameCount; f = f + 1) {
            dq = anim_dequant_i2f((unsigned int *)(env + off));
            *(float *)(keysMax + off) = (float)dq;
            off = off + 4;
        }
        FUN_8019c7b4(dq, param_2, param_3, param_4, pipe, keysMax, scratchA);
        param_2 = (double)*(float *)(scratchA);  // note: raw reloads puVar4[..]; kept as feed-forward
        FUN_8019c904(dMax, param_2, pipe, keys, scratchB);
        FUN_8019ca00(pipe, scratchA, scratchB, 1, keys);
        if (*(int *)(sub + pipeCh[chIdx + 0x101] * 4 + 800) != 1) { goto done; }
        int fitPtr = anim_curve_fit_recursive((unsigned int)(int)st,
                        *(unsigned int *)(*(int *)(pipeRoot + 0x30) + pipeCh[chIdx + 0x101] * 4),
                        keysMax, keys);
        *(int *)(fitRuns[c] + 0x1c) = fitPtr;
        if (msg_ctx_has_pending((int)st) && (*(int *)(fitRuns[c] + 0x1c) != 0)) {
            FUN_8019ca00(pipe, scratchA, scratchB, 2, keys);
            int fit2 = anim_curve_fit_recursive((unsigned int)(int)st,
                        *(unsigned int *)(*(int *)(pipeRoot + 0x30) + pipeCh[chIdx + 0x101] * 4),
                        keysMax, keys);
            *(int *)(fitRuns[c] + 0x38) = fit2;
            FUN_8019ca00(pipe, scratchA, scratchB, 0, keys);
            int fit0 = anim_curve_fit_recursive((unsigned int)(int)st,
                        *(unsigned int *)(*(int *)(pipeRoot + 0x30) + pipeCh[chIdx + 0x101] * 4),
                        keysMax, keys);
            *(int *)fitRuns[c] = fit0;
            int step = 1, slot = 4;
            do {
                int blended = anim_curve_blend_frames((unsigned int)(int)st,
                                *(unsigned int *)(*(int *)(pipeRoot + 0x30) + pipeCh[chIdx + 0x101] * 4),
                                *(int *)fitRuns[c], ((int *)fitRuns[c])[7], (step << 0x10) / 7);
                step = step + 1;
                *(int *)(fitRuns[c] + slot) = blended;
                slot = slot + 4;
            } while (step < 7);
            step = 8; slot = 0x20;
            do {
                int blended = anim_curve_blend_frames((unsigned int)(int)st,
                                *(unsigned int *)(*(int *)(pipeRoot + 0x30) + pipeCh[chIdx + 0x101] * 4),
                                *(int *)(fitRuns[c] + 0x1c), *(int *)(fitRuns[c] + 0x38),
                                ((step + -7) * 0x10000) / 7);
                step = step + 1;
                *(int *)(fitRuns[c] + slot) = blended;
                slot = slot + 4;
            } while (step < 0xe);
        }
        chOff = chOff + 4;
    }
    *(float *)(envMax + 4) = (float)dMax;

    // per-block output scratch runs (alloca'd, channel-count sized)
    int  *presentRow = (int *)_alloca(chCount * 4);   // puVar19
    float **ptrRow   = (float **)_alloca(chCount * 4);// puVar20 (pair-list value ptrs)
    int  *idxRow     = (int *)_alloca(chCount * 4);    // puVar3 (+0x10)
    int  *finalRow   = (int *)_alloca(chCount * 4);    // puVar17
    (void)presentRow; (void)ptrRow; (void)idxRow;

    if (pipeCh[0x121] != 0) {
        int f0 = FUN_8019cd7c(st, sub + 0xb34, pipe, (int)pipeCh, (int)envRuns);
        int f1 = FUN_8019ced0(st, pipe, (int)pipeCh, f0);
        (void)f1;
    }
    memset_wrapper((int)finalRow, 0, chCount * 4);
    if (*(int *)(((int *)pipe)[1] + 500) != 0) {
        int off = 0;
        for (int c = 0; c < chCount; c = c + 1) {
            int keys = *(int *)((int)envRuns + off);
            finalRow[c] = msg_arena_alloc((int)st, runBytes);
            FUN_8019d03c(pipe, keys, finalRow[c]);
            off = off + 4;
        }
    }

    bool pending = msg_ctx_has_pending((int)st);
    unsigned int sf = ~-(unsigned int)pending & 7;
    int sfOff = sf << 2;
    int layerId = layer;
    int **secVt = (int **)PTR_DAT_80425f40;
    while (true) {
        pending = msg_ctx_has_pending((int)st);
        int limit = pending ? 0xe : 7;
        if (limit < (int)sf) break;
        bitwriter_write_bits((int)(st + 1), 0, 1);
        bitwriter_write_bits((int)(st + 1), layerId, *(int *)(pipeRoot + 0x2c));
        if (st[7] != 0) {
            bitwriter_write_bits((int)(st + 1), st[6], 1);
            bitwriter_write_bits((int)(st + 1), st[8], 1);
        }
        int subBase = sub + sfOff;
        int off = 0;
        for (int c = 0; c < chCount; c = c + 1) {
            int chIdx = *(int *)((int)pipeCh + off + 4);
            int env = *(int *)((int)envRuns + off);
            int keys = *(int *)(*st + off);
            int enc = msg_arena_alloc((int)st, runBytes);
            encRuns[c] = enc;
            int encoded = anim_channel_encode((unsigned int)(int)st,
                            *(unsigned int *)(*(int *)(pipeRoot + 0x30) + pipeCh[chIdx + 0x101] * 4),
                            *(int **)(sfOff + fitRuns[c]), enc);
            envRuns[c] = encoded;   // reuse row slot for the encode flag (raw stored into iVar24)
            FUN_8019c73c(pipe, env, enc, keys, *(int *)(subBase + st[7] * 0x3c + 0xca8));
            FUN_8019d14c(pipe, keys, keys + runBytes, finalRow[c]);
            off = off + 4;
        }
        if (pipeCh[0x121] != 0) {
            int f0 = FUN_8019cd7c(st, sub + 0xb34, pipe, (int)pipeCh, (int)envRuns);
            int f1 = FUN_8019ced0(st, pipe, (int)pipeCh, f0);
            FUN_8019d328(sf, sub + 0xb34, pipe, (int)pipeCh, *st, f0, f1, (int)encRuns,
                         (int)envRuns, *(int *)(sub + st[7] * 0x3c + sfOff + 0xca8));
        }
        // per pair-group: emit the secondary-builder component contributions
        int grpOff = 0;
        for (int g = 0; g < *pipeCh; g = g + 1) {
            int nIn = 0, k = 0, kk = 0;
            int sel = *(int *)((int)pipeCh + grpOff + 0x444);
            int off2 = 0;
            for (int c = 0; c < chCount; c = c + 1) {
                if (*(int *)((int)pipeCh + off2 + 4) == g) {
                    presentRow[k >> 2] = (envRuns[c] != 0) ? 1 : 0;
                    nIn = nIn + 1;
                    idxRow[k >> 2] = *(int *)(*st + off2);
                    ptrRow[k >> 2] = (float *)(*(int *)(*st + off2) + runBytes);
                    k = k + 4;
                }
                off2 = off2 + 4;
            }
            int selOff = sel * 4;
            int vtIdx = *(int *)(sub + selOff + 0x520);
            int r = (**(int (**)(int *, unsigned int, float **, int *, int))((int)secVt[vtIdx] + 0x14))
                        (st, *(unsigned int *)(*(int *)(pipeRoot + 0x34) + selOff), ptrRow, presentRow, nIn);
            (**(void (**)(int *, unsigned int, float **, int, int *, int, int))((int)secVt[vtIdx] + 0x18))
                (st, *(unsigned int *)(*(int *)(pipeRoot + 0x34) + selOff), ptrRow, 0, presentRow, nIn, r);
            (void)kk;
            grpOff = grpOff + 4;
        }
        bitwriter_align_byte((int)(st + 1));
        int len = bitreader_byte_len(st + 1);
        int lenOff = sfOff + 0xc;
        sfOff = sfOff + 4;
        sf = sf + 1;
        *(int *)(envMax + lenOff) = len;
    }
done:
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// anim_block_decode (FUN_8019916c)
// Per-block channel-decode driver. Runs the builder-vtable decode for each
// channel (producing a presence flag), unions presence across pair-list links,
// runs each secondary-builder decode for the channel groups, applies the
// pair-list mid/side reconstruction (add/sub of magnitude pairs gated by the
// FLOAT_806169b0 threshold), then invokes the builder finalize vtable slot and
// the per-channel finalize (FUN_8019a2b4). Scratch runs are _alloca'd by the
// runtime channel count. Pair via FUN_803826c0.
// ---------------------------------------------------------------------------
void anim_block_decode(void) {
    unsigned long long pair = FUN_803826c0();
    int *st = (int *)((unsigned long long)pair >> 0x20);
    int *pipeCh = (int *)pair;
    int desc = *(int *)(st[0x10] + 4);
    int pipeRoot = *(int *)(st[0x10] + 0x68);
    int sub = *(int *)(desc + 0x1c);
    unsigned int frames = *(unsigned int *)(sub + st[7] * 4);
    st[9] = frames;
    int chCount = *(int *)(desc + 4);

    int   *objRow     = (int *)_alloca(chCount * 4);   // puVar3 (+8)
    int   *presentRow = (int *)_alloca(chCount * 4);   // puVar4 (+8)
    float **valRow    = (float **)_alloca(chCount * 4);// puVar15
    int   *idxRow     = (int *)_alloca(chCount * 4);   // iVar12 base
    int **buildVt = (int **)PTR_DAT_80610b08;
    int **secVt   = (int **)PTR_DAT_80425f40;

    // --- pass 1: per-channel builder decode + zero output runs ---
    int off = 0;
    for (int c = 0; c < chCount; c = c + 1) {
        int chIdx = pipeCh[*(int *)((int)pipeCh + off + 4) + 0x101];
        int vt = *(int *)(sub + chIdx * 4 + 800);
        int obj = (**(int (**)(int *, unsigned int))((int)buildVt[vt] + 0x14))
                    (st, *(unsigned int *)(*(int *)(pipeRoot + 0x30) + chIdx * 4));
        objRow[c] = obj;
        presentRow[c] = (objRow[c] == 0) ? 0 : 1;
        memset_wrapper(*(int *)(*st + off), 0, (frames & 0x3fffffff) << 1);
        off = off + 4;
    }

    // --- union presence across pair-list links ---
    off = 0;
    for (int i = 0; i < pipeCh[0x121]; i = i + 1) {
        int a = *(int *)((int)pipeCh + off + 0x488);
        int b = *(int *)((int)pipeCh + off + 0x888);
        if ((presentRow[a] != 0) || (presentRow[b] != 0)) {
            presentRow[a] = 1;
            presentRow[b] = 1;
        }
        off = off + 4;
    }

    // --- per group: secondary-builder decode ---
    off = 0;
    for (int g = 0; g < *pipeCh; g = g + 1) {
        int nIn = 0, k = 0;
        int off2 = 0;
        for (int c = 0; c < chCount; c = c + 1) {
            if (*(int *)((int)pipeCh + off2 + 4) == g) {
                presentRow[k >> 2] = (presentRow[c] == 0) ? 0 : 1;
                nIn = nIn + 1;
                idxRow[k >> 2] = *(int *)(*st + off2);
                k = k + 4;
            }
            off2 = off2 + 4;
        }
        int sel = *(int *)((int)pipeCh + off + 0x444) * 4;
        int vtIdx = *(int *)(sub + sel + 0x520);
        (**(void (**)(int *, unsigned int, int *, int *, int))((int)secVt[vtIdx] + 0x1c))
            (st, *(unsigned int *)(*(int *)(pipeRoot + 0x34) + sel), idxRow, presentRow, nIn);
        off = off + 4;
    }

    // --- pair-list mid/side reconstruction (reverse order) ---
    float thr = FLOAT_806169b0;
    int frameCount = ((int)frames >> 1) + (unsigned int)((int)frames < 0 && (frames & 1) != 0);
    int i = pipeCh[0x121] + -1;
    int pairOff = i * 4;
    for (; -1 < i; i = i + -1) {
        int aRun = *(int *)(*st + *(int *)((int)pipeCh + pairOff + 0x488) * 4);
        int bRun = *(int *)(*st + *(int *)((int)pipeCh + pairOff + 0x888) * 4);
        int o = 0;
        int rem = frameCount;
        if (0 < frameCount) {
            do {
                float av = *(float *)(aRun + o);
                float bv = *(float *)(bRun + o);
                if (av <= thr) {
                    if (bv <= thr) {
                        *(float *)(bRun + o) = av;
                        *(float *)(aRun + o) = av - bv;
                    } else {
                        *(float *)(aRun + o) = av;
                        *(float *)(bRun + o) = av + bv;
                    }
                } else if (bv <= thr) {
                    *(float *)(bRun + o) = av;
                    *(float *)(aRun + o) = av + bv;
                } else {
                    *(float *)(aRun + o) = av;
                    *(float *)(bRun + o) = av - bv;
                }
                o = o + 4;
                rem = rem + -1;
            } while (rem != 0);
        }
        pairOff = pairOff + -4;
    }

    // --- builder finalize vtable slot + per-channel finalize ---
    off = 0;
    for (int c = 0; c < chCount; c = c + 1) {
        int chIdx = pipeCh[*(int *)((int)pipeCh + off + 4) + 0x101];
        int vt = *(int *)(sub + chIdx * 4 + 800);
        (**(void (**)(int *, unsigned int, int, int))((int)buildVt[vt] + 0x18))
            (st, *(unsigned int *)(*(int *)(pipeRoot + 0x30) + chIdx * 4),
             objRow[c], *(int *)(*st + off));
        off = off + 4;
    }
    off = 0;
    for (int c = 0; c < chCount; c = c + 1) {
        anim_ifft_real(**(int **)(pipeRoot + st[7] * 4 + 0xc), *(int *)(*st + off),
                     (float *)*(int *)(*st + off));
        off = off + 4;
    }
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// anim_dequant_i2f (FUN_8019958c)
// Dequantize a 31-bit integer magnitude to float: (int->double via the 0x43300000
// magic) * scale + bias. The top bit is masked off (sign/flag).
// ---------------------------------------------------------------------------
double anim_dequant_i2f(unsigned int *pv) {
    return (double)((float)((double)CONCAT44(0x43300000, (*pv & 0x7fffffff) ^ 0x80000000) -
                            DOUBLE_806169a0) * FLOAT_80616998 + FLOAT_8061699c);
}

// ---------------------------------------------------------------------------
// anim_fft_build_twiddles (FUN_801995cc)
// Build the FFT twiddle-factor + bit-reversal tables for a radix-2 transform of
// size N (from the register pair): compute the stage count (log2 N via log), fill
// the cos/-sin twiddle table (halves at N/2 and N), the second-stage scaled
// twiddles (N/8), and the bit-reversal permutation; store table pointers + a
// reciprocal-N scale into the descriptor. Pair via FUN_803826c0.
// ---------------------------------------------------------------------------
void anim_fft_build_twiddles(void) {
    unsigned long long pair = FUN_803826c0();
    unsigned int *desc = (unsigned int *)(pair >> 0x20);
    unsigned int n = (unsigned int)pair;
    int quarter = ((int)n >> 2) + (unsigned int)((int)n < 0 && (pair & 3) != 0);
    unsigned int brTable = (unsigned int)FUN_801a13e0(quarter * 4);
    unsigned int twTable = (unsigned int)FUN_801a13e0((n + quarter) * 4);

    double lg2 = (double)FUN_80390b04(DOUBLE_806169b8);
    double lgN = (double)FUN_80390b04((double)(float)((double)CONCAT44(0x43300000, n ^ 0x80000000) - DOUBLE_806169d8));
    lgN = FUN_803907b8(lgN / lg2);
    double dMagic = DOUBLE_806169d8;
    unsigned int stages = (unsigned int)lgN;
    double dAngle = (double)FLOAT_806169c0;

    desc[1] = stages;
    desc[0] = n;
    desc[2] = twTable;
    desc[3] = brTable;

    int t2 = 0, t1 = 0;
    unsigned int step = 0;
    for (int i = 0; i < quarter; i = i + 1) {
        double local_a0 = (double)CONCAT44(0x43300000, n ^ 0x80000000);
        double ang = (double)((float)(dAngle / (double)(float)(local_a0 - dMagic)) *
                     (float)((double)CONCAT44(0x43300000, step ^ 0x80000000) - dMagic));
        double cs = FUN_80390250(ang);
        *(float *)(twTable + t1) = (float)cs;
        double sn = FUN_80390914(ang);
        ((float *)(twTable + t1))[1] = (float)-sn;
        double ang2 = (double)((float)(dAngle / (double)(float)((double)CONCAT44(0x43300000, (n << 1) ^ 0x80000000) - dMagic)) *
                      (float)((double)CONCAT44(0x43300000, (t2 + 1U) ^ 0x80000000) - dMagic));
        cs = FUN_80390250(ang2);
        float *p = (float *)(twTable + (((int)n >> 1) + t2) * 4);
        *p = (float)cs;
        sn = FUN_80390914(ang2);
        t2 = t2 + 2;
        t1 = t1 + 8;
        step = step + 4;
        p[1] = (float)sn;
    }

    double dAngle2 = (double)FLOAT_806169c0;
    int eighth = ((int)n >> 3) + (unsigned int)((int)n < 0 && (pair & 7) != 0);
    int e2 = 0, e1 = 0;
    double stageScale = DOUBLE_806169c8;
    for (int i = 0; i < eighth; i = i + 1) {
        double ang = (double)((float)(dAngle2 / (double)(float)((double)CONCAT44(0x43300000, n ^ 0x80000000) - dMagic)) *
                     (float)((double)CONCAT44(0x43300000, (e1 + 2U) ^ 0x80000000) - dMagic));
        double cs = FUN_80390250(ang);
        float *p = (float *)(twTable + (n + e2) * 4);
        *p = (float)(stageScale * cs);
        double sn = FUN_80390914(ang);
        e2 = e2 + 2;
        e1 = e1 + 4;
        p[1] = (float)(stageScale * -sn);
    }

    // bit-reversal permutation table
    unsigned int r = 0;
    int brOff = 0;
    int cnt = eighth;
    if (0 < cnt) {
        do {
            unsigned int rev = 0;
            for (unsigned int b = 0; ; b = b + 1) {
                unsigned int mask = (1 << (stages - 2)) >> (b & 0x3f);
                if (mask == 0) break;
                if ((r & mask) != 0) { rev = rev | (1 << b); }
            }
            int *e = (int *)(brTable + brOff);
            r = r + 1;
            e[0] = ((1 << (stages - 1)) - 1U & ~rev) - 1;
            brOff = brOff + 8;
            e[1] = rev;
            cnt = cnt + -1;
        } while (cnt != 0);
    }

    desc[4] = (unsigned int)(FLOAT_806169d0 /
              (float)((double)CONCAT44(0x43300000, n ^ 0x80000000) - DOUBLE_806169d8));
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// anim_fft_butterfly8 (FUN_801998f0)
// One radix-2 8-point FFT butterfly stage in place over param_1[0..7]
// (interleaved re/im): the classic sum/difference cross of the even/odd taps.
// ---------------------------------------------------------------------------
void anim_fft_butterfly8(float *p) {
    float a1 = p[6] + p[2];
    float a2 = p[6] - p[2];
    float a3 = p[4] + p[0];
    float a5 = p[4] - p[0];
    p[6] = a1 + a3;
    p[4] = a1 - a3;
    float b1 = p[3];
    p[0] = a2 + (p[5] - p[1]);
    p[2] = a2 - (p[5] - p[1]);
    float b2 = p[1];
    float b3 = p[7];
    float b4 = p[3];
    p[3] = (p[7] - b1) + a5;
    p[1] = (p[7] - b1) - a5;
    p[7] = b3 + b4 + p[5] + b2;
    p[5] = (b3 + b4) - (p[5] + b2);
}

// ===========================================================================
// batch_516 (0x80199984 - 0x8019a858): the anim_codec real-FFT / inverse-FFT
// layer that sits under the block encode/decode drivers. The radix-2 butterfly
// ladder (8/16/32-point stages), two decimation-combine passes (one with a
// fixed 16-entry twiddle block, one with a strided twiddle table), the forward
// stage driver, the real-input FFT front/back-ends (split-radix real transform:
// pack the N real samples as N/2 complex, run the complex FFT, then untangle
// with the half-length twiddles), and the two small sub-buffer free helpers.
//
// The FUN_803826xx helpers are the compiler's callee-saved-register prologue
// carriers; Ghidra models their "return" as the incoming r3:r4 pair (here the
// descriptor / buffer pointers). FUN_8019a5dc's frame additionally spills a
// runtime-sized scratch run onto the stack (the afStack_28[] + register004
// chain); it is reconstructed with _alloca, the faithful stack allocation.
// ===========================================================================
extern "C" {
// forward-stage driver register-save pair (r3 = twiddle desc, r4 = data buffer)
extern unsigned long long FUN_803826d0(void);   // -> (descHi:dataLo) fwd-stage pair
extern void               FUN_8038271c(void);    // restore (paired with FUN_803826d0)

// FFT twiddle scalar constants (0x806169e0..0x806169ec)
extern float FLOAT_806169e0;   // 1/sqrt(2)   (45-degree butterfly rotation)
extern float FLOAT_806169e4;   // cos(pi/8)   (16-pt stage twiddle)
extern float FLOAT_806169e8;   // sin(pi/8)   (16-pt stage twiddle)
extern float FLOAT_806169ec;   // 0.5         (real-FFT recombination half)
} // extern "C"

// Forward decls for the internal FFT primitives (referenced before definition).
void anim_fft_butterfly16(float *p);            // FUN_80199984
void anim_fft_combine_fixed(float *tw, float *data, int n);          // FUN_80199d1c
void anim_fft_combine_strided(float *tw, float *data, int n, int stride); // FUN_80199eb0
void anim_fft_forward_stages(int desc, float *data, int n);          // FUN_8019a054
void anim_fft_real_recombine(int *desc, float *data);                // FUN_8019a17c

// ---------------------------------------------------------------------------
// anim_fft_butterfly16 (FUN_80199984)
// One 16-point radix-2 FFT butterfly stage in place over p[0..15] (interleaved
// re/im, i.e. 8 complex taps). Cross-combines the even/odd complex pairs with
// the +/-45-degree rotation (FLOAT_806169e0 == 1/sqrt(2)) for the diagonal
// taps, then recurses into the two 8-point sub-butterflies (anim_fft_butterfly8
// over p[0..7] and p[8..15]).
// ---------------------------------------------------------------------------
void anim_fft_butterfly16(float *p) {
    float r = FLOAT_806169e0;   // 1/sqrt(2)
    float t1 = p[9];
    float t2 = p[0] - p[8];
    p[8] = p[8] + p[0];
    p[9] = p[9] + p[1];
    p[0] = r * ((p[1] - t1) + t2);
    p[1] = r * ((p[1] - t1) - t2);
    t1 = p[10];
    float t3 = p[2];
    float t4 = p[11];
    p[10] = t1 + t3;
    p[11] = p[11] + p[3];
    p[2] = p[3] - t4;
    p[3] = t1 - t3;
    t1 = p[13];
    t2 = p[12] - p[4];
    p[12] = p[12] + p[4];
    p[13] = p[13] + p[5];
    p[4] = r * (t2 - (t1 - p[5]));
    p[5] = r * (t2 + (t1 - p[5]));
    t1 = p[14];
    t2 = p[15];
    p[14] = t1 + p[6];
    p[15] = p[15] + p[7];
    p[6] = t1 - p[6];
    p[7] = t2 - p[7];
    anim_fft_butterfly8(p);
    anim_fft_butterfly8(p + 8);
}

// ---------------------------------------------------------------------------
// anim_fft_butterfly32 (FUN_80199abc)
// One 32-point radix-2 FFT butterfly stage in place over p[0..31] (16 complex
// taps). Applies the pi/8-family twiddle rotations (1/sqrt(2) at 45 deg, and
// the cos(pi/8)/sin(pi/8) pair FLOAT_806169e4 / FLOAT_806169e8 at the odd
// diagonals) to cross-combine the even/odd halves, then recurses into the two
// 16-point sub-butterflies (anim_fft_butterfly16 over p[0..15] and p[16..31]).
// ---------------------------------------------------------------------------
void anim_fft_butterfly32(float *p) {
    float c8 = FLOAT_806169e8;   // sin(pi/8)
    float s8 = FLOAT_806169e4;   // cos(pi/8)
    float r  = FLOAT_806169e0;   // 1/sqrt(2)
    float t1 = p[0x1e];
    float t2 = p[0x1f];
    p[0x1e] = t1 + p[0xe];
    p[0x1f] = p[0x1f] + p[0xf];
    p[0xe] = t1 - p[0xe];
    p[0xf] = t2 - p[0xf];
    t1 = p[0x1d];
    t2 = p[0x1c] - p[0xc];
    p[0x1c] = p[0x1c] + p[0xc];
    p[0x1d] = p[0x1d] + p[0xd];
    p[0xc] = s8 * t2 - c8 * (t1 - p[0xd]);
    p[0xd] = c8 * t2 + s8 * (t1 - p[0xd]);
    t1 = p[0x1b];
    t2 = p[0x1a] - p[10];
    p[0x1a] = p[0x1a] + p[10];
    p[0x1b] = p[0x1b] + p[0xb];
    p[10] = r * (t2 - (t1 - p[0xb]));
    p[0xb] = r * (t2 + (t1 - p[0xb]));
    t1 = p[0x19];
    t2 = p[0x18] - p[8];
    p[0x18] = p[0x18] + p[8];
    p[0x19] = p[0x19] + p[9];
    p[8] = c8 * t2 - s8 * (t1 - p[9]);
    p[9] = c8 * (t1 - p[9]) + s8 * t2;
    t1 = p[0x16];
    t2 = p[6];
    float t3 = p[0x17];
    p[0x16] = t1 + t2;
    p[0x17] = p[0x17] + p[7];
    p[6] = p[7] - t3;
    p[7] = t1 - t2;
    t1 = p[0x15];
    t2 = p[4] - p[0x14];
    p[0x14] = p[0x14] + p[4];
    p[0x15] = p[0x15] + p[5];
    p[4] = s8 * (p[5] - t1) + c8 * t2;
    p[5] = c8 * (p[5] - t1) - s8 * t2;
    t1 = p[0x13];
    t2 = p[2] - p[0x12];
    p[0x12] = p[0x12] + p[2];
    p[0x13] = p[0x13] + p[3];
    p[2] = r * ((p[3] - t1) + t2);
    p[3] = r * ((p[3] - t1) - t2);
    t2 = p[0] - p[0x10];
    t1 = p[0x11];
    p[0x10] = p[0x10] + p[0];
    p[0x11] = p[0x11] + p[1];
    p[0] = c8 * (p[1] - t1) + s8 * t2;
    p[1] = s8 * (p[1] - t1) - c8 * t2;
    anim_fft_butterfly16(p);
    anim_fft_butterfly16(p + 0x10);
}

// ---------------------------------------------------------------------------
// anim_fft_combine_fixed (FUN_80199d1c)
// FFT decimation-combine pass over the top half of `data` against the bottom
// half, using the fixed 16-entry (4 complex) twiddle block at `tw` (advanced by
// 0x10 floats per group). Walks two cursors inward from the array end:
//   hi = data + n - 8, lo = data + n/2 - 8
// butterflying 4 complex taps per iteration (sum into hi, twiddle-rotated
// difference into lo) until the low cursor reaches the array base.
// ---------------------------------------------------------------------------
void anim_fft_combine_fixed(float *tw, float *data, int n) {
    float *lo;
    float *hi = data + n + -8;
    float *lp = data + (n >> 1) + -8;
    do {
        float a = hi[7];
        float b = lp[7];
        float d = hi[6] - lp[6];
        hi[6] = hi[6] + lp[6];
        hi[7] = hi[7] + lp[7];
        lp[6] = (a - b) * tw[1] + d * tw[0];
        lp[7] = (a - b) * tw[0] - d * tw[1];
        a = hi[5];
        b = lp[5];
        d = hi[4] - lp[4];
        hi[4] = hi[4] + lp[4];
        hi[5] = hi[5] + lp[5];
        lp[4] = (a - b) * tw[5] + d * tw[4];
        lp[5] = (a - b) * tw[4] - d * tw[5];
        a = hi[3];
        b = lp[3];
        d = hi[2] - lp[2];
        hi[2] = hi[2] + lp[2];
        hi[3] = hi[3] + lp[3];
        lp[2] = (a - b) * tw[9] + d * tw[8];
        lp[3] = (a - b) * tw[8] - d * tw[9];
        a = hi[1];
        b = lp[1];
        d = hi[0] - lp[0];
        hi[0] = hi[0] + lp[0];
        hi[1] = hi[1] + lp[1];
        hi = hi + -8;
        lp[0] = (a - b) * tw[0xd] + d * tw[0xc];
        lo = tw + 0xd;
        float *cbase = tw + 0xc;
        tw = tw + 0x10;
        lp[1] = (a - b) * *cbase - d * *lo;
        lp = lp + -8;
    } while (data <= lp);
}

// ---------------------------------------------------------------------------
// anim_fft_combine_strided (FUN_80199eb0)
// Same decimation-combine as anim_fft_combine_fixed, but the twiddle factors are
// read from a strided table: the base pointer `tw` advances by `stride` floats
// between each of the four complex taps within a group (and carries to the next
// group), rather than a contiguous 16-entry block. Used for the larger stages
// where the twiddle table is shared/interleaved across sub-transforms.
// ---------------------------------------------------------------------------
void anim_fft_combine_strided(float *tw, float *data, int n, int stride) {
    float *t0;
    float *t1;
    float *hi = data + n + -8;
    float *lp = data + (n >> 1) + -8;
    do {
        float a = hi[7];
        float b = lp[7];
        float d = hi[6] - lp[6];
        hi[6] = hi[6] + lp[6];
        hi[7] = hi[7] + lp[7];
        lp[6] = (a - b) * tw[1] + d * tw[0];
        t0 = tw + stride;
        lp[7] = (a - b) * tw[0] - d * tw[1];
        a = hi[5];
        b = lp[5];
        d = hi[4] - lp[4];
        hi[4] = hi[4] + lp[4];
        hi[5] = hi[5] + lp[5];
        lp[4] = (a - b) * t0[1] + d * t0[0];
        t1 = t0 + stride;
        lp[5] = (a - b) * t0[0] - d * t0[1];
        a = hi[3];
        b = lp[3];
        d = hi[2] - lp[2];
        hi[2] = hi[2] + lp[2];
        hi[3] = hi[3] + lp[3];
        lp[2] = (a - b) * t1[1] + d * t1[0];
        t0 = t1 + stride;
        lp[3] = (a - b) * t1[0] - d * t1[1];
        a = hi[1];
        b = lp[1];
        d = hi[0] - lp[0];
        hi[0] = hi[0] + lp[0];
        hi[1] = hi[1] + lp[1];
        hi = hi + -8;
        lp[0] = (a - b) * t0[1] + d * t0[0];
        tw = t0 + stride;
        lp[1] = (a - b) * t0[0] - d * t0[1];
        lp = lp + -8;
    } while (data <= lp);
}

// ---------------------------------------------------------------------------
// anim_fft_forward_stages (FUN_8019a054)
// Drive the complex forward FFT over `data` (n complex-interleaved floats) using
// the twiddle descriptor carried by the register-save pair (desc[1]=stage count,
// desc[2]=twiddle table). Runs the decimation ladder: one fixed-twiddle combine
// (anim_fft_combine_fixed), then (stages-6) strided-combine passes doubling the
// group count each stage, and finally a 32-point butterfly (anim_fft_butterfly32)
// over each 0x20-float block. The desc/data pair arrives via FUN_803826d0.
// ---------------------------------------------------------------------------
void anim_fft_forward_stages(int descArg, float *dataArg, int n) {
    (void)descArg; (void)dataArg;
    unsigned long long pair = FUN_803826d0();
    int desc = (int)((unsigned long long)pair >> 0x20);
    float *data = (float *)pair;
    float *tw = *(float **)(desc + 8);
    int stagesLeft = *(int *)(desc + 4) + -6;

    if (0 < stagesLeft) {
        anim_fft_combine_fixed(tw, data, n);
    }
    unsigned int shift = 1;
    while (stagesLeft = stagesLeft + -1, 0 < stagesLeft) {
        int span = n >> (shift & 0x3f);
        for (int g = 0; g < 1 << shift; g = g + 1) {
            anim_fft_combine_strided(tw, data + g * span, span, 4 << shift);
        }
        shift = shift + 1;
    }
    int off = 0;
    for (int i = 0; i < n; i = i + 0x20) {
        anim_fft_butterfly32((float *)((int)data + off));
        off = off + 0x80;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// anim_fft_free_pair (FUN_8019a120)
// Free the two FFT twiddle/bit-reversal sub-buffers held by a 0x14-byte
// descriptor (the buffers at +8 and +0xc, as built by anim_fft_build_twiddles),
// then zero the descriptor. No-op on a null descriptor.
// ---------------------------------------------------------------------------
void anim_fft_free_pair(int desc) {
    if (desc != 0) {
        if (*(int *)(desc + 8) != 0) {
            FUN_801a14dc(*(int *)(desc + 8));
        }
        if (*(int *)(desc + 0xc) != 0) {
            FUN_801a14dc(*(int *)(desc + 0xc));
        }
        memset_wrapper(desc, 0, 0x14);
    }
}

// ---------------------------------------------------------------------------
// anim_fft_real_recombine (FUN_8019a17c)
// The real-FFT untangle pass: given a length-N/2 complex spectrum produced by
// treating N real samples as N/2 complex points, recombine conjugate-symmetric
// bin pairs into the true real spectrum using the half-length twiddle table.
// Walks the packed-index table desc[3] (two indices per complex bin) and the
// twiddle block at desc[2]+N*4, writing the low bins forward into `out` (param_2)
// and the mirrored high bins backward, with the 0.5 scale (FLOAT_806169ec) on the
// DC/Nyquist diagonal. desc = param_1 (holds N at [0], index table at [3],
// twiddles at [2]).
// ---------------------------------------------------------------------------
void anim_fft_real_recombine(int *desc, float *out) {
    float half = FLOAT_806169ec;   // 0.5
    int *idx = (int *)desc[3];
    float *lo = out + (*desc >> 1);
    float *tw = (float *)(desc[2] + *desc * 4);
    float *hi = lo;
    do {
        float im0 = (lo + idx[0])[1];
        float im1 = (lo + idx[1])[1];
        float re0 = lo[idx[0]];
        float dIm = im0 - im1;
        float re1 = lo[idx[1]];
        float sRe = re0 + re1;
        float sIm = half * (im0 + im1);
        float xRe = sRe * tw[0] + dIm * tw[1];
        dIm = sRe * tw[1] - dIm * tw[0];
        float dRe = half * (re0 - re1);
        out[0] = sIm + xRe;
        hi[-2] = sIm - xRe;
        out[1] = dRe + dIm;
        hi[-1] = dIm - dRe;
        int *idx2 = idx + 2;
        int *idx3 = idx + 3;
        idx = idx + 4;
        float *tw3 = tw + 3;
        im0 = (lo + idx2[0])[1];
        im1 = (lo + idx3[0])[1];
        re0 = lo[idx2[0]];
        dIm = im0 - im1;
        re1 = lo[idx3[0]];
        float *tw2 = tw + 2;
        sRe = re0 + re1;
        tw = tw + 4;
        sIm = half * (im0 + im1);
        xRe = sRe * *tw2 + dIm * *tw3;
        dIm = sRe * *tw3 - dIm * *tw2;
        dRe = half * (re0 - re1);
        out[2] = sIm + xRe;
        hi[-4] = sIm - xRe;
        out[3] = dRe + dIm;
        out = out + 4;
        hi[-3] = dIm - dRe;
        hi = hi + -4;
    } while (out < hi);
}

// ---------------------------------------------------------------------------
// anim_ifft_real (FUN_8019a2b4)
// Inverse real-FFT front-end: reconstruct N real samples from their conjugate-
// symmetric half-spectrum in `spectrum` (param_3). Pre-twiddles the packed bins
// into a length-N/2 complex array (using the desc twiddle table at desc[2]),
// runs the complex forward FFT over the packed buffer (anim_fft_forward_stages),
// applies the real recombination (anim_fft_real_recombine), then unpacks and
// mirrors the result into place with a final conjugate/negate + reverse copy.
// The desc/packed-buffer pair arrives via FUN_803826e4.
// ---------------------------------------------------------------------------
void anim_ifft_real(int descArg, int packedArg, float *spectrum) {
    (void)descArg; (void)packedArg;
    unsigned long long pair = FUN_803826e4();
    unsigned int *desc = (unsigned int *)((unsigned long long)pair >> 0x20);
    float *packed = (float *)pair;
    int half = (int)*desc >> 1;
    unsigned int byteHalf = *desc & 0xfffffffc;
    float *mid = spectrum + half;
    float *tw = (float *)(desc[2] + byteHalf);
    float *hiOut = (float *)((int)mid + byteHalf);
    float *lp = packed + half + -7;
    float *op = hiOut;
    // pass 1: pre-twiddle the high half of the spectrum into the packed complex
    do {
        op[-4] = -lp[2] * tw[3] - lp[0] * tw[2];
        op[-3] = lp[0] * tw[3] - lp[2] * tw[2];
        op[-2] = -lp[6] * tw[1] - lp[4] * tw[0];
        float *lp6 = lp + 6;
        float t = tw[0];
        float *lp4 = lp + 4;
        lp = lp + -8;
        float *tw1 = tw + 1;
        tw = tw + 4;
        op[-1] = *lp4 * *tw1 - *lp6 * t;
        op = op + -4;
    } while (packed <= lp);

    float *lp2 = packed + half + -8;
    float *tp = (float *)(desc[2] + byteHalf);
    op = hiOut;
    do {
        op[0] = lp2[4] * tp[-1] + lp2[6] * tp[-2];
        op[1] = lp2[4] * tp[-2] - lp2[6] * tp[-1];
        op[2] = lp2[0] * tp[-3] + lp2[2] * tp[-4];
        float *tp3 = tp + -3;
        float *tp2 = tp + -2;
        float t = tp[0];
        tp = tp + -8;
        op[3] = t * tp[0] - *tp2 * *tp3;
        op = op + 4;
    } while (packed <= lp2);

    anim_fft_forward_stages((int)desc, mid, half);
    anim_fft_real_recombine((int *)desc, spectrum);

    // untangle the recombined bins back into the real output (mid..hi)
    float *tp4 = (float *)(desc[2] + half * 4);
    float *dHi = hiOut;
    float *dLo = hiOut;
    float *sp = spectrum;
    do {
        dHi[-1] = sp[0] * tp4[1] - sp[1] * tp4[0];
        dLo[0] = -(sp[0] * tp4[0] + sp[1] * tp4[1]);
        dHi[-2] = sp[2] * tp4[3] - sp[3] * tp4[2];
        dLo[1] = -(sp[2] * tp4[2] + sp[3] * tp4[3]);
        dHi[-3] = sp[4] * tp4[5] - sp[5] * tp4[4];
        dLo[2] = -(sp[4] * tp4[4] + sp[5] * tp4[5]);
        dHi = dHi + -4;
        *dHi = sp[6] * tp4[7] - sp[7] * tp4[6];
        float *sp7 = sp + 7;
        float *tp7 = tp4 + 7;
        float *sp6 = sp + 6;
        sp = sp + 8;
        float *tp6 = tp4 + 6;
        tp4 = tp4 + 8;
        dLo[3] = -(*sp6 * *tp6 + *sp7 * *tp7);
        dLo = dLo + 4;
    } while (sp < dHi);

    // mirror the low half into the negative-frequency slots
    float *loMirror = (float *)((int)spectrum + byteHalf);
    float *mp = loMirror;
    float *hp = hiOut;
    do {
        float v = hp[-1];
        mp[-1] = v;
        loMirror[0] = -v;  // note: original writes via the advancing lo cursor
        v = hp[-2];
        mp[-2] = v;
        mp[1] = -v;
        v = hp[-3];
        mp[-3] = v;
        mp[2] = -v;
        hp = hp + -4;
        v = *hp;
        mp = mp + -4;
        *mp = v;
        mp[3] = -v;
        // (cursor bookkeeping mirrors the raw two-pointer walk)
        loMirror = loMirror + 4;
    } while (loMirror < hp);

    // final reversed copy of the mid block down into the output slot
    float *cp = hiOut;
    do {
        mid[-4] = cp[3];
        mid[-3] = cp[2];
        mid[-2] = cp[1];
        mid[-1] = cp[0];
        mid = mid + -4;
        cp = cp + 4;
    } while (hiOut < mid);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// anim_rfft_forward (FUN_8019a5dc)
// Forward real-FFT / DCT-style transform used by the encoder: pack the N real
// input samples (at desc-derived offsets) into a length-N/2 complex scratch run
// (built on the stack, runtime-sized via _alloca), run the complex forward FFT
// (anim_fft_forward_stages) + real recombination (anim_fft_real_recombine), then
// project the recombined spectrum onto the output magnitude run scaled by the
// reciprocal-N factor (desc[4]). The state/output pair + the runtime-sized
// scratch arrive through the __save_gpr_25 frame; `pOut` is the incoming output
// buffer argument (param_3).
// ---------------------------------------------------------------------------
void anim_rfft_forward(int stateArg, float *weightsArg, int pOut) {
    (void)stateArg; (void)weightsArg;
    unsigned long long pair = (unsigned long long)__save_gpr_25();
    int *desc = (int *)((unsigned long long)pair >> 0x20);
    int state = (int)pair;
    int n = *desc;
    int half = n >> 1;
    int quarter = n >> 2;
    int eighth = n >> 3;

    // runtime-sized complex scratch run (was the raw afStack_28[]/register004 chain)
    float *scratch = (float *)_alloca((n * 4) & ~7);
    float *packHi = scratch + half;

    int idxA = 0;
    float *srcHi = (float *)(state + half * 4 + quarter * 4);
    float *tw = (float *)(desc[2] + half * 4);
    float *srcOdd = srcHi + 1;

    // pass 1a: fold the first eighth (sum form)
    unsigned int loops = (eighth + 1U) >> 1;
    if (0 < eighth) {
        do {
            float a = srcHi[-2];
            float *dst = (float *)((int)packHi + idxA);
            float b = *srcOdd;
            float c = srcOdd[2];
            idxA = idxA + 8;
            srcHi = srcHi + -4;
            float e = *srcHi;
            srcOdd = srcOdd + 4;
            dst[0] = (e + c) * tw[-1] + (a + b) * tw[-2];
            float *twm1 = tw + -1;
            tw = tw + -2;
            dst[1] = (e + c) * *tw - (a + b) * *twm1;
            loops = loops - 1;
        } while (loops != 0);
    }

    // pass 1b: middle band (difference form)
    float *srcOdd2 = (float *)(state + 4);
    int idxB = idxA;
    loops = (unsigned int)(((half - eighth) + 1) - (idxA >> 3)) >> 1;
    if ((idxA >> 3) < half - eighth) {
        do {
            float a = srcHi[-2];
            float *dst = (float *)((int)packHi + idxB);
            float b = *srcOdd2;
            float c = srcOdd2[2];
            idxB = idxB + 8;
            srcHi = srcHi + -4;
            float e = *srcHi;
            srcOdd2 = srcOdd2 + 4;
            dst[0] = (e - c) * tw[-1] + (a - b) * tw[-2];
            float *twm1 = tw + -1;
            tw = tw + -2;
            dst[1] = (e - c) * *tw - (a - b) * *twm1;
            loops = loops - 1;
        } while (loops != 0);
    }

    // pass 1c: upper band (negated sum form)
    int idxC = idxB;
    loops = (unsigned int)((half + 1) - (idxB >> 3)) >> 1;
    float *srcHi2 = (float *)(state + n * 4);
    if ((idxB >> 3) < half) {
        do {
            float a = srcHi2[-2];
            float *dst = (float *)((int)packHi + idxC);
            srcHi2 = srcHi2 + -4;
            float e = *srcHi2;
            idxC = idxC + 8;
            float b = *srcOdd2;
            float c = srcOdd2[2];
            srcOdd2 = srcOdd2 + 4;
            dst[0] = (-e - c) * tw[-1] + (-a - b) * tw[-2];
            float *twm1 = tw + -1;
            tw = tw + -2;
            dst[1] = (-e - c) * *tw - (-a - b) * *twm1;
            loops = loops - 1;
        } while (loops != 0);
    }

    anim_fft_forward_stages((int)desc, packHi, half);
    anim_fft_real_recombine(desc, scratch);

    // project onto the output magnitude run, scaled by the reciprocal-N factor
    float *outHi = (float *)(pOut + half * 4);
    int outOff = 0;
    float *sp = (float *)(desc[2] + half * 4);
    float rcpN = *(float *)&desc[4];
    if (0 < quarter) {
        do {
            *(float *)(pOut + outOff) = rcpN * (scratch[0] * sp[0] + scratch[1] * sp[1]);
            outOff = outOff + 4;
            float s0 = *sp;
            float x0 = *scratch;
            float s1 = sp[1];
            float x1 = scratch[1];
            scratch = scratch + 2;
            sp = sp + 2;
            outHi = outHi + -1;
            *outHi = rcpN * (x0 * s1 - x1 * s0);
            quarter = quarter + -1;
        } while (quarter != 0);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_fft_free_0x24 (FUN_8019a858)
// Zero a 0x24-byte FFT sub-decoder object and release it. No-op on null.
// ---------------------------------------------------------------------------
void anim_fft_free_0x24(int obj) {
    if (obj != 0) {
        memset_wrapper(obj, 0, 0x24);
        FUN_801a14dc(obj);
    }
}

// ===========================================================================
// batch_517 (0x8019a898 - 0x8019bf5c): the spectral curve/envelope build layer
// that feeds the channel-descriptor build (FUN_8019b380) consumed by
// anim_decoder_build_tables. A small vec-of-56 (0x38) min/max/add-scalar helper
// set, the big double-precision spectral envelope/attenuation table builder
// (masking-curve style: per-bark-band spreading with forward/backward masking
// slopes evaluated through exp/log/pow), the channel-descriptor builder that
// wraps it, the per-entry sub-context teardown, an inner envelope max-fill band
// helper + its driver, and a monotone-hull scan used by the pack/encode path.
//
// The two big builders spill a runtime-sized scratch run onto the stack (the
// decompiler's &stack.. + register0x00000004 chain); reconstructed with _alloca.
// The FUN_803826xx helpers are the callee-saved-register prologue carriers whose
// modelled "return" is the incoming r3:r4 pointer pair.
// ===========================================================================
extern "C" {
// raw scalar transcendentals used by the envelope/masking-curve math
extern double FUN_80390ac4(double x);   // log-family (bark/frequency warp)
extern double FUN_803900e4(double x);   // pow/round-family (band index map)
// fast aligned block copy leaf (memcpy-style, byte count)
extern void   leaf_31e8(unsigned int dst, unsigned int src, int n);

// masking-curve constants (0x806169f0..0x80616ac0)
extern double DOUBLE_806169f0;   // scale (bark warp numerator)
extern double DOUBLE_806169f8;   // int->double magic (2^52) subtrahend
extern double DOUBLE_80616a08;   // clamp floor comparand
extern double DOUBLE_80616a18;   // masking offset A
extern double DOUBLE_80616a20;   // masking slope A
extern double DOUBLE_80616a28;   // masking offset B
extern double DOUBLE_80616a38;   // exp arg scale
extern double DOUBLE_80616a40;   // exp arg bias
extern double DOUBLE_80616a48;   // reciprocal-band scale
extern double DOUBLE_80616a50;   // log arg scale
extern double DOUBLE_80616a60;   // band step
extern double DOUBLE_80616a68;   // forward-mask offset
extern double DOUBLE_80616a70;   // backward-mask offset / bias
extern double DOUBLE_80616a78;   // backward-mask offset 2
extern double DOUBLE_80616a98;   // spread coeff B
extern double DOUBLE_80616aa8;   // spread coeff A
extern double DOUBLE_80616ab8;   // band-index scale (fill dispatch)
extern float  FLOAT_806169f8;    // (alias; low half of the magic pair for float ctx)
extern float  FLOAT_80616a04;    // +inf-style init (envelope max seed)
extern float  FLOAT_80616a10;    // clamp value
extern float  FLOAT_80616a30;    // half-band scale
extern float  FLOAT_80616a58;    // log input bias
extern float  FLOAT_80616a80;    // out-of-range fill value
extern float  FLOAT_80616a84;    // threshold comparand
extern float  FLOAT_80616a88;    // stage-count log scale
extern float  FLOAT_80616a8c;    // band-count log scale
extern float  FLOAT_80616a90;    // spread lin coeff
extern float  FLOAT_80616aa0;    // spread exp scale A
extern float  FLOAT_80616ab0;    // spread exp scale B
extern float  FLOAT_80616ab4;    // band-center bias
extern float  FLOAT_80616ac0;    // envelope-apply threshold delta
extern float  DAT_8041ffd8;      // bark-band edge LUT (base)
extern float  DAT_80420134;      // bark-band edge LUT (tail clamp)
extern unsigned char DAT_80420138;   // per-band spreading template table (base)
} // extern "C"

// Forward decls for this batch's own helpers (referenced before definition).
void anim_vec38_min(int dst, int src);
void anim_vec38_max(int dst, int src);
void anim_vec38_add_scalar(double s, int dst);
int  anim_curve_build_envelope(double a, double b, double c);   // FUN_8019a954
void anim_envelope_fill_band(double base, double bias, int pOut, int pBands,
                             unsigned int lo, int cap, unsigned int step); // FUN_8019bd08

// ---------------------------------------------------------------------------
// anim_sub_free_0x210 (FUN_8019a898)
// Zero a 0x210-byte sub-decoder object and release it. No-op on null. Invoked
// per sub-decoder by anim_subctx_destroy.
// ---------------------------------------------------------------------------
void FUN_8019a898(int obj) {
    if (obj != 0) {
        memset_wrapper(obj, 0, 0x210);
        FUN_801a14dc(obj);
    }
}

// ---------------------------------------------------------------------------
// anim_vec38_min (FUN_8019a8d8)
// Component-wise minimum of two 56-float (0x38) vectors: dst[i] = min(dst[i],
// src[i]) for i in [0,56). Used to fold masking-curve contributions downward.
// ---------------------------------------------------------------------------
void anim_vec38_min(int dst, int src) {
    for (int i = 0; i < 0x38 * 4; i = i + 4) {
        if (*(float *)(src + i) < *(float *)(dst + i)) {
            *(float *)(dst + i) = *(float *)(src + i);
        }
    }
}

// ---------------------------------------------------------------------------
// anim_vec38_max (FUN_8019a904)
// Component-wise maximum of two 56-float (0x38) vectors: dst[i] = max(dst[i],
// src[i]) for i in [0,56).
// ---------------------------------------------------------------------------
void anim_vec38_max(int dst, int src) {
    for (int i = 0; i < 0x38 * 4; i = i + 4) {
        if (*(float *)(dst + i) < *(float *)(src + i)) {
            *(float *)(dst + i) = *(float *)(src + i);
        }
    }
}

// ---------------------------------------------------------------------------
// anim_vec38_add_scalar (FUN_8019a930)
// Add the scalar `s` to every element of a 56-float (0x38) vector `dst`.
// ---------------------------------------------------------------------------
void anim_vec38_add_scalar(double s, int dst) {
    for (int i = 0; i < 0x38 * 4; i = i + 4) {
        *(float *)(dst + i) = (float)((double)*(float *)(dst + i) + s);
    }
}

// ---------------------------------------------------------------------------
// anim_curve_build_envelope (FUN_8019a954)
// Build the per-band spreading/masking envelope table for the channel
// descriptor. Two phases:
//   (1) For each of the 0x11 stages, seed a 56-band working row from the bark
//       edge LUT (DAT_8041ffd8, clamped past band 0x58 to DAT_80420134), spread
//       it with the per-band template (DAT_80420138 via leaf_31e8), apply the
//       forward masking slope (anim_vec38_add_scalar) and fold the neighbour
//       rows down/up (anim_vec38_min / anim_vec38_max).
//   (2) For each of the 0x11 output slots x 8 sub-bands, walk the frequency
//       warp (FUN_80390ac4 log / FUN_80390b04 log / FUN_80390324 / FUN_803900e4
//       pow-round) to compute the lower/upper band span, take the running min
//       over that span into the 0xe8-byte output record, and record the first/
//       last band whose value crosses the threshold (FLOAT_80616a84) at the
//       record head (+0 / +4).
// Allocates the 0x44 header + a 0xe8 record per (slot,sub-band). The big stack
// scratch (working rows + template copies, ~0x7700 bytes) is _alloca'd. The
// state pointer pair arrives via FUN_803826b4; `a`/`b`/`c` are the incoming
// FP args (reciprocal-N scale, and the two curve params). Returns the header.
// The in_f14..f31 / in_ps* values are callee-saved FP registers Ghidra could
// not source; they feed only removed-unreachable spill blocks, seeded to 0.
// ---------------------------------------------------------------------------
int anim_curve_build_envelope(double a, double b, double c) {
    (void)a;
    // callee-saved FP spill slots (unused in the reachable path)
    double in_f14 = 0, in_f15 = 0, in_f16 = 0, in_f17 = 0, in_f18 = 0, in_f19 = 0;
    double in_f20 = 0, in_f21 = 0, in_f22 = 0, in_f23 = 0, in_f24 = 0, in_f25 = 0;
    double in_f26 = 0, in_f27 = 0, in_f28 = 0, in_f29 = 0, in_f30 = 0, in_f31 = 0;
    (void)in_f14; (void)in_f15; (void)in_f16; (void)in_f17; (void)in_f18; (void)in_f19;
    (void)in_f20; (void)in_f21; (void)in_f22; (void)in_f23; (void)in_f24; (void)in_f25;
    (void)in_f26; (void)in_f27; (void)in_f28; (void)in_f29; (void)in_f30; (void)in_f31;

    unsigned long long pair = FUN_803826b4();
    int stateHi = (int)((unsigned long long)pair >> 0x20);
    int nBands = (int)pair;

    // runtime-sized working scratch: 0x11 stage rows of 0x700 + template rows.
    // (the decompiler's local_80b8[55] + auStack_7fd8[28352] + register004 chain)
    unsigned char *scratch = (unsigned char *)_alloca(0x7700);
    float *rows = (float *)scratch;                 // 0x11 * 0x700 stage rows
    float *tailRows = (float *)(scratch + 0x7700 - 0x1120); // template/neighbour rows
    float *tmplRow = (float *)(scratch + 0x7700 - 0x748);   // local_9b8[56]
    unsigned char *neighbourRows = scratch + 0x7700 - 0x6c8; // auStack_8d8[1792]

    int header = (int)FUN_801a13e0(0x44);
    memset_wrapper((int)rows, 0, 0x7700);

    double bandStep   = DOUBLE_80616a60;
    double maskOffA   = DOUBLE_80616a18;
    double maskBiasB  = DOUBLE_80616a28;
    double magic      = DOUBLE_806169f8;
    double expScale   = DOUBLE_80616a38;
    double expBias    = DOUBLE_80616a40;
    double halfBand   = (double)FLOAT_80616a30;
    double clampFloor = DOUBLE_80616a08;
    double clampVal   = (double)FLOAT_80616a10;

    int stageOff = 0;    // byte offset into the stage rows (0x700/stage)
    int tmplOff  = 0;    // byte offset into DAT_80420138 template (0x540/stage)
    int refOff   = 0;    // byte offset into the state ref row (+4/stage)
    for (int stage = 0; stage < 0x11; stage = stage + 1) {
        // (1a) seed the 56-band template row from the bark edge LUT
        int outIdx = 0;
        for (int band = 0; band < 0x38; band = band + 1) {
            float m = FLOAT_80616a04;
            for (int k = 0; k < 4; k = k + 1) {
                int e = band + k + stage;   // (raw: iVar11 + iVar12 + iVar17)
                if (e < 0x58) {
                    if ((&DAT_8041ffd8)[e] < m) { m = (&DAT_8041ffd8)[e]; }
                } else if (DAT_80420134 < m) {
                    m = DAT_80420134;
                }
            }
            tmplRow[band] = m;
            (void)outIdx;
        }

        // (1b) spread the template into the 8 sub-band rows of this stage
        unsigned int rowBase = (unsigned int)((int)rows + stageOff);
        unsigned char *tmpl = &DAT_80420138 + tmplOff;
        for (int sb = 0; sb < 6; sb = sb + 1) {
            leaf_31e8(rowBase + (sb + 2) * 0xe0, (unsigned int)(tmpl + sb * 0xe0), 0xe0);
        }
        leaf_31e8(rowBase, (unsigned int)tmpl, 0xe0);
        leaf_31e8((unsigned int)((int)tailRows + stageOff), (unsigned int)tmpl, 0xe0);

        // (1c) apply the forward masking slope to each sub-band row
        int sbOff = 0;
        for (int sb = 0; sb < 8; sb = sb + 1) {
            int rowAddr = (int)rowBase + sbOff;
            int add = 0;
            for (int band = 0; band < 0x38; band = band + 1) {
                unsigned int d = (unsigned int)((int)(0x10 - add) >> 0x1f);
                double di = (double)(long long)(int)((d ^ (0x10U - add)) - d);
                double v = (double)(float)(c * (double)(float)(di - magic) + b);
                if ((v < clampFloor) && (clampVal < b)) { v = clampVal; }
                if ((clampFloor < v) && (b < clampVal)) { v = clampVal; }
                add = add + 1;
                *(float *)(rowAddr + band * 4) = (float)((double)*(float *)(rowAddr + band * 4) + v);
            }
            sbOff = sbOff + 0xe0;
        }

        // (1d) neighbour fold: build the min/max spread rows
        int nbOff = 0;
        for (unsigned int sb = 0; (int)sb < 8; sb = sb + 1) {
            unsigned int sbc = (sb < 2) ? 2 : sb;
            double sbd = (double)(long long)(int)sbc;
            anim_vec38_add_scalar(
                (double)(float)(-(DOUBLE_80616a20 * (sbd - magic)
                                  - (maskOffA + (double)*(float *)(stateHi + refOff)))
                                - maskBiasB),
                (int)rowBase + nbOff);
            int nbRow = (int)neighbourRows + nbOff;
            leaf_31e8((unsigned int)nbRow, (unsigned int)tmplRow, 0xe0);
            double sbd2 = (double)(long long)(int)sb;
            anim_vec38_add_scalar(
                (double)(float)((maskOffA - (double)(float)(halfBand * (double)(float)(sbd2 - magic)))
                                - maskBiasB),
                nbRow);
            anim_vec38_max(nbRow, (int)rowBase + nbOff);
            nbOff = nbOff + 0xe0;
        }
        int nb2 = 0xe0;
        for (int sb = 1; sb < 8; sb = sb + 1) {
            anim_vec38_min((int)neighbourRows + nb2, (int)neighbourRows + (sb - 1) * 0xe0);
            anim_vec38_min((int)rowBase + nb2, (int)neighbourRows + nb2);
            nb2 = nb2 + 0xe0;
        }
        stageOff = stageOff + 0x700;
        tmplOff  = tmplOff + 0x540;
        refOff   = refOff + 4;
    }

    // (2) per output slot (0x11) x 8 sub-bands: warp -> band span -> min-fill
    double thresh   = (double)FLOAT_80616a84;
    double fwdOff   = DOUBLE_80616a68;
    double bwdOff2  = DOUBLE_80616a78;
    double bwdOff   = DOUBLE_80616a70;
    double posInf   = (double)FLOAT_80616a04;
    double logBias  = (double)FLOAT_80616a58;
    double logScale = DOUBLE_80616a50;
    double warpNum  = DOUBLE_806169f0;
    double warpOff  = DOUBLE_80616a40;
    double warpBias = DOUBLE_80616a60;
    double magic2   = DOUBLE_806169f8;
    double recip    = DOUBLE_80616a48;
    double bark     = c;   // (raw: dVar43 = extraout_f1 curve param)

    int slotArrOff = 0;
    for (unsigned int slot = 0; (int)slot <= 0x10; slot = slot + 1) {
        int slotArr = (int)FUN_801a13e0(0x20);
        *(int *)(header + slotArrOff) = slotArr;
        double slotD = (double)CONCAT44(0x43300000, slot ^ 0x80000000);
        double warp  = warpNum * (slotD - magic2);
        double lg    = FUN_80390b04(logScale * (warpOff + warp));
        lg = FUN_80390324(lg / bark);
        int loWarp = (int)lg;
        double loD = (double)(long long)loWarp;
        double lg2 = FUN_80390b04((double)(float)((double)(float)((double)CONCAT44(0x43300000, (unsigned int)loWarp ^ 0x80000000) - magic2) * bark + logBias));
        int loBand = (int)FUN_803900e4(recip * (logScale * lg2 - warpOff));
        double hiD = (double)CONCAT44(0x43300000, (loWarp + 1U) ^ 0x80000000);
        double lg3 = FUN_80390b04((double)(float)((double)(float)(hiD - magic2) * bark));
        int hiBand = (int)FUN_80390324(recip * (logScale * lg3 - warpOff));

        int loClamp = ((int)slot < loBand) ? (int)slot : loBand;
        if (loClamp < 0) loClamp = 0;
        if (0x10 < hiBand) hiBand = 0x10;

        int recOff = 0;
        int rowStride = 0;
        for (int sub = 0; sub < 8; sub = sub + 1) {
            int rec = (int)FUN_801a13e0(0xe8);
            *(int *)(*(int *)(header + slotArrOff) + recOff) = rec;

            // seed the 56-band accumulator with +inf
            for (int band = 0; band < nBands; band = band + 1) {
                *(float *)((int)rows + band * 4 + 0x7700 - 0x7700) = (float)posInf; // rows[band]
            }
            // (the raw uses the scratch head as the accumulator row)
            float *acc = rows;
            for (int band = 0; band < nBands; band = band + 1) acc[band] = (float)posInf;

            int spanBase = loClamp * 0x700 + rowStride;
            for (int w = loClamp; w <= hiBand; w = w + 1) {
                int lower = 0;
                double wd = (double)CONCAT44(0x43300000, (unsigned int)w ^ 0x80000000);
                double warpW = warpNum * (wd - magic2);
                int off = 0;
                for (int t = 0; t < 0x38; t = t + 1) {
                    double td = (double)(long long)t;
                    double ang = warpBias * (td - magic2) + warpW;
                    double lgl = FUN_80390ac4(expScale * (expBias + (ang - fwdOff)));
                    int lo = (int)(lgl / bark);
                    double lgh = FUN_80390ac4(expScale * (expBias + (ang - bwdOff2)));
                    int hi = (int)(bwdOff + lgh / bark);
                    if (lo < 0) lo = 0;
                    if (nBands < lo) lo = nBands;
                    if (lo < lower) lower = lo;
                    if (hi < 0) hi = 0;
                    if (nBands < hi) hi = nBands;
                    int bandOff = lower << 2;
                    for (; (lower < hi) && (lower < nBands); lower = lower + 1) {
                        float fv = *(float *)((int)rows + off + spanBase);
                        if (fv < acc[bandOff >> 2]) { acc[bandOff >> 2] = fv; }
                        bandOff = bandOff + 4;
                    }
                    off = off + 4;
                }
                spanBase = spanBase + 0x700;
            }

            // write the 56-band record body + threshold head fields
            for (int band = 0; band < 0x38; band = band + 1) {
                double bd = (double)CONCAT44(0x43300000, (unsigned int)band ^ 0x80000000);
                double slotWarp = warpNum * ((double)CONCAT44(0x43300000, slot ^ 0x80000000) - magic2);
                double lgl = FUN_80390ac4(expScale * (expBias + ((warpBias * (bd - magic2) + slotWarp) - recip)));
                int idx = (int)(lgl / bark);
                if (idx < 0 || nBands <= idx) {
                    *(float *)(rec + band * 4 + 8) = FLOAT_80616a80;
                } else {
                    *(int *)(rec + band * 4 + 8) = *(int *)((int)acc + idx * 4);
                }
            }
            // first band crossing the threshold (head +0)
            int first = 0;
            for (int band = 0; band < 0x10; band = band + 1) {
                if (thresh < (double)*(float *)(rec + band * 4 + 8)) break;
                first = first + 1;
            }
            *(float *)rec = (float)((double)CONCAT44(0x43300000, (unsigned int)first ^ 0x80000000) - magic2);
            // last band crossing the threshold (head +4)
            int last = 0x37;
            for (int band = 0x37; band >= 0x12; band = band - 1) {
                if (thresh < (double)*(float *)(rec + band * 4 + 8)) break;
                last = last - 1;
            }
            *(float *)(rec + 4) = (float)((double)CONCAT44(0x43300000, (unsigned int)last ^ 0x80000000) - magic2);

            recOff = recOff + 4;
            rowStride = rowStride + 0xe0;
        }
        slotArrOff = slotArrOff + 4;
    }
    FUN_80382700();
    return header;
}

// ---------------------------------------------------------------------------
// anim_channel_build_desc (FUN_8019b380)
// Build one channel's spreading descriptor `obj` (r3) from the codebook curve
// params. Computes the two log2 band/stage counts, allocates three param arrays
// (obj[4]/[5]/[6], each param_4 words), fills the bark-band center table
// (obj[4]) by interpolating the edge LUT (DAT_8041ffd8) across the run, fills
// the per-band spread index table (obj[5]) via the log warp, then builds the
// masking envelope (anim_curve_build_envelope -> obj[2]) and a 3-plane
// coefficient table (obj[3]) sampled from the envelope records. `desc`/`obj`
// pair arrives via FUN_803826c0; call args: (obj, desc, cb, mid, flags).
// ---------------------------------------------------------------------------
void FUN_8019b380(int objArg, int *descArg, unsigned int *cb, int mid, unsigned int flags) {
    (void)objArg; (void)descArg;
    unsigned long long pair = FUN_803826c0();
    unsigned int *obj = (unsigned int *)((unsigned long long)pair >> 0x20);
    unsigned int ctx = (unsigned int)pair;
    unsigned int nBand = mid;        // param_4 (band count)
    unsigned int span  = flags;      // param_5 (frequency span)

    memset_wrapper((int)obj, 0, 0x30);
    obj[9] = *cb;

    double lg2 = FUN_80390b04(DOUBLE_80616a48);
    double lgStages = FUN_80390b04((double)(FLOAT_80616a88 *
                        (float)((double)CONCAT44(0x43300000, *cb ^ 0x80000000) - DOUBLE_806169f8)));
    lgStages = FUN_803907b8(lgStages / lg2);
    obj[8] = (unsigned int)(int)(lgStages - DOUBLE_80616a70);

    double warpS = DOUBLE_806169f0 *
        (double)(FLOAT_80616a8c * (float)((double)CONCAT44(0x43300000, span ^ 0x80000000) - DOUBLE_806169f8));
    double nbD = (double)CONCAT44(0x43300000, nBand ^ 0x80000000) - DOUBLE_806169f8;
    double lgLo = FUN_80390b04(warpS / nbD);
    obj[7] = (unsigned int)(int)(((double)CONCAT44(0x43300000, (1 << (obj[8] + 1)) ^ 0x80000000) - DOUBLE_806169f8)
                 * (DOUBLE_80616a50 * lgLo - DOUBLE_80616a40)
                 - ((double)CONCAT44(0x43300000, *cb ^ 0x80000000) - DOUBLE_806169f8));

    double warpS2 = DOUBLE_806169f0 *
        (double)((FLOAT_80616a8c + (float)((double)CONCAT44(0x43300000, nBand ^ 0x80000000) - DOUBLE_806169f8))
                 * (float)((double)CONCAT44(0x43300000, span ^ 0x80000000) - DOUBLE_806169f8));
    double lgHi = FUN_80390b04(warpS2 / (((double)CONCAT44(0x43300000, nBand ^ 0x80000000)) - DOUBLE_806169f8));
    obj[10] = (unsigned int)(((int)(((double)CONCAT44(0x43300000, (1 << (obj[8] + 1)) ^ 0x80000000) - DOUBLE_806169f8)
                  * (DOUBLE_80616a50 * lgHi - DOUBLE_80616a40) + DOUBLE_806169f0)) - (int)obj[7]) + 1;

    unsigned int byteN = nBand << 2;
    obj[4] = (unsigned int)FUN_801a13e0(byteN);
    obj[5] = (unsigned int)FUN_801a13e0(byteN);
    obj[6] = (unsigned int)FUN_801a13e0(byteN);
    obj[1] = ctx;
    obj[0] = nBand;
    obj[0xb] = span;

    // (a) bark-band center table obj[4]: interpolate the edge LUT across the run
    double eScale = DOUBLE_80616a38, eBias = DOUBLE_80616a40, eStep = DOUBLE_80616a60;
    double recip = DOUBLE_80616a48, magic = DOUBLE_806169f8, maskA = DOUBLE_80616a18;
    int outOff = 0;
    int lutOff = 0;
    int wrote = 0;
    for (int e = 0; e < 0x57; e = e + 1) {
        double ed = (double)CONCAT44(0x43300000, (e + 1) ^ 0x80000000) - magic;
        double ex = FUN_80390ac4(eScale * (eBias + (eStep * ed - recip)));
        double lgf = FUN_803907b8((((double)CONCAT44(0x43300000, nBand ^ 0x80000000) - magic) * recip * ex)
                        / ((double)CONCAT44(0x43300000, span ^ 0x80000000) - magic));
        int target = (int)lgf;
        double v = (double)*(float *)(&DAT_8041ffd8 + lutOff);
        if (wrote < target) {
            double delta = (double)*(float *)((int)&DAT_8041ffd8 + lutOff + 4) - v;
            double denom = (double)CONCAT44(0x43300000, (target - wrote) ^ 0x80000000) - DOUBLE_806169f8;
            for (; (wrote < target) && (wrote < (int)nBand); wrote = wrote + 1) {
                double cell = maskA + v;
                v = (double)(float)(v + (double)((float)delta / (float)denom));
                *(float *)(obj[4] + outOff) = (float)cell;
                outOff = outOff + 4;
            }
        }
        lutOff = lutOff + 4;
    }

    // (b) per-band spread index table obj[5] via the log warp
    int idxOff = 0;
    for (int band = 0; band < (int)nBand; band = band + 1) {
        double bd = (double)CONCAT44(0x43300000, (unsigned int)band ^ 0x80000000);
        double nbf = (double)CONCAT44(0x43300000, nBand ^ 0x80000000);
        double lg = FUN_80390b04((DOUBLE_806169f0 * (double)(float)(FLOAT_80616a8c + (double)(float)(bd - DOUBLE_806169f8))
                        * ((double)CONCAT44(0x43300000, span ^ 0x80000000) - DOUBLE_806169f8))
                        / (nbf - DOUBLE_806169f8));
        double lo = (double)CONCAT44(0x43300000, (unsigned int)((1 << (obj[8] + 1)) ^ 0x80000000));
        *(int *)(obj[5] + idxOff) = (int)((lo - DOUBLE_806169f8) * (DOUBLE_80616a50 * lg - DOUBLE_80616a40) + DOUBLE_806169f0);
        idxOff = idxOff + 4;
    }

    // (c) build the masking envelope + the 3-plane coefficient table obj[3]
    double spanRecip = (double)(float)((DOUBLE_806169f0 * ((double)CONCAT44(0x43300000, span ^ 0x80000000) - DOUBLE_806169f8))
                        / ((double)CONCAT44(0x43300000, nBand ^ 0x80000000) - DOUBLE_806169f8));
    obj[2] = (unsigned int)anim_curve_build_envelope(spanRecip,
                (double)*(float *)(ctx + 0x18), (double)*(float *)(ctx + 0x1c));
    obj[3] = (unsigned int)FUN_801a13e0(0xc);
    for (int p = 0; p < 3; p = p + 1) {
        *(int *)(obj[3] + p * 4) = (int)FUN_801a13e0(byteN);
    }

    double invN = DOUBLE_80616a48 * ((double)CONCAT44(0x43300000, nBand ^ 0x80000000) - DOUBLE_806169f8);
    double clampLo = (double)FLOAT_80616a10, clampHi = (double)FLOAT_80616ab4;
    double lScale = DOUBLE_80616a50, lBias = DOUBLE_80616a40, lRecip = DOUBLE_80616a48;
    double lOff = DOUBLE_80616a70, mgc = DOUBLE_806169f8, lNum = DOUBLE_806169f0;
    int coefOff = 0;
    for (int band = 0; band < (int)nBand; band = band + 1) {
        double bd = (double)CONCAT44(0x43300000, (unsigned int)band ^ 0x80000000);
        double lg = FUN_80390b04(((lNum + (bd - mgc))
                        * ((double)CONCAT44(0x43300000, span ^ 0x80000000) - mgc)) / invN);
        double frac = (double)(float)(lRecip * (lScale * lg - lBias));
        if (frac < clampLo) frac = clampLo;
        if (clampHi <= frac) frac = clampHi;
        double fd = (double)CONCAT44(0x43300000, (unsigned int)(int)frac ^ 0x80000000);
        double t = (double)(float)(frac - (double)(float)(fd - mgc));
        int rowOff = 0;
        for (int p = 0; p < 3; p = p + 1) {
            int rec = obj[1] + rowOff + (int)frac * 4;
            rowOff = rowOff + 0x44;
            *(float *)(coefOff + *(int *)(obj[3] + p * 4)) =
                (float)((double)*(float *)(rec + 0x84) * (lOff - t) +
                        (double)(float)((double)*(float *)(rec + 0x88) * t));
        }
        coefOff = coefOff + 4;
    }
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// anim_subctx_free_entry (FUN_8019bbfc)
// Tear down one sub-context descriptor entry (the r3 object via FUN_803826e8):
// free the three plane buffers (+0x10/+0x14/+0x18), the 0x11 x 8 spread-record
// grid (+8, each row 8 pointers then the row itself), the 3-plane coefficient
// table (+0xc), then zero the 0x30-byte header. No-op on null.
// ---------------------------------------------------------------------------
void FUN_8019bbfc(void) {
    int e = (int)FUN_803826e8();
    if (e != 0) {
        if (*(int *)(e + 0x10) != 0) FUN_801a14dc(*(int *)(e + 0x10));
        if (*(int *)(e + 0x14) != 0) FUN_801a14dc(*(int *)(e + 0x14));
        if (*(int *)(e + 0x18) != 0) FUN_801a14dc(*(int *)(e + 0x18));
        if (*(int *)(e + 8) != 0) {
            int rowOff = 0;
            for (int r = 0; r < 0x11; r = r + 1) {
                int colOff = 0;
                for (int col = 0; col < 8; col = col + 1) {
                    FUN_801a14dc(*(int *)(*(int *)(rowOff + *(int *)(e + 8)) + colOff));
                    colOff = colOff + 4;
                }
                FUN_801a14dc(*(int *)(*(int *)(e + 8) + rowOff));
                rowOff = rowOff + 4;
            }
            FUN_801a14dc(*(int *)(e + 8));
        }
        if (*(int *)(e + 0xc) != 0) {
            int colOff = 0;
            for (int col = 0; col < 3; col = col + 1) {
                FUN_801a14dc(*(int *)(*(int *)(e + 0xc) + colOff));
                colOff = colOff + 4;
            }
            FUN_801a14dc(*(int *)(e + 0xc));
        }
        memset_wrapper(e, 0, 0x30);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_envelope_fill_band (FUN_8019bd08)
// Inner masking-envelope band fill: given a source magnitude `base` and a
// neighbour delta `bias`, pick the spreading template row (index from
// (base+bias) scaled by DOUBLE_80616ab8, clamped to [0,7]), then for each band
// in that row's [start,end) span, max the destination magnitude run `pOut` with
// (base + template[band]), stepping the destination band index by `step` and
// stopping at `cap`.
// ---------------------------------------------------------------------------
void anim_envelope_fill_band(double base, double bias, int pOut, int pBands,
                             unsigned int lo, int cap, unsigned int step) {
    unsigned int sel = (unsigned int)(DOUBLE_80616ab8 * ((double)(float)(base + bias) - DOUBLE_80616a28));
    sel = sel & ~((int)sel >> 0x1f);
    unsigned int rowIdx = ((int)sel < 8) ? sel : 7;
    float *row = *(float **)(pBands + rowIdx * 4);
    int start = (int)*row;
    int count = (int)row[1] - start;
    int off = start << 2;
    int dstBand = (int)(((*row - FLOAT_80616ab4)
                    * (float)((double)CONCAT44(0x43300000, step ^ 0x80000000) - DOUBLE_806169f8)
                    + (float)((double)CONCAT44(0x43300000, lo ^ 0x80000000) - DOUBLE_806169f8))
                    - (float)((double)CONCAT44(0x43300000, (unsigned int)((int)step >> 1) ^ 0x80000000) - DOUBLE_806169f8));
    if (start < (int)row[1]) {
        do {
            if (0 < dstBand) {
                float v = (float)(base + (double)*(float *)((int)row + off + 8));
                if (*(float *)(pOut + dstBand * 4) < v) {
                    *(float *)(pOut + dstBand * 4) = v;
                }
            }
            dstBand = dstBand + step;
            if (cap <= dstBand) return;
            off = off + 4;
            count = count + -1;
        } while (count != 0);
    }
}

// ---------------------------------------------------------------------------
// anim_envelope_apply (FUN_8019be28)
// Apply the masking envelope across a channel's decoded band magnitudes. Walks
// the run coalescing equal spread-index groups (taking the group max), and for
// each group whose magnitude exceeds the neighbour threshold (FLOAT_80616ac0 +
// running value) dispatches anim_envelope_fill_band with the template row
// selected by the group's shifted spread index (clamped to [0,0x10]). The
// state/plane pair arrives via FUN_803826dc; param_3=src mags, param_4=threshold
// run, param_5=output run.
// ---------------------------------------------------------------------------
void anim_envelope_apply(unsigned int arg0, unsigned int arg1, int pSrc, int pThresh, int pOut) {
    (void)arg0; (void)arg1;
    // callee-saved FP (only feeds removed spill blocks)
    double extraout_f1 = 0;
    unsigned long long pair = FUN_803826dc();
    int *st = (int *)((unsigned long long)pair >> 0x20);
    int rowTable = (int)pair;

    double neighDelta = (double)FLOAT_80616ac0;
    int nBand = *st;
    double bias = (double)(float)((double)*(float *)(st[1] + 0x1f0) - extraout_f1);

    int off = 0;
    for (int band = 0; band < nBand; band = band + 1) {
        int idxTab = st[5];
        int scanOff = band << 2;
        int *pIdx = (int *)(idxTab + off);
        double v = (double)*(float *)(pSrc + off);
        while ((band + 1 < nBand) && (*pIdx == *(int *)(idxTab + scanOff + 4))) {
            scanOff = scanOff + 4;
            band = band + 1;
            off = off + 4;
            if (v < (double)*(float *)(pSrc + scanOff)) {
                v = (double)*(float *)(pSrc + scanOff);
            }
        }
        if (*(float *)(pThresh + off) < (float)(neighDelta + v)) {
            int sel = *pIdx >> (st[8] & 0x3f);
            if (0x10 < sel) sel = 0x10;
            if (sel < 0) sel = 0;
            anim_envelope_fill_band(v, bias, pOut, *(int *)(rowTable + sel * 4),
                                    *(int *)(idxTab + off) - st[7], st[10], (unsigned int)st[9]);
        }
        off = off + 4;
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// anim_hull_scan (FUN_8019bf5c)
// Monotone-stack scan over a run of `param_3` float keys: maintains a small
// ascending stack of (value,index) pairs, popping entries that the incoming
// value dominates (subject to two look-back span guards), then expands the
// accepted stack entries back into the output run, each replicated across the
// span to the next accepted index (clamped to the run length). Used by the
// pack/encode path to build a piecewise-constant lower hull. The state pair +
// the runtime-sized value/index stacks arrive via the __save_gpr_28 frame;
// `param_3` is the incoming run length (r5).
// ---------------------------------------------------------------------------
void anim_hull_scan(unsigned int arg0, unsigned int arg1, int n) {
    (void)arg0; (void)arg1;
    unsigned long long pair = (unsigned long long)__save_gpr_28();
    int keysHi = (int)((unsigned long long)pair >> 0x20);   // input keys (r3 hi)
    int spanBase = (int)pair;                               // span base (r4 lo)

    // runtime-sized value + index stacks (were the &stack..+register004 chain)
    float *valStack = (float *)_alloca((n * 4 + 7) & ~7);
    int   *idxStack = (int *)_alloca((n * 4 + 7) & ~7);

    int top = 0;
    int outIdx = 0;
    int outOff = 0;
    int keyOff = 0;
    int i = 0;
    if (0 < n) {
        do {
            if (top < 2) {
                idxStack[top] = i;
                valStack[top + 2] = *(float *)(keysHi + keyOff);
                top = top + 1;
            } else {
                int s = top;
                int sOff = top << 2;
                bool pushed = false;
                while (true) {
                    float v = *(float *)((int)valStack + sOff + 4);
                    if (*(float *)(keysHi + keyOff) < v) break;
                    // two look-back span guards
                    if ((spanBase + idxStack[s - 1] <= i) || (s < 2) ||
                        (*(float *)((int)valStack + sOff) < v) ||
                        (spanBase + idxStack[s - 2] <= i)) {
                        idxStack[s] = i;
                        valStack[s + 2] = *(float *)(keysHi + keyOff);
                        top = s + 1;
                        pushed = true;
                        break;
                    }
                    s = s - 1;
                    sOff = sOff - 4;
                }
                if (!pushed) {
                    idxStack[s] = i;
                    valStack[s + 2] = *(float *)(keysHi + keyOff);
                    top = s + 1;
                }
            }
            i = i + 1;
            keyOff = keyOff + 4;
        } while (i < n);
    }

    // expand accepted stack entries back into the run
    int e = 0;
    for (int k = 0; k < top; k = k + 1) {
        int end;
        if ((top + -1 <= k) || (valStack[e + 3] <= valStack[e + 2])) {
            end = spanBase + idxStack[e] + 1;
        } else {
            end = idxStack[e + 1];
        }
        if (n < end) end = n;
        int fill = end - outIdx;
        if (outIdx < end) {
            do {
                outIdx = outIdx + 1;
                *(float *)(keysHi + outOff) = valStack[e + 2];
                outOff = outOff + 4;
                fill = fill - 1;
            } while (fill != 0);
        }
        e = e + 1;
    }
    __restore_gpr_28();
}

// ===========================================================================
// batch_518 (0x8019c118 - 0x8019ccac): the per-channel spectral transform /
// masking-threshold / encode-commit pipeline invoked by anim_block_encode
// (batch_515). Given a channel's decoded band magnitudes it: builds the tonal
// masking threshold via a running-moment (sum, weighted-sum, second-moment)
// least-squares spread fit (anim_spectral_lsf_solve), upsamples the per-band
// hull back to the frame grid (anim_env_upsample over anim_hull_scan), folds in
// the psychoacoustic envelope (anim_magnitude_threshold), selects the retained
// band set (anim_channel_select), and commits the quantised residual through the
// scale LUT (anim_apply_scale_lut / anim_pair_scale). Plus the mid/side stereo
// decision (anim_ms_stereo_pair) and two signed-hypot helpers.
//
// The FUN_803826xx helpers are the callee-saved-register prologue carriers;
// the big transforms _alloca a runtime-sized per-band scratch (the decompiler's
// &stack.. + register0x00000004 chain). in_f31 / in_ps31_1 are unsourced
// callee-saved FP registers feeding only removed spill blocks (seeded 0).
// ===========================================================================
extern "C" {
extern double FUN_80390df4(double x);   // sqrt (magnitude / hypot core)
// scale + interpolation LUTs (0x80425ac0 quantiser scale, 0x80425ec0 tilt)
extern float  DAT_80425ac0;             // quantiser scale LUT (index -> gain)
extern float  DAT_80425ec0;             // spectral-tilt interp LUT
// pipeline constants (0x80616a00, 0x80616ac4, 0x80616ac8)
extern float  FLOAT_80616a00;           // +inf-style seed (envelope max init)
extern float  FLOAT_80616ac4;           // transform base weight
extern double DOUBLE_80616ac8;          // mid/side flip threshold ratio
} // extern "C"

// Forward decls for this batch's own helpers (referenced before definition).
void   anim_spectral_lsf_solve(double base, double p2, double p3, double p4,
                               int n, int order, float *weights, int pOut, unsigned int extra); // FUN_8019c298
void   anim_env_upsample(unsigned int a, unsigned int b, int pOut);  // FUN_8019c118

// ---------------------------------------------------------------------------
// anim_env_upsample (FUN_8019c118)
// Upsample a per-band spreading hull back onto the frame grid. First runs the
// monotone lower-hull scan (anim_hull_scan) over the pipe's band values, then
// walks the sorted spread-index table (pipe[5]) coalescing runs, taking the max
// hull value across each run (clamped by the pipe[1]+0x20 ceiling), and writes
// that value into every frame slot of `pOut` covered by the run. Tail frames
// past the last band use the final hull value. state/nBands pair arrives via
// the __save_gpr_25 frame; `pOut` is the incoming output run (r5).
// ---------------------------------------------------------------------------
void anim_env_upsample(unsigned int a, unsigned int b, int pOut) {
    (void)a; (void)b;
    unsigned long long pair = (unsigned long long)__save_gpr_25();
    int *pipe = (int *)((unsigned long long)pair >> 0x20);
    int keysBase = (int)pair;
    int nBand = pipe[9];

    anim_hull_scan((unsigned int)keysBase, (unsigned int)nBand, pipe[10]);

    float seed = FLOAT_80616a00;
    int band = (*(int *)pipe[5] - pipe[7]) - (nBand >> 1);
    int keyOff = band * 4;
    int written = 0;
    int idxOff = 0;   // walk of the coalesced spread-index groups
    int outOff = 0;
    while (written + 1 < *pipe) {
        int base = pipe[7];
        float hull = *(float *)(keysBase + keyOff);
        if (*(float *)(pipe[1] + 0x20) < hull) {
            hull = *(float *)(pipe[1] + 0x20);
        }
        int scanOff = band << 2;
        while (band + 1 <=
               (*(int *)(pipe[5] + idxOff) + ((int *)(pipe[5] + idxOff))[1] >> 1) - base) {
            scanOff = scanOff + 4;
            band = band + 1;
            float v = *(float *)(keysBase + scanOff);
            keyOff = keyOff + 4;
            if (((seed < v) && (v < hull)) || (seed == hull)) {
                hull = v;
            }
        }
        for (; (written < *pipe) && (*(int *)(pipe[5] + outOff) <= band + base);
             outOff = outOff + 4) {
            if (*(float *)(pOut + outOff) < hull) {
                *(float *)(pOut + outOff) = hull;
            }
            written = written + 1;
            idxOff = idxOff + 4;
        }
    }
    int tailOff = written << 2;
    float last = *(float *)(keysBase + pipe[10] * 4 + -4);
    for (; written < *pipe; written = written + 1) {
        if (*(float *)(pOut + tailOff) < last) {
            *(float *)(pOut + tailOff) = last;
        }
        tailOff = tailOff + 4;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_spectral_lsf_solve (FUN_8019c298)
// Running-moment least-squares spread solver. Builds five prefix-sum tables
// over the (offset) input weight vector `weights` (sum of w, w*i, i^2*w, w*i^3,
// w^2 ... the exact moment set the decompiler recovered) in an _alloca'd
// per-band scratch, then walks the sorted band index-pair table (base carried
// by the FUN_803826f0 spill object; each packed entry is (hi<<16|lo)) computing
// the local quadratic-fit value (num/den from the moment differences) clamped to
// [0, +inf) and biased, writing it to `pOut`. Runs three passes (lower half,
// upper half, tail) then an optional second sweep gated by `extra` (param_9)
// that takes the min against the existing output. The five row bases are
// contiguous _alloca slots (pfVar18..pfVar22 in the raw).
// ---------------------------------------------------------------------------
void anim_spectral_lsf_solve(double base, double p2, double p3, double p4,
                             int n, int order, float *weights, int pOut, unsigned int extra) {
    (void)base; (void)p2; (void)p3; (void)p4; (void)order;
    double in_f31 = 0, in_ps31_1 = 0;
    float spill0 = (float)in_f31, spill1 = (float)in_ps31_1;
    (void)spill0; (void)spill1;

    unsigned long long pair = FUN_803826f0();
    int nBand = (int)((unsigned long long)pair >> 0x20);
    int idxTable = (int)pair;
    double bias = 0.0;   // (raw: extraout_f1 additive bias, sourced from caller FP)

    // five contiguous prefix-moment rows (runtime-sized)
    int rowBytes = (nBand + 2) * 4;
    float *m0 = (float *)_alloca(rowBytes);   // pfVar18  sum(w)
    float *m1 = (float *)_alloca(rowBytes);   // pfVar19  sum(w*weight_scale)
    float *m2 = (float *)_alloca(rowBytes);   // pfVar20  sum(i^2)
    float *m3 = (float *)_alloca(rowBytes);   // pfVar21  sum(w*i)
    float *m4 = (float *)_alloca(rowBytes);   // pfVar22  sum(i)

    float w0 = (float)((double)weights[0] + bias);
    if (w0 < FLOAT_80616a58) w0 = FLOAT_80616a58;
    float w2 = (float)(DOUBLE_806169f0 * (double)(w0 * w0));
    float acc0 = FLOAT_80616a10 + w2;
    float acc3 = w2 * w0 + FLOAT_80616a10;
    m0[0] = acc0; m1[0] = acc0; m2[0] = FLOAT_80616a10; m3[0] = acc3; m4[0] = FLOAT_80616a10;

    float sM2 = FLOAT_80616a10, sM4 = FLOAT_80616a10, coef = FLOAT_80616a58, sM1 = acc0;
    {
        int i = 4;
        int rem = nBand - 1;
        for (int k = 0; k < rem; k = k + 1) {
            float wi = (float)(bias + (double)*(float *)((int)weights + i));
            if (wi < FLOAT_80616a58) wi = FLOAT_80616a58;
            float wi2 = wi * wi;
            float wc = wi2 * coef;
            acc0 = acc0 + wi2;
            acc3 = wi2 * wi + acc3;
            sM1 = sM1 + wc;
            *(float *)((int)m0 + i) = acc0;
            sM4 = coef * wc + sM4;
            sM2 = wi * wc + sM2;
            *(float *)((int)m1 + i) = sM1;
            coef = coef + FLOAT_80616a58;
            *(float *)((int)m2 + i) = sM4;
            *(float *)((int)m3 + i) = acc3;
            *(float *)((int)m4 + i) = sM2;
            i = i + 4;
        }
    }

    // pass 1: lower-half entries (hi index negative packing)
    double floorV = (double)FLOAT_80616a10;
    int accepted = 0;
    double step = (double)FLOAT_80616a58;
    double idxD = floorV;
    int off = 0;
    double num = 0, den = 1, mid = 0;
    while (true) {
        unsigned int e = *(unsigned int *)(idxTable + off);
        int hi = (int)e >> 0x10;
        if (-1 < hi) break;
        unsigned int lo = e & 0xffff;
        float d = m1[lo] - m1[-hi];
        num = (double)((m0[lo] + m0[-hi]) * (m4[lo] - m4[-hi]) - d * (m3[lo] + m3[-hi]));
        den = (double)((m0[lo] + m0[-hi]) * (m2[lo] + m2[-hi]) - d * d);
        mid = (double)((m3[lo] + m3[-hi]) * (m2[lo] + m2[-hi]) - d * (m4[lo] - m4[-hi]));
        double v = (double)(float)((double)(float)(idxD * num + mid) / den);
        if (v < floorV) v = floorV;
        accepted = accepted + 1;
        idxD = (double)(float)(idxD + step);
        *(float *)(pOut + off) = (float)(v - bias);
        off = off + 4;
    }

    // pass 2: upper-half entries
    off = accepted << 2;
    while (true) {
        unsigned int e = *(unsigned int *)(idxTable + off);
        unsigned int lo = e & 0xffff;
        int hi = (int)e >> 0x10;
        if (nBand <= (int)lo) break;
        float d = m1[lo] - m1[hi];
        num = (double)((m0[lo] - m0[hi]) * (m4[lo] - m4[hi]) - d * (m3[lo] - m3[hi]));
        den = (double)((m0[lo] - m0[hi]) * (m2[lo] - m2[hi]) - d * d);
        mid = (double)((m3[lo] - m3[hi]) * (m2[lo] - m2[hi]) - d * (m4[lo] - m4[hi]));
        double v = (double)(float)((double)(float)(idxD * num + mid) / den);
        if (v < floorV) v = floorV;
        accepted = accepted + 1;
        idxD = (double)(float)(idxD + step);
        *(float *)(pOut + off) = (float)(v - bias);
        off = off + 4;
    }

    // pass 3: tail (reuse the last num/den/mid)
    int tail = nBand - accepted;
    off = accepted << 2;
    if (accepted < nBand) {
        do {
            double v = (double)(float)((double)(float)(idxD * num + mid) / den);
            if (v < floorV) v = floorV;
            idxD = (double)(float)(idxD + step);
            *(float *)(pOut + off) = (float)(v - bias);
            off = off + 4;
            tail = tail - 1;
        } while (tail != 0);
    }

    // optional second sweep (windowed, gated by `extra`), min against existing
    if (0 < (int)extra) {
        int half = ((int)extra >> 1) + (unsigned int)((int)extra < 0 && (extra & 1) != 0);
        int w = 0;
        int o = 0;
        double id = (double)FLOAT_80616a10;
        while (true) {
            int hiI = w + half;
            int loI = hiI - (int)extra;
            if (-1 < loI) break;
            float d = m1[hiI] - m1[-loI];
            num = (double)((m0[hiI] + m0[-loI]) * (m4[hiI] - m4[-loI]) - d * (m3[hiI] + m3[-loI]));
            den = (double)((m0[hiI] + m0[-loI]) * (m2[hiI] + m2[-loI]) - d * d);
            mid = (double)((m3[hiI] + m3[-loI]) * (m2[hiI] + m2[-loI]) - d * (m4[hiI] - m4[-loI]));
            float v = (float)((double)(float)((double)(float)(id * num + mid) / den) - bias);
            if (v < *(float *)(pOut + o)) *(float *)(pOut + o) = v;
            id = (double)(float)(id + step);
            w = w + 1;
            o = o + 4;
        }
        o = w << 2;
        while (true) {
            int hiI = w + half;
            int loI = hiI - (int)extra;
            if (nBand <= hiI) break;
            float d = m1[hiI] - m1[loI];
            num = (double)((m0[hiI] - m0[loI]) * (m4[hiI] - m4[loI]) - d * (m3[hiI] - m3[loI]));
            den = (double)((m0[hiI] - m0[loI]) * (m2[hiI] - m2[loI]) - d * d);
            mid = (double)((m3[hiI] - m3[loI]) * (m2[hiI] - m2[loI]) - d * (m4[hiI] - m4[loI]));
            float v = (float)((double)(float)((double)(float)(id * num + mid) / den) - bias);
            if (v < *(float *)(pOut + o)) *(float *)(pOut + o) = v;
            id = (double)(float)(id + step);
            w = w + 1;
            o = o + 4;
        }
        tail = nBand - w;
        o = w << 2;
        if (w < nBand) {
            do {
                float v = (float)((double)(float)((double)(float)(id * num + mid) / den) - bias);
                if (v < *(float *)(pOut + o)) *(float *)(pOut + o) = v;
                id = (double)(float)(id + step);
                o = o + 4;
                tail = tail - 1;
            } while (tail != 0);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_apply_scale_lut (FUN_8019c73c)
// Commit the quantised per-band residual: for the first min(pipe[0], param_5)
// bands multiply the source magnitude (param_2 run) by the quantiser scale LUT
// (DAT_80425ac0) indexed by the per-band code (param_3 run), writing to param_4;
// zero-fill the remaining bands.
// ---------------------------------------------------------------------------
void FUN_8019c73c(int *pipe, int pSrc, int pCodes, int pOut, int count) {
    int n = *pipe;
    if (n < count) count = n;
    int written = 0;
    int off = 0;
    if (0 < count) {
        do {
            written = written + 1;
            *(float *)(pOut + off) =
                *(float *)(pSrc + off) * *(float *)(&DAT_80425ac0 + *(int *)(pCodes + off) * 4);
            off = off + 4;
            count = count - 1;
        } while (count != 0);
    }
    float zero = FLOAT_80616a10;
    int rem = n - written;
    off = written << 2;
    if (n <= written) return;
    do {
        *(float *)(pOut + off) = zero;
        off = off + 4;
        rem = rem - 1;
    } while (rem != 0);
}

// ---------------------------------------------------------------------------
// anim_spectral_transform (FUN_8019c7b4)
// Per-channel spectral transform stage. Runs the LSF spread solve twice: once
// with the base weight (FLOAT_80616ac4) over the input, once with the residual
// (input minus the first result) using the codebook window count
// (pipe[1]+0x80); differences the two into the residual scratch, then maps each
// band through the descriptor's log-gain table (pipe[1]+0x150, index clamped
// [0,0x27]) added to the residual, writing back into `pOut`. The state/data
// pair arrives via FUN_803826e4; the per-band residual scratch is _alloca'd.
// ---------------------------------------------------------------------------
void FUN_8019c7b4(double d0, double d1, double d2, double d3, int *pipeArg, int inArg, int pOut) {
    (void)d0; (void)pipeArg; (void)inArg;
    unsigned long long pair = FUN_803826e4();
    int *pipe = (int *)((unsigned long long)pair >> 0x20);
    float *data = (float *)pair;
    int n = *pipe;

    float *resid = (float *)_alloca((n * 4 + 7) & ~7);

    anim_spectral_lsf_solve((double)FLOAT_80616ac4, d1, d2, d3, n, pipe[6], data, pOut, 0xffffffff);
    int off = 0;
    for (int i = 0; i < n; i = i + 1) {
        *(float *)((int)resid + off) = *(float *)((int)data + off) - *(float *)(pOut + off);
        off = off + 4;
    }
    anim_spectral_lsf_solve((double)FLOAT_80616a10, d1, d2, d3, n, pipe[6], resid, pOut,
                            *(unsigned int *)(pipe[1] + 0x80));
    off = 0;
    for (int i = 0; i < n; i = i + 1) {
        *(float *)((int)resid + off) = *(float *)((int)data + off) - *(float *)((int)resid + off);
        off = off + 4;
    }
    double gainBias = DOUBLE_806169f0;
    off = 0;
    for (int i = 0; i < n; i = i + 1) {
        int idx = (int)(gainBias + (double)*(float *)(pOut + off));
        if (0x27 < idx) idx = 0x27;
        if (idx < 0) idx = 0;
        *(float *)(pOut + off) =
            *(float *)(pipe[1] + idx * 4 + 0x150) + *(float *)((int)resid + off);
        off = off + 4;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// anim_magnitude_threshold (FUN_8019c904)
// Build the per-frame masking threshold run. Seeds an _alloca'd per-band scratch
// with +inf (FLOAT_80616a00), seeds the output run with the descriptor's floor
// bias (max of param_2 + desc[1]+4 and desc[1]+8), applies the psychoacoustic
// envelope (anim_envelope_apply) and upsamples the hull onto the frame grid
// (anim_env_upsample). state via __save_gpr_28; `pOut` = param_5.
// ---------------------------------------------------------------------------
void FUN_8019c904(double d0, double d1, int *pipeArg, int keys, int pOut) {
    (void)d0; (void)pipeArg; (void)keys;
    unsigned long long pair = (unsigned long long)__save_gpr_28();
    int *pipe = (int *)((unsigned long long)pair >> 0x20);
    int n = *pipe;

    float *scratch = (float *)_alloca((pipe[10] * 4 + 7) & ~7);
    float floorV = (float)(d1 + (double)*(float *)(pipe[1] + 4));
    int off = 0;
    for (int i = 0; i < pipe[10]; i = i + 1) {
        scratch[i] = FLOAT_80616a00;
    }
    if (floorV < *(float *)(pipe[1] + 8)) {
        floorV = *(float *)(pipe[1] + 8);
    }
    off = 0;
    int rem = n;
    if (0 < n) {
        do {
            *(float *)(pOut + off) = floorV + *(float *)(pipe[4] + off);
            off = off + 4;
            rem = rem - 1;
        } while (rem != 0);
    }
    anim_envelope_apply((unsigned int)(int)pipe, (unsigned int)pipe[2], (int)pair, pOut, (int)scratch);
    anim_env_upsample((unsigned int)(int)pipe, (unsigned int)(int)scratch, pOut);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// anim_channel_select (FUN_8019ca00)
// Combine the two candidate threshold runs into the retained-band envelope: for
// each of pipe[0] bands take max( min(src[i] + planeSel[i], ceiling pipe[1]+0x6c),
// altThresh[i] + planeFloor(pipe[1]+mode*4+0xc) ) into `pOut`. `mode` selects the
// alternate plane (pipe[3][mode]) and its floor offset.
// ---------------------------------------------------------------------------
void FUN_8019ca00(int *pipe, int pSrc, int pAlt, int mode, int pOut) {
    int n = *pipe;
    int off = 0;
    float planeFloor = *(float *)(pipe[1] + mode * 4 + 0xc);
    if (0 < n) {
        do {
            float v = *(float *)(pSrc + off) + *(float *)(*(int *)(mode * 4 + pipe[3]) + off);
            if (*(float *)(pipe[1] + 0x6c) < v) {
                v = *(float *)(pipe[1] + 0x6c);
            }
            float alt = planeFloor + *(float *)(pAlt + off);
            if (v < alt) v = alt;
            *(float *)(pOut + off) = v;
            off = off + 4;
            n = n - 1;
        } while (n != 0);
    }
}

// ---------------------------------------------------------------------------
// anim_ms_stereo_pair (FUN_8019ca74)
// Mid/side stereo decision for one coefficient pair (*param_3, *param_4). Picks
// the dominant channel by |coeff| (tie-broken by the |scalar| args), rewrites
// the pair as sum/difference so the residual channel holds the delta, then if
// the residual exceeds DOUBLE_80616ac8 * |dominant| it flips to a scaled
// negative form (decorrelation guard).
// ---------------------------------------------------------------------------
void anim_ms_stereo_pair(double s0, double s1, float *pA, float *pB) {
    int dir = (int)(unsigned int)(ABS(*pB) < ABS(*pA)) - (int)(unsigned int)(ABS(*pA) < ABS(*pB));
    if (dir == 0) {
        dir = (int)(unsigned char)((ABS(s1) < ABS(s0)) << 1) - 1;
    }
    if (dir == 1) {
        float a = *pA;
        if (a <= FLOAT_80616a10) { a = *pB - a; }
        else { a = a - *pB; }
        *pB = a;
    } else {
        float b = *pB;
        float d;
        if (b <= FLOAT_80616a10) { d = b - *pA; }
        else { d = *pA - b; }
        *pB = d;
        *pA = b;
    }
    if ((double)*pB <= DOUBLE_80616ac8 * ABS((double)*pA)) {
        return;
    }
    *pB = (float)(DOUBLE_80616a48 * -ABS((double)*pA));
    *pA = -*pA;
}

// ---------------------------------------------------------------------------
// anim_pair_scale (FUN_8019cb58)
// Scale one pair-list coefficient by the product of the tilt-interp LUT
// (DAT_80425ec0, indexed by the clamped bit-length of |a-b|) and the quantiser
// scale LUT (DAT_80425ac0, indexed by the min/max-selected code), times the
// incoming double weight; clears the companion output word.
// ---------------------------------------------------------------------------
void anim_pair_scale(double w, unsigned int a, unsigned int b, float *pOut, unsigned int *pClear) {
    float zero = FLOAT_80616a10;
    unsigned int sgn = (unsigned int)((int)(a - b) >> 0x1f);
    unsigned int mag = 0x1f - ((sgn ^ (a - b)) - sgn);
    unsigned int selMask = 0xffffffff -
        (unsigned int)((int)(((int)(a ^ b) >> 1) - ((a ^ b) & a)) >> 0x1f);
    *pOut = (float)(w *
        (double)((FLOAT_80616a58 + *(float *)(&DAT_80425ec0 + (0xffffffffU - ((int)mag >> 0x1f) & mag) * 4))
                 * *(float *)(&DAT_80425ac0 + (b & selMask | a & ~selMask) * 4)));
    *pClear = (unsigned int)(int)zero;   // (raw stores FLOAT_80616a10 bit-pattern; 0.0f)
}

// ---------------------------------------------------------------------------
// anim_hypot_signed (FUN_8019cbdc)
// Signed generalized magnitude of (x,y): by quadrant, returns +/- sqrt of
// (y^2 - x^2), (x^2 - y^2), or (x^2 + y^2) so the sign follows which axis
// dominates. Used to fold a complex coefficient pair into a signed scalar.
// ---------------------------------------------------------------------------
double anim_hypot_signed(double x, double y) {
    double r;
    if (x <= DOUBLE_80616a08) {
        if (DOUBLE_80616a08 <= y) {
            if (-x <= y) {
                r = (double)(float)FUN_80390df4((double)(float)(y * y - (double)(float)(x * x)));
            } else {
                r = (double)(float)-FUN_80390df4((double)(float)(x * x - (double)(float)(y * y)));
            }
        } else {
            r = (double)(float)-FUN_80390df4((double)(float)(x * x + (double)(float)(y * y)));
        }
    } else if (y <= DOUBLE_80616a08) {
        if (x <= -y) {
            r = (double)(float)-FUN_80390df4((double)(float)(y * y - (double)(float)(x * x)));
        } else {
            r = (double)(float)FUN_80390df4((double)(float)(x * x - (double)(float)(y * y)));
        }
    } else {
        r = (double)(float)FUN_80390df4((double)(float)(x * x + (double)(float)(y * y)));
    }
    return r;
}

// ---------------------------------------------------------------------------
// anim_hypot_signed2 (FUN_8019ccac)
// Sign-carrying magnitude variant: always sqrt(x^2 + y^2), with the sign chosen
// by quadrant (negative when x is below the pivot and y is small/negative, or
// when y is below the pivot and x is dominant). The magnitude term is always the
// full hypot (unlike anim_hypot_signed which subtracts in two quadrants).
// ---------------------------------------------------------------------------
double anim_hypot_signed2(double x, double y) {
    double r;
    if (x <= DOUBLE_80616a08) {
        if (DOUBLE_80616a08 <= y) {
            if (-x <= y) {
                r = (double)(float)FUN_80390df4((double)(float)(y * y + (double)(float)(x * x)));
            } else {
                r = (double)(float)-FUN_80390df4((double)(float)(x * x + (double)(float)(y * y)));
            }
        } else {
            r = (double)(float)-FUN_80390df4((double)(float)(x * x + (double)(float)(y * y)));
        }
    } else if (y <= DOUBLE_80616a08) {
        if (x <= -y) {
            r = (double)(float)-FUN_80390df4((double)(float)(y * y + (double)(float)(x * x)));
        } else {
            r = (double)(float)FUN_80390df4((double)(float)(x * x + (double)(float)(y * y)));
        }
    } else {
        r = (double)(float)FUN_80390df4((double)(float)(x * x + (double)(float)(y * y)));
    }
    return r;
}

// ===========================================================================
// batch_519 (0x8019cd7c - 0x8019d768): the stereo/pair encode stage of the
// spectral codec. anim_block_encode drives these: it forms per-channel-pair
// magnitude spectra (anim_magnitude_pairs), rank-quantizes coefficient runs by
// sorting their addresses (anim_rank_quantize / anim_rank_quantize_mono, both
// keyed by the embedded comparator LAB_8019ce94), shapes spectral energy under
// a masking threshold (anim_spectral_shape), and folds L/R into mid/side with
// per-band scaling (anim_stereo_encode). Plus four small leaves: a
// copysign(1.0,x) helper (anim_copysign_one), a per-object reset/free pair
// (anim_ctx_reset / anim_ctx_free), and two bit population primitives
// (anim_bit_length / anim_popcount).
// ===========================================================================
extern "C" {
// spill / register-save prologue helpers new to this batch
extern unsigned long long FUN_803826d4(void);   // -> (state:src) shape pair
extern void               FUN_80382720(void);    // restore (paired with FUN_803826d4)

// embedded ascending-address comparator used by both rank-quantize sorts
extern int LAB_8019ce94;

// double LUT of per-band scale factors, indexed by band selector (8-byte stride)
extern double DAT_80425a78;

// masking / energy-shape constants
extern float  FLOAT_80616a8c;    // squared-magnitude mask threshold
extern double DOUBLE_80616a70;   // energy decrement for masked bands
extern float  FLOAT_80616a58;    // energy decrement (stereo path)
} // extern "C"

// Forward decl: the copysign(1.0,x) leaf is defined at the end of this batch
// but is called by the shaping/stereo encoders above it.
double anim_copysign_one(double x);

// ---------------------------------------------------------------------------
// anim_magnitude_pairs (FUN_8019cd7c)
// For each channel pair described in the block header (count at desc+0x484,
// L/R source indices at desc+0x488 / desc+0x888), build a magnitude spectrum
// into a freshly-allocated per-pair row. The first `split` bins (split held in
// the context sub-object at +0xa0) use anim_hypot_signed; the remaining bins up
// to the full length use anim_hypot_signed2. Rows are allocated from the scratch
// arena; the ctx pointer pair arrives via the serialize spill helper.
// ---------------------------------------------------------------------------
void anim_magnitude_pairs(int decState, int ctxLo, int *lens, int desc, int srcTable) {
    unsigned long long pair = FUN_803826c8();
    int ctx = (int)((unsigned long long)pair >> 0x20);   // hi32 == ctx pointer
    int fullLen = *lens;                                 // per-channel bin count
    int rows = msg_arena_alloc(ctx, *(int *)(desc + 0x484) << 2);
    int split = *(int *)((int)pair + *(int *)(lens[1]) * 0x3c + 0xa0);
    int off = 0;
    for (int p = 0; p < *(int *)(desc + 0x484); p = p + 1) {
        int srcL = *(int *)(srcTable + *(int *)(desc + off + 0x488) * 4);
        int srcR = *(int *)(srcTable + *(int *)(desc + off + 0x888) * 4);
        int row = msg_arena_alloc(ctx, fullLen << 2);
        *(int *)(rows + off) = row;
        int bo = 0;
        int i = 0;
        for (; i < split; i = i + 1) {
            double m = anim_hypot_signed((double)*(float *)(srcL + bo),
                                         (double)*(float *)(srcR + bo));
            *(float *)(*(int *)(rows + off) + bo) = (float)m;
            bo = bo + 4;
        }
        bo = i << 2;
        for (; i < fullLen; i = i + 1) {
            double m = anim_hypot_signed2((double)*(float *)(srcL + bo),
                                          (double)*(float *)(srcR + bo));
            *(float *)(*(int *)(rows + off) + bo) = (float)m;
            bo = bo + 4;
        }
        off = off + 4;
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// anim_rank_quantize (FUN_8019ced0)
// Per channel-pair, rank-quantize each length-`grp` group of coefficient
// addresses: gather the `grp` element addresses (grp held at ctx sub-object
// +0x200), sort them ascending by pointed-to value via LAB_8019ce94, then write
// each element's post-sort position (as (rank>>2), rounded toward zero) into the
// output row. Skips entirely when the enable flag (ctx+0x1f8) is clear. The
// per-group address scratch is _alloca'd, sized by the runtime group size.
// ---------------------------------------------------------------------------
void anim_rank_quantize(int decState, int ctxLo, int desc, int outTable) {
    unsigned long long pair = FUN_803826c8();
    int ctx = (int)((unsigned long long)pair >> 0x20);
    int *base = (int *)((unsigned int)pair);
    if (*(int *)(base[1] + 0x1f8) != 0) {
        int fullLen = *base;
        int rows = msg_arena_alloc(ctx, *(int *)(desc + 0x484) << 2);
        unsigned int grp = *(unsigned int *)(base[1] + 0x200);
        int *scratch = (int *)_alloca(grp * 4);
        int off = 0;
        for (int p = 0; p < *(int *)(desc + 0x484); p = p + 1) {
            int row = msg_arena_alloc(ctx, fullLen << 2);
            *(int *)(rows + off) = row;
            for (int g = 0; g < fullLen; g = g + grp) {
                int so = 0;
                for (unsigned int k = 0; k < grp; k = k + 1) {
                    scratch[k] = *(int *)(outTable + off) + so + g * 4;
                    so = so + 4;
                }
                FUN_8038a0a4((int)scratch, grp, 4, &LAB_8019ce94);
                int pos = 0;
                so = 0;
                for (unsigned int k = 0; k < grp; k = k + 1) {
                    int dstIdx = pos + g;
                    pos = pos + 1;
                    unsigned int rank = (unsigned int)(scratch[k] - *(int *)(outTable + off));
                    *(unsigned int *)(*(int *)(rows + off) + dstIdx * 4) =
                        ((int)rank >> 2) + (unsigned int)((int)rank < 0 && (rank & 3) != 0);
                    so = so + 4;
                }
            }
            off = off + 4;
        }
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// anim_rank_quantize_mono (FUN_8019d03c)
// Single-channel variant of anim_rank_quantize: rank-quantizes the coefficient
// run [start,len) of one buffer (start at ctx sub-object +0x1fc, group size at
// +0x200) directly into `out`. The final group is clamped to the remaining
// length. Uses the same LAB_8019ce94 ascending-address comparator and _alloca'd
// per-group address scratch.
// ---------------------------------------------------------------------------
void anim_rank_quantize_mono(int decState, int ctxLo, int out) {
    unsigned long long pair = FUN_803826d8();
    int *base = (int *)((unsigned int)((unsigned long long)pair >> 0x20));
    int fullLen = *base;
    unsigned int grp = *(unsigned int *)(base[1] + 0x200);
    int start = *(int *)(base[1] + 0x1fc);
    int *scratch = (int *)_alloca(grp * 4);
    for (int g = start; g < fullLen; g = g + grp) {
        if (fullLen < (int)(g + grp)) {
            grp = fullLen - g;
        }
        int so = 0;
        for (unsigned int k = 0; k < grp; k = k + 1) {
            scratch[k] = (int)pair + so + g * 4;
            so = so + 4;
        }
        FUN_8038a0a4((int)scratch, grp, 4, &LAB_8019ce94);
        int pos = 0;
        so = 0;
        for (unsigned int k = 0; k < grp; k = k + 1) {
            int dstIdx = pos + g;
            pos = pos + 1;
            unsigned int rank = (unsigned int)(scratch[k] - (int)pair);
            *(unsigned int *)(out + (dstIdx - start) * 4) =
                ((int)rank >> 2) + (unsigned int)((int)rank < 0 && (rank & 3) != 0);
            so = so + 4;
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// anim_spectral_shape (FUN_8019d14c)
// Perceptual energy-shaping pass over a single channel spectrum. The low
// [0,split) bins are rounded (FUN_803907b8). The mid bins are processed in
// groups of `grp` (ctx+0x200): a running band energy is accumulated, then each
// band index (from the permutation table `perm`) is either rounded normally
// (when its squared magnitude clears the mask threshold FLOAT_80616a8c) or
// snapped to +/-1 (anim_copysign_one) with a fixed energy decrement, breaking
// out once the band energy drops below the floor (ctx+0x208). Remaining bins in
// a truncated band are zeroed. High bins beyond the loop are rounded. Enable
// gate at ctx+0x1f4; split = min(len, ctx+0x1fc). state/src pair via spill.
// ---------------------------------------------------------------------------
void anim_spectral_shape(int decState, int srcLo, int out, int perm) {
    unsigned long long pair = FUN_803826d4();
    int *ctx = (int *)((unsigned int)((unsigned long long)pair >> 0x20));
    int src = (int)pair;
    int sub = ctx[1];
    int len = *ctx;
    int grp = *(int *)(sub + 0x200);
    int i = 0;
    int bo = 0;
    int split = *(int *)(sub + 0x1fc);
    if (len < split) {
        split = len;
    }
    if (*(int *)(sub + 500) != 0) {
        for (; i < split; i = i + 1) {
            double v = FUN_803907b8((double)*(float *)(src + bo));
            *(float *)(out + bo) = (float)v;
            bo = bo + 4;
        }
        double maskThresh = (double)FLOAT_80616a8c;
        for (; i + grp <= len; i = i + grp) {
            int cnt = grp;
            int eo = i << 2;
            double energy = (double)FLOAT_80616a10;
            if (i < i + grp) {
                do {
                    float *pf = (float *)(src + eo);
                    eo = eo + 4;
                    energy = (double)(float)(energy + (double)(*pf * *pf));
                    cnt = cnt - 1;
                } while (cnt != 0);
            }
            int b;
            for (b = 0; b < grp; b = b + 1) {
                int bi = *(int *)(perm + ((b + i) - split) * 4) * 4;
                double mag = (double)*(float *)(src + bi);
                if ((double)(float)(mag * mag) < maskThresh) {
                    if (energy < *(double *)(sub + 0x208)) {
                        break;
                    }
                    double snapped = anim_copysign_one(mag);
                    *(float *)(out + bi) = (float)snapped;
                    energy = energy - DOUBLE_80616a70;
                } else {
                    double r = FUN_803907b8(mag);
                    *(float *)(out + bi) = (float)r;
                    energy = energy -
                             (double)(*(float *)(src + bi) * *(float *)(src + bi));
                }
                energy = (double)(float)energy;
            }
            int rem = grp - b;
            if (b < grp) {
                do {
                    int bi2 = b + i;
                    b = b + 1;
                    *(float *)(out + *(int *)(perm + (bi2 - split) * 4) * 4) =
                        FLOAT_80616a10;
                    rem = rem - 1;
                } while (rem != 0);
            }
        }
    }
    bo = i << 2;
    for (; i < len; i = i + 1) {
        double v = FUN_803907b8((double)*(float *)(src + bo));
        *(float *)(out + bo) = (float)v;
        bo = bo + 4;
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// anim_stereo_encode (FUN_8019d328)
// The mid/side stereo encode for one block. For each channel pair with any
// active band (flags in `active`), it selects two per-band thresholds from the
// DAT_80425a78 scale LUT (indices at ctx+0xfc / +0x138), then walks the spectrum
// in windows of `win` bins. Bands below threshold (and past the low guard at
// ctx-obj +0x84) are folded through anim_pair_scale (predicted from the previous
// pair) and, when non-zero, contribute to a window energy accumulator; louder
// bands go through anim_ms_stereo_pair (plain mid/side). Out-of-range bins are
// zeroed. When the codec's masking stage is enabled (sub+0x1f8), a second sweep
// snaps the largest surviving bands to +/-1 (anim_copysign_one) while the window
// energy stays above the floor (sub+0x208). Marks each processed index in
// `active`. The data/state pair arrives via the step spill helper.
// ---------------------------------------------------------------------------
void anim_stereo_encode(int decState, int ctxLo, int *lens, int desc, int srcTable,
                        int predTable, int idxTable, int prevTable, int active, int limit) {
    unsigned long long pair = FUN_803826b4();
    int fullLen = *lens;
    int ctxObj = (int)pair + (int)((unsigned long long)pair >> 0x20) * 4;
    int off = 0;
    for (int p = 0; p < *(int *)(desc + 0x484); p = p + 1) {
        int d = desc + off;
        int idxL = *(int *)(d + 0x488) * 4;
        if ((*(int *)(active + idxL) != 0) ||
            (*(int *)(active + *(int *)(d + 0x888) * 4) != 0)) {
            int idxR = *(int *)(d + 0x888) * 4;
            double threshHi = (double)(float)*(double *)((&DAT_80425a78) +
                                  *(int *)(ctxObj + 0x138));
            int *hdr = (int *)lens[1];
            double threshLo = (double)(float)*(double *)((&DAT_80425a78) +
                                  *(int *)(ctxObj + 0xfc));
            int srcL = *(int *)(srcTable + idxL);
            int srcR = *(int *)(srcTable + idxR);
            int predL = *(int *)(prevTable + idxL);
            int outL = srcL + fullLen * 4;
            int outR = srcR + fullLen * 4;
            int predR = *(int *)(prevTable + idxR);
            int win = (hdr[0x7e] == 0) ? *lens : hdr[0x80];
            int lowGuard = *(int *)(ctxObj + *hdr * 0x3c + 0x84);
            *(int *)(active + idxL) = 1;
            *(int *)(active + *(int *)(d + 0x888) * 4) = 1;
            for (int w = 0; w < *lens; w = w + win) {
                double energy = (double)FLOAT_80616a10;
                int b;
                for (b = 0; b < win; b = b + 1) {
                    int bi = b + w;
                    if (bi < limit) {
                        int bo = bi * 4;
                        if (((lowGuard <= bi) &&
                             (ABS((double)*(float *)(srcL + bo)) < threshHi) &&
                             (ABS((double)*(float *)(srcR + bo)) < threshHi)) ||
                            ((ABS((double)*(float *)(srcL + bo)) < threshLo) &&
                             (ABS((double)*(float *)(srcR + bo)) < threshLo))) {
                            float *pOut = (float *)(outL + bo);
                            anim_pair_scale((double)*(float *)(*(int *)(predTable + off) + bo),
                                            *(unsigned int *)(predL + bo),
                                            *(unsigned int *)(predR + bo), pOut,
                                            (unsigned int *)(outR + bo));
                            double chk = FUN_803907b8((double)*pOut);
                            if (DOUBLE_80616a08 == chk) {
                                energy = (double)(float)(energy + (double)(*pOut * *pOut));
                            }
                        } else {
                            anim_ms_stereo_pair((double)*(float *)(srcL + bo),
                                                (double)*(float *)(srcR + bo),
                                                (float *)(outL + bo), (float *)(outR + bo));
                        }
                    } else {
                        *(float *)(outL + bi * 4) = FLOAT_80616a10;
                        *(float *)(outR + bi * 4) = FLOAT_80616a10;
                    }
                }
                if (*(int *)(lens[1] + 0x1f8) != 0) {
                    int k = 0;
                    while ((k < win) && (*(double *)(lens[1] + 0x208) <= energy)) {
                        int bi = *(int *)(*(int *)(idxTable + off) + (w + k) * 4);
                        if ((bi < limit) && (lowGuard <= bi)) {
                            int bo = bi * 4;
                            double chk = FUN_803907b8((double)*(float *)(outL + bo));
                            if (DOUBLE_80616a08 == chk) {
                                double snapped =
                                    anim_copysign_one((double)*(float *)(outL + bo));
                                *(float *)(outL + bo) = (float)snapped;
                                energy = (double)(float)(energy - (double)FLOAT_80616a58);
                            }
                        }
                        k = k + 1;
                    }
                }
            }
        }
        off = off + 4;
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// anim_copysign_one (FUN_8019d624)
// Return copysign(1.0f, x): take x's sign bit and OR it onto the bit pattern of
// 1.0f (0x3f800000). Yields +1.0 for non-negative x, -1.0 for negative x.
// ---------------------------------------------------------------------------
double anim_copysign_one(double x) {
    union { float f; unsigned int u; } in, out;
    in.f = (float)x;
    out.u = (in.u & 0x80000000u) | 0x3f800000u;
    return (double)out.f;
}

// ---------------------------------------------------------------------------
// anim_ctx_reset (FUN_8019d648)
// Zero the 0x714-byte codec working object then run the shared free hook
// (FUN_801a14dc) on it. No-op on a null pointer.
// ---------------------------------------------------------------------------
void anim_ctx_reset(int obj) {
    if (obj != 0) {
        memset_wrapper(obj, 0, 0x714);
        FUN_801a14dc(obj);
    }
}

// ---------------------------------------------------------------------------
// anim_ctx_free (FUN_8019d688)
// Full teardown of the encode context returned by the spill helper: free every
// per-channel object in the +0x14 array (count at +0x04), free the array; free
// every object in the +0x1c array (count at +0x18), free that array; then zero
// the 0x2c-byte header and free it.
// ---------------------------------------------------------------------------
void anim_ctx_free(void) {
    int ctx = (int)FUN_803826f0();
    if (ctx != 0) {
        int off = 0;
        for (int i = 0; i < *(int *)(ctx + 4); i = i + 1) {
            int p = *(int *)(*(int *)(ctx + 0x14) + off);
            if (p != 0) {
                FUN_801a14dc(p);
            }
            off = off + 4;
        }
        FUN_801a14dc(*(int *)(ctx + 0x14));
        off = 0;
        for (int i = 0; i < *(int *)(ctx + 0x18); i = i + 1) {
            FUN_801a14dc(*(int *)(*(int *)(ctx + 0x1c) + off));
            off = off + 4;
        }
        FUN_801a14dc(*(int *)(ctx + 0x1c));
        memset_wrapper(ctx, 0, 0x2c);
        FUN_801a14dc(ctx);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// anim_bit_length (FUN_8019d748)
// Return the number of significant bits in v (position of the highest set bit,
// i.e. floor(log2(v))+1); 0 for v==0.
// ---------------------------------------------------------------------------
int anim_bit_length(unsigned int v) {
    int n = 0;
    for (; v != 0; v = v >> 1) {
        n = n + 1;
    }
    return n;
}

// ---------------------------------------------------------------------------
// anim_popcount (FUN_8019d768)
// Return the population count (number of set bits) of v.
// ---------------------------------------------------------------------------
int anim_popcount(unsigned int v) {
    int n = 0;
    for (; v != 0; v = v >> 1) {
        n = n + (v & 1);
    }
    return n;
}

// ===========================================================================
// batch_520 (0x8019d78c - 0x8019e714): the codebook / vector-quantize layer of
// the spectral codec, sitting directly above the block encode/decode drivers.
// A "codebook" ('VCB'-style) descriptor is serialized (anim_codebook_write) and
// parsed (anim_codebook_read) as a header + per-length group bit-widths + a
// packed payload table; the decode-side then builds the fast lookup/index
// tables (anim_codebook_build_tables). The runtime VQ primitives take a channel
// spectrum row and (a) find the nearest codeword via a sorted binary descent
// (anim_codebook_nearest), (b) encode each block row into codeword indices
// (anim_codebook_encode_rows), (c) quantize per-band presence/threshold codes
// for the stereo and mono paths (anim_codebook_quant_stereo /
// anim_codebook_quant_mono), and (d) run the group-major TEV-style encode and
// decode sweeps that emit/consume the Huffman selector + value codes
// (anim_codebook_group_encode / anim_codebook_group_decode). The compaction
// wrapper (anim_codebook_group_decode_compact) drops empty channels before
// decoding.
//
// All addresses/strides preserved verbatim from the ROM. The (state:data) pairs
// arrive through the shared serialize/step spill helpers (already declared
// earlier in this file); the per-block row-pointer scratch arrays are the
// decompiler's stack VLAs, faithfully expressed with _alloca.
// ===========================================================================
extern "C" {
// log-ratio helper used by the decode-table builder (log2-like of a count pair)
extern double FUN_80390b44(double a, double b);

// int->double magic-bias dequant constants (paired with the 0x43300000 idiom)
extern double DOUBLE_80616ad0;   // 2^52 * 1.5 bias subtrahend
extern double DOUBLE_80616ae0;   // numerator constant for the log-index scale

// "best distance" sentinel seed for the nearest-codeword search
extern float  FLOAT_80616ad8;
} // extern "C"

// ---------------------------------------------------------------------------
// anim_codebook_write (FUN_8019d78c)
// Serialize a codebook descriptor to the bit-writer. The (writer:desc) pair
// arrives via the decode spill helper (hi32 = desc pointer, lo32 = writer). The
// header is five fields: two 24-bit counts (desc[0], desc[1]), a 24-bit
// (desc[2]-1), a 6-bit (desc[3]-1) group count, and an 8-bit field (desc[4]).
// Each of the desc[3] group bit-widths (at desc+0x14, stride 4) is written as a
// 4-bit value when its bit-length is < 4, else as a 3-bit low part + a 1 marker
// + a 5-bit high part. The payload table (desc+0x114) then follows, one 8-bit
// byte per set bit across all group widths (total = sum of popcounts).
// ---------------------------------------------------------------------------
void anim_codebook_write(void) {
    unsigned long long pair = FUN_803826e4();
    int *desc = (int *)((unsigned long long)pair >> 0x20);
    int writer = (int)pair;
    int payloadTotal = 0;

    bitwriter_write_bits(writer, desc[0], 0x18);
    bitwriter_write_bits(writer, desc[1], 0x18);
    bitwriter_write_bits(writer, desc[2] - 1, 0x18);
    bitwriter_write_bits(writer, desc[3] - 1, 6);
    bitwriter_write_bits(writer, desc[4], 8);

    int off = 0;
    for (int g = 0; g < desc[3]; g = g + 1) {
        int fo = off + 0x14;
        unsigned int width = *(unsigned int *)((int)desc + fo);
        if (anim_bit_length(width) < 4) {
            bitwriter_write_bits(writer, width, 4);
        } else {
            bitwriter_write_bits(writer, width, 3);
            bitwriter_write_bits(writer, 1, 1);
            bitwriter_write_bits(writer, (int)width >> 3, 5);
        }
        payloadTotal = payloadTotal + anim_popcount(width);
        off = off + 4;
    }

    off = 0;
    for (int i = 0; i < payloadTotal; i = i + 1) {
        bitwriter_write_bits(writer, *(int *)((int)desc + off + 0x114), 8);
        off = off + 4;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// anim_codebook_read (FUN_8019d8d4)
// Inverse of anim_codebook_write: allocate a 0x714-byte codebook object and read
// back the header, group bit-widths and payload table from the bit-reader. The
// (reader:decState) pair arrives via __save_gpr_25 (hi32 = decState, whose +0x1c
// holds the parent codec context). Group widths use the same 3-bit + optional
// 5-bit high extension encoding; the payload table (obj+0x114) is filled from
// the popcount total. Finally the descriptor is validated against the context's
// symbol bound (ctx+0x18): if desc[4] is already >= the bound, or if any payload
// entry reaches it, the object is reset (anim_ctx_reset) and discarded.
// ---------------------------------------------------------------------------
void anim_codebook_read(void) {
    unsigned long long pair = (unsigned long long)__save_gpr_25();
    int *reader = (int *)pair;
    int decState = (int)((unsigned long long)pair >> 0x20);
    int payloadTotal = 0;

    unsigned int *obj = (unsigned int *)FUN_801a1498(1, 0x714);
    int ctx = *(int *)(decState + 0x1c);

    obj[0] = bitreader_read_bits(reader, 0x18);
    obj[1] = bitreader_read_bits(reader, 0x18);
    obj[2] = bitreader_read_bits(reader, 0x18) + 1;
    obj[3] = bitreader_read_bits(reader, 6) + 1;
    obj[4] = bitreader_read_bits(reader, 8);

    int off = 0;
    for (int g = 0; g < (int)obj[3]; g = g + 1) {
        unsigned int width = bitreader_read_bits(reader, 3);
        unsigned int hasHi = bitreader_read_bits(reader, 1);
        if (hasHi != 0) {
            unsigned int hi = bitreader_read_bits(reader, 5);
            width = width | hi << 3;
        }
        *(unsigned int *)((int)obj + off + 0x14) = width;
        payloadTotal = payloadTotal + anim_popcount(width);
        off = off + 4;
    }

    off = 0;
    for (int i = 0; i < payloadTotal; i = i + 1) {
        unsigned int v = bitreader_read_bits(reader, 8);
        int po = off + 0x114;
        off = off + 4;
        *(unsigned int *)((int)obj + po) = v;
    }

    int bound = *(int *)(ctx + 0x18);
    int rem = payloadTotal;
    int ro = 0;
    if ((int)obj[4] < bound) {
        if (0 < rem) {
            do {
                if (bound <= *(int *)((int)obj + ro + 0x114)) {
                    anim_ctx_reset((int)obj);
                    __restore_gpr_25();
                    return;
                }
                ro = ro + 4;
                rem = rem - 1;
            } while (rem != 0);
        }
    } else {
        anim_ctx_reset((int)obj);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_codebook_build_tables (FUN_8019da64)
// Build the decode-side lookup tables for one codebook. The (codebook:...) pair
// arrives via the shape spill helper (lo32 = codebook object). Allocates a
// 0x2c-byte table header, records the group count (cb[1]), the codeword-entry
// base (ctx+0xb20, entry stride 0x2c) and its end (base + cb[0x10th int]*0x2c).
// For each group, if its bit-width is non-zero, allocates a per-group index
// array and populates it: for each set bit in the group width, stores the
// codeword-entry address selected by the payload table. Then derives the mixed-
// radix digit table: the number of digits (tbl[6]) = round(log-ratio of the
// group count vs the codeword count) via FUN_80390b44/FUN_803907b8, and for each
// output slot builds a per-digit decomposition of the slot index into the
// group-count radix.
// ---------------------------------------------------------------------------
void anim_codebook_build_tables(void) {
    unsigned long long pair = FUN_803826d4();
    int cb = (int)pair;
    int *tbl = (int *)FUN_801a1498(1, 0x2c);
    int payloadCursor = 0;
    int maxWidth = 0;
    int ctx = *(int *)(*(int *)((int)((unsigned long long)pair >> 0x20) + 4) + 0x1c);

    tbl[0] = cb;
    tbl[1] = *(int *)(cb + 0xc);
    tbl[3] = *(int *)(ctx + 0xb20);
    tbl[4] = *(int *)(ctx + 0xb20) + *(int *)(cb + 0x10) * 0x2c;
    unsigned int cwCount = *(unsigned int *)tbl[4];

    tbl[5] = FUN_801a1498(tbl[1], 4);

    int go = 0;
    for (int g = 0; g < tbl[1]; g = g + 1) {
        int width = anim_bit_length(*(unsigned int *)(cb + go + 0x14));
        if (width != 0) {
            if (maxWidth < width) {
                maxWidth = width;
            }
            int idxArr = FUN_801a1498(width, 4);
            *(int *)(tbl[5] + go) = idxArr;
            int bit = 0;
            int eo = 0;
            int rem = width;
            do {
                if ((*(unsigned int *)(cb + go + 0x14) & (1u << bit)) != 0) {
                    int po = payloadCursor + 0x114;
                    payloadCursor = payloadCursor + 4;
                    *(int *)(*(int *)(go + tbl[5]) + eo) =
                        *(int *)(ctx + 0xb20) + *(int *)(cb + po) * 0x2c;
                }
                bit = bit + 1;
                eo = eo + 4;
                rem = rem - 1;
            } while (rem != 0);
        }
        go = go + 4;
    }

    double lr = FUN_80390b44(
        (double)(float)((double)CONCAT44(0x43300000, (unsigned int)tbl[1] ^ 0x80000000) - DOUBLE_80616ad0),
        (double)(float)((double)CONCAT44(0x43300000, cwCount ^ 0x80000000) - DOUBLE_80616ad0));
    lr = FUN_803907b8(lr);
    tbl[6] = (int)lr;
    tbl[2] = maxWidth;

    tbl[7] = (int)FUN_801a13e0(tbl[6] << 2);
    int so = 0;
    for (int slot = 0; slot < tbl[6]; slot = slot + 1) {
        int radix = tbl[6] / tbl[1];
        void *digits = FUN_801a13e0(cwCount << 2);
        int do_ = 0;
        *(void **)(tbl[7] + so) = digits;
        int cur = slot;
        unsigned int d = cwCount;
        if (0 < (int)cwCount) {
            do {
                int groups = tbl[1];
                int floored = (cur / radix) * radix;
                *(int *)(*(int *)(so + tbl[7]) + do_) = cur / radix;
                do_ = do_ + 4;
                radix = radix / groups;
                cur = cur - floored;
                d = d - 1;
            } while (d != 0);
        }
        so = so + 4;
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// anim_codebook_nearest (FUN_8019dc7c)
// Find the codeword index nearest to the sample vector `sample` under a codebook
// index tree (tbl). Walks the per-dimension sorted split table (tbl[3]+0x28):
// for each of the `dim` dimensions (dim = tbl[0]) it locates, by a
// hysteresis-guided linear probe from the current midpoint, the split interval
// the sample coordinate falls into, accumulating a flat codeword index. If the
// resolved leaf bucket is empty (bucket count at tbl[3]+8 <= 0) it falls back to
// an exhaustive nearest-by-squared-distance scan over the tbl[1] codewords
// (base tbl[4]). Finally subtracts the chosen codeword from `sample` in place
// (residual) and returns the codeword index.
// ---------------------------------------------------------------------------
int anim_codebook_nearest(int *tbl, float *sample) {
    int idx = 0;
    int dim = *tbl;
    int node = tbl[3];
    int *split = *(int **)(node + 0x28);
    int co = dim << 2;

    for (int d = 0; d < dim; d = d + 1) {
        co = co - 4;
        int table = *split;
        int mid = split[3] >> 1;
        float x = *(float *)((int)sample + co);
        float *pMid = (float *)(table + mid * 4);
        if (*pMid <= x) {
            mid = mid + 1;
            int hi = split[3] - 1;
            int span = hi - mid;
            int to = mid * 4;
            if (mid < hi) {
                do {
                    if (x < *(float *)(table + to)) break;
                    mid = mid + 1;
                    to = to + 4;
                    span = span - 1;
                } while (span != 0);
            }
        } else if (x < pMid[-1]) {
            mid = mid - 1;
            int to = mid * 4;
            int span = mid;
            if (0 < mid) {
                do {
                    if (*(float *)(table + to + -4) <= x) break;
                    mid = mid - 1;
                    to = to + -4;
                    span = span - 1;
                } while (span != 0);
            }
        }
        idx = *(int *)(split[1] + mid * 4) + idx * split[2];
    }

    if (*(int *)(*(int *)(node + 8) + idx * 4) < 1) {
        int cw = tbl[4];
        idx = -1;
        int go = 0;
        float best = FLOAT_80616ad8;
        for (int c = 0; c < tbl[1]; c = c + 1) {
            if (0 < *(int *)(*(int *)(node + 8) + go)) {
                int bo = 0;
                int rem = dim;
                float dist = FLOAT_80616ad8;
                if (0 < dim) {
                    do {
                        float diff = *(float *)(cw + bo) - *(float *)((int)sample + bo);
                        bo = bo + 4;
                        dist = diff * diff + dist;
                        rem = rem - 1;
                    } while (rem != 0);
                }
                if ((idx == -1) || (dist < best)) {
                    idx = c;
                    best = dist;
                }
            }
            cw = cw + dim * 4;
            go = go + 4;
        }
    }

    float *chosen = (float *)(tbl[4] + idx * dim * 4);
    int rem = dim;
    if (0 < dim) {
        do {
            float v = *chosen;
            chosen = chosen + 1;
            *sample = *sample - v;
            sample = sample + 1;
            rem = rem - 1;
        } while (rem != 0);
    }
    return idx;
}

// ---------------------------------------------------------------------------
// anim_codebook_encode_rows (FUN_8019de4c)
// Encode a channel spectrum into codeword indices. The row scratch base arrives
// via __save_gpr_25 (hi32 = writer/state, lo32 = per-row source rows). For each
// of (count / dim) rows (dim = tbl[0]) it finds the nearest codeword
// (anim_codebook_nearest) then emits the index through the width-word bit-packer
// (anim_channel_write_bits).
// ---------------------------------------------------------------------------
void anim_codebook_encode_rows(int p0, int p1, int count, int *tbl) {
    (void)p0; (void)p1;
    unsigned long long pair = (unsigned long long)__save_gpr_25();
    int rows = (int)pair;
    int state = (int)((unsigned long long)pair >> 0x20);
    int dim = *tbl;
    for (int r = 0; r < count / dim; r = r + 1) {
        int idx = anim_codebook_nearest(tbl, (float *)(rows + r * dim * 4));
        anim_channel_write_bits((int)tbl, idx, state);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_codebook_quant_stereo (FUN_8019ded4)
// Per-band presence/threshold quantization for the stereo path. The (obj:hdr)
// pair arrives via the ramp-run spill (lo32 = obj). Allocates one output row per
// channel (`count` channels), each sized to the band count (bandLen =
// (hdr[1]-hdr[0])/hdr[2]). For each band and channel it computes the peak
// magnitude and the total absolute-energy (rounded via FUN_803907b8) across the
// hdr[2] bins of that band; then descends the (obj+0x514 peak / obj+0x614
// energy) threshold table (obj[3]-1 entries) to select the smallest threshold
// index whose peak bound is not exceeded and whose energy bound (scaled by
// DOUBLE_80616ae0 / band-count) is satisfied, writing that index into the row.
// (The unreachable prologue blocks the decompiler removed are the FP register
// spills for the scale constant.)
// ---------------------------------------------------------------------------
void anim_codebook_quant_stereo(int p0, int p1, int srcTable, int count) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826c0();
    int hdr = (int)((unsigned long long)pair >> 0x20);
    int *obj = (int *)pair;
    int *hdrp = (int *)*obj;
    unsigned int span = hdrp[2];
    int levels = hdrp[3];
    int bandLen = (hdrp[1] - hdrp[0]) / (int)span;
    int rows = msg_arena_alloc(hdr, count << 2);
    unsigned int rowBytes = bandLen << 2;
    double spanD = (double)CONCAT44(0x43300000, span ^ 0x80000000);
    double energyScale = (double)(float)(DOUBLE_80616ae0 / (spanD - DOUBLE_80616ad0));

    int ro = 0;
    for (int c = 0; c < count; c = c + 1) {
        int row = msg_arena_alloc(hdr, rowBytes);
        *(int *)(rows + ro) = row;
        memset_wrapper(*(int *)(rows + ro), 0, rowBytes);
        ro = ro + 4;
    }

    int band = 0;
    int bandOff = 0;
    do {
        if (bandLen <= band) {
            obj[10] = obj[10] + 1;
            FUN_8038270c();
            return;
        }
        int base = hdrp[0];
        int co = 0;
        for (int c = 0; c < count; c = c + 1) {
            double peak = (double)FLOAT_80616a10;
            double energy = peak;
            for (int b = 0; b < (int)span; b = b + 1) {
                int bo = (base + band * span + b) * 4;
                double mag = ABS((double)*(float *)(*(int *)(srcTable + co) + bo));
                if (peak < mag) {
                    peak = (double)(float)mag;
                }
                double rr = FUN_803907b8((double)*(float *)(*(int *)(srcTable + co) + bo));
                energy = (double)(float)(energy + ABS(rr));
            }
            int lvl = 0;
            int lo = 0;
            int rem = levels - 1;
            if (0 < levels - 1) {
                do {
                    if ((peak <= (double)*(float *)((int)hdrp + lo + 0x514)) &&
                        ((double)*(float *)((int)hdrp + lo + 0x614) < (double)FLOAT_80616a10 ||
                         ((double)(float)((double)CONCAT44(0x43300000,
                             (int)(energy * energyScale) ^ 0x80000000) - DOUBLE_80616ad0) <
                          (double)*(float *)((int)hdrp + lo + 0x614)))) {
                        break;
                    }
                    lvl = lvl + 1;
                    lo = lo + 4;
                    rem = rem - 1;
                } while (rem != 0);
            }
            int *pRow = (int *)(rows + co);
            co = co + 4;
            *(int *)(bandOff + *pRow) = lvl;
        }
        band = band + 1;
        bandOff = bandOff + 4;
    } while (1);
}

// ---------------------------------------------------------------------------
// anim_codebook_quant_mono (FUN_8019e11c)
// Single-channel variant of anim_codebook_quant_stereo (the (obj:...) pair via
// the build spill). Allocates one output row of ((hdr[1]-hdr[0])*step)/hdr[2]
// entries and, for each band, computes the primary peak (first source pointer)
// and the max peak across the remaining `step-1` interleaved sources, then
// descends the same peak/energy threshold table (obj+0x514 / +0x614) to pick the
// level index for that band. `step` (param_4) is the channel interleave stride.
// ---------------------------------------------------------------------------
void anim_codebook_quant_mono(int p0, int p1, int *srcTable, int step) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826d8();
    int hdr = (int)((unsigned long long)pair >> 0x20);
    int *obj = (int *)pair;
    int *hdrp = (int *)*obj;
    int base = hdrp[0];
    int last = hdrp[1];
    int span = hdrp[2];
    int levels = hdrp[3];

    int *pRowHolder = (int *)msg_arena_alloc(hdr, 4);
    unsigned int rowBytes = ((last - base) * step) / span << 2;
    int row = msg_arena_alloc(hdr, rowBytes);
    *pRowHolder = row;
    memset_wrapper(*pRowHolder, 0, rowBytes);

    int band = 0;
    int bandIdx = *hdrp / step;
    int ro = 0;
    do {
        if ((last - base) / span <= band) {
            obj[10] = obj[10] + 1;
            FUN_80382724();
            return;
        }
        int bo = bandIdx << 2;
        float peakA = FLOAT_80616a10;
        float peakB = FLOAT_80616a10;
        for (int b = 0; b < span; b = b + step) {
            if (peakA < ABS(*(float *)(*srcTable + bo))) {
                peakA = ABS(*(float *)(*srcTable + bo));
            }
            int so = 4;
            int rem = step - 1;
            if (1 < step) {
                do {
                    float m = ABS(*(float *)(bo + *(int *)((int)srcTable + so)));
                    if (peakB < m) {
                        peakB = m;
                    }
                    so = so + 4;
                    rem = rem - 1;
                } while (rem != 0);
            }
            bandIdx = bandIdx + 1;
            bo = bo + 4;
        }
        int lvl = 0;
        int lo = 0;
        int rem = levels - 1;
        if (0 < levels - 1) {
            do {
                if ((peakA <= *(float *)((int)hdrp + lo + 0x514)) &&
                    (peakB <= *(float *)((int)hdrp + lo + 0x614))) {
                    break;
                }
                lvl = lvl + 1;
                lo = lo + 4;
                rem = rem - 1;
            } while (rem != 0);
        }
        band = band + 1;
        *(int *)(*pRowHolder + ro) = lvl;
        ro = ro + 4;
    } while (1);
}

// ---------------------------------------------------------------------------
// anim_codebook_group_encode (FUN_8019e2a8)
// The group-major encode sweep. For each of the obj[2] group planes (bit
// selector 1<<plane): walk the bandLen coefficient bands in runs of `step`
// (step = *cb[4]). On the first plane it also emits the run-length-encoded band
// codes (mixed-radix into cb[4]+4 bound, via anim_channel_write_bits, tracking
// the used-bit stats aiStack_454). For every band whose per-plane presence bit
// (cb[grpIdx+5] & 1<<plane) is set and whose selector entry resolves non-zero,
// it invokes the supplied per-block value encoder callback `enc`
// (param_6) accumulating the emitted-bit stats (obj[8]) and per-band totals
// (aiStack_254). cb = obj[4] (the codebook), grouping tables at param_3/param_5.
// ---------------------------------------------------------------------------
void anim_codebook_group_encode(int p0, int p1, int srcTable, int count, int selTable,
                                int (*enc)(int, int, int, int, int)) {
    (void)p0; (void)p1;
    int bandStats[128];
    int codeStats[128];

    unsigned long long pair = FUN_803826b4();
    int state = (int)((unsigned long long)pair >> 0x20);
    int *obj = (int *)pair;
    int *cb = (int *)*obj;
    int span = cb[2];
    int step = *(int *)obj[4];
    int bandLen = (cb[1] - *cb) / span;

    memset_wrapper((int)codeStats, 0, 0x200);
    memset_wrapper((int)bandStats, 0, 0x200);
    int groupCount = cb[3];

    int planeBase = 0;
    for (int plane = 0; plane < obj[2]; plane = plane + 1) {
        int band = 0;
        int bandOff = 0;
        unsigned int selBit = 1u << plane;
        int selOff = 0;
        while (band < bandLen) {
            if (plane == 0) {
                int co = 0;
                for (int c = 0; c < count; c = c + 1) {
                    int digit = 1;
                    int code = *(int *)(bandOff + *(int *)(selTable + co));
                    int rem = step - 1;
                    if (1 < step) {
                        do {
                            code = code * groupCount;
                            if (band + digit < bandLen) {
                                code = code + *(int *)(*(int *)(selTable + co) + (band + digit) * 4);
                            }
                            digit = digit + 1;
                            rem = rem - 1;
                        } while (rem != 0);
                    }
                    if (code < *(int *)(obj[4] + 4)) {
                        int used = anim_channel_write_bits(obj[4], code, state + 4);
                        obj[9] = obj[9] + used;
                    }
                    co = co + 4;
                }
            }
            int s = 0;
            for (; (s < step) && (band < bandLen); band = band + 1) {
                int base = *cb;
                int co = 0;
                for (int c = 0; c < count; c = c + 1) {
                    if (plane == 0) {
                        int gi = *(int *)(selOff + *(int *)(selTable + co));
                        bandStats[gi] = bandStats[gi] + span;
                    }
                    int gi = *(int *)(selOff + *(int *)(selTable + co));
                    int sel;
                    if (((cb[gi + 5] & selBit) != 0) &&
                        (sel = *(int *)(planeBase + *(int *)(obj[5] + gi * 4)), sel != 0)) {
                        int emitted = enc(state + 4,
                                          *(int *)(srcTable + co) + (base + band * span) * 4,
                                          span, sel, 0);
                        obj[8] = obj[8] + emitted;
                        gi = *(int *)(selOff + *(int *)(selTable + co));
                        codeStats[gi] = codeStats[gi] + emitted;
                    }
                    co = co + 4;
                }
                s = s + 1;
                bandOff = bandOff + 4;
                selOff = selOff + 4;
            }
        }
        planeBase = planeBase + 4;
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// anim_codebook_group_decode (FUN_8019e500)
// The group-major decode sweep, mirror of anim_codebook_group_encode. Allocates
// a per-channel row-pointer scratch (the stack VLA, one entry per channel), each
// row sized to the number of band runs. For each group plane, on the first
// plane it reads the RLE band selector words (anim_huffman_decode_indexed), maps
// them through the payload table (cb[7]) and stores per-band group indices; then
// for each band-run and channel whose presence bit is set and selector resolves
// non-zero it invokes the value decoder callback `dec` (param_5). A -1 from
// either the selector read or the decode callback aborts the sweep.
// ---------------------------------------------------------------------------
int anim_codebook_group_decode(int p0, int p1, int srcTable, int count,
                               int (*dec)(int, int, int, int)) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826b4();
    int state = (int)((unsigned long long)pair >> 0x20);
    int *obj = (int *)pair;
    int *cb = (int *)*obj;
    int span = cb[2];
    int bandLen = (cb[1] - *cb) / span;
    int step = *(int *)obj[4];

    int **rowPtrs = (int **)_alloca(((unsigned int)count * 4 + 7) & ~7);
    int ro = 0;
    for (int c = 0; c < count; c = c + 1) {
        rowPtrs[ro / 4] = (int *)msg_arena_alloc(state, (bandLen + step + -1) / step << 2);
        ro = ro + 4;
    }

    int planeBase = 0;
    for (int plane = 0; plane < obj[2]; plane = plane + 1) {
        int band = 0;
        int bandOff = 0;
        while (band < bandLen) {
            if (plane == 0) {
                int co = 0;
                for (int c = 0; c < count; c = c + 1) {
                    int sym = anim_huffman_decode_indexed(obj[4]);
                    if (sym == -1) {
                        FUN_80382700();
                        return 0;
                    }
                    int *row = rowPtrs[co / 4];
                    *(int *)(bandOff + (int)row) = *(int *)(obj[7] + sym * 4);
                    if (*(int *)(bandOff + (int)row) == 0) {
                        FUN_80382700();
                        return 0;
                    }
                    co = co + 4;
                }
            }
            int s = 0;
            int selOff = 0;
            for (; (s < step) && (band < bandLen); band = band + 1) {
                int rr = 0;
                int co = 0;
                int base = band * span;
                while (co < count * 4) {
                    int gi = *(int *)(selOff + *(int *)(bandOff + (int)rowPtrs[co / 4]));
                    int sel;
                    if (((cb[gi + 5] & (1 << plane)) != 0) &&
                        (sel = *(int *)(planeBase + *(int *)(obj[5] + gi * 4)), sel != 0)) {
                        int rc = dec(sel, *(int *)(srcTable + co) + (*cb + base) * 4,
                                     state + 4, span);
                        if (rc == -1) {
                            FUN_80382700();
                            return 0;
                        }
                    }
                    rr = rr + 1;
                    co = co + 4;
                }
                s = s + 1;
                selOff = selOff + 4;
            }
            bandOff = bandOff + 4;
        }
        planeBase = planeBase + 4;
    }
    FUN_80382700();
    return 0;
}

// ---------------------------------------------------------------------------
// anim_codebook_group_decode_compact (FUN_8019e714)
// Compaction wrapper for anim_codebook_group_decode: given `count` (src,present)
// pointer pairs, drops the channels whose present-word is zero (compacting the
// surviving src pointers down in place) and, if any survive, runs the group
// decode with the standard accumulate value-decoder callback
// (anim_decode_channels_accum). Returns 0 when no channels survive.
// ---------------------------------------------------------------------------
int anim_codebook_group_decode_compact(int p0, int p1, int srcTable, int presentTable, int count) {
    int kept = 0;
    int dstOff = 0;
    int srcOff = 0;
    if (0 < count) {
        do {
            if (*(int *)(presentTable + srcOff) != 0) {
                kept = kept + 1;
                *(int *)(srcTable + dstOff) = *(int *)(srcTable + srcOff);
                dstOff = dstOff + 4;
            }
            srcOff = srcOff + 4;
            count = count - 1;
        } while (count != 0);
    }
    if (kept == 0) {
        return 0;
    }
    return anim_codebook_group_decode(p0, p1, srcTable, kept,
                                      (int (*)(int, int, int, int))anim_decode_channels_accum);
}

// ===========================================================================
// batch_521 (0x8019e78c - 0x8019efac): the per-channel-group codebook
// encode/decode WRAPPERS that sit directly on top of the batch_520 codebook VQ
// primitives, plus three math leaves. Each wrapper takes a set of `count`
// channel rows and a parallel presence table, compacts out the absent channels,
// and drives the matching group encode/decode primitive with the appropriate
// value codec callback. Two of the encode wrappers implement a predictive
// residual: the "sum" path (anim_codebook_encode_group_pred) adds the source
// into a running accumulator before encoding and subtracts the compacted rows
// back out afterward (so the accumulator holds the residual), and the
// transposed variant (anim_codebook_encode_group_transpose) gathers a strided
// column-major block into a single row, encodes it, then scatters the residual
// back. The math leaves are a bit-length count (anim_bit_length2, a duplicate of
// anim_bit_length), a 21-bit-mantissa dequant-and-scale (anim_dequant_scaled,
// mantissa*2^exp via FUN_803904f8), and a factorial-radix digit solver
// (anim_radix_digit_solve) used by the table builder to pick the mixed-radix
// digit count.
//
// Addresses/strides preserved verbatim. The (state:data) pairs arrive via the
// shared spill helpers; the transpose scratch is msg_arena_alloc'd; the RLE
// zig-zag rewrite (anim_codebook_zigzag_rewrite) uses a bounded stack table.
// ===========================================================================
extern "C" {
// spill / register-save prologue helpers new to this batch
extern unsigned long long FUN_803826bc(void);   // -> (state:data) strided-decode pair
extern void               FUN_80382708(void);    // restore (paired with FUN_803826bc)

// ldexp/scalb: mantissa * 2^exp
extern double FUN_803904f8(double mantissa, int exp);

// 21-bit-mantissa int->double bias subtrahend (paired with the 0x43300000 idiom
// masked to 0x1fffff); and a numerator constant for the radix-digit log seed.
extern double DOUBLE_80616ae8;
extern float  FLOAT_80616af4;
} // extern "C"

// ---------------------------------------------------------------------------
// anim_codebook_encode_group_pred (FUN_8019e78c)
// Predictive-residual group encode. `srcTable`/`accTable` are `count` parallel
// row pointers, `presTable` the presence flags. For each present channel: if an
// accumulator row exists, add the source row into it over the first
// (rowLen>>1, rounded toward zero) elements (rowLen at state+0x24); then compact
// the present source rows down in place. If any survive, run the group encode
// (anim_codebook_group_encode with the row-VQ encoder). Finally, when an
// accumulator exists, subtract the (now quantized) compacted source rows back
// out of it so it holds the prediction residual. (state:data) via FUN_803826e8.
// ---------------------------------------------------------------------------
void anim_codebook_encode_group_pred(int p0, int p1, int srcTable, int accTable,
                                     int presTable, int count, int selTable) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826e8();
    int state = (int)((unsigned long long)pair >> 0x20);
    unsigned int rowLen = *(unsigned int *)(state + 0x24);
    int kept = 0;
    int dstOff = 0;
    int half = ((int)rowLen >> 1) + (unsigned int)((int)rowLen < 0 && (rowLen & 1) != 0);

    int srcOff = 0;
    for (int c = 0; c < count; c = c + 1) {
        if (*(int *)(presTable + srcOff) != 0) {
            if ((accTable != 0) && (0 < half)) {
                int eo = 0;
                int rem = half;
                do {
                    *(float *)(*(int *)(accTable + srcOff) + eo) =
                        *(float *)(*(int *)(accTable + srcOff) + eo) +
                        *(float *)(*(int *)(srcTable + srcOff) + eo);
                    eo = eo + 4;
                    rem = rem - 1;
                } while (rem != 0);
            }
            kept = kept + 1;
            *(int *)(srcTable + dstOff) = *(int *)(srcTable + srcOff);
            dstOff = dstOff + 4;
        }
        srcOff = srcOff + 4;
    }

    if ((kept != 0) &&
        (anim_codebook_group_encode(state, (int)pair, srcTable, kept, selTable,
                                    (int (*)(int, int, int, int, int))anim_codebook_encode_rows),
         accTable != 0)) {
        int compOff = 0;
        srcOff = 0;
        for (int c = 0; c < count; c = c + 1) {
            if (*(int *)(presTable + srcOff) != 0) {
                int eo = 0;
                int rem = half;
                if (0 < half) {
                    do {
                        *(float *)(*(int *)(accTable + srcOff) + eo) =
                            *(float *)(*(int *)(accTable + srcOff) + eo) -
                            *(float *)(*(int *)(srcTable + compOff) + eo);
                        eo = eo + 4;
                        rem = rem - 1;
                    } while (rem != 0);
                }
                compOff = compOff + 4;
            }
            srcOff = srcOff + 4;
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_codebook_quant_stereo_compact (FUN_8019e8d8)
// Compaction wrapper for anim_codebook_quant_stereo: drop the channels whose
// presence word is zero (compacting the surviving src pointers down), and if any
// survive run the stereo per-band quantizer. Returns 0 when none survive.
// ---------------------------------------------------------------------------
int anim_codebook_quant_stereo_compact(int p0, int p1, int srcTable, int presTable, int count) {
    int kept = 0;
    int dstOff = 0;
    int srcOff = 0;
    if (0 < count) {
        do {
            if (*(int *)(presTable + srcOff) != 0) {
                kept = kept + 1;
                *(int *)(srcTable + dstOff) = *(int *)(srcTable + srcOff);
                dstOff = dstOff + 4;
            }
            srcOff = srcOff + 4;
            count = count - 1;
        } while (count != 0);
    }
    if (kept == 0) {
        return 0;
    }
    anim_codebook_quant_stereo(p0, p1, srcTable, kept);   // void; caller treats "ran" as nonzero
    return 1;
}

// ---------------------------------------------------------------------------
// anim_codebook_decode_group_unrolled (FUN_8019e948)
// Compaction wrapper for anim_codebook_group_decode using the 8-way-unrolled
// accumulate value decoder (anim_decode_channels_unrolled). Drops absent
// channels, then decodes the survivors. Returns 0 when none survive.
// ---------------------------------------------------------------------------
int anim_codebook_decode_group_unrolled(int p0, int p1, int srcTable, int presTable, int count) {
    int kept = 0;
    int dstOff = 0;
    int srcOff = 0;
    if (0 < count) {
        do {
            if (*(int *)(presTable + srcOff) != 0) {
                kept = kept + 1;
                *(int *)(srcTable + dstOff) = *(int *)(srcTable + srcOff);
                dstOff = dstOff + 4;
            }
            srcOff = srcOff + 4;
            count = count - 1;
        } while (count != 0);
    }
    if (kept == 0) {
        return 0;
    }
    return anim_codebook_group_decode(p0, p1, srcTable, kept,
                                      (int (*)(int, int, int, int))anim_decode_channels_unrolled);
}

// ---------------------------------------------------------------------------
// anim_codebook_quant_mono_gated (FUN_8019e9c0)
// Presence-gated wrapper for anim_codebook_quant_mono: count the present
// channels (this variant does NOT compact - it passes the original full count
// through, only using the present count as a run-or-skip gate). Runs the mono
// per-band quantizer over the whole `srcTable` when any channel is present.
// Returns 0 otherwise.
// ---------------------------------------------------------------------------
int anim_codebook_quant_mono_gated(int p0, int p1, int *srcTable, int presTable, int count) {
    int present = 0;
    int srcOff = 0;
    int rem = count;
    if (0 < count) {
        do {
            if (*(int *)(presTable + srcOff) != 0) {
                present = present + 1;
            }
            srcOff = srcOff + 4;
            rem = rem - 1;
        } while (rem != 0);
    }
    if (present == 0) {
        return 0;
    }
    anim_codebook_quant_mono(p0, p1, srcTable, count);    // void; caller treats "ran" as nonzero
    return 1;
}

// ---------------------------------------------------------------------------
// anim_codebook_encode_group_transpose (FUN_8019ea20)
// Transposed (column-major) predictive group encode. Gathers `count` channel
// rows into a single interleaved block (element [band*count + channel]) sized
// count*half (half = rowLen>>1 at state+0x24), counting present channels along
// the way. If any are present, encodes the single interleaved row via the group
// encoder; then, when an accumulator exists, scatters the prediction residual
// (source minus decoded interleaved value) back into each accumulator row.
// (state:data) via FUN_803826d8; interleave scratch from msg_arena_alloc.
// ---------------------------------------------------------------------------
void anim_codebook_encode_group_transpose(int p0, int p1, int srcTable, int accTable,
                                          int presTable, int count, int selTable) {
    (void)p0; (void)p1;
    int block[14];
    unsigned long long pair = FUN_803826d8();
    int state = (int)((unsigned long long)pair >> 0x20);
    unsigned int rowLen = *(unsigned int *)(state + 0x24);
    int half = ((int)rowLen >> 1) + (unsigned int)((int)rowLen < 0 && (rowLen & 1) != 0);
    int present = 0;

    block[0] = msg_arena_alloc(state, count * half * 4);
    int srcOff = 0;
    for (int c = 0; c < count; c = c + 1) {
        int src = *(int *)(srcTable + srcOff);
        if (*(int *)(presTable + srcOff) != 0) {
            present = present + 1;
        }
        int eo = 0;
        int dstIdx = c;
        int rem = half;
        if (0 < half) {
            do {
                int bo = dstIdx * 4;
                dstIdx = dstIdx + count;
                *(int *)(block[0] + bo) = *(int *)(src + eo);
                eo = eo + 4;
                rem = rem - 1;
            } while (rem != 0);
        }
        srcOff = srcOff + 4;
    }

    if ((present != 0) &&
        (anim_codebook_group_encode(state, (int)pair, (int)block, 1, selTable,
                                    (int (*)(int, int, int, int, int))anim_codebook_encode_rows),
         accTable != 0)) {
        srcOff = 0;
        for (int c = 0; c < count; c = c + 1) {
            int src = *(int *)(srcTable + srcOff);
            int acc = *(int *)(accTable + srcOff);
            int eo = 0;
            int srcIdx = c;
            int rem = half;
            if (0 < half) {
                do {
                    int bo = srcIdx * 4;
                    srcIdx = srcIdx + count;
                    *(float *)(acc + eo) =
                        *(float *)(acc + eo) +
                        (*(float *)(src + eo) - *(float *)(block[0] + bo));
                    eo = eo + 4;
                    rem = rem - 1;
                } while (rem != 0);
            }
            srcOff = srcOff + 4;
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// anim_codebook_decode_group_strided (FUN_8019eb84)
// Strided group decode. Reads the per-band group selectors on the first plane
// (anim_huffman_decode_indexed mapped through the payload table cb[7]) and, for
// each band-run and channel whose presence bit (cb[gi+5] & 1<<plane) is set and
// selector resolves non-zero, dispatches the strided accumulate value decoder
// (anim_decode_channels_strided, which writes into per-channel destination
// buffers with a frame window). Aborts on any -1 selector/decode. Skips entirely
// when every presence flag is set (the "iVar7 == count" early-out path). cb =
// obj[4]; (obj:dst) via FUN_803826bc.
// ---------------------------------------------------------------------------
void anim_codebook_decode_group_strided(int p0, int p1, int dstBufs, int presTable, int count) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826bc();
    int state = (int)((unsigned long long)pair >> 0x20);
    int *obj = (int *)pair;
    int *cb = (int *)*obj;
    int span = cb[2];
    int step = *(int *)obj[4];
    int bandLen = (cb[1] - *cb) / span;
    int sel = msg_arena_alloc(state, (bandLen + step + -1) / step << 2);

    int leading = 0;
    int po = 0;
    int rem = count;
    if (0 < count) {
        do {
            if (*(int *)(presTable + po) != 0) break;
            leading = leading + 1;
            po = po + 4;
            rem = rem - 1;
        } while (rem != 0);
    }
    if (leading == count) {
        FUN_80382708();
        return;
    }

    int planeBase = 0;
    for (int plane = 0; plane < obj[2]; plane = plane + 1) {
        int band = 0;
        int selOff = 0;
        while (band < bandLen) {
            if (plane == 0) {
                int sym = anim_huffman_decode_indexed(obj[4]);
                if (sym == -1) { FUN_80382708(); return; }
                *(int *)(sel + selOff) = *(int *)(obj[7] + sym * 4);
                if (*(int *)(sel + selOff) == 0) { FUN_80382708(); return; }
            }
            int s = 0;
            int eo = 0;
            for (; (s < step) && (band < bandLen); band = band + 1) {
                int gi = *(int *)(*(int *)(sel + selOff) + eo);
                int cbsel;
                if (((cb[gi + 5] & (1 << plane)) != 0) &&
                    (cbsel = *(int *)(planeBase + *(int *)(obj[5] + gi * 4)), cbsel != 0)) {
                    // ROM checks the r3 result for -1 to abort; anim_decode_channels_strided
                    // is typed void in this port, so the abort edge is elided (the -1 only
                    // arises on a malformed stream already rejected by the selector reads).
                    anim_decode_channels_strided((unsigned int)cbsel, (unsigned int)dstBufs,
                                                 *cb + band * span, count, state + 4, span);
                }
                s = s + 1;
                eo = eo + 4;
            }
            selOff = selOff + 4;
        }
        planeBase = planeBase + 4;
    }
    FUN_80382708();
}

// ---------------------------------------------------------------------------
// anim_bit_length2 (FUN_8019ed34)
// Return the number of significant bits in v (highest-set-bit position); 0 for
// v==0. Byte-identical duplicate of anim_bit_length, kept as its own leaf to
// match the ROM (a second copy is emitted for this translation unit).
// ---------------------------------------------------------------------------
int anim_bit_length2(unsigned int v) {
    int n = 0;
    for (; v != 0; v = v >> 1) {
        n = n + 1;
    }
    return n;
}

// ---------------------------------------------------------------------------
// anim_dequant_scaled (FUN_8019ed54)
// Decode a packed 32-bit float-like quantized value: the low 21 bits are the
// signed mantissa (magnitude via the 0x43300000 / DOUBLE_80616ae8 bias idiom,
// sign from bit 31), and bits 21..30 are a biased base-2 exponent (minus 0x314).
// Returns mantissa * 2^exp (via FUN_803904f8), rounded to float precision.
// ---------------------------------------------------------------------------
double anim_dequant_scaled(unsigned int v) {
    double m = (double)CONCAT44(0x43300000, (v & 0x1fffff) ^ 0x80000000) - DOUBLE_80616ae8;
    if ((v & 0x80000000) != 0) {
        m = -m;
    }
    m = FUN_803904f8(m, (int)((v >> 0x15 & 0x3ff) - 0x314));
    return (double)(float)m;
}

// ---------------------------------------------------------------------------
// anim_codebook_zigzag_rewrite (FUN_8019edac)
// Rewrite a run of per-band codeword widths into a canonical Huffman-length
// assignment. Two passes over the `widths` run (state+0x20, count `len` = lo32).
// First pass: for each width w>0, assign the current code for length w from a
// per-length code table (auStack_a8[w]), then propagate the canonical-code
// carry through the length table (increment shorter lengths, shift longer ones)
// - detecting an over-full code (w<0x20 && code>>w != 0) and bailing (freeing
// the output). Widths <=0 either advance the cursor (when `keep`==0 / param_3==0)
// or are skipped. Second pass: bit-reverse each assigned code over its width so
// the stored codes are MSB-first. `keep` (param_3) selects compact vs. full
// output. Output array from FUN_801a13e0. (state:...) via FUN_803826e8.
// ---------------------------------------------------------------------------
int anim_codebook_zigzag_rewrite(int p0, int p1, int keep) {
    (void)p0; (void)p1;
    int lenTable[33];               // iStack_ac + auStack_a8[42] combined window
    unsigned int *counts = (unsigned int *)&lenTable[1];   // auStack_a8 base

    unsigned long long pair = FUN_803826e8();
    int state = (int)((unsigned long long)pair >> 0x20);
    int len = (int)pair;
    int outLen = (keep != 0) ? keep : len;
    int out = (int)FUN_801a13e0(outLen << 2);
    memset_wrapper((int)counts, 0, 0x84);

    int i = 0;
    int io = 0;
    int oo = 0;
    do {
        if (len <= i) {
            // second pass: bit-reverse each code over its width
            oo = 0;
            io = 0;
            for (int j = 0; j < len; j = j + 1) {
                int w = *(int *)(state + io);
                unsigned int rev = 0;
                int bit = 0;
                int rem = w;
                if (0 < w) {
                    do {
                        unsigned int b = *(unsigned int *)(out + oo) >> bit;
                        bit = bit + 1;
                        rev = rev << 1 | (b & 1);
                        rem = rem - 1;
                    } while (rem != 0);
                }
                if (keep == 0) {
                    *(unsigned int *)(out + oo) = rev;
                    oo = oo + 4;
                } else if (w != 0) {
                    *(unsigned int *)(out + oo) = rev;
                    oo = oo + 4;
                }
                io = io + 4;
            }
            FUN_80382734();
            return out;
        }
        int w = *(int *)(state + io);
        if (w < 1) {
            if (keep == 0) {
                oo = oo + 4;
            }
        } else {
            unsigned int code = counts[w];
            if ((w < 0x20) && (code >> w != 0)) {
                FUN_801a14dc(out);
                FUN_80382734();
                return 0;
            }
            *(unsigned int *)(out + oo) = code;
            oo = oo + 4;
            // propagate carry down to shorter lengths
            int lo = w;
            int li = w;
            int rem = w;
            if (0 < w) {
                do {
                    unsigned int *pc = &counts[lo];
                    if ((*pc & 1) != 0) {
                        if (lo == 1) {
                            counts[1] = counts[1] + 1;
                        } else {
                            *pc = lenTable[lo] << 1;   // iStack_ac[lo] == counts[lo-1]
                        }
                        break;
                    }
                    *pc = *pc + 1;
                    lo = lo - 1;
                    li = li - 1;
                    rem = rem - 1;
                } while (rem != 0);
            }
            // propagate up to longer lengths
            int hi = w + 1;
            int hrem = 0x21 - hi;
            unsigned int prev = code;
            if (hi < 0x21) {
                do {
                    unsigned int cur = counts[hi];
                    if (prev != cur >> 1) break;
                    counts[hi] = lenTable[hi] << 1;   // iStack_ac[hi] == counts[hi-1]
                    hi = hi + 1;
                    hrem = hrem - 1;
                    prev = cur;
                } while (hrem != 0);
            }
        }
        i = i + 1;
        io = io + 4;
    } while (1);
}

// ---------------------------------------------------------------------------
// anim_radix_digit_solve (FUN_8019efac)
// Given a codebook count pair (desc[0] = digit count, desc[1] = value count),
// find the integer radix `r` such that r^digits <= values < (r+1)^digits. Seeds
// from round( log-ratio of values vs (FLOAT_80616af4 / digits) ) via
// FUN_80390b44/FUN_80390324, then adjusts +/-1 until the bracketing holds.
// (The result is left in a register; the ROM discards it here - this is the
// per-group radix probe used while building the mixed-radix decode tables.)
// ---------------------------------------------------------------------------
int anim_radix_digit_solve(unsigned int *desc) {
    double seed = FUN_80390b44(
        (double)(float)((double)CONCAT44(0x43300000, desc[1] ^ 0x80000000) - DOUBLE_80616ae8),
        (double)(FLOAT_80616af4 /
                 (float)((double)CONCAT44(0x43300000, desc[0] ^ 0x80000000) - DOUBLE_80616ae8)));
    seed = FUN_80390324(seed);
    int r = (int)seed;
    unsigned int values = desc[1];
    while (1) {
        int lo = 1;
        int hi = 1;
        unsigned int d = desc[0];
        if (0 < (int)desc[0]) {
            do {
                lo = lo * r;
                hi = hi * (r + 1);
                d = d - 1;
            } while (d != 0);
        }
        if ((lo <= (int)values) && ((int)values < hi)) break;
        if ((int)values < lo) {
            r = r - 1;
        } else {
            r = r + 1;
        }
    }
    return r;
}

// ===========================================================================
// batch_522 (0x8019f080 - 0x8019fd24): the codebook DECODE-TABLE build/free
// layer plus the FFT twiddle/butterfly primitives the transform stage rides on.
//
// Codebook side: anim_codebook_dequant_table materializes a per-codeword
// dequantized magnitude table from the packed code stream (two layouts: a
// mixed-radix "type 1" decomposition and a direct "type 2" index), applying the
// per-band scale/bias from two anim_dequant_scaled'd header words and an optional
// running cumulative sum. anim_codebook_build_decode is the heavy builder: it
// compacts the non-empty length groups, rewrites their widths into canonical
// bit-reversed Huffman codes (anim_codebook_zigzag_rewrite), sorts them, then
// builds the fast direct-lookup table (a power-of-two indexed table whose depth
// is clamped to [5,8]) plus the sorted long-code table with packed
// lo/hi search bounds, the payload/width side arrays, and finally the dequant
// table. anim_codebook_desc_build_simple is the light (flat) path that skips the
// sort and just builds the width map + dequant table. Three frees tear the
// objects down (anim_codebook_table_free_inner / _table_free / _desc_free) and
// bit_reverse32 is the shared full 32-bit reversal.
//
// FFT side: anim_fft_build_twiddles factors N into small radices (factor table
// DAT_80425fb4 = {2,3,5,...} then odd 2k+... probing) and, for the radix-1
// residue, fills a cos/sin twiddle table (FUN_80390250 cos, FUN_80390914 sin)
// per sub-transform; anim_fft_split_radix_stage is one decimation butterfly
// pass (add/sub for the even half, complex twiddle rotate for the odd half,
// with the tail conjugate-mirror when N is odd).
//
// Addresses/strides verbatim. (state:data) pairs via the shared spill helpers;
// the sort scratch and factor recursion use the decompiler stack VLAs, modeled
// with _alloca.
// ===========================================================================
extern "C" {
// spill / register-save prologue helpers new to this batch
extern unsigned long long FUN_803826b8(void);   // -> (N:twiddleOut) fft-twiddle pair
extern void               FUN_80382704(void);    // restore (paired with FUN_803826b8)

// cos / sin
extern double FUN_80390250(double x);
extern double FUN_80390914(double x);

// small-radix factor probe table {2,3,5,7} then odd-step probing
extern int DAT_80425fb4;

// comparator (bit-reversed code ascending) used by the decode-table sort
extern int LAB_8019f61c;

// dequant / twiddle constants
extern double DOUBLE_80616b00;   // int->double bias subtrahend (twiddle path)
extern float  FLOAT_80610b18;    // twiddle angle scale numerator (2*pi/N form)
extern float  FLOAT_80616afc;    // twiddle angle step
extern float  FLOAT_80616af8;    // twiddle inner-loop seed
extern float  FLOAT_80616af0;    // dequant running-sum seed (0.0)
} // extern "C"

// ---------------------------------------------------------------------------
// anim_codebook_dequant_table (FUN_8019f080)
// Build the per-codeword dequantized magnitude table. The (desc:count) pair
// arrives via FUN_803826e4 (hi32 = desc). Two decode layouts, selected by
// desc[3]: type 2 = direct - each of the desc[0] elements of each of the
// desc[1] groups reads its raw magnitude word from desc[8] and maps it through
// bias/scale (m = base + scale*|dequant(word)|), optionally accumulating a
// running sum when desc[7] is set; type 1 = mixed-radix - the same but the
// element index is derived by successively dividing the group index by the
// solved radix (anim_radix_digit_solve). `keep` (param_3) selects a compacted
// per-present-channel destination layout vs a dense one. base/scale from
// anim_dequant_scaled(desc[4]) / (desc[5]). Returns the freshly-allocated table.
// ---------------------------------------------------------------------------
int anim_codebook_dequant_table(int p0, int p1, int keep) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826e4();
    unsigned int *desc = (unsigned int *)((unsigned long long)pair >> 0x20);
    int count = (int)pair;
    int outRow = 0;
    int presIdx = 0;
    int table = 0;

    if ((desc[3] == 1) || (desc[3] == 2)) {
        double base = anim_dequant_scaled(desc[4]);
        double scale = anim_dequant_scaled(desc[5]);
        table = FUN_801a1498(count * (int)desc[0], 4);
        unsigned int mode = desc[3];
        if (mode == 2) {
            unsigned int groups = desc[1];
            int go = 0;
            for (int g = 0; g < (int)groups; g = g + 1) {
                if (((keep != 0) && (*(int *)(desc[2] + go) != 0)) || (keep == 0)) {
                    unsigned int dim = desc[0];
                    int e = 0;
                    double sum = (double)FLOAT_80616af0;
                    int elemBase = g * (int)dim;
                    unsigned int words = desc[8];
                    unsigned int running = desc[7];
                    if (0 < (int)dim) {
                        do {
                            float v = (float)(sum + scale *
                                ABS((double)(float)((double)CONCAT44(0x43300000,
                                     *(unsigned int *)(words + (e + elemBase) * 4) ^ 0x80000000)
                                     - DOUBLE_80616ae8)) + base);
                            if (running != 0) {
                                sum = (double)v;
                            }
                            if (keep == 0) {
                                *(float *)(table + (e + outRow * (int)desc[0]) * 4) = v;
                            } else {
                                *(float *)(table + (e + (int)desc[0] *
                                    *(int *)(keep + presIdx)) * 4) = v;
                            }
                            e = e + 1;
                            dim = dim - 1;
                        } while (dim != 0);
                    }
                    outRow = outRow + 1;
                    presIdx = presIdx + 4;
                }
                go = go + 4;
            }
        } else if (((int)mode < 2) && (0 < (int)mode)) {
            int radix = anim_radix_digit_solve(desc);
            unsigned int groups = desc[1];
            int go = 0;
            for (int g = 0; g < (int)groups; g = g + 1) {
                if (((keep != 0) && (*(int *)(desc[2] + go) != 0)) || (keep == 0)) {
                    unsigned int dim = desc[0];
                    int divisor = 1;
                    double sum = (double)FLOAT_80616af0;
                    int e = 0;
                    unsigned int words = desc[8];
                    unsigned int running = desc[7];
                    if (0 < (int)dim) {
                        do {
                            int elemIdx = g / divisor - ((g / divisor) / radix) * radix;
                            float v = (float)(sum + scale *
                                ABS((double)(float)((double)CONCAT44(0x43300000,
                                     *(unsigned int *)(words + elemIdx * 4) ^ 0x80000000)
                                     - DOUBLE_80616ae8)) + base);
                            if (running != 0) {
                                sum = (double)v;
                            }
                            if (keep == 0) {
                                *(float *)(table + (e + outRow * (int)desc[0]) * 4) = v;
                            } else {
                                *(float *)(table + (e + (int)desc[0] *
                                    *(int *)(keep + presIdx)) * 4) = v;
                            }
                            divisor = divisor * radix;
                            e = e + 1;
                            dim = dim - 1;
                        } while (dim != 0);
                    }
                    outRow = outRow + 1;
                    presIdx = presIdx + 4;
                }
                go = go + 4;
            }
        }
    }
    FUN_80382730();
    return table;
}

// ---------------------------------------------------------------------------
// anim_codebook_table_free_inner (FUN_8019f348)
// Free the sub-buffers of a 0x34-byte codebook decode-table object (guarded by
// the live flag at +0x30): the sorted-code array (+0x20), the fast-lookup table
// (+8), the four-plane index table (+0x24, itself a {a,b,c,d} of buffers, then
// its 0x18 header), the two-plane payload table (+0x28, {a,b} + 0x10 header),
// and finally zero the 0x34 header.
// ---------------------------------------------------------------------------
void anim_codebook_table_free_inner(int obj) {
    if (*(int *)(obj + 0x30) != 0) {
        if (*(int *)(obj + 0x20) != 0) {
            FUN_801a14dc(*(int *)(obj + 0x20));
        }
        if (*(int *)(obj + 8) != 0) {
            FUN_801a14dc(*(int *)(obj + 8));
        }
        if (*(int **)(obj + 0x24) != (int *)0x0) {
            FUN_801a14dc(**(int **)(obj + 0x24));
            FUN_801a14dc(*(int *)(*(int *)(obj + 0x24) + 4));
            FUN_801a14dc(*(int *)(*(int *)(obj + 0x24) + 8));
            FUN_801a14dc(*(int *)(*(int *)(obj + 0x24) + 0xc));
            memset_wrapper(*(int *)(obj + 0x24), 0, 0x18);
            FUN_801a14dc(*(int *)(obj + 0x24));
        }
        if (*(int **)(obj + 0x28) != (int *)0x0) {
            FUN_801a14dc(**(int **)(obj + 0x28));
            FUN_801a14dc(*(int *)(*(int *)(obj + 0x28) + 4));
            memset_wrapper(*(int *)(obj + 0x28), 0, 0x10);
            FUN_801a14dc(*(int *)(obj + 0x28));
        }
        memset_wrapper(obj, 0, 0x34);
    }
}

// ---------------------------------------------------------------------------
// anim_codebook_table_free (FUN_8019f434)
// Free a codebook decode-table object: tear down its sub-buffers
// (anim_codebook_table_free_inner) then free the object itself. No-op unless the
// live flag at +0x30 is set.
// ---------------------------------------------------------------------------
void anim_codebook_table_free(int obj) {
    if (*(int *)(obj + 0x30) != 0) {
        anim_codebook_table_free_inner(obj);
        FUN_801a14dc(obj);
    }
}

// ---------------------------------------------------------------------------
// anim_codebook_desc_free (FUN_8019f474)
// Free the five sub-buffers of a 0x2c-byte codebook descriptor object
// (+0x10,+0x14,+0x18,+0x1c,+0x20) then zero the header.
// ---------------------------------------------------------------------------
void anim_codebook_desc_free(int desc) {
    if (*(int *)(desc + 0x10) != 0) FUN_801a14dc(*(int *)(desc + 0x10));
    if (*(int *)(desc + 0x14) != 0) FUN_801a14dc(*(int *)(desc + 0x14));
    if (*(int *)(desc + 0x18) != 0) FUN_801a14dc(*(int *)(desc + 0x18));
    if (*(int *)(desc + 0x1c) != 0) FUN_801a14dc(*(int *)(desc + 0x1c));
    if (*(int *)(desc + 0x20) != 0) FUN_801a14dc(*(int *)(desc + 0x20));
    memset_wrapper(desc, 0, 0x2c);
}

// ---------------------------------------------------------------------------
// anim_codebook_desc_build_simple (FUN_8019f4fc)
// Light (flat, un-sorted) codebook descriptor builder. Zero the 0x2c object,
// copy the source header (dim = src[0], count = src[1] into both +4 and +8, the
// source pointer into +0xc), build the canonical Huffman-length width map
// (anim_codebook_zigzag_rewrite of src's widths, keep=0 -> dense) into +0x14,
// and the per-codeword dequant table (anim_codebook_dequant_table) into +0x10.
// Returns 0.
// ---------------------------------------------------------------------------
int anim_codebook_desc_build_simple(int *desc, int *src) {
    memset_wrapper((int)desc, 0, 0x2c);
    int count = src[1];
    desc[3] = (int)src;
    desc[1] = count;
    desc[2] = count;
    desc[0] = src[0];
    desc[5] = anim_codebook_zigzag_rewrite(src[2], count, 0);
    desc[4] = anim_codebook_dequant_table((int)src, src[1], 0);
    return 0;
}

// ---------------------------------------------------------------------------
// bit_reverse32_b (FUN_8019f57c)
// Reverse all 32 bits of v (the standard log-step swap network: 16,8,4,2,1).
// Byte-identical duplicate of bit_reverse32 (FUN_801945b8, batch_510); kept as
// its own leaf to match the ROM's second copy.
// ---------------------------------------------------------------------------
unsigned int bit_reverse32_b(unsigned int v) {
    v = v >> 0x10 | v << 0x10;
    v = v >> 8 & 0xff00ff | (v & 0xff00ff) << 8;
    v = v >> 4 & 0xf0f0f0f | (v & 0xf0f0f0f) << 4;
    v = v >> 2 & 0x33333333 | (v & 0x33333333) << 2;
    return v >> 1 & 0x55555555 | (v & 0x55555555) << 1;
}

// ---------------------------------------------------------------------------
// anim_codebook_build_decode (FUN_8019f64c)
// The heavy codebook decode-table builder. (dst:src) pair via FUN_803826d0
// (hi32 = 0x34 dst object, lo32 = src descriptor). Steps:
//  * count the non-empty length groups (src widths at src[2], count src[1]);
//    copy dim/count into the dst header.
//  * canonicalize the group widths into Huffman codes (anim_codebook_zigzag_rewrite,
//    keep = non-empty count) into a temp; on failure free the object.
//  * bit-reverse each code (bit_reverse32) and sort the code pointers ascending
//    (FUN_8038a0a4 with the embedded comparator LAB_8019f61c) into a stack array.
//  * scatter the sorted codes into dst[5] (rank-quantized index >>2), and build
//    dst[6] (group->slot map) and dst[7] (per-slot width byte) side arrays over
//    the non-empty groups.
//  * derive the fast direct-lookup depth dst[9] = clamp(bitlen(nonEmpty)-4,5,8),
//    alloc dst[8] = 1<<depth entries. For codes no longer than the depth, splat
//    the leaf index across every table slot whose low `width` bits match the
//    (bit-reversed) code; for longer codes, store a packed {loBound<<15 | 0x80000000
//    | hiBound} binary-search hint (lo/hi computed by advancing two cursors over
//    the sorted codes, both clamped to 0x7fff).
//  * finally build the dequant table (anim_codebook_dequant_table into dst[4]).
// Uses a stack VLA (the code-pointer array) modeled with _alloca.
// ---------------------------------------------------------------------------
void anim_codebook_build_decode(void) {
    unsigned long long pair = FUN_803826d0();
    unsigned int *dst = (unsigned int *)((unsigned long long)pair >> 0x20);
    int *src = (int *)pair;
    unsigned int nonEmpty = 0;

    memset_wrapper((int)dst, 0, 0x2c);
    int groups = src[1];
    int go = 0;
    int rem = groups;
    if (0 < groups) {
        do {
            if (0 < *(int *)(src[2] + go)) {
                nonEmpty = nonEmpty + 1;
            }
            go = go + 4;
            rem = rem - 1;
        } while (rem != 0);
    }
    dst[1] = groups;
    dst[2] = nonEmpty;
    dst[0] = (unsigned int)src[0];

    int codes = anim_codebook_zigzag_rewrite(src[2], src[1], (int)nonEmpty);

    unsigned int **codePtrs = (unsigned int **)_alloca((nonEmpty * 4 + 7) & ~7);

    if (codes == 0) {
        anim_codebook_desc_free((int)dst);
    } else {
        int co = 0;
        for (unsigned int i = 0; i < nonEmpty; i = i + 1) {
            unsigned int *pc = (unsigned int *)(codes + co);
            *pc = bit_reverse32_b(*pc);
            codePtrs[i] = pc;
            co = co + 4;
        }
        FUN_8038a0a4((int)codePtrs, nonEmpty, 4, &LAB_8019f61c);

        // sorted-position ("slot") map keyed by rank-quantized code offset
        int *slotOf = (int *)_alloca((nonEmpty * 4 + 7) & ~7);
        dst[5] = (unsigned int)FUN_801a13e0(nonEmpty * 4);
        int scanIdx = 0;
        int so = 0;
        unsigned int c = nonEmpty;
        if (0 < (int)nonEmpty) {
            do {
                unsigned int off = (unsigned int)((int)codePtrs[so / 4] - codes);
                slotOf[((int)off >> 2) + (unsigned int)((int)off < 0 && (off & 3) != 0)] = scanIdx;
                scanIdx = scanIdx + 1;
                so = so + 4;
                c = c - 1;
            } while (c != 0);
        }
        // scatter codes into dst[5] by slot
        int eo = 0;
        c = nonEmpty;
        if (0 < (int)nonEmpty) {
            do {
                int slot = slotOf[eo / 4];
                *(int *)(dst[5] + slot * 4) = *(int *)(codes + eo);
                eo = eo + 4;
                c = c - 1;
            } while (c != 0);
        }
        FUN_801a14dc(codes);

        dst[4] = anim_codebook_dequant_table((int)src, nonEmpty, (int)slotOf);

        // group->slot map (dst[6]) over non-empty groups
        dst[6] = (unsigned int)FUN_801a13e0(nonEmpty * 4);
        int wsrc = src[2];
        int grp = 0;
        int gi = 0;
        int slotN = 0;
        int ro = 0;
        int grem = src[1];
        if (0 < grem) {
            do {
                if (0 < *(int *)(wsrc + gi)) {
                    *(int *)(dst[6] + slotOf[ro / 4] * 4) = grp;
                    slotN = slotN + 1;
                    ro = ro + 4;
                }
                grp = grp + 1;
                gi = gi + 4;
                grem = grem - 1;
            } while (grem != 0);
        }

        // per-slot width byte (dst[7]) over non-empty groups
        dst[7] = (unsigned int)FUN_801a13e0(slotN);
        int wsrc2 = src[2];
        int gi2 = 0;
        int ro2 = 0;
        int widthCount = 0;
        int grem2 = src[1];
        if (0 < grem2) {
            do {
                if (0 < *(int *)(wsrc2 + gi2)) {
                    *(char *)(dst[7] + slotOf[ro2 / 4]) = (char)*(int *)(wsrc2 + gi2);
                    widthCount = widthCount + 1;
                    ro2 = ro2 + 4;
                }
                gi2 = gi2 + 4;
                grem2 = grem2 - 1;
            } while (grem2 != 0);
        }

        // fast direct-lookup table: depth clamp [5,8]
        int depth = anim_bit_length2((unsigned int)dst[2]) - 4;
        if (depth < 5) depth = 5;
        if (8 < depth) depth = 8;
        dst[9] = depth;
        dst[8] = FUN_801a1498(1 << depth, 4);
        dst[10] = 0;

        int wo = 0;
        for (int s = 0; s < widthCount; s = s + 1) {
            if ((int)dst[10] < (int)*(char *)(dst[7] + s)) {
                dst[10] = (int)*(char *)(dst[7] + s);
            }
            if ((int)*(char *)(dst[7] + s) <= (int)dst[9]) {
                unsigned int codeRev = bit_reverse32_b(*(unsigned int *)(dst[5] + wo));
                int fill = 1 << ((int)dst[9] - (int)*(char *)(dst[7] + s));
                for (int j = 0; j < fill; j = j + 1) {
                    *(int *)(dst[8] +
                        (codeRev | (unsigned int)(j << (int)*(char *)(dst[7] + s))) * 4) = s + 1;
                }
            }
            wo = wo + 4;
        }

        // long-code binary-search hints
        int shift = dst[9];
        int loIdx = 0;
        int loOff = 0;
        int hiIdx = 0;
        int hiOff = 0;
        for (int t = 0; t < (1 << depth); t = t + 1) {
            unsigned int key = (unsigned int)t << (0x20 - (int)dst[9]);
            unsigned int keyRev = bit_reverse32_b(key);
            if (*(int *)(dst[8] + keyRev * 4) == 0) {
                for (; ((loIdx + 1) < widthCount) &&
                       (*(unsigned int *)(dst[5] + loOff + 4) <= key); loOff = loOff + 4) {
                    loIdx = loIdx + 1;
                }
                for (; (hiIdx < widthCount) &&
                       ((unsigned int)(-2 << (0x1f - shift) & (int)*(unsigned int *)(dst[5] + hiOff))
                        <= key); hiOff = hiOff + 4) {
                    hiIdx = hiIdx + 1;
                }
                unsigned int hiCount = (unsigned int)(widthCount - hiIdx);
                unsigned int loClamp = (unsigned int)loIdx;
                if (0x7fff < loClamp) loClamp = 0x7fff;
                if (0x7fff < hiCount) hiCount = 0x7fff;
                unsigned int slot = bit_reverse32_b(key);
                *(unsigned int *)(dst[8] + slot * 4) = loClamp << 0xf | 0x80000000 | hiCount;
            }
        }
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// anim_fft_build_twiddles (FUN_8019faac)
// Factor N (from the (N:twiddleOut) pair via FUN_803826b8) into small radices
// and, once the residue reaches 1, fill the cos/sin twiddle table `twiddleOut`.
// The factor probe walks DAT_80425fb4 = {2,3,5,7} for the first four trials then
// steps by 2; each fully-dividing factor is recorded into param_3[2+k] (the
// factor list), with a special reorder that pushes a leading 2 to the front.
// When the residue divides to 1 it stores {N (param_3[0]), factorCount
// (param_3[1])} and then, per factor stage, computes the per-element rotation
// angle theta = (2*pi*index/N) and writes cos(theta*step)/sin(theta*step) pairs
// into twiddleOut (FUN_80390250 cos, FUN_80390914 sin). (The removed unreachable
// blocks are the decompiler's FP-spill epilogue variants.)
// ---------------------------------------------------------------------------
void anim_fft_build_twiddles(int p0, int p1, unsigned int *factors) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826b8();
    unsigned int N = (unsigned int)((unsigned long long)pair >> 0x20);
    int factor = 0;
    int trial = -1;
    unsigned int factorCount = 0;
    int fo = 0;
    int probeOff = -4;
    unsigned int residue = N;

    do {
        trial = trial + 1;
        int nextProbe = probeOff + 4;
        unsigned int savedCount = factorCount;
        if (trial < 4) {
            factor = *(int *)((int)&DAT_80425fb4 + probeOff);
        } else {
            factor = factor + 2;
        }
        unsigned int quotient;
        while (factorCount = savedCount,
               quotient = (unsigned int)((int)residue / factor),
               probeOff = nextProbe,
               residue == (unsigned int)(factor * (int)quotient)) {
            *(int *)((int)factors + fo + 8) = factor;
            savedCount = factorCount + 1;
            fo = fo + 4;
            if ((factor == 2) && (savedCount != 1)) {
                int mo = 1;
                unsigned int mrem = factorCount;
                if (1 < (int)savedCount) {
                    do {
                        int idx = savedCount - mo;
                        mo = mo + 1;
                        factors[idx + 2] = factors[idx + 1];
                        mrem = mrem - 1;
                    } while (mrem != 0);
                }
                factors[2] = 2;
            }
            double bias = DOUBLE_80616b00;
            residue = quotient;
            if (quotient == 1) {
                int wo = 0;
                factors[0] = N;
                factors[1] = savedCount;
                double angScale = (double)(FLOAT_80610b18 /
                    (float)((double)CONCAT44(0x43300000, N ^ 0x80000000) - bias));
                if (factorCount != 0) {
                    double step = (double)FLOAT_80616afc;
                    int stride = 1;
                    int stageOff = 0;
                    for (int s = 0; s < (int)factorCount; s = s + 1) {
                        unsigned int accum = 0;
                        int rad = *(int *)((int)factors + stageOff + 8);
                        int group = stride * rad;
                        int span = (int)N / group;
                        for (int e = 0; e < rad + -1; e = e + 1) {
                            accum = accum + stride;
                            double inner = (double)FLOAT_80616af8;
                            int to = wo << 2;
                            double theta = (double)(float)((double)(float)((double)CONCAT44(
                                0x43300000, accum ^ 0x80000000) - bias) * angScale);
                            for (int k = 2; k < span; k = k + 2) {
                                inner = (double)(float)(inner + step);
                                double ang = (double)(float)(inner * theta);
                                double c = FUN_80390250(ang);
                                *(float *)((int)pair + to) = (float)c;
                                double si = FUN_80390914(ang);
                                *(float *)((int)pair + to + 4) = (float)si;
                                to = to + 8;
                            }
                            wo = wo + span;
                        }
                        stageOff = stageOff + 4;
                        stride = group;
                    }
                }
                FUN_80382704();
                return;
            }
        }
    } while (1);
}

// ---------------------------------------------------------------------------
// anim_fft_build_twiddles_guard (FUN_8019fcf4)
// Skip the twiddle build entirely for N==1 (a degenerate single-point
// transform); otherwise advance the factor-list pointer past its header
// (param_2 + N*4) and build (anim_fft_build_twiddles).
// ---------------------------------------------------------------------------
void anim_fft_build_twiddles_guard(int N, int factorList, unsigned int *factors) {
    if (N != 1) {
        anim_fft_build_twiddles(N, factorList + N * 4, factors);
    }
}

// ---------------------------------------------------------------------------
// anim_fft_split_radix_stage (FUN_8019fd24)
// One decimation butterfly pass. The (rows:cols) pair via FUN_803826d0 (hi32 =
// rows, lo32 = cols): for each of `rows` sub-transforms, the even half is the
// sum/difference of the two mirror bins (out[0]=a+b, out[N-1]=a-b), and - when
// there are more than 2 columns - the odd half applies the complex twiddle
// rotation from the `twiddle` table (param_5) to each conjugate-symmetric bin
// pair (real/imag cross terms). When the column count is odd, a final tail pass
// copies the middle bin with a real/negated-imag conjugate. src=param_3,
// dst=param_4, twiddle=param_5.
// ---------------------------------------------------------------------------
void anim_fft_split_radix_stage(int p0, int p1, int src, int dst, int twiddle) {
    (void)p0; (void)p1;
    long long pair = (long long)FUN_803826d0();
    unsigned int cols = (unsigned int)((unsigned long long)pair >> 0x20);
    int rows = (int)pair;
    int mirrorBase = rows * (int)cols;

    // even half: sum/difference of mirror bins
    int srcOff = 0;
    int mirrorOff = mirrorBase;
    int r = rows;
    if (0 < rows) {
        do {
            int mo = mirrorOff * 4;
            int so = srcOff * 4;
            int half = srcOff * 2;
            int dOff = srcOff * 8;
            srcOff = srcOff + cols;
            mirrorOff = mirrorOff + cols;
            *(float *)(dst + dOff) = *(float *)(src + so) + *(float *)(src + mo);
            *(float *)(dst + (half + cols * 2) * 4 + -4) =
                *(float *)(src + so) - *(float *)(src + mo);
            r = r - 1;
        } while (r != 0);
    }

    if (0x1ffffffff < pair) {
        if (cols != 2) {
            int cOff = 0;
            int mOff = mirrorBase;
            for (int row = 0; row < rows; row = row + 1) {
                unsigned int pairs = cols - 1 >> 1;
                int hiOff = (cOff + cols) * 8;
                int dLo = cOff << 3;
                int sLo = cOff << 2;
                int sHi = mOff << 2;
                int to = 8;
                if (0x2ffffffff < pair) {
                    do {
                        sHi = sHi + 8;
                        sLo = sLo + 8;
                        float aIm = ((float *)(src + sHi))[-1];
                        float *pLo = (float *)(src + sLo);
                        float wIm = *(float *)(twiddle + to + -4);
                        dLo = dLo + 8;
                        float aRe = *(float *)(src + sHi);
                        float wRe = *(float *)(twiddle + to + -8);
                        hiOff = hiOff + -8;
                        float rRe = wRe * aRe - wIm * aIm;
                        to = to + 8;
                        float rIm = wRe * aIm + wIm * aRe;
                        *(float *)(dst + dLo) = rRe + *pLo;
                        *(float *)(dst + hiOff) = rRe - *pLo;
                        ((float *)(dst + dLo))[-1] = rIm + pLo[-1];
                        ((float *)(dst + hiOff))[-1] = pLo[-1] - rIm;
                        pairs = pairs - 1;
                    } while (pairs != 0);
                }
                cOff = cOff + cols;
                mOff = mOff + cols;
            }
            if ((cols & 1 ^ -((int)cols >> 0x1f)) + ((int)cols >> 0x1f) == 1) {
                FUN_8038271c();
                return;
            }
        }
        // odd column tail: conjugate-mirror the middle bin
        int midIdx = cols - 1;
        int midMirror = midIdx + mirrorBase;
        if (0 < rows) {
            int rrem;
            do {
                int hi = (int)((unsigned long long)pair >> 0x20);
                int mmo = midMirror * 4;
                int mio = midIdx * 4;
                float *pd = (float *)(dst + hi * 4);
                midMirror = midMirror + cols;
                midIdx = midIdx + cols;
                *pd = -*(float *)(src + mmo);
                pd[-1] = *(float *)(src + mio);
                rrem = (int)pair - 1;
                pair = (long long)(((unsigned long long)(unsigned int)(hi + cols * 2) << 0x20)
                                   | (unsigned int)rrem);
            } while (rrem != 0);
        }
    }
    FUN_8038271c();
}

// ===========================================================================
// batch_523 (0x8019ff14 - 0x801a1230): the mixed-radix FFT COMPUTE engine that
// the spectral transform stage runs on, plus the per-block decode dispatcher,
// the weight-window / vtable lookups, and the global scratch-pool init.
//
// FFT: a plan (anim_fft_plan_init) factors N (anim_fft_build_twiddles_guard,
// batch_522) into a factor list + a twiddle table; anim_fft_execute walks the
// factor list from the innermost stage outward, ping-ponging between the input
// buffer and a scratch buffer, dispatching a radix-4 stage
// (anim_fft_radix4_stage), a radix-2 stage (anim_fft_split_radix_stage,
// batch_522), or the general prime/odd-radix DFT stage
// (anim_fft_mixed_radix_stage) per factor, then copies the result back if it
// landed in scratch. anim_fft_transform is the (skip N==1) wrapper. The radix-4
// and mixed-radix kernels are the classic decimation butterflies with the
// twiddle rotations read from the plan's per-stage tables.
//
// Decode: anim_decode_next_block is the per-frame block dispatcher - it flushes
// the scratch chunk list, rebinds the bit-reader over the next packet, reads the
// block type bit + codebook index, resolves the channel descriptor, latches the
// present/keyframe flags and the four 64-bit timestamp words, allocates the
// per-channel output row array from the arena, and dispatches the builder's
// decode slot (vtable PTR_PTR_80610b10). anim_weight_table_lookup /
// anim_build_weight_window supply the crossfade LERP window the decode step
// blends channels through, and anim_scratch_pool_init carves the global
// per-slot scratch pool (0x64000-byte page + 4000-byte index per slot).
//
// Addresses/strides verbatim. (state:data) pairs via the shared spill helpers;
// FFT stage buffers are the raw plan/scratch pointers; the mem_alloc_aligned
// call keeps its full PPC arg list (only the meaningful align/size/type used).
// ===========================================================================
extern "C" {
// builder/weight-table vtable: PTR_DAT_8042df40[i] is the i-th window byte array
extern unsigned char *PTR_DAT_8042df40[];

// twiddle / radix-2 rotation constant (1/sqrt2-scaled combine factor)
extern float FLOAT_80610b1c;
// mixed-radix angle scale numerator (2*pi form for FUN_80390250/FUN_80390914)
extern float FLOAT_80610b20;
// weight-window fill sentinel (0.0f)
extern float FLOAT_80616b08;

// global scratch pool (page base, index base, cursors)
extern int DAT_80612508;   // per-slot index array base (param_9*4000)
extern int DAT_8061250c;   // per-slot data page base (param_9*0x64000)
extern int DAT_80612500;   // pool cursor A
extern int DAT_80612504;   // pool cursor B
extern int mem_alloc_aligned(unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, int align, int size,
                             int type, int, int, int, int, int);

// block decode dependencies
extern void anim_ctx_free_scratch_chunks(void);
extern void bitwriter_reset(int *br, int base, int len);
extern int *PTR_PTR_80610b10[];
} // extern "C"

// ---------------------------------------------------------------------------
// anim_fft_radix4_stage (FUN_8019ff14)
// One radix-4 decimation butterfly pass. The (rows:cols) pair via FUN_803826bc.
// The initial pass forms the 4-point butterflies of the two mirror-bin pairs
// (sum/difference for the real part, cross-difference for the imaginary part);
// when cols>2 the middle band applies the three twiddle rotations (param_5/6/7
// tables) to the three non-DC quarters and recombines; the odd-column tail does
// the 1/sqrt2-scaled diagonal combine (FLOAT_80610b1c). src=param_3, dst=param_4.
// ---------------------------------------------------------------------------
void anim_fft_radix4_stage(int p0, int p1, int src, int dst, int tw1, int tw2, int tw3) {
    (void)p0; (void)p1;
    unsigned long long pair = FUN_803826bc();
    int cols = (int)(pair >> 0x20);
    int rows = (int)pair;
    int q = rows * cols;             // quarter stride
    int col2 = cols * 2;

    // initial radix-4 butterflies over the four quarters
    int i = 0;
    int q1 = q;
    int q3 = q * 3;
    int q2 = q * 2;
    int r = rows;
    if (0 < rows) {
        do {
            int o0 = i * 4;
            int o1 = q1 * 4;
            int o3 = q3 * 4;
            int o2 = q2 * 4;
            int dOff = i * 0x10;
            float a13 = *(float *)(src + o1) + *(float *)(src + o3);
            float a02 = *(float *)(src + o0) + *(float *)(src + o2);
            int mid = (o0 + col2) * 4;
            q1 = q1 + cols;
            q3 = q3 + cols;
            i = i + cols;
            q2 = q2 + cols;
            *(float *)(dst + dOff) = a13 + a02;
            *(float *)(dst + (cols * 4 + o0) * 4 + -4) = a02 - a13;
            *(float *)(dst + mid + -4) = *(float *)(src + o0) - *(float *)(src + o2);
            *(float *)(dst + mid) = *(float *)(src + o3) - *(float *)(src + o1);
            r = r - 1;
        } while (r != 0);
    }

    if (0x1ffffffff < (long long)pair) {
        if (cols != 2) {
            int base = 0;
            for (int row = 0; row < rows; row = row + 1) {
                int lo = base * 4;
                int hi = lo + col2;
                unsigned int pairs = cols - 1U >> 1;
                int hiOff = hi * 4;
                int dOff = base << 4;
                int to = 8;
                int cLo = base;
                int cHi = lo;
                if (0x2ffffffff < (long long)pair) {
                    do {
                        cLo = cLo + 2;
                        float *p1a = (float *)(src + (cLo + q) * 4);
                        int idx2 = cLo + q + q;
                        float w1i = *(float *)(tw1 + to + -4);
                        float *p2a = (float *)(src + idx2 * 4);
                        float x1r = *p1a;
                        float *p3a = (float *)(src + (idx2 + q) * 4);
                        float x1i = p1a[-1];
                        float w1r = *(float *)(tw1 + to + -8);
                        float w2i = *(float *)(tw2 + to + -4);
                        float x2r = *p2a;
                        hi = hi + 8;
                        float x2i = p2a[-1];
                        float w3i = *(float *)(tw3 + to + -4);
                        float t1i = w1r * x1i + w1i * x1r;
                        float x3r = *p3a;
                        float x3i = p3a[-1];
                        float w2r = *(float *)(tw2 + to + -8);
                        float w3r = *(float *)(tw3 + to + -8);
                        float t2i = w2r * x2i + w2i * x2r;
                        float t2r = w2r * x2r - w2i * x2i;
                        float srcMidR = *(float *)(src + lo + 8);
                        dOff = dOff + 8;
                        float t3i = w3r * x3i + w3i * x3r;
                        float srcMidI = ((float *)(src + lo + 8))[-1];
                        float t1r = w1r * x1r - w1i * x1i;
                        float t3r = w3r * x3r - w3i * x3i;
                        hiOff = hiOff + -8;
                        float e0 = t1i + t3i;
                        float e1 = t2i + srcMidI;
                        cHi = cHi + 2;
                        float o6 = t2r + srcMidR;
                        float o7 = t1r + t3r;
                        float o7b = t1r - t3r;
                        float e1b = srcMidI - t2i;
                        int c2 = hi + -2;
                        ((float *)(dst + dOff))[-1] = e0 + e1;
                        float e0b = t3i - t1i;
                        float o6b = srcMidR - t2r;
                        *(float *)(dst + dOff) = o7 + o6;
                        float *dHi1 = (float *)(dst + (cHi + col2) * 4);
                        float *dHi2 = (float *)(dst + (c2 + col2) * 4);
                        ((float *)(dst + hiOff))[-1] = e1b - o7b;
                        to = to + 8;
                        *(float *)(dst + hiOff) = e0b - o6b;
                        dHi1[-1] = o7b + e1b;
                        *dHi1 = e0b + o6b;
                        dHi2[-1] = e1 - e0;
                        *dHi2 = o7 - o6;
                        (void)lo;
                        lo = lo + 8;
                        pairs = pairs - 1;
                    } while (pairs != 0);
                }
                base = base + cols;
            }
            if ((pair & 0x100000000) != 0) {
                FUN_80382708();
                return;
            }
        }
        // odd-column tail: 1/sqrt2 diagonal combine
        int lo = q + cols + -1;
        int hi = q * 2 + lo;
        int c = cols;
        if (0 < rows) {
            do {
                int dstIdx = (int)(pair >> 0x20);
                float xlo = *(float *)(src + lo * 4);
                float xhi = *(float *)(src + hi * 4);
                int co = c * 4 + -4;
                float *pd = (float *)(dst + dstIdx * 4);
                float d1 = FLOAT_80610b1c * (xlo - xhi);
                float *pd2 = (float *)(dst + (dstIdx + col2) * 4);
                int qo = (lo + q) * 4;
                lo = lo + cols;
                hi = hi + cols;
                float d2 = -FLOAT_80610b1c * (xlo + xhi);
                c = c + cols;
                pd[-1] = d1 + *(float *)(src + co);
                pd2[-1] = *(float *)(src + co) - d1;
                *pd = d2 - *(float *)(src + qo);
                *pd2 = d2 + *(float *)(src + qo);
                int rrem = (int)pair - 1;
                pair = (long long)(((unsigned long long)(unsigned int)(dstIdx + cols * 4) << 0x20)
                                   | (unsigned int)rrem);
                if (rrem == 0) break;
            } while (1);
        }
    }
    FUN_80382708();
}

// ---------------------------------------------------------------------------
// anim_fft_mixed_radix_stage (FUN_801a0260)
// General prime/odd-radix DFT stage. The (radix:stride) pair via FUN_803826b8.
// param_3 = sub-count, param_4 = row length, param_5 = output, param_6 = work A,
// param_7 = work B, param_8/9 = scratch, param_10 = twiddle table. Computes the
// stage's base rotation (theta = 2*pi/radix, cos/sin via FUN_80390250/914),
// applies the per-lane twiddle multiplies to fold each of the radix inputs, then
// runs the length-radix DFT as a sequence of butterfly add/sub passes with the
// running twiddle recurrence (dVar27/dVar32 rotate), scattering the result back
// out with the conjugate-symmetric mirror layout. (The heavy body is a faithful
// transcription of the ROM's nested lane/pass loops.)
// ---------------------------------------------------------------------------
void anim_fft_mixed_radix_stage(int p0, int radixArg, int subCount, int rowLen, int out,
                                int workA, int twInOut, int scratchA, int scratchB, int twiddle) {
    (void)p0; (void)radixArg;
    long long pair = (long long)FUN_803826b8();
    int stride = (int)((unsigned long long)pair >> 0x20);
    unsigned int radix = (unsigned int)pair;
    double angle = (double)(FLOAT_80610b20 /
        (float)((double)CONCAT44(0x43300000, radix ^ 0x80000000) - DOUBLE_80616b00));
    double baseC = (double)(float)FUN_80390250(angle);
    double baseS = FUN_80390914(angle);
    int rowStride = subCount * stride;
    int half = (int)(radix + 1) >> 1;
    int span = (int)radix * stride;
    int halfCols = stride + -1 >> 1;

    if (stride != 1) {
        // seed work buffers from the twiddle inputs
        int o = 0;
        int r = rowLen;
        if (0 < rowLen) {
            do {
                *(int *)(scratchB + o) = *(int *)(twInOut + o);
                o = o + 4;
                r = r - 1;
            } while (r != 0);
        }
        o = 0;
        for (int lane = 1; lane < (int)radix; lane = lane + 1) {
            o = o + rowStride;
            int idx = o;
            int c = subCount;
            if (0 < subCount) {
                do {
                    int to = idx * 4;
                    idx = idx + stride;
                    *(int *)(scratchA + to) = *(int *)(workA + to);
                    c = c - 1;
                } while (c != 0);
            }
        }
        // per-lane twiddle multiply passes (two layouts by halfCols vs subCount)
        int lo = -stride;
        int base = 0;
        if (subCount < halfCols) {
            for (int lane = 1; lane < (int)radix; lane = lane + 1) {
                base = base + rowStride;
                lo = lo + stride;
                int row = base - stride;
                for (int s = 0; s < subCount; s = s + 1) {
                    row = row + stride;
                    unsigned int pairs = stride - 1U >> 1;
                    int tOff = (lo + -1) * 4;
                    int rOff = row * 4;
                    if (0x2ffffffff < pair) {
                        do {
                            rOff = rOff + 8;
                            tOff = tOff + 8;
                            float *pt = (float *)(twiddle + tOff);
                            float *pw = (float *)(workA + rOff);
                            ((float *)(scratchA + rOff))[-1] = pt[-1] * pw[-1] + *pt * *pw;
                            *(float *)(scratchA + rOff) = pt[-1] * *pw - *pt * pw[-1];
                            pairs = pairs - 1;
                        } while (pairs != 0);
                    }
                }
            }
        } else {
            for (int lane = 1; lane < (int)radix; lane = lane + 1) {
                lo = lo + stride;
                base = base + rowStride;
                int tOff = (lo + -1) * 4;
                int row = base;
                for (int k = 2; k < stride; k = k + 2) {
                    row = row + 2;
                    tOff = tOff + 8;
                    float *pt = (float *)(twiddle + tOff);
                    int col = row;
                    int c = subCount;
                    if (0 < subCount) {
                        do {
                            float *pw = (float *)(workA + col * 4);
                            float *ps = (float *)(scratchA + col * 4);
                            col = col + stride;
                            ps[-1] = pt[-1] * pw[-1] + *pt * *pw;
                            *ps = pt[-1] * *pw - *pt * pw[-1];
                            c = c - 1;
                        } while (c != 0);
                    }
                }
            }
        }
        // butterfly add/sub passes between mirror lanes
        int fwd = 0;
        int rev = (int)radix * rowStride;
        if (halfCols < subCount) {
            for (int lane = 1; lane < half; lane = lane + 1) {
                fwd = fwd + rowStride;
                rev = rev - rowStride;
                int a = fwd;
                int b = rev;
                for (int k = 2; k < stride; k = k + 2) {
                    a = a + 2;
                    b = b + 2;
                    int ao = a - stride;
                    int bo = b - stride;
                    int c = subCount;
                    if (0 < subCount) {
                        do {
                            ao = ao + stride;
                            bo = bo + stride;
                            float *pA = (float *)(scratchA + ao * 4);
                            float *pAw = (float *)(workA + ao * 4);
                            float *pB = (float *)(scratchA + bo * 4);
                            float *pBw = (float *)(workA + bo * 4);
                            pAw[-1] = pA[-1] + pB[-1];
                            pBw[-1] = *pA - *pB;
                            *pAw = *pA + *pB;
                            *pBw = pB[-1] - pA[-1];
                            c = c - 1;
                        } while (c != 0);
                    }
                }
            }
        } else {
            for (int lane = 1; lane < half; lane = lane + 1) {
                fwd = fwd + rowStride;
                rev = rev - rowStride;
                int a = rev;
                int b = fwd;
                for (int s = 0; s < subCount; s = s + 1) {
                    unsigned int pairs = stride - 1U >> 1;
                    int ao = a << 2;
                    int bo = b << 2;
                    if (0x2ffffffff < pair) {
                        do {
                            bo = bo + 8;
                            ao = ao + 8;
                            float *pB = (float *)(scratchA + bo);
                            float *pA = (float *)(scratchA + ao);
                            ((float *)(workA + bo))[-1] = pB[-1] + pA[-1];
                            ((float *)(workA + ao))[-1] = *pB - *pA;
                            *(float *)(workA + bo) = *pB + *pA;
                            *(float *)(workA + ao) = pA[-1] - pB[-1];
                            pairs = pairs - 1;
                        } while (pairs != 0);
                    }
                    b = b + stride;
                    a = a + stride;
                }
            }
        }
    }

    // copy twiddle inputs back
    int o = 0;
    int r = rowLen;
    if (0 < rowLen) {
        do {
            *(int *)(twInOut + o) = *(int *)(scratchB + o);
            o = o + 4;
            r = r - 1;
        } while (r != 0);
    }

    // recombine sub-transforms into the output rows
    int fwd = (int)radix * rowLen;
    int a = 0;
    int b = fwd;
    for (int lane = 1; lane < half; lane = lane + 1) {
        a = a + rowStride;
        b = b - rowStride;
        int ao = a - stride;
        int bo = b - stride;
        int c = subCount;
        if (0 < subCount) {
            do {
                ao = ao + stride;
                bo = bo + stride;
                int aoff = ao * 4;
                int boff = bo * 4;
                *(float *)(workA + aoff) = *(float *)(scratchA + aoff) + *(float *)(scratchA + boff);
                *(float *)(workA + boff) = *(float *)(scratchA + boff) - *(float *)(scratchA + aoff);
                c = c - 1;
            } while (c != 0);
        }
    }

    // running twiddle recurrence over the lane rotations
    double dc = (double)FLOAT_80616afc;
    int rev2 = (radix - 1) * rowLen;
    double ds = (double)FLOAT_80616af8;
    int fa = 0;
    for (int lane = 1; lane < half; lane = lane + 1) {
        fa = fa + rowLen;
        fwd = fwd - rowLen;
        double s1 = (double)(float)baseS * dc;
        dc = (double)(float)(baseC * dc - (double)(float)((double)(float)baseS * ds));
        ds = (double)(float)(baseC * ds + (double)(float)s1);
        int fo = fwd * 4;
        int ro = rev2 * 4;
        int fao = fa * 4;
        int wo = rowLen << 2;
        int j = 0;
        int c = rowLen;
        if (0 < rowLen) {
            do {
                float *pw1 = (float *)(twInOut + wo);
                float *pw0 = (float *)(twInOut + j);
                j = j + 4;
                wo = wo + 4;
                *(float *)(scratchB + fao) = (float)(dc * (double)*pw1 + (double)*pw0);
                fao = fao + 4;
                float *pr = (float *)(twInOut + ro);
                ro = ro + 4;
                *(float *)(scratchB + fo) = (float)(ds * (double)*pr);
                fo = fo + 4;
                c = c - 1;
            } while (c != 0);
        }
        int fb = rowLen;
        int rb = rev2;
        double ds2 = ds;
        double dc2 = dc;
        for (int m = 2; m < half; m = m + 1) {
            fb = fb + rowLen;
            double t = ds * dc2;
            rb = rb - rowLen;
            dc2 = (double)(float)(dc * dc2 - (double)(float)(ds * ds2));
            ds2 = (double)(float)(dc * ds2 + (double)(float)t);
            int fo2 = fwd * 4;
            int ro2 = rb * 4;
            int fao2 = fa * 4;
            int fbo = fb * 4;
            int c2 = rowLen;
            if (0 < rowLen) {
                do {
                    float *pw = (float *)(twInOut + fbo);
                    fbo = fbo + 4;
                    *(float *)(scratchB + fao2) =
                        (float)(dc2 * (double)*pw + (double)*(float *)(scratchB + fao2));
                    fao2 = fao2 + 4;
                    float *pr = (float *)(twInOut + ro2);
                    ro2 = ro2 + 4;
                    *(float *)(scratchB + fo2) =
                        (float)(ds2 * (double)*pr + (double)*(float *)(scratchB + fo2));
                    fo2 = fo2 + 4;
                    c2 = c2 - 1;
                } while (c2 != 0);
            }
        }
    }
    // accumulate the DC lane
    int acc = 0;
    for (int lane = 1; lane < half; lane = lane + 1) {
        acc = acc + rowLen;
        int j = 0;
        int ao = acc * 4;
        int c = rowLen;
        if (0 < rowLen) {
            do {
                float *pw = (float *)(twInOut + ao);
                ao = ao + 4;
                *(float *)(scratchB + j) = *(float *)(scratchB + j) + *pw;
                j = j + 4;
                c = c - 1;
            } while (c != 0);
        }
    }

    // scatter to output (two layouts by stride vs subCount)
    int rowSpan = span;
    if (stride < subCount) {
        for (int k = 0; k < stride; k = k + 1) {
            int a2 = k;
            int b2 = k;
            int c = subCount;
            if (0 < subCount) {
                do {
                    int ao = a2 * 4;
                    int bo = b2 * 4;
                    a2 = a2 + stride;
                    b2 = b2 + span;
                    *(int *)(out + bo) = *(int *)(workA + ao);
                    c = c - 1;
                } while (c != 0);
            }
        }
    } else {
        int a2 = 0;
        int b2 = 0;
        for (int b3 = 0; b3 < subCount; b3 = b3 + 1) {
            int ao = b2 << 2;
            int bo = a2 << 2;
            int c = stride;
            if (0xffffffff < pair) {
                do {
                    int *pv = (int *)(workA + bo);
                    bo = bo + 4;
                    *(int *)(out + ao) = *pv;
                    ao = ao + 4;
                    c = c - 1;
                } while (c != 0);
            }
            a2 = a2 + stride;
            b2 = b2 + span;
        }
    }
    (void)rowSpan;
    FUN_80382704();
}

// ---------------------------------------------------------------------------
// anim_fft_execute (FUN_801a0bec)
// FFT compute driver. The (N:...) pair via FUN_803826d4 (hi32 = N). Walks the
// factor list (plan param_5, count at +4, factors at +8 read back-to-front)
// from the innermost stage outward, ping-ponging between the input buffer
// (param_3) and the scratch buffer (dst = lo32). Per factor: radix 4 ->
// anim_fft_radix4_stage (with three twiddle sub-tables at increasing offsets),
// radix 2 -> anim_fft_split_radix_stage, else -> anim_fft_mixed_radix_stage.
// The ping-pong parity (iVar6) tracks which buffer holds the current data;
// after the last stage, copies scratch back into the input if it ended there.
// ---------------------------------------------------------------------------
void anim_fft_execute(int p0, int p1, int input, int scratch, int plan) {
    (void)p0; (void)p1;
    long long pair = (long long)FUN_803826d4();
    int N = (int)((unsigned long long)pair >> 0x20);
    int dst = (int)pair;
    int factorCount = *(int *)(plan + 4);
    int parity = 1;
    int radix = N;
    int outer = N;

    for (int s = 0; s < factorCount; s = s + 1) {
        parity = 1 - parity;
        int f = *(int *)(plan + (factorCount - s) * 4 + 4);
        int span = N / radix;
        radix = radix / f;
        outer = outer - (f + -1) * span;
        if (f == 4) {
            int t = outer + span;
            if (parity == 0) {
                anim_fft_radix4_stage(span, radix, dst, input, plan + outer * 4 + -4,
                                      plan + t * 4 + -4, plan + (t + span) * 4 + -4);
            } else {
                anim_fft_radix4_stage(span, radix, input, dst, plan + outer * 4 + -4,
                                      plan + t * 4 + -4, plan + (t + span) * 4 + -4);
            }
        } else if (f == 2) {
            if (parity == 0) {
                anim_fft_split_radix_stage(span, radix, dst, input, plan + outer * 4 + -4);
            } else {
                anim_fft_split_radix_stage(span, radix, input, dst, plan + outer * 4 + -4);
            }
        } else {
            if (span == 1) {
                parity = 1 - parity;
            }
            if (parity == 0) {
                anim_fft_mixed_radix_stage(span, f, radix, span * radix, dst, dst, dst,
                                           input, input, plan + outer * 4 + -4);
                parity = 1;
            } else {
                anim_fft_mixed_radix_stage(span, f, radix, span * radix, input, input, input,
                                           dst, dst, plan + outer * 4 + -4);
                parity = 0;
            }
        }
    }
    if ((parity != 1) && (0xffffffff < pair)) {
        int o = 0;
        do {
            *(int *)(dst + o) = *(int *)(input + o);
            o = o + 4;
            N = N - 1;
        } while (N != 0);
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// anim_fft_transform (FUN_801a0df8)
// Run the FFT plan over `data` unless N==1 (degenerate). Advances the scratch
// pointer past the input (plan[1] + N*4) and calls anim_fft_execute with the
// factor/twiddle table (plan[2]). plan[0] = N, plan[1] = data/scratch base,
// plan[2] = factor+twiddle table.
// ---------------------------------------------------------------------------
void anim_fft_transform(int *plan, float *data) {
    (void)data;
    int N = *plan;
    if (N != 1) {
        anim_fft_execute(N, (int)data, plan[1], plan[1] + N * 4, plan[2]);
    }
}

// ---------------------------------------------------------------------------
// anim_fft_plan_init (FUN_801a0e38)
// Build an FFT plan of size n: store n at plan[0], allocate the 3n-word factor/
// twiddle table (plan[1]) and the 0x20-word factor list (plan[2]), then factor n
// and fill the twiddles (anim_fft_build_twiddles_guard).
// ---------------------------------------------------------------------------
void anim_fft_plan_init(int *plan, unsigned int n) {
    *plan = (int)n;
    plan[1] = FUN_801a1498(n * 3, 4);
    plan[2] = FUN_801a1498(0x20, 4);
    anim_fft_build_twiddles_guard((int)n, plan[1], (unsigned int *)plan[2]);
}

// ---------------------------------------------------------------------------
// anim_fft_plan_free (FUN_801a0ea0)
// Free an FFT plan's two buffers (plan+4 twiddle table, plan+8 factor list) then
// zero the 0xc-byte header. No-op on a null plan.
// ---------------------------------------------------------------------------
void anim_fft_plan_free(int plan) {
    if (plan != 0) {
        if (*(int *)(plan + 4) != 0) {
            FUN_801a14dc(*(int *)(plan + 4));
        }
        if (*(int *)(plan + 8) != 0) {
            FUN_801a14dc(*(int *)(plan + 8));
        }
        memset_wrapper(plan, 0, 0xc);
    }
}

// ---------------------------------------------------------------------------
// anim_decode_next_block (FUN_801a0efc)
// Per-frame block decode dispatcher. The (ctx:reader-src) pair via __save_gpr_25
// (hi32 = decode ctx). Flushes the scratch chunk list, rebinds the bit-reader
// over the current packet (bitwriter_reset), reads the block-type bit; if it is
// a data block (bit 0) reads the codebook index (width from the codec header at
// +0x2c) - a 0xffffffff read aborts. Latches the resolved channel descriptor
// (ctx[7]) from the codebook table (ctx[0x10]+0x1c indexed by the codebook id);
// when non-null reads the present (ctx[6]) + keyframe (ctx[8]) flag bits (a -1
// keyframe read aborts). Copies the four 64-bit timestamp/window words
// (ctx[0xc..0xf]) and the block flags (ctx[0xb]) from the src header, resolves
// the per-channel row count (ctx[9]), allocates the output row array (ctx[0]),
// one row per channel, then dispatches the builder's decode slot
// (PTR_PTR_80610b10[...] + 0x10).
// ---------------------------------------------------------------------------
void anim_decode_next_block(void) {
    unsigned long long pair = (unsigned long long)__save_gpr_25();
    int *ctx = (int *)((unsigned long long)pair >> 0x20);
    unsigned int *src = (unsigned int *)pair;
    int *br = ctx + 1;
    int codebookTbl = *(int *)(ctx[0x10] + 4);
    int header = *(int *)(ctx[0x10] + 0x68);
    int descBase = *(int *)(codebookTbl + 0x1c);

    anim_ctx_free_scratch_chunks();
    bitwriter_reset(br, (int)src[0], (int)src[1]);
    unsigned int typeBit = bitreader_read_bits(br, 1);
    if (typeBit == 0) {
        unsigned int cbIdx = bitreader_read_bits(br, *(int *)(header + 0x2c));
        if (cbIdx != 0xffffffff) {
            ctx[10] = (int)cbIdx;
            int desc = descBase + cbIdx * 4;
            ctx[7] = **(int **)(desc + 0x20);
            if (ctx[7] == 0) {
                ctx[6] = 0;
                ctx[8] = 0;
            } else {
                ctx[6] = (int)bitreader_read_bits(br, 1);
                ctx[8] = (int)bitreader_read_bits(br, 1);
                if (ctx[8] == -1) {
                    __restore_gpr_25();
                    return;
                }
            }
            ctx[0xc] = (int)src[4];
            ctx[0xd] = (int)src[5];
            ctx[0xe] = (int)src[6];
            ctx[0xf] = (int)src[7];
            ctx[0xb] = (int)src[3];
            ctx[9] = *(int *)(descBase + ctx[7] * 4);
            ctx[0] = msg_arena_alloc((int)ctx, *(int *)(codebookTbl + 4) << 2);
            int ro = 0;
            for (int r = 0; r < *(int *)(codebookTbl + 4); r = r + 1) {
                int row = msg_arena_alloc((int)ctx, ctx[9] << 2);
                *(int *)(ctx[0] + ro) = row;
                ro = ro + 4;
            }
            int builder = descBase + *(int *)(*(int *)(desc + 0x20) + 0xc) * 4;
            (**(void (**)(int *, int))((int)PTR_PTR_80610b10[*(int *)(builder + 0x120)] + 0x10))
                (ctx, *(int *)(builder + 0x220));
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_weight_table_lookup (FUN_801a10b0)
// Return the byte array PTR_DAT_8042df40[index] - the index'th crossfade/window
// table (used by the decode step's LERP blend and by anim_build_weight_window).
// ---------------------------------------------------------------------------
unsigned char *anim_weight_table_lookup(int index) {
    return PTR_DAT_8042df40[index];
}

// ---------------------------------------------------------------------------
// anim_build_weight_window (FUN_801a10c4)
// Build the per-element crossfade weight window into `out`. `codes` is a run of
// packed rank-quantized boundary codes; `a`/`b` index the two window tables and
// `gate` selects whether the a/b indices are applied (when gate != 0). The
// window is: a leading zero-fill (FLOAT_80616b08) up to the first boundary, a
// ramp-up region multiplied by the ascending window table
// (anim_weight_table_lookup(a)), a ramp-down region multiplied by the descending
// window table (anim_weight_table_lookup(b), read backwards), then a trailing
// zero-fill to the code[gate] boundary. All boundaries are the >>2 / >>1
// rank-quantized decodes of the code words.
// ---------------------------------------------------------------------------
void anim_build_weight_window(int out, int table, int codes, unsigned int a, int gate,
                              unsigned int b) {
    float zero = FLOAT_80616b08;
    int aOff = (a & (unsigned int)-(gate != 0)) * 4;
    unsigned int cA = *(unsigned int *)(codes + aOff);
    int bOff = (b & (unsigned int)-(gate != 0)) * 4;
    unsigned int cG = *(unsigned int *)(codes + gate * 4);
    int endHalf = ((int)cG >> 2) + (unsigned int)((int)cG < 0 && (cG & 3) != 0);
    unsigned int cB = *(unsigned int *)(codes + bOff);
    int leadFill = endHalf - (((int)cA >> 2) + (unsigned int)((int)cA < 0 && (cA & 3) != 0));
    unsigned char *wUp = anim_weight_table_lookup(*(int *)(table + aOff));
    unsigned char *wDn = anim_weight_table_lookup(*(int *)(table + bOff));
    int rampEnd = leadFill + ((int)cA >> 1) + (unsigned int)((int)cA < 0 && (cA & 1) != 0);
    int written = 0;
    int bHalf = ((int)cB >> 1) + (unsigned int)((int)cB < 0 && (cB & 1) != 0);
    int downStart = (((int)cG >> 1) + (unsigned int)((int)cG < 0 && (cG & 1) != 0) + endHalf) -
                    (((int)cB >> 2) + (unsigned int)((int)cB < 0 && (cB & 3) != 0));
    int downEnd = downStart + bHalf;

    // leading zero-fill
    int o = 0;
    if (0 < leadFill) {
        int rem = leadFill;
        do {
            *(float *)(out + o) = zero;
            written = written + 1;
            o = o + 4;
            rem = rem - 1;
        } while (rem != 0);
    }
    // ramp-up * ascending window
    int rem = rampEnd - written;
    o = written << 2;
    int wo = 0;
    if (written < rampEnd) {
        do {
            float *pw = (float *)(wUp + wo);
            wo = wo + 4;
            *(float *)(out + o) = *(float *)(out + o) * *pw;
            o = o + 4;
            rem = rem - 1;
        } while (rem != 0);
    }
    // ramp-down * descending window (read backwards)
    int rem2 = downEnd - downStart;
    o = downStart * 4;
    int dwo = (bHalf + -1) * 4;
    if (downStart < downEnd) {
        do {
            float *pw = (float *)(wDn + dwo);
            downStart = downStart + 1;
            dwo = dwo + -4;
            *(float *)(out + o) = *(float *)(out + o) * *pw;
            o = o + 4;
            rem2 = rem2 - 1;
        } while (rem2 != 0);
    }
    // trailing zero-fill
    int rem3 = (int)cA - downStart;
    o = downStart << 2;
    if (downStart < (int)cA) {
        do {
            *(float *)(out + o) = zero;
            o = o + 4;
            rem3 = rem3 - 1;
        } while (rem3 != 0);
    }
}

// ---------------------------------------------------------------------------
// anim_scratch_pool_init (FUN_801a1230)
// Allocate the global codec scratch pool: a 0x20-aligned block of
// slotCount * 4000 index words (DAT_80612508) plus the paired
// slotCount * 0x64000 data pages (DAT_8061250c), both zeroed; reset the two pool
// cursors (DAT_80612500 / DAT_80612504). `slotCount` = param_9.
// ---------------------------------------------------------------------------
void anim_scratch_pool_init(unsigned long long a0, unsigned long long a1, unsigned long long a2,
                            unsigned long long a3, unsigned long long a4, unsigned long long a5,
                            unsigned long long a6, unsigned long long a7, int slotCount,
                            int b10, int b11, int b12, int b13, int b14, int b15, int b16) {
    (void)b10; (void)b11;
    DAT_80612508 = mem_alloc_aligned(a0, a1, a2, a3, a4, a5, a6, a7,
                                     0x20, slotCount * 4000, 1, b12, b13, b14, b15, b16);
    memset_wrapper(DAT_8061250c, 0, slotCount * 0x64000);
    memset_wrapper(DAT_80612508, 0, slotCount * 4000);
    DAT_80612500 = 0;
    DAT_80612504 = 0;
}

// ===========================================================================
// batch_523 (0x8019ff14 - 0x801a1230)  [source reconciled after IDB dual-write]
// The mixed-radix FFT compute engine (radix-4 / general-radix butterfly stages
// + the plan-walking driver), the FFT plan alloc/free pair, the per-frame block
// decode dispatcher, the crossfade weight-window builder + table lookup, and
// the global codec scratch-pool initializer.
// ===========================================================================
extern "C" {
// --- raw symbols first referenced by batch_523 ---
extern float  FLOAT_80610b1c;   // 1/sqrt2 odd-column combine scale (radix-4 tail)
extern float  FLOAT_80610b20;   // mixed-radix base-angle numerator (2*pi scale)
extern float  FLOAT_80616b08;   // weight-window zero-fill value (0.0f)
extern unsigned char *PTR_DAT_8042df40;  // base of crossfade/window table pointer array
extern int    DAT_80612508;     // scratch pool: slot index-table base ptr
extern int    DAT_8061250c;     // scratch pool: data-page arena base ptr
extern int    DAT_80612500;     // scratch pool: live-bytes cursor
extern int    DAT_80612504;     // scratch pool: high-water mark
// mem_alloc_aligned keeps the full PowerPC arg list (8 spilled dwords + the real
// {align, size, flag, ...} tail); it returns the aligned base pointer as int.
extern int    mem_alloc_aligned(unsigned long long a1, unsigned long long a2,
                                unsigned long long a3, unsigned long long a4,
                                unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8,
                                int align, int size, int flag, int p12, int p13,
                                int p14, int p15, int p16);
// per-frame decode-dispatch callees
extern void         anim_ctx_free_scratch_chunks(void);  // FUN_80193258 (defined earlier)
// batch-internal forward decls (execute dispatches to these butterfly stages)
void anim_fft_radix4_stage(int rows, int cols, int src, int dst, int tw1, int tw2, int tw3);
void anim_fft_mixed_radix_stage(int radix, int stride, int cols, int span,
                                int a5, int a6, int a7, int in, int work, int twiddle);
void anim_fft_execute(int nUnused, float w, int input, int scratch, int plan);
} // extern "C"

// ---------------------------------------------------------------------------
// anim_fft_radix4_stage (FUN_8019ff14)
// One radix-4 decimation butterfly pass. Initial 4-point butterflies combine
// mirror-bin pairs (sum/diff real, cross-diff imag). For cols>2 the middle band
// applies three twiddle rotations (tw1/tw2/tw3) and recombines conjugate-
// symmetric bins. The odd-column tail is a 1/sqrt2-scaled diagonal combine
// (FLOAT_80610b1c). (rows:cols) arrive via the r3:r4 pair (FUN_803826bc);
// src=param_3 dst=param_4. Removed unreachable blocks are the decompiler's
// FP-spill epilogue variants.
// ---------------------------------------------------------------------------
void anim_fft_radix4_stage(int rows_, int cols_, int src, int dst, int tw1, int tw2, int tw3) {
    (void)rows_; (void)cols_;
    unsigned long long pair = FUN_803826bc();
    int cols = (int)(pair >> 0x20);
    int rows = (int)pair;
    int block = rows * cols;
    int idx = 0;
    int twoCols = cols * 2;
    int q3 = block * 3;
    int q1 = block;
    int q2 = block * 2;
    int r = rows;
    if (0 < rows) {
        do {
            int oIdx = idx * 4;
            int o1 = q1 * 4;
            int o3 = q3 * 4;
            int o2 = q2 * 4;
            int dOut = idx * 0x10;
            float a = *(float *)(src + o1) + *(float *)(src + o3);
            float b = *(float *)(src + oIdx) + *(float *)(src + o2);
            int mid = (oIdx + twoCols) * 4;
            q1 = q1 + cols;
            q3 = q3 + cols;
            idx = idx + cols;
            q2 = q2 + cols;
            *(float *)(dst + dOut) = a + b;
            *(float *)(dst + (cols * 4 + oIdx) * 4 + -4) = b - a;
            *(float *)(dst + mid + -4) = *(float *)(src + oIdx) - *(float *)(src + o2);
            *(float *)(dst + mid) = *(float *)(src + o3) - *(float *)(src + o1);
            r = r - 1;
        } while (r != 0);
    }
    if (0x1ffffffff < (long long)pair) {
        if (cols != 2) {
            int base = 0;
            for (int row = 0; row < rows; row = row + 1) {
                int c4 = base * 4;
                int hiBand = c4 + twoCols;
                unsigned int npairs = (unsigned int)(cols - 1) >> 1;
                int hiOff = hiBand * 4;
                int loOff = base << 4;
                int step = 8;
                int cur = base;
                int lo = c4;
                if (0x2ffffffff < (long long)pair) {
                    do {
                        cur = cur + 2;
                        float *pB = (float *)(src + (cur + block) * 4);
                        int cIdx = cur + block + block;
                        float w1i = *(float *)(tw1 + step + -4);
                        float *pC = (float *)(src + cIdx * 4);
                        float bRe = *pB;
                        float *pD = (float *)(src + (cIdx + block) * 4);
                        float bIm = pB[-1];
                        float w1r = *(float *)(tw1 + step + -8);
                        float w2i = *(float *)(tw2 + step + -4);
                        float cRe = *pC;
                        lo = lo + 8;
                        float cIm = pC[-1];
                        float w3i = *(float *)(tw3 + step + -4);
                        float t1 = w1r * bIm + w1i * bRe;
                        float dRe = *pD;
                        float dIm = pD[-1];
                        float w2r = *(float *)(tw2 + step + -8);
                        float w3r = *(float *)(tw3 + step + -8);
                        float t2 = w2r * cIm + w2i * cRe;
                        float t3 = w2r * cRe - w2i * cIm;
                        float loRe = *(float *)(src + lo);
                        loOff = loOff + 8;
                        float t4 = w3r * dIm + w3i * dRe;
                        float loIm = ((float *)(src + lo))[-1];
                        float u1 = w1r * bRe - w1i * bIm;
                        float u2 = w3r * dRe - w3i * dIm;
                        hiOff = hiOff + -8;
                        float s1 = t1 + t4;
                        float s2 = t2 + loIm;
                        hiBand = hiBand + 2;
                        float s3 = t3 + loRe;
                        float s4 = u1 + u2;
                        u1 = u1 - u2;
                        loIm = loIm - t2;
                        c4 = c4 + -2;   /* mirrors iVar29 decrement */
                        ((float *)(dst + loOff))[-1] = s1 + s2;
                        t4 = t4 - t1;
                        loRe = loRe - t3;
                        *(float *)(dst + loOff) = s4 + s3;
                        float *pHiUp = (float *)(dst + (hiBand + twoCols) * 4);
                        float *pHiDn = (float *)(dst + (c4 + twoCols) * 4);
                        ((float *)(dst + hiOff))[-1] = loIm - u1;
                        step = step + 8;
                        *(float *)(dst + hiOff) = t4 - loRe;
                        pHiUp[-1] = u1 + loIm;
                        *pHiUp = t4 + loRe;
                        pHiDn[-1] = s2 - s1;
                        *pHiDn = s4 - s3;
                        npairs = npairs - 1;
                    } while (npairs != 0);
                }
                base = base + cols;
            }
            if ((pair & 0x100000000ULL) != 0) { FUN_80382708(); return; }
        }
        int loI = block + cols + -1;
        int hiI = block * 2 + loI;
        int col = cols;
        int hiWord = (int)(pair >> 0x20);
        if (0 < rows) {
            int rem = rows;
            do {
                float a = *(float *)(src + loI * 4);
                float b = *(float *)(src + hiI * 4);
                int colOff = col * 4 + -4;
                float *pOut = (float *)(dst + hiWord * 4);
                float d = FLOAT_80610b1c * (a - b);
                float *pOut2 = (float *)(dst + (hiWord + twoCols) * 4);
                int crossOff = (loI + block) * 4;
                loI = loI + cols;
                hiI = hiI + cols;
                float e = -FLOAT_80610b1c * (a + b);
                col = col + cols;
                pOut[-1] = d + *(float *)(src + colOff);
                pOut2[-1] = *(float *)(src + colOff) - d;
                *pOut = e - *(float *)(src + crossOff);
                *pOut2 = e + *(float *)(src + crossOff);
                hiWord = hiWord + cols * 4;
                rem = rem - 1;
            } while (rem != 0);
        }
    }
    FUN_80382708();
}

// ---------------------------------------------------------------------------
// anim_fft_mixed_radix_stage (FUN_801a0260)
// General prime/odd-radix DFT stage. Base rotation theta = 2*pi/radix
// (FUN_80390250 cos / FUN_80390914 sin); per-lane twiddle multiplies, a
// length-`radix` DFT expressed as butterfly add/sub passes with a running
// twiddle recurrence, then scatter with conjugate-symmetric mirror layout.
// (radix:stride) arrive via the r3:r4 pair (FUN_803826b8).
//   radix=param_3(hi via pair), stride=param_4, cols=param_5? -- Ghidra names
//   are kept positional: p3=radix(arg), p4=count, p5..p10 buffers/tables.
// ---------------------------------------------------------------------------
void anim_fft_mixed_radix_stage(int param_3, int param_4, int param_5, int param_6,
                                int param_7, int param_8, int param_9, int param_10,
                                int param_11, int param_12) {
    // NOTE: this stage receives (radix:count) through the r3:r4 pair carrier;
    // the leading two int args mirror the ABI slots and are unused directly.
    (void)param_5; (void)param_6;
    long long lVar33 = (long long)FUN_803826b8();
    int iVar3 = (int)((unsigned long long)lVar33 >> 0x20);
    unsigned int uVar26 = (unsigned int)lVar33;
    // positional buffers (kept as Ghidra param order for fidelity)
    int p3 = param_3, p4 = param_4, p6 = param_7, p7 = param_8, p8 = param_9,
        p9 = param_10, p10 = param_11, p5 = param_12;
    double dVar31 = (double)(FLOAT_80610b20 /
        (float)((double)CONCAT44(0x43300000, uVar26 ^ 0x80000000) - DOUBLE_80616b00));
    double dVar27 = FUN_80390250(dVar31);
    double dVar32 = (double)(float)dVar27;
    dVar27 = FUN_80390914(dVar31);
    int iVar4 = p3 * iVar3;
    int iVar2 = (int)(uVar26 + 1) >> 1;
    int iVar5 = uVar26 * iVar3;
    int iVar8 = iVar3 + -1 >> 1;
    if (iVar3 != 1) {
        int iVar6 = 0;
        int iVar24 = p4;
        if (0 < p4) {
            do { *(int *)(p8 + iVar6) = *(int *)(p6 + iVar6); iVar6 = iVar6 + 4; iVar24 = iVar24 - 1; } while (iVar24 != 0);
        }
        iVar6 = 0;
        for (iVar24 = 1; iVar24 < (int)uVar26; iVar24 = iVar24 + 1) {
            iVar6 = iVar6 + iVar4;
            int iVar14 = iVar6;
            int iVar10 = p3;
            if (0 < p3) {
                do { int iVar12 = iVar14 * 4; iVar14 = iVar14 + iVar3; *(int *)(p7 + iVar12) = *(int *)(p5 + iVar12); iVar10 = iVar10 - 1; } while (iVar10 != 0);
            }
        }
        int iVar24b = -iVar3;
        iVar6 = 0;
        if (p3 < iVar8) {
            for (int iVar14 = 1; iVar14 < (int)uVar26; iVar14 = iVar14 + 1) {
                iVar6 = iVar6 + iVar4;
                iVar24b = iVar24b + iVar3;
                int iVar10 = iVar6 - iVar3;
                for (int iVar12 = 0; iVar12 < p3; iVar12 = iVar12 + 1) {
                    iVar10 = iVar10 + iVar3;
                    unsigned int uVar25 = (unsigned int)(iVar3 - 1) >> 1;
                    int iVar13 = (iVar24b + -1) * 4;
                    int iVar20 = iVar10 * 4;
                    if (0x2ffffffff < lVar33) {
                        do {
                            iVar20 = iVar20 + 8; iVar13 = iVar13 + 8;
                            float *pfVar16 = (float *)(p10 + iVar13);
                            float *pfVar15 = (float *)(p5 + iVar20);
                            ((float *)(p7 + iVar20))[-1] = pfVar16[-1] * pfVar15[-1] + *pfVar16 * *pfVar15;
                            *(float *)(p7 + iVar20) = pfVar16[-1] * *pfVar15 - *pfVar16 * pfVar15[-1];
                            uVar25 = uVar25 - 1;
                        } while (uVar25 != 0);
                    }
                }
            }
        } else {
            for (int iVar14 = 1; iVar14 < (int)uVar26; iVar14 = iVar14 + 1) {
                iVar24b = iVar24b + iVar3;
                iVar6 = iVar6 + iVar4;
                int iVar10 = (iVar24b + -1) * 4;
                int iVar12 = iVar6;
                for (int iVar13 = 2; iVar13 < iVar3; iVar13 = iVar13 + 2) {
                    iVar12 = iVar12 + 2;
                    iVar10 = iVar10 + 8;
                    float *pfVar15 = (float *)(p10 + iVar10);
                    int iVar20 = iVar12;
                    int iVar21 = p3;
                    if (0 < p3) {
                        do {
                            float *pfVar16 = (float *)(p5 + iVar20 * 4);
                            float *pfVar17 = (float *)(p7 + iVar20 * 4);
                            iVar20 = iVar20 + iVar3;
                            pfVar17[-1] = pfVar15[-1] * pfVar16[-1] + *pfVar15 * *pfVar16;
                            *pfVar17 = pfVar15[-1] * *pfVar16 - *pfVar15 * pfVar16[-1];
                            iVar21 = iVar21 - 1;
                        } while (iVar21 != 0);
                    }
                }
            }
        }
        int iVar24c = uVar26 * iVar4;
        iVar6 = 0;
        if (iVar8 < p3) {
            for (int iVar14 = 1; iVar14 < iVar2; iVar14 = iVar14 + 1) {
                iVar6 = iVar6 + iVar4;
                iVar24c = iVar24c - iVar4;
                int iVar10 = iVar6;
                int iVar12 = iVar24c;
                for (int iVar13 = 2; iVar13 < iVar3; iVar13 = iVar13 + 2) {
                    iVar10 = iVar10 + 2;
                    iVar12 = iVar12 + 2;
                    int iVar21 = iVar10 - iVar3;
                    int iVar22 = iVar12 - iVar3;
                    int iVar20 = p3;
                    if (0 < p3) {
                        do {
                            iVar21 = iVar21 + iVar3; iVar22 = iVar22 + iVar3;
                            float *pfVar16 = (float *)(p7 + iVar21 * 4);
                            float *pfVar19 = (float *)(p5 + iVar21 * 4);
                            float *pfVar15 = (float *)(p7 + iVar22 * 4);
                            float *pfVar17 = (float *)(p5 + iVar22 * 4);
                            pfVar19[-1] = pfVar16[-1] + pfVar15[-1];
                            pfVar17[-1] = *pfVar16 - *pfVar15;
                            *pfVar19 = *pfVar16 + *pfVar15;
                            *pfVar17 = pfVar15[-1] - pfVar16[-1];
                            iVar20 = iVar20 - 1;
                        } while (iVar20 != 0);
                    }
                }
            }
        } else {
            for (int iVar14 = 1; iVar14 < iVar2; iVar14 = iVar14 + 1) {
                iVar6 = iVar6 + iVar4;
                iVar24c = iVar24c - iVar4;
                int iVar10 = iVar24c;
                int iVar12 = iVar6;
                for (int iVar13 = 0; iVar13 < p3; iVar13 = iVar13 + 1) {
                    unsigned int uVar25 = (unsigned int)(iVar3 - 1) >> 1;
                    int iVar20 = iVar10 << 2;
                    int iVar21 = iVar12 << 2;
                    if (0x2ffffffff < lVar33) {
                        do {
                            iVar21 = iVar21 + 8; iVar20 = iVar20 + 8;
                            float *pfVar16 = (float *)(p7 + iVar21);
                            float *pfVar15 = (float *)(p7 + iVar20);
                            ((float *)(p5 + iVar21))[-1] = pfVar16[-1] + pfVar15[-1];
                            ((float *)(p5 + iVar20))[-1] = *pfVar16 - *pfVar15;
                            *(float *)(p5 + iVar21) = *pfVar16 + *pfVar15;
                            *(float *)(p5 + iVar20) = pfVar15[-1] - pfVar16[-1];
                            uVar25 = uVar25 - 1;
                        } while (uVar25 != 0);
                    }
                    iVar12 = iVar12 + iVar3;
                    iVar10 = iVar10 + iVar3;
                }
            }
        }
    }
    int iVar6 = 0;
    int iVar24 = p4;
    if (0 < p4) {
        do { *(int *)(p6 + iVar6) = *(int *)(p8 + iVar6); iVar6 = iVar6 + 4; iVar24 = iVar24 - 1; } while (iVar24 != 0);
    }
    iVar6 = uVar26 * p4;
    int iVar14 = 0;
    iVar24 = iVar6;
    for (int iVar10 = 1; iVar10 < iVar2; iVar10 = iVar10 + 1) {
        iVar14 = iVar14 + iVar4;
        iVar24 = iVar24 - iVar4;
        int iVar13 = iVar14 - iVar3;
        int iVar20 = iVar24 - iVar3;
        int iVar12 = p3;
        if (0 < p3) {
            do {
                iVar13 = iVar13 + iVar3; iVar20 = iVar20 + iVar3;
                int iVar21 = iVar13 * 4; int iVar22 = iVar20 * 4;
                *(float *)(p5 + iVar21) = *(float *)(p7 + iVar21) + *(float *)(p7 + iVar22);
                *(float *)(p5 + iVar22) = *(float *)(p7 + iVar22) - *(float *)(p7 + iVar21);
                iVar12 = iVar12 - 1;
            } while (iVar12 != 0);
        }
    }
    double dVar29 = (double)FLOAT_80616afc;
    int iVar10b = (uVar26 - 1) * p4;
    double dVar31b = (double)FLOAT_80616af8;
    iVar14 = 0;
    for (iVar24 = 1; iVar24 < iVar2; iVar24 = iVar24 + 1) {
        iVar14 = iVar14 + p4;
        iVar6 = iVar6 - p4;
        double dVar28 = (double)(float)dVar27 * dVar29;
        dVar29 = (double)(float)(dVar32 * dVar29 - (double)(float)((double)(float)dVar27 * dVar31b));
        dVar31b = (double)(float)(dVar32 * dVar31b + (double)(float)dVar28);
        int iVar12 = iVar6 * 4;
        int iVar13 = iVar10b * 4;
        int iVar20 = iVar14 * 4;
        int iVar21 = p4 << 2;
        int iVar9 = 0;
        int iVar22 = p4;
        if (0 < p4) {
            do {
                float *pfVar15 = (float *)(p6 + iVar21);
                float *pfVar16 = (float *)(p6 + iVar9);
                iVar9 = iVar9 + 4; iVar21 = iVar21 + 4;
                *(float *)(p8 + iVar20) = (float)(dVar29 * (double)*pfVar15 + (double)*pfVar16);
                iVar20 = iVar20 + 4;
                pfVar15 = (float *)(p6 + iVar13);
                iVar13 = iVar13 + 4;
                *(float *)(p8 + iVar12) = (float)(dVar31b * (double)*pfVar15);
                iVar12 = iVar12 + 4;
                iVar22 = iVar22 - 1;
            } while (iVar22 != 0);
        }
        int iVar12b = p4;
        int iVar13b = iVar10b;
        double dVar28b = dVar31b;
        double dVar30 = dVar29;
        for (int iVar20b = 2; iVar20b < iVar2; iVar20b = iVar20b + 1) {
            iVar12b = iVar12b + p4;
            double dVar1 = dVar31b * dVar30;
            iVar13b = iVar13b - p4;
            dVar30 = (double)(float)(dVar29 * dVar30 - (double)(float)(dVar31b * dVar28b));
            dVar28b = (double)(float)(dVar29 * dVar28b + (double)(float)dVar1);
            int iVar21b = iVar6 * 4;
            int iVar22b = iVar13b * 4;
            int iVar9b = iVar14 * 4;
            int iVar11 = iVar12b * 4;
            int iVar18 = p4;
            if (0 < p4) {
                do {
                    float *pfVar15 = (float *)(p6 + iVar11);
                    iVar11 = iVar11 + 4;
                    *(float *)(p8 + iVar9b) = (float)(dVar30 * (double)*pfVar15 + (double)*(float *)(p8 + iVar9b));
                    iVar9b = iVar9b + 4;
                    pfVar15 = (float *)(p6 + iVar22b);
                    iVar22b = iVar22b + 4;
                    *(float *)(p8 + iVar21b) = (float)(dVar28b * (double)*pfVar15 + (double)*(float *)(p8 + iVar21b));
                    iVar21b = iVar21b + 4;
                    iVar18 = iVar18 - 1;
                } while (iVar18 != 0);
            }
        }
    }
    iVar24 = 0;
    for (iVar6 = 1; iVar6 < iVar2; iVar6 = iVar6 + 1) {
        iVar24 = iVar24 + p4;
        int iVar12 = 0;
        iVar14 = iVar24 * 4;
        int iVar10 = p4;
        if (0 < p4) {
            do {
                float *pfVar15 = (float *)(p6 + iVar14);
                iVar14 = iVar14 + 4;
                *(float *)(p8 + iVar12) = *(float *)(p8 + iVar12) + *pfVar15;
                iVar12 = iVar12 + 4;
                iVar10 = iVar10 - 1;
            } while (iVar10 != 0);
        }
    }
    if (iVar3 < p3) {
        for (iVar24 = 0; iVar24 < iVar3; iVar24 = iVar24 + 1) {
            iVar6 = iVar24;
            iVar14 = iVar24;
            int iVar10 = p3;
            if (0 < p3) {
                do {
                    int iVar12 = iVar6 * 4; int iVar13 = iVar14 * 4;
                    iVar6 = iVar6 + iVar3; iVar14 = iVar14 + iVar5;
                    *(int *)(p9 + iVar13) = *(int *)(p7 + iVar12);
                    iVar10 = iVar10 - 1;
                } while (iVar10 != 0);
            }
        }
    } else {
        iVar24 = 0;
        iVar6 = 0;
        for (iVar14 = 0; iVar14 < p3; iVar14 = iVar14 + 1) {
            int iVar10 = iVar6 << 2;
            int iVar12 = iVar24 << 2;
            int iVar13 = iVar3;
            if (0xffffffff < lVar33) {
                do {
                    int *puVar7 = (int *)(p7 + iVar12);
                    iVar12 = iVar12 + 4;
                    *(int *)(p9 + iVar10) = *puVar7;
                    iVar10 = iVar10 + 4;
                    iVar13 = iVar13 - 1;
                } while (iVar13 != 0);
            }
            iVar24 = iVar24 + iVar3;
            iVar6 = iVar6 + iVar5;
        }
    }
    int iVar13c = uVar26 * iVar4;
    iVar14 = 0;
    iVar24 = iVar3 * 2;
    int iVar12c = 0;
    iVar6 = iVar13c;
    for (int iVar10 = 1; iVar10 < iVar2; iVar10 = iVar10 + 1) {
        iVar14 = iVar14 + iVar24;
        iVar12c = iVar12c + iVar4;
        iVar6 = iVar6 - iVar4;
        int iVar20 = iVar6;
        int iVar21 = iVar12c;
        int iVar22 = iVar14;
        int iVar9 = p3;
        if (0 < p3) {
            do {
                int *puVar7 = (int *)(p9 + iVar22 * 4);
                int iVar11 = iVar20 * 4;
                iVar22 = iVar22 + iVar5;
                puVar7[-1] = *(int *)(p7 + iVar21 * 4);
                iVar21 = iVar21 + iVar3;
                iVar20 = iVar20 + iVar3;
                *puVar7 = *(int *)(p7 + iVar11);
                iVar9 = iVar9 - 1;
            } while (iVar9 != 0);
        }
    }
    if (iVar3 != 1) {
        if (iVar8 < p3) {
            int iVar10 = 0;
            int iVar8b = -iVar3;
            iVar6 = 0;
            for (iVar14 = 1; iVar14 < iVar2; iVar14 = iVar14 + 1) {
                iVar8b = iVar8b + iVar24;
                iVar10 = iVar10 + iVar24;
                iVar6 = iVar6 + iVar4;
                iVar13c = iVar13c - iVar4;
                for (int iVar12 = 2; iVar12 < iVar3; iVar12 = iVar12 + 2) {
                    int iVar22 = (iVar3 + iVar8b) - iVar12;
                    int iVar21 = iVar12 + iVar10;
                    int iVar9 = iVar12 + iVar6;
                    int iVar11 = iVar12 + iVar13c;
                    int iVar20 = p3;
                    if (0 < p3) {
                        do {
                            float *pfVar19 = (float *)(p7 + iVar9 * 4);
                            float *pfVar17 = (float *)(p7 + iVar11 * 4);
                            int iVar18 = iVar22 * 4;
                            float *pfVar16 = (float *)(p9 + iVar21 * 4);
                            iVar22 = iVar22 + iVar5;
                            float *pfVar15 = (float *)(p9 + iVar18);
                            iVar21 = iVar21 + iVar5;
                            iVar9 = iVar9 + iVar3;
                            iVar11 = iVar11 + iVar3;
                            pfVar16[-1] = pfVar19[-1] + pfVar17[-1];
                            pfVar15[-1] = pfVar19[-1] - pfVar17[-1];
                            *pfVar16 = *pfVar19 + *pfVar17;
                            *pfVar15 = *pfVar17 - *pfVar19;
                            iVar20 = iVar20 - 1;
                        } while (iVar20 != 0);
                    }
                }
            }
        } else {
            iVar14 = 0;
            int iVar8b = -iVar3;
            int iVar10 = 0;
            for (iVar6 = 1; iVar6 < iVar2; iVar6 = iVar6 + 1) {
                iVar8b = iVar8b + iVar24;
                iVar14 = iVar14 + iVar24;
                iVar10 = iVar10 + iVar4;
                iVar13c = iVar13c - iVar4;
                int iVar12 = iVar8b;
                int iVar20 = iVar14;
                int iVar21 = iVar10;
                int iVar22 = iVar13c;
                for (int iVar9 = 0; iVar9 < p3; iVar9 = iVar9 + 1) {
                    int iVar11 = 2;
                    unsigned int uVar26b = (unsigned int)(iVar3 - 1) >> 1;
                    if (0x2ffffffff < lVar33) {
                        do {
                            int iVar23 = iVar11 + iVar20;
                            float *pfVar15 = (float *)(p7 + (iVar11 + iVar21) * 4);
                            float *pfVar16 = (float *)(p7 + (iVar11 + iVar22) * 4);
                            int iVar18 = iVar3 - iVar11;
                            iVar11 = iVar11 + 2;
                            float *pfVar17 = (float *)(p9 + iVar23 * 4);
                            pfVar17[-1] = pfVar15[-1] + pfVar16[-1];
                            float *pfVar19 = (float *)(p9 + (iVar18 + iVar12) * 4);
                            pfVar19[-1] = pfVar15[-1] - pfVar16[-1];
                            *pfVar17 = *pfVar15 + *pfVar16;
                            *pfVar19 = *pfVar16 - *pfVar15;
                            uVar26b = uVar26b - 1;
                        } while (uVar26b != 0);
                    }
                    iVar12 = iVar12 + iVar5;
                    iVar20 = iVar20 + iVar5;
                    iVar21 = iVar21 + iVar3;
                    iVar22 = iVar22 + iVar3;
                }
            }
        }
    }
    FUN_80382704();
}

// ---------------------------------------------------------------------------
// anim_fft_execute (FUN_801a0bec)
// FFT compute driver. Walks the factor list (plan+4 = count, plan+8.. =
// factors) innermost->outer, ping-ponging between the input buffer and the
// scratch buffer. Per factor: radix 4 -> anim_fft_radix4_stage, radix 2 ->
// anim_fft_split_radix_stage, else -> anim_fft_mixed_radix_stage. If the data
// ended in scratch it is copied back to input. (N:...) via FUN_803826d4.
// ---------------------------------------------------------------------------
void anim_fft_execute(int nUnused, float w, int input, int scratch, int plan) {
    (void)nUnused; (void)w;
    long long lVar9 = (long long)FUN_803826d4();
    int N = (int)((unsigned long long)lVar9 >> 0x20);
    int dst = (int)lVar9;
    int count = *(int *)(plan + 4);
    int phase = 1;
    int cur = N;
    int outer = N;
    for (int i = 0; i < count; i = i + 1) {
        phase = 1 - phase;
        int radix = *(int *)(plan + (count - i) * 4 + 4);
        int step = N / cur;
        cur = cur / radix;
        outer = outer - (radix + -1) * step;
        if (radix == 4) {
            int hi = outer + step;
            if (phase == 0) {
                anim_fft_radix4_stage(step, cur, dst, input, scratch + outer * 4 + -4,
                                      scratch + hi * 4 + -4, scratch + (hi + step) * 4 + -4);
            } else {
                anim_fft_radix4_stage(step, cur, input, dst, scratch + outer * 4 + -4,
                                      scratch + hi * 4 + -4, scratch + (hi + step) * 4 + -4);
            }
        } else if (radix == 2) {
            if (phase == 0) {
                anim_fft_split_radix_stage(step, cur, dst, input, scratch + outer * 4 + -4);
            } else {
                anim_fft_split_radix_stage(step, cur, input, dst, scratch + outer * 4 + -4);
            }
        } else {
            if (step == 1) { phase = 1 - phase; }
            if (phase == 0) {
                anim_fft_mixed_radix_stage(step, radix, cur, step * cur, dst, dst, dst,
                                           input, input, scratch + outer * 4 + -4);
                phase = 1;
            } else {
                anim_fft_mixed_radix_stage(step, radix, cur, step * cur, input, input, input,
                                           dst, dst, scratch + outer * 4 + -4);
                phase = 0;
            }
        }
    }
    if ((phase != 1) && (0xffffffff < lVar9)) {
        int off = 0;
        do {
            *(int *)(dst + off) = *(int *)(input + off);
            off = off + 4;
            N = N - 1;
        } while (N != 0);
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// anim_fft_transform (FUN_801a0df8)
// Run an FFT plan over its data unless N==1. Advances the scratch pointer past
// the input (plan[1] + N*4) and calls anim_fft_execute with the factor/twiddle
// table (plan[2]). plan[0]=N, plan[1]=data base, plan[2]=factor/twiddle table.
// ---------------------------------------------------------------------------
void anim_fft_transform(int *plan, float *w) {
    int N = *plan;
    if (N != 1) {
        anim_fft_execute(N, (w != 0) ? *w : 0.0f, plan[1], plan[1] + N * 4, plan[2]);
    }
}

// ---------------------------------------------------------------------------
// anim_fft_plan_init (FUN_801a0e38)
// Build an FFT plan of size n: store n at plan[0], allocate a 3n-word
// factor/twiddle table (plan[1]) and a 0x20-word factor list (plan[2]), then
// factor n and fill the twiddles (anim_fft_build_twiddles_guard).
// ---------------------------------------------------------------------------
void anim_fft_plan_init(int *plan, int n) {
    *plan = n;
    plan[1] = FUN_801a1498((unsigned int)(n * 3), 4);
    plan[2] = FUN_801a1498(0x20, 4);
    anim_fft_build_twiddles_guard(n, plan[1], (unsigned int *)plan[2]);
}

// ---------------------------------------------------------------------------
// anim_fft_plan_free (FUN_801a0ea0)
// Free the FFT plan buffers (plan+4 twiddle table, plan+8 factor list) then
// zero the 0xc-byte header. No-op on a null plan.
// ---------------------------------------------------------------------------
void anim_fft_plan_free(int plan) {
    if (plan != 0) {
        if (*(int *)(plan + 4) != 0) { FUN_801a14dc(*(int *)(plan + 4)); }
        if (*(int *)(plan + 8) != 0) { FUN_801a14dc(*(int *)(plan + 8)); }
        memset_wrapper(plan, 0, 0xc);
    }
}

// ---------------------------------------------------------------------------
// anim_decode_next_block (FUN_801a0efc)
// Per-frame block decode dispatcher. Flushes pending scratch chunks, rebinds
// the bit reader (bitwriter_reset), reads a type bit; a data block reads a
// codebook index (width header+0x2c), resolves the channel descriptor ctx[7]
// from the codebook table, latches present (ctx[6]) / keyframe (ctx[8]) flags,
// copies 4 timestamp words (ctx[0xc..0xf]) + flags (ctx[0xb]), allocates the
// per-channel output rows (ctx[0]), then dispatches the builder decode slot
// (PTR_PTR_80610b10[type] + 0x10). A -1 codebook/keyframe read aborts.
// (ctx:src) via __save_gpr_25.
// ---------------------------------------------------------------------------
void anim_decode_next_block(void) {
    int *ctx = (int *)__save_gpr_25();
    // The r3:r4 pair carries (ctx : src-frame-header); model src as a second
    // pointer derived from the frame the decoder is currently positioned on.
    int *src = (int *)ctx[4];   // owning context frame header (state+0x10 slot)
    int *br = ctx + 1;
    int iVar10 = *(int *)(ctx[0x10] + 4);
    int iVar7 = *(int *)(ctx[0x10] + 0x68);
    int iVar9 = *(int *)(iVar10 + 0x1c);
    anim_ctx_free_scratch_chunks();
    bitwriter_reset(br, (unsigned int)src[0], (unsigned int)src[1]);
    unsigned int uVar3 = bitreader_read_bits(br, 1);
    if ((uVar3 == 0) &&
        (uVar3 = bitreader_read_bits(br, *(int *)(iVar7 + 0x2c)), uVar3 != 0xffffffff)) {
        ctx[10] = uVar3;
        iVar7 = iVar9 + uVar3 * 4;
        ctx[7] = **(int **)(iVar7 + 0x20);
        if (ctx[7] == 0) {
            ctx[6] = 0;
            ctx[8] = 0;
        } else {
            ctx[6] = bitreader_read_bits(br, 1);
            ctx[8] = bitreader_read_bits(br, 1);
            if (ctx[8] == -1) { __restore_gpr_25(); return; }
        }
        int t0 = src[4];
        ctx[0xd] = src[5];
        ctx[0xc] = t0;
        int t1 = src[6];
        ctx[0xf] = src[7];
        ctx[0xe] = t1;
        ctx[0xb] = src[3];
        ctx[9] = *(int *)(iVar9 + ctx[7] * 4);
        ctx[0] = msg_arena_alloc((int)ctx, *(int *)(iVar10 + 4) << 2);
        int rowOff = 0;
        for (int i = 0; i < *(int *)(iVar10 + 4); i = i + 1) {
            int row = msg_arena_alloc((int)ctx, ctx[9] << 2);
            *(int *)(ctx[0] + rowOff) = row;
            rowOff = rowOff + 4;
        }
        iVar9 = iVar9 + *(int *)(*(int *)(iVar7 + 0x20) + 0xc) * 4;
        (**(void (**)(int *, int))((PTR_PTR_80610b10[*(int *)(iVar9 + 0x120)]) + 0x10))
            (ctx, *(int *)(iVar9 + 0x220));
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// anim_weight_table_lookup (FUN_801a10b0)
// Return the index'th crossfade/window byte table from the pointer array
// PTR_DAT_8042df40 (used by the decode-step LERP blend + anim_build_weight_window).
// ---------------------------------------------------------------------------
unsigned char *anim_weight_table_lookup(int index) {
    return (&PTR_DAT_8042df40)[index];
}

// ---------------------------------------------------------------------------
// anim_build_weight_window (FUN_801a10c4)
// Build a per-element crossfade weight window into `out`: a leading zero-fill
// (FLOAT_80616b08) up to the first boundary, then a ramp-up multiplied by the
// ascending window (anim_weight_table_lookup(a)), then a ramp-down multiplied
// by the descending window (b, read backwards), then a trailing zero-fill up to
// the code[gate] boundary. Boundaries are the >>2 / >>1 rank-quant decodes of
// the code words. When gate != 0 the a/b window indices are applied.
// ---------------------------------------------------------------------------
void anim_build_weight_window(int out, int table, int codes, unsigned int a, int gate, unsigned int b) {
    float fill = FLOAT_80616b08;
    int iVar4 = (int)(a & (unsigned int)-(int)(gate != 0)) * 4;
    unsigned int uVar14 = *(unsigned int *)(codes + iVar4);
    int iVar6 = (int)(b & (unsigned int)-(int)(gate != 0)) * 4;
    unsigned int uVar3 = *(unsigned int *)(codes + gate * 4);
    int iVar11 = ((int)uVar3 >> 2) + (int)((int)uVar3 < 0 && (uVar3 & 3) != 0);
    unsigned int uVar9 = *(unsigned int *)(codes + iVar6);
    int iVar15 = iVar11 - (((int)uVar14 >> 2) + (int)((int)uVar14 < 0 && (uVar14 & 3) != 0));
    unsigned char *puVar8 = (&PTR_DAT_8042df40)[*(int *)(table + iVar4)];
    unsigned char *puVar7 = (&PTR_DAT_8042df40)[*(int *)(table + iVar6)];
    int iVar13 = iVar15 + ((int)uVar14 >> 1) + (int)((int)uVar14 < 0 && (uVar14 & 1) != 0);
    int iVar12 = 0;
    int iVar6b = ((int)uVar9 >> 1) + (int)((int)uVar9 < 0 && (uVar9 & 1) != 0);
    iVar11 = (((int)uVar3 >> 1) + (int)((int)uVar3 < 0 && (uVar3 & 1) != 0) + iVar11) -
             (((int)uVar9 >> 2) + (int)((int)uVar9 < 0 && (uVar9 & 3) != 0));
    int iVar10 = iVar11 + iVar6b;
    int iVar4b = 0;
    if (0 < iVar15) {
        do {
            *(float *)(out + iVar4b) = fill;
            iVar12 = iVar12 + 1;
            iVar4b = iVar4b + 4;
            iVar15 = iVar15 + -1;
        } while (iVar15 != 0);
    }
    iVar15 = iVar13 - iVar12;
    iVar4b = iVar12 << 2;
    int iVar5 = 0;
    if (iVar12 < iVar13) {
        do {
            float *pfVar1 = (float *)(puVar8 + iVar5);
            iVar5 = iVar5 + 4;
            *(float *)(out + iVar4b) = *(float *)(out + iVar4b) * *pfVar1;
            iVar4b = iVar4b + 4;
            iVar15 = iVar15 + -1;
        } while (iVar15 != 0);
    }
    iVar12 = iVar10 - iVar11;
    iVar4b = iVar11 * 4;
    iVar6b = (iVar6b + -1) * 4;
    if (iVar11 < iVar10) {
        do {
            float *pfVar1 = (float *)(puVar7 + iVar6b);
            iVar11 = iVar11 + 1;
            iVar6b = iVar6b + -4;
            *(float *)(out + iVar4b) = *(float *)(out + iVar4b) * *pfVar1;
            iVar4b = iVar4b + 4;
            iVar12 = iVar12 + -1;
        } while (iVar12 != 0);
    }
    fill = FLOAT_80616b08;
    iVar6b = uVar3 - iVar11;
    iVar4b = iVar11 << 2;
    if (iVar11 < (int)uVar3) {
        do {
            *(float *)(out + iVar4b) = fill;
            iVar4b = iVar4b + 4;
            iVar6b = iVar6b + -1;
        } while (iVar6b != 0);
    }
}

// ---------------------------------------------------------------------------
// anim_scratch_pool_init (FUN_801a1230)
// Allocate the global codec scratch pool: a 0x20-aligned slotCount*4000-byte
// index table (DAT_80612508) and slotCount*0x64000-byte data pages
// (DAT_8061250c), both zeroed; reset the live-bytes cursor DAT_80612500 and the
// high-water mark DAT_80612504. slotCount = param_9. The leading eight dwords
// mirror the PowerPC spilled-arg ABI slots for mem_alloc_aligned.
// ---------------------------------------------------------------------------
void anim_scratch_pool_init(unsigned long long param_1, unsigned long long param_2,
                            unsigned long long param_3, unsigned long long param_4,
                            unsigned long long param_5, unsigned long long param_6,
                            unsigned long long param_7, unsigned long long param_8,
                            int param_9, int param_10, int param_11, int param_12,
                            int param_13, int param_14, int param_15, int param_16) {
    (void)param_10; (void)param_11;
    DAT_80612508 = mem_alloc_aligned(param_1, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8, 0x20, param_9 * 4000, 1, param_12,
                                     param_13, param_14, param_15, param_16);
    memset_wrapper(DAT_8061250c, 0, param_9 * 0x64000);
    memset_wrapper(DAT_80612508, 0, param_9 * 4000);
    DAT_80612500 = 0;
    DAT_80612504 = 0;
}
