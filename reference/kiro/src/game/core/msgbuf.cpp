// msgbuf.cpp
// Growable message/packet buffers used by the serialization layer. Two related
// objects live here:
//
//   * The large "stream" object (0x168 bytes) - owns a 0x4000-byte data buffer
//     plus two 0x400-entry index arrays (word[4], word[5]); grows the data
//     buffer / index arrays on demand. Constructor msg_stream_init, destructor
//     msg_stream_free, growth helpers msg_stream_reserve_data / _index.
//
//   * The small "packet" object (0x1c bytes) - a header pointer (word[0]) with a
//     length (word[1]) and a secondary buffer (word[2], length word[3]). Its
//     init/free clear the block and release the header buffer; msg_packet_finalize
//     computes a CRC-32/MPEG-2 (poly 0x04C11DB7, table DAT_8041f2c0, MSB-first)
//     over the header + secondary buffer and stores it big-endian at header+0x16.
//     msg_scratch_reserve grows a global scratch buffer (via spill FUN_803826f0),
//     compacting a consumed prefix first.
//
// Address range: 0x80192484 - 0x80192874
// Batch: batch_507
#include "common.h"

extern "C" {
extern void *FUN_801a13e0(unsigned int size);   // buffer alloc
extern int   FUN_801a1528(void);                 // buffer grow (returns new base)
extern void  FUN_801a14dc(int buf);              // buffer free
extern void  memset_wrapper(int dst, int val, int len);
extern void  FUN_80387c0c(int dst, int src, int len);   // memmove (compact)
extern unsigned long long FUN_803826f0(void);    // spill: fetch active scratch object
extern void  FUN_8038273c(void);                 // spill restore
extern unsigned int DAT_8041f2c0[];               // CRC-32/MPEG-2 table
} // extern "C"

// forward decl (msg_packet_free calls msg_packet_init, defined below)
unsigned int msg_packet_init(int pkt);

// ---------------------------------------------------------------------------
// msg_stream_init (FUN_80192484)
// Zero the 0x168-byte stream object, allocate the 0x4000-byte data buffer and
// the two index arrays (0x400 words / 0x400 dwords), and stash the user context
// at word[0x54]. Returns 0 on success, 0xffffffff if the object ptr is null.
// ---------------------------------------------------------------------------
unsigned int msg_stream_init(unsigned int *s, unsigned int ctx) {
    if (s == (unsigned int *)0) {
        return 0xffffffff;
    }
    memset_wrapper((int)s, 0, 0x168);
    s[1] = 0x4000;
    s[0] = (unsigned int)(unsigned long long)FUN_801a13e0(s[1]);
    s[6] = 0x400;
    s[4] = (unsigned int)(unsigned long long)FUN_801a13e0(s[6] << 2);
    s[5] = (unsigned int)(unsigned long long)FUN_801a13e0(s[6] << 3);
    s[0x54] = ctx;
    return 0;
}

// ---------------------------------------------------------------------------
// msg_stream_free (FUN_80192514)
// Release the data buffer (word[0]) and the two index arrays (word[4], word[5]),
// then clear the object. No-op on a null object. Always returns 0.
// ---------------------------------------------------------------------------
unsigned int msg_stream_free(int *s) {
    if (s != (int *)0) {
        if (*s != 0)     FUN_801a14dc(*s);
        if (s[4] != 0)   FUN_801a14dc(s[4]);
        if (s[5] != 0)   FUN_801a14dc(s[5]);
        memset_wrapper((int)s, 0, 0x168);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// msg_stream_reserve_index (FUN_801925dc)
// Ensure the index arrays can hold `count` more entries; when the used count
// (+0x1c) plus request meets capacity (+0x18), grow capacity by request+0x20 and
// reallocate both index arrays (word at +0x10 and +0x14).
// ---------------------------------------------------------------------------
void msg_stream_reserve_index(int s, int count) {
    if (*(int *)(s + 0x18) <= *(int *)(s + 0x1c) + count) {
        *(int *)(s + 0x18) = count + *(int *)(s + 0x18) + 0x20;
        *(int *)(s + 0x10) = FUN_801a1528();
        *(int *)(s + 0x14) = FUN_801a1528();
    }
}

// ---------------------------------------------------------------------------
// msg_stream_reserve_data (FUN_80192584)
// Ensure the data buffer (word[0]) can hold `count` more bytes; when used
// (word[2]) plus request meets capacity (word[1]), grow capacity by request+0x400
// and reallocate the data buffer.
// ---------------------------------------------------------------------------
void msg_stream_reserve_data(unsigned int *s, int count) {
    if ((int)s[1] <= (int)s[2] + count) {
        s[1] = count + s[1] + 0x400;
        s[0] = (unsigned int)FUN_801a1528();
    }
}

// ---------------------------------------------------------------------------
// msg_packet_finalize (FUN_8019264c)
// Compute a CRC-32/MPEG-2 over the packet header buffer (word[0], length word[1])
// followed by the secondary buffer (word[2], length word[3]) using the MSB-first
// table DAT_8041f2c0, and store it little-endian into the header at +0x16..+0x19.
// The four header CRC bytes are zeroed before hashing. No-op on a null packet.
// ---------------------------------------------------------------------------
void msg_packet_finalize(int *pkt) {
    if (pkt == (int *)0) {
        return;
    }
    *(unsigned char *)(*pkt + 0x16) = 0;
    *(unsigned char *)(*pkt + 0x17) = 0;
    *(unsigned char *)(*pkt + 0x18) = 0;
    *(unsigned char *)(*pkt + 0x19) = 0;

    unsigned int crc = 0;
    int remaining = pkt[1];
    int i = 0;
    while (remaining > 0) {
        unsigned char b = *(unsigned char *)(*pkt + i);
        i++;
        crc = (crc << 8) ^ DAT_8041f2c0[(crc >> 0x18 ^ (unsigned int)b) & 0xff];
        remaining--;
    }
    remaining = pkt[3];
    i = 0;
    while (remaining > 0) {
        unsigned char b = *(unsigned char *)(pkt[2] + i);
        i++;
        crc = (crc << 8) ^ DAT_8041f2c0[(crc >> 0x18 ^ (unsigned int)b) & 0xff];
        remaining--;
    }
    *(char *)(*pkt + 0x16) = (char)crc;
    *(char *)(*pkt + 0x17) = (char)(crc >> 8);
    *(char *)(*pkt + 0x18) = (char)(crc >> 0x10);
    *(char *)(*pkt + 0x19) = (char)(crc >> 0x18);
}

// ---------------------------------------------------------------------------
// msg_scratch_reserve (FUN_801927b0)
// Grow the active scratch buffer (fetched via the spill accessor FUN_803826f0)
// to fit `need` more bytes. First compacts any consumed prefix (word[3]) by
// memmoving the live tail down and clearing the consumed count; then, if the
// free space (word[1]-word[2]) is smaller than the request, grows capacity to
// need+used+0x1000 and (re)allocates the buffer.
// ---------------------------------------------------------------------------
void msg_scratch_reserve(int a1) {
    (void)a1;
    unsigned long long frame = FUN_803826f0();
    unsigned int *sc = (unsigned int *)(frame >> 0x20);
    int need = (int)frame;

    if (sc[3] != 0) {
        sc[2] = sc[2] - sc[3];
        if ((int)sc[2] > 0) {
            FUN_80387c0c(sc[0], sc[0] + sc[3], sc[2]);
        }
        sc[3] = 0;
    }
    if ((int)(sc[1] - sc[2]) < need) {
        unsigned int newCap = need + sc[2] + 0x1000;
        if (sc[0] == 0) {
            sc[0] = (unsigned int)(unsigned long long)FUN_801a13e0(newCap);
        } else {
            sc[0] = (unsigned int)FUN_801a1528();
        }
        sc[1] = newCap;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// msg_packet_init (FUN_80192734)
// Zero the 0x1c-byte packet object. No-op on null. Always returns 0.
// ---------------------------------------------------------------------------
unsigned int msg_packet_init(int pkt) {
    if (pkt != 0) {
        memset_wrapper(pkt, 0, 0x1c);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// msg_packet_free (FUN_80192768)
// Release the packet header buffer (word[0]) and clear the object. Returns 0.
// ---------------------------------------------------------------------------
unsigned int msg_packet_free(int *pkt) {
    if (pkt != (int *)0) {
        if (*pkt != 0) {
            FUN_801a14dc(*pkt);
        }
        msg_packet_init((int)pkt);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// msg_cursor_advance (FUN_80192874)
// Advance the byte cursor at +8 by `count`, bounded by the limit at +4. Returns
// 0xffffffff (leaving the cursor unchanged) if it would exceed the limit, else 0.
// ---------------------------------------------------------------------------
unsigned int msg_cursor_advance(int obj, int count) {
    int next = *(int *)(obj + 8) + count;
    if (*(int *)(obj + 4) < next) {
        return 0xffffffff;
    }
    *(int *)(obj + 8) = next;
    return 0;
}

// ===========================================================================
// batch_508 additions: packet framing/validation, header parse, stream chunk
// iteration, channel/context lifecycle, and the arena allocator.
// Address range 0x8019289c - 0x801931a8.
// ===========================================================================

extern "C" {
extern unsigned long long FUN_803826c0(void);   // spill: fetch active parse ctx
extern void  FUN_8038270c(void);                 // spill restore (parse ctx)
extern unsigned long long FUN_803826f0(void);    // spill: fetch active ctx (also above)
extern int   __save_gpr_28(void);
extern void  __restore_gpr_28(void);
extern int   leaf_31e8(int dst, int src, int len);           // memcpy
extern int   FUN_80387b68(int a, int b, int len);            // memcmp (0 == equal)
extern char *FUN_80387be0(int buf, char ch, int len);        // memchr
extern int   FUN_801a1498(int count, int size);              // calloc
extern float FLOAT_80616910;                                  // default channel rate

// serialization helpers defined in bitwriter.cpp
extern void  bitwriter_init(int bw);
extern void  bitwriter_free(int bw);
} // extern "C"

// header accessors defined in bitreader.cpp (used by msg_parse_header)
unsigned char      bitreader_hdr_field4(int *br);
unsigned char      bitreader_hdr_flag0(int *br);
unsigned char      bitreader_hdr_flag1(int *br);
unsigned char      bitreader_hdr_flag2(int *br);
unsigned long long bitreader_hdr_field6(int *br);
unsigned int       bitreader_hdr_field_e(int *br);
unsigned int       bitreader_hdr_field12(int *br);

// forward decls for intra-file callers
int  msg_frame_extract(void);
int  msg_stream_next_chunk(int *stream, int *out, int consume);

// ---------------------------------------------------------------------------
// msg_frame_extract (FUN_8019289c)
// Pull the next framed packet out of the receive stream (fetched via spill
// FUN_803826c0 -> {stream, outRecord}). A frame is a 0x1b-byte fixed header
// (magic checked with FUN_80387b68 against -0x7f9ef508) followed by
// header[0x1a] length bytes and the accumulated payload. Validates the stored
// CRC (msg_packet_finalize over a temp packet view); on match, hands the caller
// a {ptr,hdrLen,payloadPtr,payloadLen} record and advances the read cursor. On a
// magic/CRC miss, resynchronizes by scanning forward for the next 'O' sync byte
// (FUN_80387be0). Register frame modelled through __save_gpr_28.
// ---------------------------------------------------------------------------
int msg_frame_extract(void) {
    unsigned char tmpCrc[4];
    int localRec[4];
    int status = 0;

    unsigned long long frame = (unsigned long long)(unsigned int)__save_gpr_28();
    int *st = (int *)(frame >> 0x20);
    int *outRec = (int *)(unsigned int)frame;

    int cur = *st + st[3];
    int avail = st[2] - st[3];

    bool haveHeader = (st[5] != 0);
    if (!haveHeader) {
        if (avail < 0x1b) { __restore_gpr_28(); return 0; }
        if (FUN_80387b68(cur, -0x7f9ef508, 4) != 0) {
            // magic miss -> resync
            st[5] = 0;
            st[6] = 0;
            char *hit = FUN_80387be0(cur + 1, 'O', avail + -1);
            if (hit == (char *)0) {
                hit = (char *)(*st + st[2]);
            }
            st[3] = (int)hit - *st;
            __restore_gpr_28();
            return 0;
        }
        int total = *(unsigned char *)(cur + 0x1a) + 0x1b;
        if (avail < total) { __restore_gpr_28(); return 0; }
        for (int i = 0; i < (int)(unsigned int)*(unsigned char *)(cur + 0x1a); i++) {
            st[6] += (unsigned int)*(unsigned char *)(cur + i + 0x1b);
        }
        st[5] = total;
        haveHeader = true;
    }

    // haveHeader path
    if (avail < st[6] + st[5]) { __restore_gpr_28(); return 0; }
    leaf_31e8((int)tmpCrc, cur + 0x16, 4);
    memset_wrapper(cur + 0x16, 0, 4);
    localRec[0] = cur;
    localRec[1] = st[5];
    localRec[2] = cur + st[5];
    localRec[3] = st[6];
    msg_packet_finalize(localRec);
    if (FUN_80387b68((int)tmpCrc, cur + 0x16, 4) == 0) {
        int base = *st;
        int rd = st[3];
        if (outRec != (int *)0) {
            outRec[0] = base + rd;
            outRec[1] = st[5];
            outRec[2] = base + rd + st[5];
            outRec[3] = st[6];
        }
        st[4] = 0;
        st[3] = st[3] + st[5] + st[6];
        st[5] = 0;
        st[6] = 0;
        __restore_gpr_28();
        return 1;   // frame produced
    }
    // CRC miss: restore the bytes we zeroed, then resync on 'O'
    leaf_31e8(cur + 0x16, (int)tmpCrc, 4);
    st[5] = 0;
    st[6] = 0;
    char *hit = FUN_80387be0(cur + 1, 'O', avail + -1);
    if (hit == (char *)0) {
        hit = (char *)(*st + st[2]);
    }
    st[3] = (int)hit - *st;
    __restore_gpr_28();
    return status;
}

// ---------------------------------------------------------------------------
// msg_recv_pump (FUN_80192a90)
// Repeatedly extract frames; returns 1 as soon as a frame is produced. When the
// extractor makes no progress (returns 0 via the shared record) it stops; a
// zero-length stall the first time latches the overflow flag at +0x10 and
// returns 0xffffffff. NB: the shared record is consumed inside msg_frame_extract.
// ---------------------------------------------------------------------------
unsigned int msg_recv_pump(int ctx) {
    for (;;) {
        int r = msg_frame_extract();
        if (0 < r) {
            return 1;
        }
        if (r == 0) {
            break;
        }
        if (*(int *)(ctx + 0x10) == 0) {
            *(int *)(ctx + 0x10) = 1;
            return 0xffffffff;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// msg_parse_header (FUN_80192b04)
// Parse an incoming message header (via the bitreader_hdr_* accessors) for the
// active parse context (spill FUN_803826c0), reconcile the stream's data buffer
// and per-entry index arrays (dropping already-consumed prefixes with
// FUN_80387c0c), append the new payload run, and record per-fragment lengths +
// flags (0x100 continuation, 0x200 end, 0x400 gap marker) into the index arrays.
// Advances the expected sequence id at word[0x55].
// ---------------------------------------------------------------------------
void msg_parse_header(void) {
    unsigned long long frame = FUN_803826c0();
    unsigned int *ctx = (unsigned int *)(frame >> 0x20);
    int *hdr = (int *)(unsigned int)frame;

    int base = *hdr;
    unsigned int payloadSrc = (unsigned int)hdr[2];
    int payloadLen = hdr[3];
    int consumed = 0;

    char field4      = (char)bitreader_hdr_field4(hdr);
    unsigned char f0 = bitreader_hdr_flag0(hdr);
    unsigned char f1 = bitreader_hdr_flag1(hdr);
    unsigned char f2 = bitreader_hdr_flag2(hdr);
    unsigned long long field6 = bitreader_hdr_field6(hdr);
    unsigned int chan  = bitreader_hdr_field_e(hdr);
    unsigned int seq   = bitreader_hdr_field12(hdr);

    unsigned int dataConsumed = ctx[3];
    unsigned int fragCount = (unsigned int)*(unsigned char *)(base + 0x1a);
    unsigned int idxConsumed = ctx[9];

    if (dataConsumed != 0) {
        ctx[2] = ctx[2] - dataConsumed;
        if (ctx[2] != 0) {
            FUN_80387c0c(ctx[0], ctx[0] + dataConsumed, ctx[2]);
        }
        ctx[3] = 0;
    }
    if (idxConsumed != 0) {
        if (ctx[7] - idxConsumed != 0) {
            FUN_80387c0c(ctx[4], ctx[4] + idxConsumed * 4, (ctx[7] - idxConsumed) * 4);
            FUN_80387c0c(ctx[5], ctx[5] + idxConsumed * 8, (ctx[7] - idxConsumed) * 8);
        }
        ctx[7] = ctx[7] - idxConsumed;
        ctx[8] = ctx[8] - idxConsumed;
        ctx[9] = 0;
    }

    if (chan == ctx[0x54] && field4 == '\0') {
        msg_stream_reserve_index((int)ctx, fragCount + 1);
        if (seq != ctx[0x55]) {
            unsigned int i = ctx[8];
            int off = i << 2;
            for (; (int)i < (int)ctx[7]; i++) {
                unsigned int *e = (unsigned int *)(ctx[4] + off);
                off += 4;
                ctx[2] = ctx[2] - (*e & 0xff);
            }
            ctx[7] = ctx[8];
            if (ctx[0x55] != 0xffffffff) {
                unsigned int slot = ctx[7];
                ctx[7] = slot + 1;
                *(unsigned int *)(ctx[4] + slot * 4) = 0x400;
                ctx[8] = ctx[8] + 1;
            }
            if (f0 != 0) {
                f1 = 0;
                for (unsigned int n = fragCount; n != 0; n--) {
                    unsigned int len = (unsigned int)*(unsigned char *)(base + consumed + 0x1b);
                    payloadSrc += len;
                    payloadLen -= len;
                    consumed++;
                    if (len < 0xff) break;
                }
            }
        }
        if (payloadLen != 0) {
            msg_stream_reserve_data(ctx, payloadLen);
            leaf_31e8(ctx[0] + ctx[2], payloadSrc, payloadLen);
            ctx[2] = ctx[2] + payloadLen;
        }
        int remain = fragCount - consumed;
        unsigned int lastFull = 0xffffffff;
        if (consumed < (int)fragCount) {
            do {
                unsigned int len = (unsigned int)*(unsigned char *)(base + consumed + 0x1b);
                *(unsigned int *)(ctx[4] + ctx[7] * 4) = len;
                unsigned int *entry = (unsigned int *)(ctx[5] + ctx[7] * 8);
                entry[1] = 0xffffffff;
                entry[0] = 0xffffffff;
                if (f1 != 0) {
                    f1 = 0;
                    *(unsigned int *)(ctx[4] + ctx[7] * 4) |= 0x100;
                }
                if (len < 0xff) lastFull = ctx[7];
                consumed++;
                ctx[7] = ctx[7] + 1;
                if (len < 0xff) ctx[8] = ctx[7];
                remain--;
            } while (remain != 0);
        }
        if (lastFull != 0xffffffff) {
            *(unsigned long long *)(ctx[5] + lastFull * 8) = field6;
        }
        if (f2 != 0) {
            ctx[0x52] = 1;
            if (0 < (int)ctx[7]) {
                int p = ctx[4] + ctx[7] * 4;
                *(unsigned int *)(p + -4) = *(unsigned int *)(p + -4) | 0x200;
            }
        }
        ctx[0x55] = seq + 1;
    }
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// msg_stream_next_chunk (FUN_80192e68)
// Iterate the stream's index array from the read position (word[9]) to gather
// the next logical chunk: entries continue while their low byte is 0xff, so the
// chunk length is the running sum until a <0xff terminator. Fills the caller
// record (ptr/len/flags/seq/extra) and, when `consume` is set, advances the read
// cursor + sequence counters. Skips 0x400 gap markers. Returns 0 at end, else 1
// (0xffffffff on a consumed gap marker).
// ---------------------------------------------------------------------------
int msg_stream_next_chunk(int *st, int *out, int consume) {
    int idx = st[9];
    if (st[8] <= idx) {
        return 0;
    }
    unsigned int e = *(unsigned int *)(st[4] + idx * 4);
    if ((e & 0x400) != 0) {
        st[9] = idx + 1;
        unsigned int s = st[0x57];
        st[0x57] = s + 1;
        st[0x56] = st[0x56] + (unsigned int)(0xfffffffe < s);
        return -1;
    }
    if (out == (int *)0 && consume == 0) {
        return 1;
    }
    unsigned int low = e & 0xff;
    unsigned int endFlag = e & 0x200;
    int off = idx << 2;
    unsigned int total = low;
    while (low == 0xff) {
        off += 4;
        idx++;
        unsigned int e2 = *(unsigned int *)(st[4] + off);
        low = e2 & 0xff;
        if ((e2 & 0x200) != 0) endFlag = 0x200;
        total += low;
    }
    if (out != (int *)0) {
        out[3] = endFlag;
        out[2] = e & 0x100;
        out[0] = *st + st[3];
        out[6] = st[0x56];
        out[7] = st[0x57];
        int *pair = (int *)(st[5] + idx * 8);
        out[4] = pair[0];
        out[5] = pair[1];
        out[1] = total;
    }
    if (consume != 0) {
        st[3] = st[3] + total;
        st[9] = idx + 1;
        unsigned int s = st[0x57];
        st[0x57] = s + 1;
        st[0x56] = st[0x56] + (unsigned int)(0xfffffffe < s);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// msg_stream_consume_chunk (FUN_80192fb0)
// Thin wrapper: fetch and consume the next chunk into `out`.
// ---------------------------------------------------------------------------
void msg_stream_consume_chunk(int *st, int *out) {
    msg_stream_next_chunk(st, out, 1);
}

// ---------------------------------------------------------------------------
// msg_ctx_free (FUN_80192fd4)
// Tear down the active network context (spill FUN_803826f0). Releases the data
// buffer (word[0]), the aux buffers (word[1], word[5], word[0xc..0xe]), the array
// of per-fragment bitwriters (word[0x18], word[2] entries at 0x14 stride, each
// bitwriter_free'd) and word[0x19], then zeroes the 0x68-word block.
// ---------------------------------------------------------------------------
void msg_ctx_free(void) {
    int *c = (int *)(unsigned int)FUN_803826f0();
    if (c != (int *)0) {
        if (c[0] != 0)    FUN_801a14dc(c[0]);
        if (c[1] != 0)    FUN_801a14dc(c[1]);
        if (c[5] != 0)    FUN_801a14dc(c[5]);
        if (c[0xc] != 0)  FUN_801a14dc(c[0xc]);
        if (c[0xd] != 0)  FUN_801a14dc(c[0xd]);
        if (c[0xe] != 0)  FUN_801a14dc(c[0xe]);
        if (c[0x18] != 0) {
            if (c[2] == 0) {
                bitwriter_free(c[0x18]);
            } else {
                int off = 0;
                for (int i = 0; i < c[2]; i++) {
                    bitwriter_free(c[0x18] + off);
                    off += 0x14;
                }
            }
            FUN_801a14dc(c[0x18]);
        }
        if (c[0x19] != 0) FUN_801a14dc(c[0x19]);
        memset_wrapper((int)c, 0, 0x68);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// msg_ctx_has_pending (FUN_801930dc)
// True when the channel's arena (ctx->[0x40]->[0x68]->[0x50], current offset) is
// non-zero, i.e. there is buffered outgoing data awaiting flush.
// ---------------------------------------------------------------------------
bool msg_ctx_has_pending(int ch) {
    return *(int *)(*(int *)(*(int *)(ch + 0x40) + 0x68) + 0x50) != 0;
}

// ---------------------------------------------------------------------------
// bits_for_count (FUN_801930f4)
// Number of bits needed to index `n` values: ceil(log2(n)) computed as the bit
// width of (n-1). Returns 0 for n <= 1.
// ---------------------------------------------------------------------------
int bits_for_count(unsigned int n) {
    int bits = 0;
    if (n != 0) n = n - 1;
    for (; n != 0; n >>= 1) {
        bits++;
    }
    return bits;
}

// ---------------------------------------------------------------------------
// msg_channel_init (FUN_80193120)
// Initialize a 0x70-byte channel object: stash the owner stream at +0x40, clear
// the arena cursor/head (+0x4c/+0x44), and when the stream is live allocate a
// 0x48-byte sub-block (+0x68), init the embedded bitwriter at +4, and seed the
// default send rate (FLOAT_80616910) at sub+4. Returns 0.
// ---------------------------------------------------------------------------
unsigned int msg_channel_init(int *stream, int ch) {
    memset_wrapper(ch, 0, 0x70);
    *(int **)(ch + 0x40) = stream;
    *(int *)(ch + 0x4c) = 0;
    *(int *)(ch + 0x44) = 0;
    if (*stream != 0) {
        int sub = FUN_801a1498(1, 0x48);
        *(int *)(ch + 0x68) = sub;
        bitwriter_init(ch + 4);
        *(float *)(sub + 4) = FLOAT_80616910;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// msg_arena_alloc (FUN_801931a8)
// Bump-allocate `size` bytes (rounded up to 8) from the channel arena at +0x44,
// chaining the exhausted block onto the free/used list at +0x54 (an 8-byte
// {prevBlock, savedField} node) and allocating a fresh block when the current
// cursor (+0x48) + request exceeds capacity (+0x4c). Returns the block pointer.
// ---------------------------------------------------------------------------
int msg_arena_alloc(int arena, int size) {
    unsigned int need = (size + 7U) & 0xfffffff8;
    if (*(int *)(arena + 0x4c) < (int)(need + *(int *)(arena + 0x48))) {
        if (*(int *)(arena + 0x44) != 0) {
            int *node = (int *)FUN_801a13e0(8);
            *(int *)(arena + 0x50) = *(int *)(arena + 0x50) + *(int *)(arena + 0x48);
            node[1] = *(int *)(arena + 0x54);
            node[0] = *(int *)(arena + 0x44);
            *(int **)(arena + 0x54) = node;
        }
        *(unsigned int *)(arena + 0x4c) = need;
        *(int *)(arena + 0x44) = (int)(unsigned long long)FUN_801a13e0(*(unsigned int *)(arena + 0x4c));
        *(int *)(arena + 0x48) = 0;
    }
    int at = *(int *)(arena + 0x48);
    *(unsigned int *)(arena + 0x48) = at + need;
    return *(int *)(arena + 0x44) + at;
}
