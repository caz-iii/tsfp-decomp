// MetroTRK EXI (External Interface) bus layer.
//
// The AMC-DDH transport rides on the GameCube EXI serial bus (channel-2
// register block at 0xCC006800). This file holds:
//   - the ring lock/commit hooks that mask/restore interrupts around a ring op
//   - the low-level EXI immediate transfer primitive (trk_exi_transfer)
//   - the EXI command reads and the EXI memory read/write built on it
//   - the EXI completion callback
//
// EXI channel-2 registers used:
//   DAT_cc006828  CSR / device-select   (bit mask 0x405 preserved, 0xc0 selects)
//   DAT_cc006834  transfer control      (bit0 = busy; len<<4 | rw<<2 | start)
//   DAT_cc006838  immediate data
//
// countLeadingZeros(x)>>5 is the SDK idiom for "x == 0 ? 1 : 0" used to fold a
// transfer's boolean result; a chain of ORs accumulates any failure.
//
// Batch: batch_1093
// Source module: os/trk_exi.cpp

#include "common.h"

// EXI channel-2 hardware registers (0xCC006800 block).
extern volatile unsigned int DAT_cc006828;   // CSR / device select
extern volatile unsigned int DAT_cc006834;   // transfer control (bit0 busy)
extern volatile unsigned int DAT_cc006838;   // immediate data word

// EXI completion-callback hook + its armed flag.
extern int (*DAT_80613020)(int);
extern int DAT_80613034;

// Interrupt mask/restore primitives (OSDisableInterrupts / OSRestoreInterrupts).
extern unsigned long long FUN_8039cca0(void);  // disable, returns prior MSR
extern void FUN_8039ccc8(void);                // restore

// ---------------------------------------------------------------------------
// Address: 0x80396f44 | Name: trk_exi_service | Date: 2026-09-14
// Poll/service hook. No-op on this build (EXI transfers are synchronous).
// ---------------------------------------------------------------------------
void trk_exi_service(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x80396f94 | Name: trk_ring_unlock | Date: 2026-09-14
// Ring commit/unlock: restores the interrupt state saved by trk_ring_lock.
// ---------------------------------------------------------------------------
void trk_ring_unlock(void)
{
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// Address: 0x80396fb8 | Name: trk_ring_lock | Date: 2026-09-14
// Ring lock: disables interrupts and stashes the prior MSR into *saved.
// ---------------------------------------------------------------------------
void trk_ring_lock(unsigned int *saved)
{
    unsigned long long prior = FUN_8039cca0();
    *saved = (unsigned int)(prior >> 0x20);
}

// ---------------------------------------------------------------------------
// Address: 0x80396fe8 | Name: trk_ring_init_done | Date: 2026-09-14
// Ring init-complete hook. No-op on this build.
// ---------------------------------------------------------------------------
void trk_ring_init_done(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x80396fec | Name: trk_exi_transfer | Date: 2026-09-14
// One EXI immediate transfer of `nbytes` (1..4). is_write != 0 loads the data
// register from `buf` big-endian first; then it programs the control register
// (len-1)<<4 | is_write<<2 | start(1) and spins on the busy bit; on a read it
// unpacks the data register back into `buf` big-endian. Always returns 1.
// ---------------------------------------------------------------------------
unsigned int trk_exi_transfer(unsigned char *buf, int nbytes, int is_write)
{
    int i;
    unsigned int word;

    if (is_write != 0) {
        i = 0;
        word = 0;
        // Big-endian pack of nbytes into the 32-bit data word.
        while (i < nbytes) {
            word |= (unsigned int)buf[i] << ((3 - i) * 8);
            i++;
        }
        DAT_cc006838 = word;
    }

    DAT_cc006834 = is_write << 2 | 1U | (nbytes - 1) * 0x10;
    do {
        word = DAT_cc006834;
    } while ((word & 1) != 0);

    if (is_write == 0) {
        i = 0;
        word = DAT_cc006838;
        while (i < nbytes) {
            buf[i] = (unsigned char)(word >> ((3 - i) * 8));
            i++;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Address: 0x80397284 | Name: trk_exi_cmd_read_status | Date: 2026-09-14
// Selects the EXI device, issues command byte 0x60, then reads a 4-byte reply
// into `out`. Returns 0 on success (folded from the two transfer results),
// nonzero on any EXI failure.
// ---------------------------------------------------------------------------
unsigned int trk_exi_cmd_read_status(unsigned char *out)
{
    unsigned char cmd[12];
    unsigned int r1, r2, csr;

    csr = DAT_cc006828;
    DAT_cc006828 = csr & 0x405 | 0xc0;   // select device

    cmd[0] = 0x60; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    r1 = countLeadingZeros(trk_exi_transfer(cmd, 2, 1)) ;
    do { csr = DAT_cc006834; } while ((csr & 1) != 0);

    r2 = countLeadingZeros(trk_exi_transfer(out, 4, 0));
    do { csr = DAT_cc006834; } while ((csr & 1) != 0);

    csr = DAT_cc006828;
    unsigned int result = countLeadingZeros((r1 | r2) >> 5);
    DAT_cc006828 = csr & 0x405;           // deselect
    return result >> 5;
}

// ---------------------------------------------------------------------------
// Address: 0x80397330 | Name: trk_exi_read_block | Date: 2026-09-14
// Reads `len` bytes from EXI address `addr` into `out` in 4-byte words. Issues
// the read command (addr<<8 | 0x20000000), then pulls words until len drains.
// Returns 0 on success, nonzero on any EXI failure (accumulated).
// ---------------------------------------------------------------------------
unsigned int trk_exi_read_block(unsigned int addr, unsigned int *out, int len)
{
    unsigned int result, csr;
    unsigned int cmd_word = (addr & 0x1fffc) << 8 | 0x20000000;
    unsigned int data_word;

    csr = DAT_cc006828;
    DAT_cc006828 = csr & 0x405 | 0xc0;
    result = countLeadingZeros(trk_exi_transfer((unsigned char *)&cmd_word, 4, 1)) >> 5;
    do { csr = DAT_cc006834; } while ((csr & 1) != 0);

    while (len != 0) {
        unsigned int r = countLeadingZeros(trk_exi_transfer((unsigned char *)&data_word, 4, 0));
        result |= r >> 5;
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        len -= 4;
        *out = data_word;
        out++;
        if (len < 0) len = 0;
    }

    csr = DAT_cc006828;
    result = countLeadingZeros(result);
    DAT_cc006828 = csr & 0x405;
    return result >> 5;
}

// ---------------------------------------------------------------------------
// Address: 0x8039740c | Name: trk_exi_write_block | Date: 2026-09-14
// Writes `len` bytes from `in` to EXI address `addr` in 4-byte words. Issues
// the write command (addr<<8 | 0xa0000000), then pushes words until len drains.
// Returns 0 on success, nonzero on any EXI failure (accumulated).
// ---------------------------------------------------------------------------
unsigned int trk_exi_write_block(unsigned int addr, unsigned int *in, int len)
{
    unsigned int result, csr;
    unsigned int cmd_word = (addr & 0x1fffc) << 8 | 0xa0000000;
    unsigned int data_word;

    csr = DAT_cc006828;
    DAT_cc006828 = csr & 0x405 | 0xc0;
    result = countLeadingZeros(trk_exi_transfer((unsigned char *)&cmd_word, 4, 1)) >> 5;
    do { csr = DAT_cc006834; } while ((csr & 1) != 0);

    while (len != 0) {
        data_word = *in;
        in++;
        unsigned int r = countLeadingZeros(trk_exi_transfer((unsigned char *)&data_word, 4, 1));
        result |= r >> 5;
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        len -= 4;
        if (len < 0) len = 0;
    }

    csr = DAT_cc006828;
    result = countLeadingZeros(result);
    DAT_cc006828 = csr & 0x405;
    return result >> 5;
}

// ---------------------------------------------------------------------------
// Address: 0x803974e8 | Name: trk_exi_cmd_read_id | Date: 2026-09-14
// Same shape as trk_exi_cmd_read_status but issues command byte 0x40 (read ID)
// then reads a 4-byte reply into `out`. Returns 0 on success.
// ---------------------------------------------------------------------------
unsigned int trk_exi_cmd_read_id(unsigned char *out)
{
    unsigned char cmd[12];
    unsigned int r1, r2, csr;

    csr = DAT_cc006828;
    DAT_cc006828 = csr & 0x405 | 0xc0;

    cmd[0] = 0x40; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    r1 = countLeadingZeros(trk_exi_transfer(cmd, 2, 1));
    do { csr = DAT_cc006834; } while ((csr & 1) != 0);

    r2 = countLeadingZeros(trk_exi_transfer(out, 4, 0));
    do { csr = DAT_cc006834; } while ((csr & 1) != 0);

    csr = DAT_cc006828;
    unsigned int result = countLeadingZeros((r1 | r2) >> 5);
    DAT_cc006828 = csr & 0x405;
    return result >> 5;
}

// ---------------------------------------------------------------------------
// Address: 0x80397594 | Name: trk_exi_on_complete | Date: 2026-09-14
// EXI transfer-complete callback: sets the completion flag and invokes the
// installed completion hook (if any).
// ---------------------------------------------------------------------------
void trk_exi_on_complete(void)
{
    DAT_80613034 = 1;
    if (DAT_80613020 != 0) {
        DAT_80613020(0);
    }
}
