// batch_1080: MetroTRK ("MetroTRK for GAMECUBE v2.6") debug-monitor message layer -
// the queue reset, the top-level TRK init/term, the version-string reply, and the
// six packet-buffer serialization primitives that read/write scalars and byte runs
// into a TRK message with host-endianness byte swapping.
//
// The message buffer is a fixed 0x880-byte packet with a small header:
//   +0x08 : length  (bytes written so far / total valid bytes)
//   +0x0C : cursor   (current read or write position)
//   +0x10 : data[0x880]
// DAT_805ef9d8 is the "wire is big-endian, host matches" flag: when it is 0 the
// scalar read/write helpers byte-swap through a local staging buffer; when nonzero
// the bytes are copied straight through. The two-slot message ring reset here is the
// same DAT_805ef9b*/DAT_805ef9d4 state driven by msg_queue.cpp (batch_1079).
//
// Non-matching decomp: raw FUN_/DAT_ symbols are declared extern per-file with the
// arity/type seen at the call sites; leaf_3268 is the 0xC-agnostic block copy.

#include "common.h"

// --- two-slot message ring state (shared with msg_queue.cpp) ----------------
extern int          DAT_805ef9b4;   // queued message count
extern int          DAT_805ef9b8;   // read cursor
extern unsigned int DAT_805ef9d4;   // rolling message id
extern int          DAT_805ef9d8;   // endianness flag: 0 => byte-swap scalars
extern char        *DAT_80613008;   // deferred-command string buffer

// TRK version banner string
extern char s_MetroTRK_for_GAMECUBE_v2_6_803e1648[];

// --- lock / copy / sub-init helpers (raw, undocumented) ---------------------
extern "C" int  FUN_80393794(void);   // acquire message-queue lock
extern "C" int  FUN_8039378c(void);   // release message-queue lock
extern "C" int  FUN_8039379c(void);   // init message-queue lock object
extern "C" void leaf_3268(int dst, int src, unsigned int n); // block copy (memcpy)

extern void FUN_80393dcc(char *s);     // free/reset deferred-command buffer
extern int  FUN_80391b4c(void);        // TRK comm-layer bring-up
extern int  FUN_80391ad8(void);        // TRK dispatch table init
extern int  FUN_80392000(void);        // TRK target-services init
extern int  FUN_80391b54(void);        // TRK event init
extern int  FUN_80395c9c(void);        // TRK host connection init
extern int  FUN_80396148(void);        // TRK notify "connected"
extern void FUN_80396f44(void);        // TRK reset transport
extern void FUN_80391e04(void);        // TRK reset event state
extern void FUN_80395f70(void);        // TRK flush pending
extern void FUN_80396058(void);        // TRK term transport
extern void FUN_80395fc8(int, int, int, int, int, int, int, int, char *,
                         int, int, int, int, int, int, int); // emit a TRK reply string

// forward decls (definitions below)
unsigned int trk_queue_reset(void);

// ---------------------------------------------------------------------------
// trk_queue_reset (0x803910dc) - (re)initialise the two-slot message ring: create
// the lock, then under it clear the count/read cursor and seed the rolling id.
unsigned int trk_queue_reset(void)
{
    FUN_8039379c();
    FUN_80393794();
    DAT_805ef9b4 = 0;
    DAT_805ef9b8 = 0;
    DAT_805ef9d4 = 0x100;
    FUN_8039378c();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_send_version (0x80391134) - reply to a version request with the TRK banner
// "MetroTRK for GAMECUBE v2.6".
void trk_send_version(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    FUN_80395fc8(a1, a2, a3, a4, a5, a6, a7, a8,
                 s_MetroTRK_for_GAMECUBE_v2_6_803e1648, a10, a11, a12, a13, a14, a15, a16);
}

// ---------------------------------------------------------------------------
// trk_comm_bringup (0x8039115c) - thin wrapper that brings up the TRK comm layer.
unsigned int trk_comm_bringup(void)
{
    FUN_80391b4c();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_init (0x80391180) - top-level MetroTRK initialisation. Marks the endianness
// flag, resets the transport/event state, then runs the init chain (queue, dispatch
// table, target services, comm/event, host connection) short-circuiting on the
// first error, and finally announces the connection. Returns the first non-zero
// error code, or 0 on success.
int trk_init(void)
{
    int rc, rc2;

    DAT_805ef9d8 = 1;
    FUN_80396f44();
    FUN_80391e04();
    rc = trk_queue_reset();
    if (rc == 0) rc = FUN_80391ad8();
    if (rc == 0) rc = FUN_80392000();
    FUN_80395f70();
    if (rc == 0) rc = FUN_80391b54();
    if (rc == 0) rc = FUN_80395c9c();
    if (rc == 0) {
        rc2 = FUN_80396148();
        FUN_80393dcc(DAT_80613008);
        if (rc2 != 0) rc = rc2;
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_term (0x803912cc) - tear down the TRK transport and reset it.
unsigned int trk_term(void)
{
    FUN_80396058();
    FUN_80396f44();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_msg_read_u32_array (0x80391310) - read `count` 32-bit words from the message
// at the current cursor. On a little-endian host (flag 0) each word is staged and
// byte-reversed into the caller's buffer; otherwise it is copied straight. Sets the
// 0x302 (underflow) status if fewer than 4 bytes remain.
void trk_msg_read_u32_array(int msg, unsigned char *out, int count)
{
    unsigned int avail, take;
    int status = 0;
    unsigned char stage[8];

    for (int i = 0; status == 0 && i < count; i = i + 1) {
        unsigned char *dst = out;
        if (DAT_805ef9d8 == 0) dst = stage;
        status = 0;
        avail = *(int *)(msg + 8) - *(int *)(msg + 0xc);
        take = 4;
        if (avail < 4) { status = 0x302; take = avail; }
        leaf_3268((int)dst, msg + *(int *)(msg + 0xc) + 0x10, take);
        *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + take;
        if (DAT_805ef9d8 == 0 && status == 0) {
            out[0] = dst[3];
            out[1] = dst[2];
            out[2] = dst[1];
            out[3] = dst[0];
        }
        out = out + 4;
    }
}

// ---------------------------------------------------------------------------
// trk_msg_read_bytes (0x80391400) - read `count` single bytes from the message at
// the current cursor (no swap). Stops with 0x302 when the buffer is exhausted.
void trk_msg_read_bytes(int msg, int out, int count)
{
    int status = 0;
    for (int i = 0; status == 0 && i < count; i = i + 1) {
        int pos = *(int *)(msg + 0xc);
        status = 0;
        bool empty = (*(int *)(msg + 8) == pos);
        if (empty) status = 0x302;
        unsigned int take = (unsigned int)(!empty);
        leaf_3268(out + i, msg + pos + 0x10, take);
        *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + take;
    }
}

// ---------------------------------------------------------------------------
// trk_msg_read_u64 (0x80391498) - read one 64-bit value from the message, byte-
// reversed on a little-endian host. Returns 0x302 on underflow.
int trk_msg_read_u64(int msg, unsigned char *out)
{
    unsigned int avail, take;
    int status = 0;
    unsigned char stage[20];
    unsigned char *dst = out;
    if (DAT_805ef9d8 == 0) dst = stage;

    avail = *(int *)(msg + 8) - *(int *)(msg + 0xc);
    take = 8;
    if (avail < 8) { status = 0x302; take = avail; }
    leaf_3268((int)dst, msg + *(int *)(msg + 0xc) + 0x10, take);
    *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + take;
    if (DAT_805ef9d8 == 0 && status == 0) {
        out[0] = dst[7];
        out[1] = dst[6];
        out[2] = dst[5];
        out[3] = dst[4];
        out[4] = dst[3];
        out[5] = dst[2];
        out[6] = dst[1];
        out[7] = dst[0];
    }
    return status;
}

// ---------------------------------------------------------------------------
// trk_msg_write_u32_array (0x80391580) - append `count` 32-bit words to the message,
// byte-reversed on a little-endian host. Updates cursor and length; sets the 0x301
// (overflow) status when the 0x880-byte buffer would be exceeded.
void trk_msg_write_u32_array(int msg, unsigned int *in, int count)
{
    int status = 0;
    for (int i = 0; status == 0 && i < count; i = i + 1) {
        unsigned int word = *in;
        unsigned char be[4];       // staged big-endian bytes for the little-endian host
        unsigned char *src;
        if (DAT_805ef9d8 == 0) {
            be[0] = (unsigned char)(word >> 0x18);
            be[1] = (unsigned char)(word >> 0x10);
            be[2] = (unsigned char)(word >> 8);
            be[3] = (unsigned char)word;
            src = be;
        } else {
            src = (unsigned char *)in;
        }
        int cur = *(int *)(msg + 0xc);
        status = 0;
        unsigned int take = 4;
        if (0x880U - cur < 4) { status = 0x301; take = 0x880U - cur; }
        if (take == 1) {
            *(unsigned char *)(msg + cur + 0x10) = *src;
        } else {
            leaf_3268(msg + cur + 0x10, (int)src, take);
        }
        in = in + 1;
        *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + take;
        *(int *)(msg + 8) = *(int *)(msg + 0xc);
    }
}

// ---------------------------------------------------------------------------
// trk_msg_write_bytes (0x8039167c) - append `count` single bytes to the message
// (no swap), advancing cursor and length. Returns 0x301 on overflow.
int trk_msg_write_bytes(int msg, unsigned char *in, int count)
{
    int status = 0;
    for (int i = 0; status == 0 && i < count; i = i + 1) {
        unsigned int cur = *(unsigned int *)(msg + 0xc);
        unsigned char b = *in;
        if (cur < 0x880) {
            *(unsigned int *)(msg + 0xc) = cur + 1;
            status = 0;
            *(unsigned char *)(msg + cur + 0x10) = b;
            *(int *)(msg + 8) = *(int *)(msg + 8) + 1;
        } else {
            status = 0x301;
        }
        in = in + 1;
    }
    return status;
}

// ===========================================================================
// batch_1081: the rest of the TRK message-buffer primitives (write-u64, raw block
// read/write, seek, reset) plus the three-slot buffer pool allocator that hands out
// the 0x890-byte buffers these primitives operate on, and the comm init stub.
//
// The buffer pool lives at DAT_805ef9e0 as 3 contiguous 0x890-byte slots. Each slot:
//   +0x00 : lock object
//   +0x04 : in-use flag (1 = allocated)     [base mirror = DAT_805ef9e4]
//   +0x08 : length
//   +0x0C : cursor
//   +0x10 : data[0x880]
// so the packet header the batch_1080 serialization helpers use starts at +0x08 of
// a pool slot. The pool is guarded by the same message-queue lock pair.
// ===========================================================================

extern unsigned char DAT_805ef9e0[];      // buffer pool base (3 x 0x890-byte slots)
extern int           DAT_805ef9e4;         // slot[0].in_use mirror (base + 4)
extern char s_ERROR___No_buffer_available_803e16a0[]; // "ERROR - No buffer available"

extern "C" void memcpy_wrapper(int dst, int val, unsigned int n); // memset-style fill
extern void FUN_80391e08(int, int, int, int, int, int, int, int, char *,
                         void *, int, int, int, int, int, int); // TRK error reply

// ---------------------------------------------------------------------------
// trk_msg_write_u64 (0x803916e4) - append one 64-bit value (given as two 32-bit
// halves) to the message, byte-reversed to big-endian wire order on a little-endian
// host. Sets 0x301 on overflow of the 0x880-byte buffer.
unsigned int trk_msg_write_u64(int msg, unsigned int hi, unsigned int lo)
{
    unsigned int words[2] = { hi, lo };   // native order (flag != 0 path)
    unsigned char be[8];                  // staged big-endian bytes (flag 0 path)
    unsigned char *src;
    if (DAT_805ef9d8 == 0) {
        // Each 32-bit half is emitted most-significant byte first; hi word first.
        be[0] = (unsigned char)(hi >> 0x18);
        be[1] = (unsigned char)(hi >> 0x10);
        be[2] = (unsigned char)(hi >> 8);
        be[3] = (unsigned char)hi;
        be[4] = (unsigned char)(lo >> 0x18);
        be[5] = (unsigned char)(lo >> 0x10);
        be[6] = (unsigned char)(lo >> 8);
        be[7] = (unsigned char)lo;
        src = be;
    } else {
        src = (unsigned char *)words;
    }
    int cur = *(int *)(msg + 0xc);
    unsigned int status = 0;
    unsigned int take = 8;
    if (0x880U - cur < 8) { status = 0x301; take = 0x880U - cur; }
    if (take == 1) *(unsigned char *)(msg + cur + 0x10) = *src;
    else           leaf_3268(msg + cur + 0x10, (int)src, take);
    *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + take;
    *(int *)(msg + 8) = *(int *)(msg + 0xc);
    return status;
}

// ---------------------------------------------------------------------------
// trk_msg_read_block (0x803917e0) - read `n` raw bytes from the message at the
// cursor into `dst` (no swap). Clamps to what remains and returns 0x302 if short.
unsigned int trk_msg_read_block(int msg, int dst, unsigned int n)
{
    unsigned int status = 0;
    if (n == 0) return 0;
    unsigned int avail = *(int *)(msg + 8) - *(int *)(msg + 0xc);
    if (avail < n) { status = 0x302; n = avail; }
    leaf_3268(dst, msg + *(int *)(msg + 0xc) + 0x10, n);
    *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + n;
    return status;
}

// ---------------------------------------------------------------------------
// trk_msg_write_block (0x8039186c) - append `n` raw bytes to the message (no swap),
// advancing cursor and length. Clamps to the free space and returns 0x301 if short.
unsigned int trk_msg_write_block(int msg, unsigned char *in, unsigned int n)
{
    unsigned int status = 0;
    if (n == 0) return 0;
    int cur = *(int *)(msg + 0xc);
    if (0x880U - cur < n) { status = 0x301; n = 0x880U - cur; }
    if (n == 1) *(unsigned char *)(msg + cur + 0x10) = *in;
    else        leaf_3268(msg + cur + 0x10, (int)in, n);
    *(unsigned int *)(msg + 0xc) = *(int *)(msg + 0xc) + n;
    *(int *)(msg + 8) = *(int *)(msg + 0xc);
    return status;
}

// ---------------------------------------------------------------------------
// trk_msg_seek (0x80391910) - set the message cursor to `pos` (<= 0x880), extending
// the length if the new cursor is beyond it. Returns 0x301 if out of range.
unsigned int trk_msg_seek(int msg, unsigned int pos)
{
    unsigned int status = 0;
    if (pos < 0x881) {
        *(unsigned int *)(msg + 0xc) = pos;
        if (*(unsigned int *)(msg + 8) < pos) {
            *(unsigned int *)(msg + 8) = pos;
        }
    } else {
        status = 0x301;
    }
    return status;
}

// ---------------------------------------------------------------------------
// trk_msg_reset (0x80391940) - rewind a message (length=0, cursor=0) and, when
// `keep` is 0, also zero the 0x880-byte data region.
void trk_msg_reset(int msg, int keep)
{
    *(unsigned int *)(msg + 8) = 0;
    *(unsigned int *)(msg + 0xc) = 0;
    if (keep == 0) {
        memcpy_wrapper(msg + 0x10, 0, 0x880);
    }
}

// ---------------------------------------------------------------------------
// trk_buffer_free (0x80391980) - release pool slot `idx` (0..2) by clearing its
// in-use flag under the lock. -1 / out-of-range indices are ignored.
void trk_buffer_free(int idx)
{
    if (idx != -1 && idx >= 0 && idx < 3) {
        FUN_80393794();
        (&DAT_805ef9e4)[idx * 0x224] = 0;   // word-stride 0x224 = 0x890 bytes
        FUN_8039378c();
    }
}

// ---------------------------------------------------------------------------
// trk_buffer_get (0x803919e4) - return the base pointer of pool slot `idx` (0..2),
// or null for an out-of-range index.
unsigned char *trk_buffer_get(int idx)
{
    unsigned char *p = 0;
    if (idx >= 0 && idx < 3) {
        p = DAT_805ef9e0 + idx * 0x890;
    }
    return p;
}

// ---------------------------------------------------------------------------
// trk_buffer_alloc (0x80391a10) - find a free slot in the 3-entry pool. Under the
// lock, the first slot whose in-use flag is clear is claimed: its length/cursor are
// zeroed, the flag set, and the slot pointer + index returned through the out
// params. Returns 0 on success or 0x300 (and emits "ERROR - No buffer available")
// when the pool is exhausted.
int trk_buffer_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                     int *out_index, unsigned int **out_ptr, int a11, int a12,
                     int a13, int a14, int a15, int a16)
{
    int rc = 0x300;
    *out_ptr = 0;
    void *slot = out_ptr;   // matches the decompiler's puVar1 fallthrough (error-reply arg)
    for (int i = 0; i < 3; i = i + 1) {
        unsigned int *entry = 0;
        if (i >= 0 && i < 3) {
            entry = (unsigned int *)(DAT_805ef9e0 + i * 0x890);
        }
        FUN_80393794();
        if (entry[1] == 0) {          // +4 in-use flag
            entry[2] = 0;             // +8 length
            rc = 0;
            entry[3] = 0;             // +0xc cursor
            entry[1] = 1;             // mark allocated
            *out_ptr = entry;
            *out_index = i;
            i = 3;
        }
        FUN_8039378c();
    }
    if (rc == 0x300) {
        FUN_80391e08(a1, a2, a3, a4, a5, a6, a7, a8,
                     s_ERROR___No_buffer_available_803e16a0, slot, a11, a12, a13, a14, a15, a16);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_buffer_pool_init (0x80391ad8) - initialise the 3-slot buffer pool: create
// each slot's lock and clear its in-use flag under the lock.
unsigned int trk_buffer_pool_init(void)
{
    unsigned char *p = DAT_805ef9e0;
    int i = 0;
    do {
        FUN_8039379c();
        FUN_80393794();
        *(unsigned int *)(p + 4) = 0;
        FUN_8039378c();
        i = i + 1;
        p = p + 0x890;
    } while (i < 3);
    return 0;
}

// ---------------------------------------------------------------------------
// trk_comm_init (0x80391b4c) - TRK comm-layer init stub (no-op, returns success).
unsigned int trk_comm_init(void) { return 0; }

// ===========================================================================
// batch_1082: the TRK command-dispatch layer - event init/post, the packet reader
// that fills a pool buffer from the transport, the char-by-char diagnostic printer,
// the top-level command dispatcher (switch on the command byte at msg+0x14), and two
// leaf command handlers (SerialIO option 0x17, CPU/exception-state query 0x1a).
// Appends to the TRK message module started in batch_1080/1081.
// ===========================================================================

extern int DAT_805f1390;   // pending-event slot / handle (-1 = none)
extern int DAT_805f1398;    // event state word
extern int DAT_805f139c;    // event state word

// cross-module helpers defined in msg_queue.cpp (this file only includes common.h,
// which does not pull generated_decls.h, so declare them locally).
unsigned int msg_queue_post(int msg);
void         msg_request_init(unsigned int *node, unsigned int value);
extern "C" void memset_wrapper(int dst, int val, unsigned int n);

// TRK diagnostic / reply strings
extern char s_MetroTRK_Option___SerialIO___803e1890[]; // "MetroTRK Option - SerialIO - "
extern char s_Disable_803e18b8[];                       // "Disable"
extern char s_Enable_803e18b0[];                        // "Enable"
extern unsigned char DAT_803e0000[];                    // OSReport format/context arg

// per-command handlers and transport/OSReport-hook helpers (raw, undocumented)
extern int  FUN_803960d0(void);     // transport: bytes available / connected?
extern int  FUN_80396094(void);     // transport: read next packet chunk status
extern void FUN_80396f44(void);     // transport reset / pump
extern void FUN_80396058(void);     // transport: send the assembled reply
extern void FUN_80396480(int on);   // apply SerialIO enable/disable
extern int  FUN_80393ddc(void);     // query CPU/exception run-state code
extern int    FUN_80392fc0(void);     // save current OSReport output hook
extern char  *FUN_80392fb4(int hook); // set/restore OSReport output hook (returns fmt)
extern int  FUN_80392f50(void);     // cmd 1 handler
extern int  FUN_80392ed8(void);     // cmd 2 handler
extern int  FUN_80392e80(int, int, int, int, int, int, int, int); // cmd 3 handler
extern int  FUN_80392e20(void);     // cmd 4 handler
extern int  FUN_80392e18(void);     // cmd 5 handler
extern int  FUN_80392e28(void);     // cmd 7 handler
extern int  FUN_80392bd4(int msg);  // cmd 0x10 handler
extern int  FUN_80392998(int msg);  // cmd 0x11 handler
extern int  FUN_803926b8(int msg);  // cmd 0x12 handler
extern int  FUN_80392428(int msg);  // cmd 0x13 handler
extern int  FUN_80392378(void);     // cmd 0x18 handler
extern int  FUN_80392158(int msg);  // cmd 0x19 handler

// forward decls (definitions below / cross-refs within this batch)
int  trk_read_packet(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                     int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16);
void trk_print(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
               char *s, int a10, int a11, int a12, int a13, int a14, int a15, int a16);
unsigned int trk_cmd_serial_io(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                               int msg, int a10, int a11, int a12, int a13, int a14, int a15, int a16);
unsigned int trk_cmd_cpu_state(void);

// ---------------------------------------------------------------------------
// trk_event_init (0x80391b54) - initialise the TRK event subsystem: clear the
// pending-event handle and state words and reset the transport a few times.
unsigned int trk_event_init(void)
{
    DAT_805f1390 = 0xffffffff;
    DAT_805f1398 = 0;
    DAT_805f139c = 0;
    FUN_80396f44();
    FUN_80396f44();
    FUN_80396f44();
    FUN_80396f44();
    FUN_80396f44();
    FUN_80396f44();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_post_event (0x80391c18) - post a type-2 event message carrying `payload` to
// the message queue, clearing the pending-event handle first.
void trk_post_event(unsigned int payload)
{
    unsigned int node[2];
    unsigned int extra;
    msg_request_init(node, 2);
    DAT_805f1390 = 0xffffffff;
    extra = payload;      // stored in the node's trailing slot by the original frame
    (void)extra;
    msg_queue_post((int)node);
}

// ---------------------------------------------------------------------------
// trk_pump_build_and_post (0x80391c68) - read the next TRK packet into a pool buffer
// and, if one was successfully claimed, post a type-2 message so the pump loop picks
// it up. This is the message-producer the msg_pump_loop calls when its queue drains.
void trk_pump_build_and_post(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                             int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    unsigned int node[2];
    int idx = trk_read_packet(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    if (idx != -1) {
        trk_buffer_get(idx);
        msg_request_init(node, 2);
        DAT_805f1390 = 0xffffffff;
        msg_queue_post((int)node);
    }
}

// ---------------------------------------------------------------------------
// trk_read_packet (0x80391cc8) - read one TRK packet from the transport into a fresh
// pool buffer: allocate a buffer, read the 0x40-byte header, then (if the declared
// length exceeds the header) the body, writing both into the message buffer. Frees
// the buffer and returns -1 on any transport error, otherwise the buffer index.
int trk_read_packet(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                    int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    int idx;
    int hdr[16];             // 0x40-byte packet header; hdr[0] = total length
    unsigned char body[2184]; // 0x888 body staging (matches auStack_890 frame)
    int slot_index;
    int msg;

    idx = FUN_803960d0();
    if (idx < 1) {
        idx = -1;
    } else {
        idx = trk_buffer_alloc(0, a2, a3, a4, a5, a6, a7, a8,
                               &slot_index, (unsigned int **)&msg, a11, a12, a13, a14, a15, a16);
        FUN_80396f44();
        trk_msg_seek(msg, 0);
        if (FUN_80396094() == 0) {
            trk_msg_write_bytes(msg, (unsigned char *)hdr, 0x40);
            idx = slot_index;
            if (0 < hdr[0] - 0x40) {
                FUN_80396f44();
                if (FUN_80396094() == 0) {
                    trk_msg_write_bytes(msg, body, hdr[0]);
                } else {
                    FUN_80396f44();
                    trk_buffer_free(slot_index);
                    idx = -1;
                }
            }
        } else {
            FUN_80396f44();
            trk_buffer_free(idx);
            idx = -1;
        }
        FUN_80396f44();
    }
    return idx;
}

// ---------------------------------------------------------------------------
// trk_event_reset (0x80391e04) - reset the TRK event state (currently a no-op).
void trk_event_reset(void) { }

// ---------------------------------------------------------------------------
// trk_print (0x80391e08) - emit a NUL-terminated string one character at a time
// through OSReport, saving and restoring the OSReport output hook around each call.
void trk_print(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
               char *s, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    char one[12];
    while (true) {
        char c = *s;
        s = s + 1;
        if (c == '\0') break;
        int saved = FUN_80392fc0();
        one[1] = 0;
        one[0] = c;
        char *fmt = FUN_80392fb4(0);
        OSReport(fmt, one);
        FUN_80392fb4(saved);
    }
}

// ---------------------------------------------------------------------------
// trk_dispatch_command (0x80391e90) - top-level TRK command dispatcher. Rewinds the
// message, reads the command byte at msg+0x14, and routes to the matching handler.
// Returns 0x500 (unknown command) if none of the cases match.
unsigned int trk_dispatch_command(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                                  int msg, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    unsigned int result = 0x500;
    trk_msg_seek(msg, 0);
    unsigned int cmd = *(unsigned char *)(msg + 0x14);
    FUN_80396f44();
    switch (*(unsigned char *)(msg + 0x14)) {
    case 1:    result = FUN_80392f50(); break;
    case 2:    result = FUN_80392ed8(); break;
    case 3:    result = FUN_80392e80(0, a2, a3, a4, a5, a6, a7, a8); break;
    case 4:    result = FUN_80392e20(); break;
    case 5:    result = FUN_80392e18(); break;
    case 7:    result = FUN_80392e28(); break;
    case 0x10: result = FUN_80392bd4(msg); break;
    case 0x11: result = FUN_80392998(msg); break;
    case 0x12: result = FUN_803926b8(msg); break;
    case 0x13: result = FUN_80392428(msg); break;
    case 0x17: result = trk_cmd_serial_io(0, a2, a3, a4, a5, a6, a7, a8, msg,
                                          0, cmd, a12, a13, a14, a15, a16); break;
    case 0x18: result = FUN_80392378(); break;
    case 0x19: result = FUN_80392158(msg); break;
    case 0x1a: result = trk_cmd_cpu_state(); break;
    }
    FUN_80396f44();
    return result;
}

// ---------------------------------------------------------------------------
// trk_target_init (0x80392000) - TRK target-services init stub (returns success).
unsigned int trk_target_init(void) { return 0; }

// ---------------------------------------------------------------------------
// trk_cmd_serial_io (0x80392008) - handle the SerialIO option command (0x17). When
// the request byte at msg+0x18 is set, prints "MetroTRK Option - SerialIO - " plus
// Enable/Disable and applies it; then builds a 0x40-byte reply (length 0x40, tag
// 0x80) and sends it.
unsigned int trk_cmd_serial_io(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                               int msg, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    char enable = *(char *)(msg + 0x1c);
    if (*(char *)(msg + 0x18) == '\x01') {
        trk_print(a1, a2, a3, a4, a5, a6, a7, a8,
                  s_MetroTRK_Option___SerialIO___803e1890, (int)DAT_803e0000,
                  a11, a12, a13, a14, a15, a16);
        if (enable == '\0') {
            trk_print(0, a2, a3, a4, a5, a6, a7, a8, s_Disable_803e18b8,
                      0, a11, a12, a13, a14, a15, a16);
        } else {
            trk_print(0, a2, a3, a4, a5, a6, a7, a8, s_Enable_803e18b0,
                      0, a11, a12, a13, a14, a15, a16);
        }
        FUN_80396480(enable);
    }
    unsigned char reply[0x40];
    memset_wrapper((int)reply, 0, 0x40);
    *(int *)reply = 0x40;       // reply length
    reply[4] = 0x80;            // reply tag
    reply[8] = 0;               // status
    FUN_80396058();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_cpu_state (0x803920b0) - handle the CPU/exception-state query (0x1a): map
// the transport's run-state code to a TRK status byte (0=running, 0x1f..0x22 stop
// reasons) and send a 0x40-byte reply carrying it.
unsigned int trk_cmd_cpu_state(void)
{
    int code = FUN_80393ddc();
    unsigned char status;
    if (code == 0x704) {
        status = 0x21;
    } else if (code < 0x704) {
        if (code == 0) status = 0; else status = 1;
    } else if (code == 0x706) {
        status = 0x20;
    } else if (code < 0x706) {
        status = 0x22;
    } else {
        status = 1;
    }
    unsigned char reply[0x40];
    memset_wrapper((int)reply, 0, 0x40);
    *(int *)reply = 0x40;       // reply length
    reply[4] = 0x80;            // reply tag
    reply[8] = status;          // CPU state status byte
    FUN_80396058();
    return 0;
}
