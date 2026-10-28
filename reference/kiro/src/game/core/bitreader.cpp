// bitreader.cpp
// Bit-unpacking deserializer - the read side of the bit-packing pair whose
// write side is bitwriter.cpp. Shares the same 5-word control block and the
// same low-N-bits mask table DAT_8041f238 ({0,1,3,7,0xf,...,(1<<n)-1}):
//   [0] byteCount   - bytes fully consumed so far
//   [1] bitPos      - current bit offset within the working byte (0..7)
//   [2] bufBase     - buffer base pointer
//   [3] readPtr     - current read cursor (bufBase + byteCount)
//   [4] capacity    - buffer capacity in bytes
// peek/read gather up to 32 bits LSB-first across up to 5 straddling bytes, with
// an overrun guard that returns 0xffffffff when the request would read past the
// buffer. The header accessors read a fixed record header located at *param_1
// (a pointer stashed in word [0] of the block): a byte at +4, three flag bits in
// the byte at +5, a 6-byte field at +6, and a 4-byte field at +0xe.
//
// Address range: 0x80192148 - 0x8019243c
// Batch: batch_506
#include "common.h"

extern "C" {
extern unsigned int DAT_8041f238[];   // low-N-bits mask table (shared with bitwriter)
} // extern "C"

// ---------------------------------------------------------------------------
// bitreader_peek_bits (FUN_80192148)
// Read `count` bits at the current cursor WITHOUT consuming them, LSB-first.
// Returns 0xffffffff when the request would straddle past the buffer capacity.
// ---------------------------------------------------------------------------
unsigned int bitreader_peek_bits(int *br, int count) {
    unsigned int bitPos = (unsigned int)br[1];
    int endBit = count + (int)bitPos;
    if (br[4] <= br[0] + 4 && (br[4] << 3) < endBit + br[0] * 8) {
        return 0xffffffff;
    }
    unsigned char *p = (unsigned char *)br[3];
    unsigned int v = (unsigned int)*p >> (bitPos & 0x3f);
    if (endBit > 8) {
        v |= (unsigned int)p[1] << (8 - bitPos);
        if (endBit > 0x10) {
            v |= (unsigned int)p[2] << (0x10 - bitPos);
            if (endBit > 0x18) {
                v |= (unsigned int)p[3] << (0x18 - bitPos);
                if (endBit > 0x20 && bitPos != 0) {
                    v |= (unsigned int)p[4] << (0x20 - bitPos);
                }
            }
        }
    }
    return DAT_8041f238[count] & v;
}

// ---------------------------------------------------------------------------
// bitreader_advance (FUN_8019220c)
// Advance the cursor by `count` bits (skip), updating byteCount/readPtr/bitPos.
// ---------------------------------------------------------------------------
void bitreader_advance(int *br, int count) {
    unsigned int endBit = (unsigned int)(count + br[1]);
    int wholeBytes = ((int)endBit >> 3) + (((int)endBit < 0 && (endBit & 7) != 0) ? 1 : 0);
    br[3] += wholeBytes;
    br[0] += wholeBytes;
    br[1] = endBit & 7;
}

// ---------------------------------------------------------------------------
// bitreader_read_bits (FUN_80192240)
// Read `count` bits at the current cursor AND consume them (peek + advance).
// Returns 0xffffffff on overrun (still consumes the requested width).
// ---------------------------------------------------------------------------
unsigned int bitreader_read_bits(int *br, int count) {
    unsigned int bitPos = (unsigned int)br[1];
    unsigned int endBit = count + bitPos;
    unsigned int v;
    if (br[0] + 4 < br[4] || (int)(endBit + br[0] * 8) <= br[4] << 3) {
        unsigned char *p = (unsigned char *)br[3];
        v = (unsigned int)*p >> (bitPos & 0x3f);
        if ((int)endBit > 8) {
            v |= (unsigned int)p[1] << (8 - bitPos);
            if ((int)endBit > 0x10) {
                v |= (unsigned int)p[2] << (0x10 - bitPos);
                if ((int)endBit > 0x18) {
                    v |= (unsigned int)p[3] << (0x18 - bitPos);
                    if ((int)endBit > 0x20 && bitPos != 0) {
                        v |= (unsigned int)p[4] << (0x20 - bitPos);
                    }
                }
            }
        }
        v &= DAT_8041f238[count];
    } else {
        v = 0xffffffff;
    }
    int wholeBytes = ((int)endBit >> 3) + (((int)endBit < 0 && (endBit & 7) != 0) ? 1 : 0);
    br[3] += wholeBytes;
    br[0] += wholeBytes;
    br[1] = endBit & 7;
    return v;
}

// ---------------------------------------------------------------------------
// bitreader_byte_len (FUN_8019232c)
// Total bytes consumed so far, rounding a partial working byte up.
// ---------------------------------------------------------------------------
int bitreader_byte_len(int *br) {
    unsigned int bits = (unsigned int)(br[1] + 7);
    return br[0] + (((int)bits >> 3) + (((int)bits < 0 && (bits & 7) != 0) ? 1 : 0));
}

// ---------------------------------------------------------------------------
// Fixed record-header accessors. The header block base is stored in br[0]
// (*param_1); each getter reads a fixed field within that header.
// ---------------------------------------------------------------------------

// bitreader_hdr_field4 (FUN_80192348): header byte at +4.
unsigned char bitreader_hdr_field4(int *br) {
    return *(unsigned char *)(*br + 4);
}

// bitreader_hdr_flag0 (FUN_80192354): flags byte at +5, bit 0.
unsigned char bitreader_hdr_flag0(int *br) {
    return *(unsigned char *)(*br + 5) & 1;
}

// bitreader_hdr_flag1 (FUN_80192364): flags byte at +5, bit 1.
unsigned char bitreader_hdr_flag1(int *br) {
    return *(unsigned char *)(*br + 5) & 2;
}

// bitreader_hdr_flag2 (FUN_80192374): flags byte at +5, bit 2.
unsigned char bitreader_hdr_flag2(int *br) {
    return *(unsigned char *)(*br + 5) & 4;
}

// ---------------------------------------------------------------------------
// bitreader_hdr_field6 (FUN_80192384)
// Assemble the 6-byte little-endian field at header +6..+0xd into a u64. The
// original composes bytes +6..+0xd (with a repeated +10 byte inside the nested
// CONCAT chain, preserved here) into the 48-bit value.
// ---------------------------------------------------------------------------
unsigned long long bitreader_hdr_field6(int *br) {
    int h = *br;
    unsigned char b6  = *(unsigned char *)(h + 6);
    unsigned char b7  = *(unsigned char *)(h + 7);
    unsigned char b8  = *(unsigned char *)(h + 8);
    unsigned char b9  = *(unsigned char *)(h + 9);
    unsigned char b10 = *(unsigned char *)(h + 10);
    unsigned char b11 = *(unsigned char *)(h + 0xb);
    unsigned char b12 = *(unsigned char *)(h + 0xc);
    unsigned char b13 = *(unsigned char *)(h + 0xd);

    unsigned int lo = ((unsigned int)b7
                       | ((unsigned int)b8 << 8)
                       | ((unsigned int)b9 << 16)
                       | ((unsigned int)b10 << 24)) << 8
                      | (unsigned int)b6;
    unsigned int hi = (unsigned int)b10
                      | ((unsigned int)b11 << 8)
                      | ((unsigned int)b12 << 16)
                      | ((unsigned int)b13 << 24);
    return ((unsigned long long)hi << 32) | lo;
}

// ---------------------------------------------------------------------------
// bitreader_hdr_field_e (FUN_8019243c)
// Assemble the 4-byte little-endian field at header +0xe..+0x11 into a u32.
// ---------------------------------------------------------------------------
unsigned int bitreader_hdr_field_e(int *br) {
    int h = *br;
    return (unsigned int)*(unsigned char *)(h + 0xe)
         | ((unsigned int)*(unsigned char *)(h + 0xf) << 8)
         | ((unsigned int)*(unsigned char *)(h + 0x10) << 16)
         | ((unsigned int)*(unsigned char *)(h + 0x11) << 24);
}

// ---------------------------------------------------------------------------
// bitreader_hdr_field12 (FUN_80192460)  [batch_507]
// Assemble the 4-byte little-endian field at header +0x12..+0x15 into a u32.
// Same record header (base at *param_1) parsed by FUN_80192b04 alongside the
// batch_506 accessors.
// ---------------------------------------------------------------------------
unsigned int bitreader_hdr_field12(int *br) {
    int h = *br;
    return (unsigned int)*(unsigned char *)(h + 0x12)
         | ((unsigned int)*(unsigned char *)(h + 0x13) << 8)
         | ((unsigned int)*(unsigned char *)(h + 0x14) << 16)
         | ((unsigned int)*(unsigned char *)(h + 0x15) << 24);
}
