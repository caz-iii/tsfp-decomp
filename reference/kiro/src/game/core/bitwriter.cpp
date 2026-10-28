// bitwriter.cpp
// Little bit-packing serializer used by the save / network record encoders.
// A BitWriter is a 5-word control block over a growable byte buffer:
//   [0] byteCount   - bytes fully committed so far
//   [1] bitPos      - current bit offset within the working byte (0..7)
//   [2] bufBase     - allocated buffer base pointer
//   [3] writePtr    - current write cursor (bufBase + byteCount)
//   [4] capacity    - allocated capacity in bytes
// write_bits packs up to 32 bits LSB-first using the low-N-bits mask table
// DAT_8041f238 ({0,1,3,7,0xf,...,(1<<n)-1}), growing the buffer by 0x100 when
// the cursor is within 4 bytes of the end. The active BitWriter object for the
// reset/write globals is fetched through the spill accessor FUN_803826f0 (object
// returned in the high dword of the u64 result).
//
// Address range: 0x80191ed4 - 0x80191ff0
// Batch: batch_505
#include "common.h"

extern "C" {
extern void *FUN_801a13e0(unsigned int size);   // buffer alloc (returns base ptr)
extern int   FUN_801a1528(void);                 // buffer grow (returns new base)
extern void  FUN_801a14dc(int buf);              // buffer free
extern void  memset_wrapper(int dst, int val, int len);
extern unsigned long long FUN_803826f0(void);    // spill: fetch active BitWriter (high dword)
extern void  FUN_8038273c(void);                 // spill restore
extern unsigned int DAT_8041f238[];               // low-N-bits mask table
} // extern "C"

// forward decl (align calls write_bits, defined below)
void bitwriter_write_bits(int a1, int value, int count);

// ---------------------------------------------------------------------------
// bitwriter_init (FUN_80191ed4)
// Zero the control block, allocate a 0x100-byte working buffer, point both the
// base and cursor at it, terminate the first byte, and record the capacity.
// ---------------------------------------------------------------------------
void bitwriter_init(int bw) {
    memset_wrapper(bw, 0, 0x14);
    void *buf = FUN_801a13e0(0x100);
    *(void **)(bw + 8) = buf;    // bufBase
    *(void **)(bw + 0xc) = buf;  // writePtr
    *(unsigned char *)buf = 0;
    *(int *)(bw + 0x10) = 0x100; // capacity
}

// ---------------------------------------------------------------------------
// bitwriter_align_byte (FUN_80191f2c)
// Pad with zero bits up to the next byte boundary (writes (8 - bitPos) bits of 0
// when not already aligned).
// ---------------------------------------------------------------------------
void bitwriter_align_byte(int bw) {
    int pad = 8 - *(int *)(bw + 4);
    if (pad < 8) {
        bitwriter_write_bits(0, 0, pad);
        (void)bw;
    }
}

// ---------------------------------------------------------------------------
// bitwriter_free (FUN_80191f60)
// Release the working buffer and clear the control block.
// ---------------------------------------------------------------------------
void bitwriter_free(int bw) {
    FUN_801a14dc(*(int *)(bw + 8));
    memset_wrapper(bw, 0, 0x14);
}

// ---------------------------------------------------------------------------
// bitwriter_reset (FUN_80191fa0)
// Reset the active BitWriter (from the spill accessor): rewind the cursor to the
// buffer base and set the given capacity.
// ---------------------------------------------------------------------------
void bitwriter_reset(int a1, int a2, int capacity) {
    (void)a1; (void)a2;
    unsigned long long frame = FUN_803826f0();
    int bw = (int)(frame >> 0x20);
    int base = (int)frame;
    memset_wrapper(bw, 0, 0x14);
    *(int *)(bw + 0xc) = base;      // writePtr = base
    *(int *)(bw + 8) = base;        // bufBase  = base
    *(int *)(bw + 0x10) = capacity; // capacity
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// bitwriter_write_bits (FUN_80191ff0)
// Pack `count` bits of `value` into the active BitWriter, LSB-first. Grows the
// buffer by 0x100 when the cursor is within 4 bytes of capacity, then splatters
// the masked value across up to 5 straddling bytes, advancing byteCount/writePtr
// by the whole bytes emitted and updating bitPos.
// ---------------------------------------------------------------------------
void bitwriter_write_bits(int a1, int value, int count) {
    (void)a1; (void)value;
    unsigned long long frame = FUN_803826f0();
    int *bw = (int *)(frame >> 0x20);
    unsigned int val = (unsigned int)frame;

    if (bw[4] <= bw[0] + 4) {          // within 4 bytes of capacity -> grow
        int newBase = FUN_801a1528();
        bw[2] = newBase;
        bw[4] = bw[4] + 0x100;
        bw[3] = bw[2] + bw[0];
    }

    unsigned int masked = val & DAT_8041f238[count];
    unsigned int endBit = (unsigned int)(count + bw[1]);

    *(unsigned char *)bw[3] = (unsigned char)(*(unsigned char *)bw[3] | (masked << bw[1]));
    if ((int)endBit > 7) {
        *(char *)(bw[3] + 1) = (char)(masked >> (8 - bw[1]));
        if ((int)endBit > 0xf) {
            *(char *)(bw[3] + 2) = (char)(masked >> (0x10 - bw[1]));
            if ((int)endBit > 0x17) {
                *(char *)(bw[3] + 3) = (char)(masked >> (0x18 - bw[1]));
                if ((int)endBit > 0x1f) {
                    if (bw[1] == 0) {
                        *(unsigned char *)(bw[3] + 4) = 0;
                    } else {
                        *(char *)(bw[3] + 4) = (char)(masked >> (0x20 - bw[1]));
                    }
                }
            }
        }
    }

    int wholeBytes = ((int)endBit >> 3) + (((int)endBit < 0 && (endBit & 7) != 0) ? 1 : 0);
    bw[0] += wholeBytes;
    bw[3] += wholeBytes;
    bw[1] = endBit & 7;
    FUN_8038273c();
}
