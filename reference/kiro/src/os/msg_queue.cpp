// batch_1079: a small locked two-slot message ring buffer and the loop that pumps
// it. The queue stores fixed 0xC-byte messages in a two-entry array guarded by a
// spinlock (acquire/release = FUN_80393794 / FUN_8039378c); msg_queue_post appends
// one (dropping when full), msg_queue_take pops one FIFO. msg_pump_loop drains the
// queue and dispatches each message by its type code, and the two tiny helpers
// initialise / release a queue-request node.
//
// Non-matching decomp: raw FUN_/DAT_ symbols this module touches are declared
// extern per-file with the arity/type seen at the call sites; the shared message
// storage keeps its DAT_805ef9** names because it is addressed by pointer
// arithmetic against a fixed base.

#include "common.h"

// --- shared two-slot message ring state (raw globals, addressed by base+index) ---
extern int          DAT_805ef9b4;   // number of queued messages (0..2)
extern int          DAT_805ef9b8;   // read cursor (0 or 1)
extern unsigned int DAT_805ef9d4;   // rolling message id (wraps to >= 0x100)
extern unsigned char DAT_805ef9c0[]; // id-slot mirror base (byte-addressed, +slot)
extern unsigned char DAT_805ef9bc[]; // base of the 2 x 0xC-byte message array

// dispatcher context globals
extern char *DAT_80613008;          // pending deferred-command string (may be empty)

// --- lock / copy / dispatch helpers (raw, undocumented) ---------------------
extern "C" int  FUN_80393794(void);   // acquire message-queue lock
extern "C" int  FUN_8039378c(void);   // release message-queue lock
extern "C" void leaf_3268(int dst, int src, int n); // 0xC-byte block copy (memcpy)

extern int  FUN_80391980(int handle); // free a queue-request payload
extern int  FUN_80393e04(void);       // is deferred command runnable?
extern int  FUN_8039643c(int a, ...); // run deferred command
extern int  FUN_80391c68(int, int, int, int, int, int, int, int, char *,
                         int, int *, int, int, int, int, int); // build/emit next message
extern int  FUN_803919e4(int arg);    // resolve type-2 message payload
extern void FUN_80391e90(int, int, int, int, int, int, int, int, int,
                         int, int *, int, int, int, int, int); // handle type-2 message
extern void FUN_80393e14(int, int, int, int, int, int, int, int, int,
                         int, int *, int, int, int, int, int); // handle type-5 message
extern void FUN_80394298(int, int, int, int, int, int, int, int, int *,
                         int, int *, int, int, int, int, int); // handle type-3/4 message

// forward declarations (definitions below)
unsigned int msg_queue_take(int out);
unsigned int msg_queue_post(int msg);
void         msg_pump_free(int msg);

// The message layout used below (fixed 0xC bytes = 3 words):
//   +0x0 : type code (>=0 dispatch, 0 sentinel, <0 stop)
//   +0x4 : id / status
//   +0x8 : payload handle (freed by FUN_80391980)
struct QueueRequest {
    unsigned int value;   // +0x0
    unsigned int flags;   // +0x4
    int          handle;  // +0x8
};

// ---------------------------------------------------------------------------
// msg_pump_loop (0x80390e14) - drain the message queue and dispatch each entry by
// its type word until a stop sentinel (type in [0,2) with a nonzero, non-negative
// value) is seen. When the queue is momentarily empty it either runs a pending
// deferred command or asks FUN_80391c68 to synthesise the next message. Every
// dispatched message is released with msg_pump_free before the next take.
void msg_pump_loop(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                   int a9, int a10, int *a11, int a12, int a13, int a14, int a15, int a16)
{
    bool done = false;
    bool primed = false;
    int  msg[3];
    int  status;

    while (!done) {
        int got = msg_queue_take((int)msg);
        if (got == 0) {
            char *deferred = 0;
            if (primed && (deferred = DAT_80613008, *DAT_80613008 == '\0')) {
                if (FUN_80393e04() == 0) {
                    FUN_8039643c(0, a10, (int)a11, a12, a13, a14, a15, a16);
                }
                primed = false;
            } else {
                primed = true;
                a1 = FUN_80391c68(a1, a2, a3, a4, a5, a6, a7, a8, deferred,
                                  a10, a11, a12, a13, a14, a15, a16);
            }
        } else {
            primed = false;
            if (msg[0] == 2) {
                int payload = FUN_803919e4(msg[2]);
                FUN_80391e90(a1, a2, a3, a4, a5, a6, a7, a8, payload,
                             a10, a11, a12, a13, a14, a15, a16);
            } else if (msg[0] < 2) {
                if (msg[0] != 0 && msg[0] >= 0) {
                    done = true;
                }
            } else if (msg[0] == 5) {
                FUN_80393e14(a1, a2, a3, a4, a5, a6, a7, a8, got,
                             a10, a11, a12, a13, a14, a15, a16);
            } else if (msg[0] < 5) {
                FUN_80394298(a1, a2, a3, a4, a5, a6, a7, a8, msg,
                             a10, a11, a12, a13, a14, a15, a16);
            }
            (void)status;
            msg_pump_free((int)msg);
        }
    }
}

// ---------------------------------------------------------------------------
// msg_pump_free (0x80390f0c) - release the payload handle stored at +0x8 of a
// dispatched message.
void msg_pump_free(int msg)
{
    FUN_80391980(*(int *)(msg + 8));
}

// ---------------------------------------------------------------------------
// msg_request_init (0x80390f30) - initialise a queue-request node: set its value,
// clear the flags word, and mark the id slot invalid (-1).
void msg_request_init(unsigned int *node, unsigned int value)
{
    node[0] = value;
    node[1] = 0;
    node[2] = 0xffffffff;
}

// ---------------------------------------------------------------------------
// msg_queue_post (0x80390f48) - append a 0xC-byte message under the lock. Returns
// 0x100 if the queue is full (2 already queued), otherwise copies the message into
// the free slot, stamps it with the next rolling id, and returns 0.
unsigned int msg_queue_post(int msg)
{
    unsigned int result = 0;
    FUN_80393794();
    if (DAT_805ef9b4 == 2) {
        result = 0x100;
    } else {
        int slot = (DAT_805ef9b8 + DAT_805ef9b4) >> 0x1f;
        slot = (((DAT_805ef9b8 + DAT_805ef9b4) & 1U) ^ (unsigned int)-slot) + slot;
        slot = slot * 0xc;
        leaf_3268((int)(DAT_805ef9bc + slot), msg, 0xc);
        *(unsigned int *)(DAT_805ef9c0 + slot) = DAT_805ef9d4;
        DAT_805ef9d4 = DAT_805ef9d4 + 1;
        if (DAT_805ef9d4 < 0x100) {
            DAT_805ef9d4 = 0x100;
        }
        DAT_805ef9b4 = DAT_805ef9b4 + 1;
    }
    FUN_8039378c();
    return result;
}

// ---------------------------------------------------------------------------
// msg_queue_take (0x80391028) - pop the oldest message under the lock into the
// caller's 0xC-byte buffer. Returns 1 on success (and advances the read cursor,
// wrapping 2 -> 0), 0 when the queue is empty.
unsigned int msg_queue_take(int out)
{
    unsigned int got = 0;
    FUN_80393794();
    if (0 < DAT_805ef9b4) {
        leaf_3268(out, (int)(DAT_805ef9bc + DAT_805ef9b8 * 0xc), 0xc);
        DAT_805ef9b8 = DAT_805ef9b8 + 1;
        DAT_805ef9b4 = DAT_805ef9b4 + -1;
        if (DAT_805ef9b8 == 2) {
            DAT_805ef9b8 = 0;
        }
        got = 1;
    }
    FUN_8039378c();
    return got;
}
