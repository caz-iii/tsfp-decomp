// batch_1084 (part 2): the MetroTRK host-side request layer - the "MessageReply"
// opcodes 0xD0-0xD4 that forward host OS services (file open/read/write plus two
// scalar queries) over the TRK link, and the transaction core that drives each
// request/reply exchange. These run on the target and talk to the debugger host:
// build a request packet with a 0x40-byte header (opcode in byte +0x14... here the
// header's type byte at +0x0d holds 0xD0..0xD4), allocate a pool buffer, send it,
// then wait for the matching reply and pull result words out of it at fixed offsets
// (+0x18 length, +0x20 result, +0x24 count, +0x28 secondary result).
//
// Non-matching decomp: TRK message/buffer helpers resolve as cross-module decls
// (common.h does not pull generated_decls.h); msl_strlen is a named SDK function.

#include "common.h"

// --- TRK message/buffer helpers (defined in trk_message.cpp) ----------------
int          trk_buffer_alloc(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                              int *out_index, unsigned int **out_ptr, int a11, int a12,
                              int a13, int a14, int a15, int a16);
unsigned char *trk_buffer_get(int idx);
void         trk_buffer_free(int idx);
int          trk_msg_write_bytes(int msg, unsigned char *in, int count);
int          trk_msg_read_bytes(int msg, int out, int count);
unsigned int trk_msg_seek(int msg, unsigned int pos);
unsigned int trk_term(void);
int          trk_read_packet(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                             int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16);
void         trk_post_event(unsigned int payload);
void         trk_flush_bytes(int base, int count);
extern "C" void memset_wrapper(int dst, int val, unsigned int n);
extern void FUN_80396f44(void);          // transport reset / pump
extern int  msl_strlen(int s);           // string length (SDK)

// forward decl (transaction core, defined below)
int trk_host_transact(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int req_msg, int *out_reply_idx, unsigned int a11, unsigned int a12,
                      int a13, int a14, int a15, int a16);

// ---------------------------------------------------------------------------
// trk_host_write (0x80393078, opcode 0xD4) - forward a host "write scalar" request:
// build a 0xD4 request carrying a value (param_9) and a handle (*param_10), run the
// transaction, and read back two result words (*param_12 secondary, *param_10 handle)
// from the reply buffer at +0x20 / +0x28. Returns the transaction status.
int trk_host_write(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                   int value, unsigned int *io_handle, unsigned char mode, unsigned int *out_result,
                   int a13, int a14, int a15, int a16)
{
    int idx, req_idx, reply_idx;
    unsigned char hdr[0x40];

    memset_wrapper((int)hdr, 0, 0x40);
    *(int *)hdr = 0x40;
    hdr[0xd] = 0xd4;                 // opcode
    *(unsigned int *)(hdr + 0x10) = *io_handle;
    *(int *)(hdr + 0xc) = value;
    hdr[0x14] = mode;

    int rc = trk_buffer_alloc(a1, a2, a3, a4, a5, a6, a7, a8, &idx, (unsigned int **)&req_idx,
                              0x40, (int)out_result, a13, a14, a15, a16);
    if (rc == 0) rc = trk_msg_write_bytes(req_idx, hdr, 0x40);
    if (rc == 0) {
        *out_result = 0;
        *io_handle = 0xffffffff;
        rc = trk_host_transact(a1, a2, a3, a4, a5, a6, a7, a8, req_idx, &reply_idx, 3, 3, 0, a14, a15, a16);
        unsigned char *reply;
        if (rc == 0 && (reply = trk_buffer_get(reply_idx)) != 0) {
            *out_result = *(unsigned int *)(reply + 0x20);
            *io_handle  = *(unsigned int *)(reply + 0x28);
        }
        trk_buffer_free(reply_idx);
    }
    trk_buffer_free(idx);
    return rc;
}

// ---------------------------------------------------------------------------
// trk_host_query (0x80393188, opcode 0xD3) - forward a host "read scalar" request:
// build a 0xD3 request, run the transaction, and read one result word (*param_10)
// from the reply at +0x20. Returns the transaction status.
int trk_host_query(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                   int arg, unsigned int *out_result, int a11, unsigned int *scratch,
                   int a13, int a14, int a15, int a16)
{
    int idx, req_idx, reply_idx;
    unsigned char hdr[0x40];

    memset_wrapper((int)hdr, 0, 0x40);
    *(int *)hdr = 0x40;
    hdr[0xd] = 0xd3;                 // opcode
    *(int *)(hdr + 0xc) = arg;

    int rc = trk_buffer_alloc(a1, a2, a3, a4, a5, a6, a7, a8, &idx, (unsigned int **)&req_idx,
                              0x40, (int)scratch, a13, a14, a15, a16);
    if (rc == 0) rc = trk_msg_write_bytes(req_idx, hdr, 0x40);
    if (rc == 0) {
        *out_result = 0;
        rc = trk_host_transact(a1, a2, a3, a4, a5, a6, a7, a8, req_idx, &reply_idx, 3, 3, 0, a14, a15, a16);
        if (rc == 0) {
            unsigned char *reply = trk_buffer_get(reply_idx);
            *out_result = *(unsigned int *)(reply + 0x20);
        }
        trk_buffer_free(reply_idx);
    }
    trk_buffer_free(idx);
    return rc;
}

// ---------------------------------------------------------------------------
// trk_host_open (0x80393270, opcode 0xD2) - forward a host "open" request carrying a
// NUL-terminated path string: build a 0xD2 request (header length includes the path,
// +0x0c = strlen+0x41, +0x10 = strlen+1), append the string, run the transaction, and
// read the handle (*param_11 at +0x18) and result (*param_12 at +0x20) from the reply.
int trk_host_open(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                  unsigned char *path, unsigned char mode, unsigned int *out_handle,
                  unsigned int *out_result, int a13, int a14, int a15, int a16)
{
    int idx, req_idx, reply_idx;
    unsigned char *reply = 0;
    unsigned char hdr[0x40];

    memset_wrapper((int)hdr, 0, 0x40);
    *out_handle = 0;
    hdr[0xd] = 0xd2;                                  // opcode
    *(int *)(hdr + 0xc) = msl_strlen((int)path) + 0x41;
    hdr[0x14] = mode;
    *(short *)(hdr + 0x18) = (short)(msl_strlen((int)path) + 1);

    trk_buffer_alloc(a1, a2, a3, a4, a5, a6, a7, a8, &idx, (unsigned int **)&req_idx,
                     0x40, (int)out_result, a13, a14, a15, a16);
    int rc = trk_msg_write_bytes(req_idx, hdr, 0x40);
    if (rc == 0) {
        rc = trk_msg_write_bytes(req_idx, path, msl_strlen((int)path) + 1);
    }
    if (rc == 0) {
        *out_result = 0;
        rc = trk_host_transact(a1, a2, a3, a4, a5, a6, a7, a8, req_idx, &reply_idx, 7, 3, 0, a14, a15, a16);
        if (rc == 0) {
            reply = trk_buffer_get(reply_idx);
        }
        *out_result = *(unsigned int *)(reply + 0x20);
        *out_handle = *(unsigned int *)(reply + 0x18);
        trk_buffer_free(reply_idx);
    }
    trk_buffer_free(idx);
    return rc;
}

// ---------------------------------------------------------------------------
// trk_host_transact (0x8039338c) - drive one host request/reply exchange. Sends the
// prepared request, then loops (up to a retry budget) reading reply packets: events
// (command byte >= 0x80) are re-posted to the local pump and skipped; the matching
// reply (byte +0x18 == 0 with command 0x80) is accepted and its buffer index left in
// *out_reply_idx. Returns 0 on success, 0x800 if no reply arrived within the budget.
int trk_host_transact(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int req_msg, int *out_reply_idx, unsigned int a11, unsigned int a12,
                      int a13, int a14, int a15, int a16)
{
    int budget = (int)a12 + 1;
    int rc = 0;
    bool drop = true;
    *out_reply_idx = -1;
    unsigned int retries = 0;

    while (!(budget == 0 || *out_reply_idx != -1 || rc != 0)) {
        FUN_80396f44();
        rc = (int)trk_term();
        if (rc != 0) { budget = budget - 1; continue; }

        int got = trk_read_packet(0, a2, a3, a4, a5, a6, a7, a8, 0, 0, a11, a12, a13, a14, a15, a16);
        *out_reply_idx = got;
        if (got == -1) {
            if (a13 != 0) {
                retries = retries + 1;
                if (retries >= 0x4c4b3ec) { /* retry budget exhausted */ }
            }
            continue;   // keep polling
        }

        // got a packet
        drop = false;
        unsigned char *pkt = trk_buffer_get(got);
        trk_msg_seek((int)pkt, 0);
        trk_flush_bytes((int)(pkt + 0x10), *(int *)(pkt + 8));
        unsigned int cmd = (unsigned char)pkt[0x14];
        FUN_80396f44();
        if (cmd < 0x80) {
            // an asynchronous event: hand it to the local pump and keep waiting
            trk_post_event((unsigned int)*out_reply_idx);
            *out_reply_idx = -1;
            continue;
        }
        // candidate reply
        if (*out_reply_idx != -1) {
            if (*(unsigned int *)(pkt + 8) < 0x40) drop = true;
            if (!drop) {
                unsigned int status = (unsigned char)pkt[0x18];
                FUN_80396f44();
                if (cmd != 0x80 || status != 0) {
                    FUN_80396f44();
                    drop = true;
                }
            }
            if (drop) {
                trk_buffer_free(*out_reply_idx);
                *out_reply_idx = -1;
            }
        }
        budget = budget - 1;
    }

    if (*out_reply_idx == -1) rc = 0x800;
    return rc;
}

// ---------------------------------------------------------------------------
// trk_host_transfer (0x8039356c, opcodes 0xD0 write / 0xD1 read) - transfer a block
// to/from the host in <=0x800-byte chunks. For each chunk it builds a 0xD0 (write) or
// 0xD1 (read) request, sends the payload (write) or runs the transaction and reads
// the returned bytes (read), tracking the host-reported count and stopping on short
// transfers. *param_11 is updated to the total transferred; *param_12 gets the status.
int trk_host_transfer(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                      int value, int base, unsigned int *io_len, unsigned int *out_status,
                      unsigned int do_transact, int is_read, int a15, int a16)
{
    if (base == 0 || *io_len == 0) {
        return 2;
    }
    bool done = false;
    *out_status = 0;
    unsigned int done_bytes = 0;
    int rc = 0;

    while (!done && done_bytes < *io_len && rc == 0 && *out_status == 0) {
        int idx, req_idx, reply_idx;
        unsigned char hdr[0x40];
        memset_wrapper((int)hdr, 0, 0x40);

        unsigned int chunk = 0x800;
        if (*io_len - done_bytes < 0x801) chunk = *io_len - done_bytes;

        hdr[0xd] = is_read ? 0xd1 : 0xd0;                 // opcode
        *(int *)hdr = is_read ? 0x40 : (int)(chunk + 0x40);
        *(short *)(hdr + 0x18) = (short)chunk;
        *(int *)(hdr + 0x14) = value;

        trk_buffer_alloc(a1, a2, a3, a4, a5, a6, a7, a8, &idx, (unsigned int **)&req_idx,
                         0x40, (int)out_status, do_transact, is_read, a15, a16);
        rc = trk_msg_write_bytes(req_idx, hdr, 0x40);
        if (!is_read && rc == 0) {
            rc = trk_msg_write_bytes(req_idx, (unsigned char *)(base + done_bytes), chunk);
        }
        if (rc == 0) {
            if (do_transact == 0) {
                rc = (int)trk_term();
            } else {
                rc = trk_host_transact(a1, a2, a3, a4, a5, a6, a7, a8, req_idx, &reply_idx, 5, 3,
                                       (is_read && value == 0) ? 1 : 0, is_read, a15, a16);
                unsigned char *reply = trk_buffer_get(reply_idx);
                unsigned int status = *(unsigned int *)(reply + 0x20);
                unsigned int got = *(unsigned short *)(reply + 0x24);
                if (is_read && rc == 0 && got <= chunk) {
                    trk_msg_seek((int)reply, 0x40);
                    rc = trk_msg_read_bytes((int)reply, base + done_bytes, got);
                    if (rc == 0x302) rc = 0;
                }
                if (got != chunk) { done = true; chunk = got; }
                *out_status = status & 0xff;
                trk_buffer_free(reply_idx);
            }
        }
        trk_buffer_free(idx);
        done_bytes = done_bytes + chunk;
    }
    *io_len = done_bytes;
    return rc;
}
