// batch_1086: MetroTRK target-control glue - the run/step state accessors, the
// FP-status snapshot, the fault-guarded memory access wrapper used by the read/
// write-memory commands, the host-request executor driven by the message pump, and
// the single-step entry called from the register command. These run on the target
// side of the TRK link and sit between the command handlers (trk_commands.cpp) and
// the host-IO layer (trk_host_io.cpp).
//
// Non-matching decomp: the memory read/write primitives, the transfer helper, the
// cache-flush and host-IO functions resolve as cross-module externs (common.h does
// not pull generated_decls.h); PPC FPSCR field reads are modelled as an argument.
//
// Address range: 0x80393cc0 - 0x80394047

#include "common.h"

// --- cross-module TRK helpers ----------------------------------------------
// Fault-guarded memory copy primitives (trk_mem_access.cpp region).
extern void trk_read_target_memory(int dst, unsigned int size, int *src);   // FUN_8039597c
extern void trk_write_target_memory(int dst, unsigned int size, int *src);  // FUN_80395b68
// Block host transfer (0xD0/0xD1), defined in trk_host_io.cpp as trk_host_transfer.
extern int  trk_host_transfer(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                              int handle, int addr, unsigned int *buf, unsigned int *out_status,
                              int is_write, unsigned int dir, int a15, int a16);
// Host scalar/open requests (0xD2-0xD4), defined in trk_host_io.cpp.
extern int  trk_host_open(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                          int handle, char mode, int path, unsigned int *out_result,
                          int a13, int a14, int a15, int a16);            // trk_host_open
extern int  trk_host_query(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                           int handle, unsigned int *out_result, int a11, int a12,
                           int a13, int a14, int a15, int a16);           // trk_host_query
extern int  trk_host_write(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                           int handle, unsigned int *io_value, char mode, unsigned int *out_result,
                           unsigned int value, int a14, int a15, int a16); // trk_host_write
extern void trk_flush_cache_range(unsigned int addr, unsigned int count);  // trk_flush_cache_range
extern void msg_request_init(void *req, int type);                         // msg_request_init
extern void msg_queue_post(int req);                                       // msg_queue_post
extern void FUN_80396f44(void);                                            // step hook (no-op)

// --- TRK target state globals ----------------------------------------------
extern int          DAT_805f144c;      // run/stop state (0 = running, 1 = stopped)
extern unsigned int DAT_805f1454;      // step-support flag set at init
extern unsigned int DAT_805f1464;      // pending host-request opcode / result slot
extern int          DAT_805f1468;      // host request handle
extern unsigned int *DAT_805f146c;     // host request buffer pointer
extern int          DAT_805f1470;      // host request address / mode
extern unsigned int DAT_805f14d8;      // cumulative host-transfer counter
extern unsigned int DAT_805f1650;      // scheduler/resume flags

// Saved FP/exception context around a guarded memory access.
extern unsigned int DAT_80498004;
extern unsigned int DAT_80498008;
extern unsigned int DAT_8049800c;
extern unsigned int DAT_80498010;      // low byte doubles as the access-fault flag
// Single-step control block consumed by the step hook.
extern unsigned int DAT_80498014;
extern unsigned int DAT_80498018;
extern unsigned int DAT_8049801c;
extern unsigned int DAT_80498020;
extern unsigned int DAT_80498024;

// ---------------------------------------------------------------------------
// trk_read_fpscr (0x80393cc0) - snapshot the PPC floating-point status/control
// register into a caller-supplied u64. Each FPSCR bit field is read from the
// register and re-packed into the 32-bit word; modelled here as one packed input
// so the layout is preserved for the guarded-access save path.
void trk_read_fpscr(unsigned long long *out)
{
    unsigned int fpscr = 0;  // real code reads the live FPSCR bit fields
    *out = (unsigned long long)fpscr;
}

// ---------------------------------------------------------------------------
// trk_nop_3ce4 (0x80393ce4) - empty companion hook (restore FPSCR); no-op here.
void trk_nop_3ce4(void)
{
}

// ---------------------------------------------------------------------------
// trk_access_memory (0x80393d08) - fault-guarded target memory access used by the
// read-memory and write-memory commands. It saves the FP/exception context, clears
// the fault flag, performs a read (param_4 == 0) or write via the low-level copy
// primitive, and if the access faulted returns TRK error 0x702 (bus error) with a
// zero count; otherwise it restores the saved context and returns success.
unsigned int trk_access_memory(int addr, unsigned int size, int *io_count, int is_write)
{
    unsigned int saved0 = DAT_80498004;
    unsigned int saved1 = DAT_80498008;
    unsigned int saved2 = DAT_8049800c;
    unsigned int saved3 = DAT_80498010;
    unsigned int status = 0;

    // clear the fault flag (low byte of DAT_80498010)
    DAT_80498010 = DAT_80498010 & 0xffffff00;

    if (is_write == 0) {
        trk_read_target_memory(addr, size, io_count);
    } else {
        trk_write_target_memory(addr, size, io_count);
    }

    if ((DAT_80498010 & 0xff) != 0) {
        status = 0x702;      // access faulted -> bus error
        *io_count = 0;
    }

    DAT_80498004 = saved0;
    DAT_80498008 = saved1;
    DAT_8049800c = saved2;
    DAT_80498010 = saved3;
    return status;
}

// ---------------------------------------------------------------------------
// trk_set_step_support (0x80393dcc) - record the step-support flag at TRK init.
void trk_set_step_support(unsigned int flag)
{
    DAT_805f1454 = flag;
}

// ---------------------------------------------------------------------------
// trk_mark_stopped (0x80393ddc) - flag the target as stopped (halted at the
// monitor); returns 0. Called when reporting CPU state to the host.
int trk_mark_stopped(void)
{
    DAT_805f144c = 1;
    return 0;
}

// ---------------------------------------------------------------------------
// trk_set_run_state (0x80393df4) - set the run/stop state to an explicit value.
void trk_set_run_state(int state)
{
    DAT_805f144c = state;
}

// ---------------------------------------------------------------------------
// trk_get_run_state (0x80393e04) - read the current run/stop state.
int trk_get_run_state(void)
{
    return DAT_805f144c;
}

// ---------------------------------------------------------------------------
// trk_exec_host_request (0x80393e14) - executor run by the message pump when a
// host-service request (opcodes 0xD0..0xD4) is pending. It dispatches to the
// matching host-IO helper, normalises a zero result to 1 when the call itself
// returned non-zero, stores the result back into the opcode/result slot, and for a
// host read (0xD1) makes the written range coherent with the instruction stream.
// Any other opcode posts a type-4 message so the pump advances and returns 0.
int trk_exec_host_request(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                          int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    unsigned int *buf = DAT_805f146c;
    unsigned int opcode = DAT_805f1464;
    int result = 0;
    unsigned int reply_status = 0;
    unsigned int io_len;
    unsigned int req[8];

    if (opcode == 0xd1 || opcode == 0xd0 || opcode == 0xd2 ||
        opcode == 0xd3 || opcode == 0xd4) {
        if (opcode == 0xd2) {
            result = trk_host_open(a1, a2, a3, a4, a5, a6, a7, a8,
                                   DAT_805f1468, (char)(int)DAT_805f146c, DAT_805f1470,
                                   &reply_status, a13, a14, a15, a16);
            if (reply_status == 0 && result != 0) {
                reply_status = 1;
            }
            DAT_805f1464 = reply_status;
        } else if (opcode == 0xd3) {
            result = trk_host_query(a1, a2, a3, a4, a5, a6, a7, a8,
                                    DAT_805f1468, &reply_status, a11, a12, a13, a14, a15, a16);
            if (reply_status == 0 && result != 0) {
                reply_status = 1;
            }
            DAT_805f1464 = reply_status;
        } else if (opcode == 0xd4) {
            io_len = *DAT_805f146c;
            result = trk_host_write(a1, a2, a3, a4, a5, a6, a7, a8,
                                    DAT_805f1468, &io_len, (char)DAT_805f1470, &reply_status,
                                    io_len, a14, a15, a16);
            if (reply_status == 0 && result != 0) {
                reply_status = 1;
            }
            DAT_805f1464 = reply_status;
            *DAT_805f146c = io_len;
        } else {
            // 0xd0 (write) / 0xd1 (read): countLeadingZeros(0xd1 - opcode) >> 5 is
            // the 0/1 direction flag (1 exactly when opcode == 0xd1).
            unsigned int diff = 0xd1 - opcode;
            unsigned int clz = 0;
            unsigned int m = diff;
            while (clz < 32 && (m & 0x80000000) == 0) { clz++; m <<= 1; }
            result = trk_host_transfer(a1, a2, a3, a4, a5, a6, a7, a8,
                                       DAT_805f1468, DAT_805f1470, DAT_805f146c, &reply_status,
                                       1, clz >> 5, a15, a16);
            if (reply_status == 0 && result != 0) {
                reply_status = 1;
            }
            DAT_805f1464 = reply_status;
            if (opcode == 0xd1) {
                trk_flush_cache_range((unsigned int)DAT_805f1470, *buf);
            }
        }
        DAT_805f14d8 = DAT_805f14d8 + 4;
    } else {
        msg_request_init(req, 4);
        msg_queue_post((int)req);
        result = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// trk_get_transfer_count (0x80394014) - read the cumulative host-transfer counter.
unsigned int trk_get_transfer_count(void)
{
    return DAT_805f14d8;
}

// ---------------------------------------------------------------------------
// trk_step (0x80394024) - arm a single instruction step from the register command:
// set the step-control block (exception cause + saved PC/args), run the target step
// hook, request a resume via the scheduler flag 0x400, decrement the nesting count
// when the recorded cause is a step/trace exception, clear the stopped state, and
// return success. A non-zero mode selects an unsupported path returning 0x703.
unsigned int trk_step(unsigned int pc, unsigned int arg, int mode)
{
    unsigned int status;

    if (mode == 0) {
        DAT_80498018 = 1;
        DAT_80498014 = 1;
        DAT_80498020 = pc;
        DAT_80498024 = arg;
        FUN_80396f44();
        DAT_805f1650 = DAT_805f1650 | 0x400;
        if (DAT_80498018 == 0 || DAT_80498018 == 0x10) {
            DAT_8049801c = DAT_8049801c - 1;
        }
        status = 0;
        DAT_805f144c = 0;
    } else {
        status = 0x703;      // unsupported step mode
    }
    return status;
}
