// batch_1083: the MetroTRK debug-command leaf handlers dispatched by
// trk_dispatch_command (batch_1082). Each reads its request fields out of the TRK
// message (command byte at +0x18, params at +0x1c/+0x20/+0x24), performs the target
// operation (register or memory read/write, continue, stop, reset), and sends a
// fixed 0x40-byte reply header built on the stack: word[0] = length 0x40, byte[4] =
// tag 0x80, byte[8] = status. Target error codes (0x700-0x706, 0x302, 0x701...) are
// mapped to TRK status bytes (0x11..0x22) before the reply.
//
// Non-matching decomp: raw FUN_/DAT_ target-access helpers are declared extern
// per-file; the message/buffer helpers (trk_msg_*, trk_term) resolve as cross-module
// declarations since common.h does not pull generated_decls.h.

#include "common.h"

// --- TRK message/buffer helpers (defined in trk_message.cpp) ----------------
unsigned int trk_msg_seek(int msg, unsigned int pos);
void         trk_msg_reset(int msg, int keep);
unsigned int trk_msg_write_block(int msg, unsigned char *in, unsigned int n);
int          trk_msg_write_bytes(int msg, unsigned char *in, int count);
unsigned int trk_msg_read_block(int msg, int dst, unsigned int n);
unsigned int trk_term(void);
extern "C" void memset_wrapper(int dst, int val, unsigned int n);

// --- transport + target-access helpers (raw, undocumented) ------------------
extern int  FUN_80396058(void);     // send the assembled reply
extern void FUN_80396f44(void);     // transport reset / pump
extern int  FUN_80393e04(void);     // is the target halted?
extern int  FUN_8039643c(int a, ...); // resume / run
extern void FUN_80395ce8(void);     // reset / reboot target
extern unsigned int FUN_80394014(void);                 // register-file size
extern int  FUN_803940dc(unsigned int a, unsigned int b); // write register block
extern int  FUN_80394024(unsigned int a, unsigned int b, unsigned int c); // read register block
extern int  FUN_8039490c(unsigned int a, unsigned int b, int msg, int *ctx, int rw); // mem space 2
extern int  FUN_80394f88(unsigned int a, unsigned int b, int msg, int *ctx, int rw); // mem space 0
extern int  FUN_80394a7c(unsigned int a, unsigned int b, int msg, int *ctx, int rw); // mem space 1
extern int  FUN_803944d4(unsigned int a, unsigned int b, int msg, int *ctx, int rw); // mem space 3
extern int  FUN_803950c8(int buf, unsigned int addr, int *len, int flag, int rw);    // read/write memory
extern int  FUN_80393d08(int buf, unsigned int addr, int *len, int rw);              // read/write memory (aligned)
int os_report(...);   // OSReport-style logger (matches generated_externs.h decl)

// Build a 0x40-byte reply header at `hdr`: length 0x40, tag 0x80, status byte.
static void trk_build_reply(unsigned char *hdr, unsigned char status)
{
    memset_wrapper((int)hdr, 0, 0x40);
    *(int *)hdr = 0x40;   // +0 length
    hdr[4] = 0x80;        // +4 tag
    hdr[8] = status;      // +8 status
}

// ---------------------------------------------------------------------------
// trk_cmd_registers (0x80392158, cmd 0x19) - read or write a range of target
// registers. Validates the register group/index against the register file size,
// requires the target to be halted, and dispatches to the register read (0x11/other)
// or write (0x10) helper. Sends an error reply (0x11 range, 0x12 unsupported, 0x16
// not-halted) when a check fails.
unsigned int trk_cmd_registers(int msg)
{
    unsigned char reply[0x40];
    trk_msg_seek(msg, 0);
    unsigned int op    = *(unsigned char *)(msg + 0x18);
    unsigned int first = *(unsigned int *)(msg + 0x20);
    unsigned int last  = *(unsigned int *)(msg + 0x24);
    unsigned int group = *(unsigned char *)(msg + 0x1c);

    bool ok = false;
    if (op == 0x10) {
        group = *(unsigned char *)(msg + 0x1c);
        if (group == 0) { trk_build_reply(reply, 0x11); FUN_80396058(); return 0; }
        ok = true;
    } else if (op == 1 || op == 0) {
        group = *(unsigned char *)(msg + 0x1c);
        if (group == 0) { trk_build_reply(reply, 0x11); FUN_80396058(); return 0; }
        ok = true;
    } else if (op > 0x11) {
        trk_build_reply(reply, 0x12); FUN_80396058(); return 0;
    } else {
        unsigned int nregs = FUN_80394014();
        if (nregs < first || last < nregs) {
            trk_build_reply(reply, 0x11); FUN_80396058(); return 0;
        }
        ok = true;
    }
    (void)ok;

    if (FUN_80393e04() == 0) {   // not halted
        trk_build_reply(reply, 0x16); FUN_80396058(); return 0;
    }
    trk_build_reply(reply, 0); FUN_80396058();

    if (op == 0x10 || op == 0) {
        return (unsigned int)FUN_803940dc(group, 0);   // write register block
    }
    return (unsigned int)FUN_80394024(first, last, 0);  // read register block
}

// ---------------------------------------------------------------------------
// trk_cmd_continue (0x80392378, cmd 0x18) - resume execution. Requires the target
// to be halted (0x16 error otherwise); on success sends the OK reply and calls the
// run helper.
unsigned int trk_cmd_continue(void)
{
    unsigned char reply[0x40];
    FUN_80396f44();
    if (FUN_80393e04() == 0) {
        trk_build_reply(reply, 0x16);
        FUN_80396058();
        return 0;
    }
    trk_build_reply(reply, 0);
    int r = FUN_80396058();
    return (unsigned int)FUN_8039643c(r, 0, 0, 0, 0, 0, 0, 0);
}

// Map a target memory/register error code to a TRK reply status byte.
static unsigned char trk_map_mem_status(int code)
{
    switch (code) {
    case 0x700: return 0x13;
    case 0x702: return 0x15;
    case 0x704: return 0x21;
    case 0x705: return 0x22;
    case 0x706: return 0x20;
    default:    return 3;
    }
}

// ---------------------------------------------------------------------------
// trk_cmd_write_memory_ex (0x80392428, cmd 0x13) - write to a target address space.
// Validates the length, dispatches to the per-space writer (space byte at +0x18:
// 0/1/2/3), then either terminates the connection on success or replies with a
// mapped status. Space > 3 yields 0x703 (unsupported).
unsigned int trk_cmd_write_memory_ex(int msg)
{
    unsigned char reply[0x40];
    int ctx[2];
    unsigned char space = *(unsigned char *)(msg + 0x18);
    unsigned int  want  = *(unsigned short *)(msg + 0x1c);
    unsigned int  avail = *(unsigned short *)(msg + 0x20);

    trk_msg_seek(msg, 0);
    if (avail < want) {
        trk_build_reply(reply, 0x14); FUN_80396058(); return 0;
    }
    trk_msg_seek(msg, 0x40);
    unsigned int rc;
    if (space == 2)       rc = FUN_8039490c(want, avail, msg, ctx, 0);
    else if (space < 2)   rc = (space == 0) ? FUN_80394f88(want, avail, msg, ctx, 0)
                                            : FUN_80394a7c(want, avail, msg, ctx, 0);
    else if (space < 4)   rc = FUN_803944d4(want, avail, msg, ctx, 0);
    else                  rc = 0x703;

    trk_msg_reset(msg, 0);
    if (rc == 0) {
        unsigned char okhdr[0x40];
        memset_wrapper((int)okhdr, 0, 0x40);
        *(int *)okhdr = 0x40; okhdr[4] = 0x80; okhdr[8] = 0;
        rc = trk_msg_write_block(msg, okhdr, 0x40);
    }
    if (rc == 0) {
        FUN_80396f44();
        unsigned int t = trk_term();
        FUN_80396f44();
        return t;
    }

    unsigned char status;
    if (rc == 0x703)                 status = 0x12;
    else if ((int)rc < 0x703) {
        if (rc == 0x701)             status = 0x14;
        else if ((int)rc > 0x700)    status = 0x15;
        else if (rc == 0x302)        status = 2;
        else                         status = 3;
    } else if (rc == 0x706)          status = 0x20;
    else if ((int)rc < 0x706)        status = ((int)rc < 0x705) ? 0x21 : 0x22;
    else                             status = 3;

    trk_build_reply(reply, status);
    FUN_80396058();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_read_registers_ex (0x803926b8, cmd 0x12) - read the full target context by
// gathering several register groups (0x24, 0x21, 0x60, 0x1f) into the reply, then
// terminating the connection on success or replying with a mapped status.
unsigned int trk_cmd_read_registers_ex(int msg)
{
    unsigned char reply[0x40];
    int ctx[2];

    if (*(unsigned short *)(msg + 0x20) < *(unsigned short *)(msg + 0x1c)) {
        trk_build_reply(reply, 0x14); FUN_80396058(); return 0;
    }
    unsigned char hdr[0x40];
    *(int *)hdr = 0x468;   // context reply length
    hdr[4] = 0x80;
    trk_msg_reset(msg, 0);
    FUN_80396f44();
    trk_msg_write_bytes(msg, hdr, 0x40);
    FUN_80396f44();

    unsigned int rc = FUN_80394f88(0, 0x24, msg, ctx, 1);
    FUN_80396f44(); FUN_80396f44();
    if (rc == 0) rc = FUN_80394a7c(0, 0x21, msg, ctx, 1);
    FUN_80396f44(); FUN_80396f44();
    if (rc == 0) rc = FUN_8039490c(0, 0x60, msg, ctx, 1);
    FUN_80396f44(); FUN_80396f44();
    if (rc == 0) rc = FUN_803944d4(0, 0x1f, msg, ctx, 1);
    FUN_80396f44(); FUN_80396f44();

    if (rc == 0) {
        FUN_80396f44();
        unsigned int t = trk_term();
        FUN_80396f44();
        return t;
    }

    unsigned char status;
    if (rc == 0x704)              status = 0x21;
    else if ((int)rc < 0x704) {
        if (rc == 0x702)          status = 0x15;
        else if ((int)rc > 0x701) status = 0x12;
        else if ((int)rc > 0x700) status = 0x14;
        else                      status = 3;
    } else if (rc == 0x706)       status = 0x20;
    else if ((int)rc < 0x706)     status = 0x22;
    else                          status = 3;

    trk_build_reply(reply, status);
    FUN_80396058();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_write_memory (0x80392998, cmd 0x11) - write a data block from the message
// into target memory. Reads the payload out of the message (offset by the low 5 bits
// of the address for the unaligned/0x40 path), performs the write, and either
// terminates on success or replies with a mapped status. Flag bit 1 => 0x12 error.
unsigned int trk_cmd_write_memory(int msg)
{
    unsigned char reply[0x40];
    unsigned int  ctx[2];
    unsigned char data[2092];

    unsigned int addr  = *(unsigned int *)(msg + 0x20);
    unsigned short len = *(unsigned short *)(msg + 0x1c);
    unsigned char flags = *(unsigned char *)(msg + 0x18);
    FUN_80396f44();

    if ((flags & 2) != 0) {
        trk_build_reply(reply, 0x12); FUN_80396058(); return 0;
    }
    ctx[0] = len;
    trk_msg_seek(msg, 0x40);
    int rc;
    if ((flags & 0x40) == 0) {
        trk_msg_read_block(msg, (int)data, ctx[0]);
        rc = FUN_803950c8((int)data, addr, (int *)ctx, ((flags >> 3) & 1) ^ 1, 0);
    } else {
        trk_msg_read_block(msg, (int)(data + (addr & 0x1f)), ctx[0]);
        rc = FUN_80393d08((int)data, addr, (int *)ctx, 0);
    }
    trk_msg_reset(msg, 0);
    if (rc == 0) {
        unsigned char okhdr[0x40];
        memset_wrapper((int)okhdr, 0, 0x40);
        *(int *)okhdr = 0x40; okhdr[4] = 0x80; okhdr[8] = 0;
        rc = trk_msg_write_block(msg, okhdr, 0x40);
    }
    if (rc == 0) {
        FUN_80396f44();
        unsigned int t = trk_term();
        FUN_80396f44();
        return t;
    }
    trk_build_reply(reply, trk_map_mem_status(rc));
    FUN_80396058();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_read_memory (0x80392bd4, cmd 0x10) - read a block of target memory into the
// reply. Performs the read, writes a header (length = data length + 0x40) followed by
// the data (offset by the low 5 bits of the address on the 0x40 path), and either
// terminates on success or replies with a mapped status. Flag bit 1 => 0x12 error.
unsigned int trk_cmd_read_memory(int msg)
{
    unsigned char reply[0x40];
    unsigned int  ctx[2];
    unsigned char data[2088];

    unsigned int addr  = *(unsigned int *)(msg + 0x20);
    unsigned short len = *(unsigned short *)(msg + 0x1c);
    unsigned char flags = *(unsigned char *)(msg + 0x18);
    FUN_80396f44();

    if ((flags & 2) != 0) {
        trk_build_reply(reply, 0x12); FUN_80396058(); return 0;
    }
    ctx[0] = len;
    int rc;
    if ((flags & 0x40) == 0) {
        rc = FUN_803950c8((int)data, addr, (int *)ctx, ((flags >> 3) & 1) ^ 1, 1);
    } else {
        rc = FUN_80393d08((int)data, addr, (int *)ctx, 1);
    }
    trk_msg_reset(msg, 0);
    if (rc == 0) {
        unsigned char hdr[0x40];
        memset_wrapper((int)hdr, 0, 0x40);
        hdr[8] = 0;
        *(int *)hdr = (int)(ctx[0] + 0x40);   // total reply length
        hdr[4] = 0x80;
        trk_msg_write_block(msg, hdr, 0x40);
        if ((flags & 0x40) == 0) {
            rc = trk_msg_write_block(msg, data, ctx[0]);
        } else {
            rc = trk_msg_write_block(msg, data + (addr & 0x1f), ctx[0]);
        }
    }
    if (rc == 0) {
        FUN_80396f44();
        unsigned int t = trk_term();
        FUN_80396f44();
        return t;
    }
    trk_build_reply(reply, trk_map_mem_status(rc));
    FUN_80396058();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_nop_5 (0x80392e18, cmd 5) - unhandled/no-op command (returns 0).
unsigned int trk_cmd_nop_5(void) { return 0; }

// ---------------------------------------------------------------------------
// trk_cmd_nop_4 (0x80392e20, cmd 4) - unhandled/no-op command (returns 0).
unsigned int trk_cmd_nop_4(void) { return 0; }

// ---------------------------------------------------------------------------
// trk_cmd_reset (0x80392e28, cmd 7) - acknowledge then reset/reboot the target.
unsigned int trk_cmd_reset(void)
{
    unsigned char reply[0x40];
    trk_build_reply(reply, 0);
    FUN_80396058();
    FUN_80395ce8();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_support_mask (0x80392e80, cmd 3) - acknowledge, then emit the support
// mask / version banner via os_report.
unsigned int trk_cmd_support_mask(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    unsigned char reply[0x40];
    trk_build_reply(reply, 0);
    FUN_80396058();
    os_report(a1, a2, a3, a4, a5, a6, a7, a8);
    return 0;
}

// ===========================================================================
// batch_1084 (part 1): two more command handlers (connect 1 / disconnect 2), the
// OSReport output-hook accessor pair used by trk_print, and the byte-flush delay
// loop. DAT_805f13a8 doubles as the connect flag and the saved OSReport hook.
// ===========================================================================

extern int DAT_805f13a8;   // connect flag (cmd 1/2) + saved OSReport output hook

void         msg_request_init(unsigned int *node, unsigned int value);
unsigned int msg_queue_post(int msg);

// forward decls (definitions below)
void trk_set_output_hook(int hook);
int  trk_get_output_hook(void);

// ---------------------------------------------------------------------------
// trk_cmd_connect (0x80392f50, cmd 1) - mark the connection active, then reply OK.
unsigned int trk_cmd_connect(void)
{
    unsigned char reply[0x40];
    DAT_805f13a8 = 1;
    trk_build_reply(reply, 0);
    FUN_80396058();
    return 0;
}

// ---------------------------------------------------------------------------
// trk_cmd_disconnect (0x80392ed8, cmd 2) - mark the connection inactive, reply OK,
// and post a type-1 message so the pump loop tears down.
unsigned int trk_cmd_disconnect(void)
{
    unsigned char reply[0x40];
    unsigned int node[3];
    DAT_805f13a8 = 0;
    trk_build_reply(reply, 0);
    FUN_80396058();
    msg_request_init(node, 1);
    msg_queue_post((int)node);
    return 0;
}

// ---------------------------------------------------------------------------
// trk_set_output_hook (0x80392fb4) - store the OSReport output hook / connect flag.
void trk_set_output_hook(int hook) { DAT_805f13a8 = hook; }

// ---------------------------------------------------------------------------
// trk_get_output_hook (0x80392fc0) - return the saved OSReport output hook.
int trk_get_output_hook(void) { return DAT_805f13a8; }

// ---------------------------------------------------------------------------
// trk_flush_bytes (0x80392fd0) - pump the transport once per byte for `count` bytes,
// with an extra pump every 16th byte, then a final pump. Paces a byte stream out.
void trk_flush_bytes(int base, int count)
{
    (void)base;
    for (int i = 0; i < count; i = i + 1) {
        FUN_80396f44();
        if ((i & 0xf) == 0xf) {   // every 16th byte
            FUN_80396f44();
        }
    }
    FUN_80396f44();
}
