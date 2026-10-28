// MetroTRK GDEV EXI serial-bus HAL layer.
//
// The low-level driver that the GDEV transport (os/trk_gdev_transport.cpp) rides
// on. Talks to the DDH/GDEV device over EXI channel 2 (registers at
// DAT_cc006828 CSR/device-select and DAT_cc006834 transfer-control) using the
// shared MetroTRK EXI primitives (trk_exi_transfer / trk_exi_read_block /
// trk_exi_write_block / trk_exi_cmd_read_id / trk_exi_cmd_read_status from
// os/trk_exi.cpp), and acks the PI interrupt at DAT_cc003000.
//
// The FUN_8039cca0 / FUN_8039ccc8 pair are the MetroTRK MSR read helpers used to
// bracket a critical section (read the interrupt-enable bit, restore later); the
// FUN_8039cc*/FUN_8039d0* helpers are the OS-glue (sleep, register interrupt
// handler). countLeadingZeros(x)>>5 folds a "transfer returned nonzero" test into
// a boolean.
//
// Batch: batch_1094
// Source module: os/trk_gdev_hal.cpp

#include "common.h"

// EXI channel-2 hardware registers (shared with os/trk_exi.cpp).
extern volatile unsigned int DAT_cc006828;   // EXI2 CSR / device-select
extern volatile unsigned int DAT_cc006834;   // EXI2 transfer control (busy bit 0)
// PI (Processor Interface) interrupt register.
extern volatile unsigned int DAT_cc003000;

// GDEV HAL state block.
extern void *DAT_80613030;                    // rx buffer write cursor
extern unsigned int DAT_80613034;             // "id valid / attached" flag
extern void (*DAT_80613024)(int);             // installed PI-interrupt hook
extern void *DAT_80613020;                    // caller-supplied context
extern unsigned int DAT_8061302c;             // negotiated packet/buffer size
extern unsigned int DAT_80613028;             // device id/status word
extern unsigned char DAT_80613034_pad;        // (kept distinct; see DAT_80613034)
extern unsigned char DAT_80613034b;            // unused alias guard
extern unsigned int DAT_80613034c;             // unused alias guard
extern unsigned char DAT_806113a8;            // write-sequence toggle counter
extern void *DAT_80613034_base;               // &DAT_80613034 (rx buffer base)

// MetroTRK OS glue.
extern unsigned long long FUN_8039cca0(void); // read MSR (save int-enable)
extern unsigned int FUN_8039ccc8(void);       // read MSR int-enable bit
extern unsigned int FUN_8039d068(unsigned int cycles);      // busy-wait / sleep
extern void FUN_8039ccec(int irq, void (*handler)(short));  // register interrupt handler
extern void FUN_8039d0f0(unsigned int cycles);              // settle delay

// Shared MetroTRK EXI primitives (os/trk_exi.cpp).
extern int trk_exi_on_complete(void);
extern int trk_exi_transfer(unsigned char *buf, int len, int is_write);
extern int trk_exi_cmd_read_status(unsigned char *out);
extern int trk_exi_read_block(int addr, void *out, unsigned int len);
extern int trk_exi_write_block(unsigned int addr, void *in, unsigned int len);
extern int trk_exi_cmd_read_id(unsigned char *out);

// ---------------------------------------------------------------------------
// gdev_hal_isr - PI-interrupt handler registered on IRQ 0x19.
// Acks the PI interrupt (write 0x1000 to the PI register) then forwards to the
// installed transport hook with the interrupt argument.
// Address: 0x803975d0 | Name: gdev_hal_isr
// ---------------------------------------------------------------------------
void gdev_hal_isr(short arg)
{
    DAT_cc003000 = 0x1000;                 // ack PI interrupt
    if (DAT_80613024 != (void (*)(int))0) {
        (*DAT_80613024)((int)arg);
    }
}

// ---------------------------------------------------------------------------
// gdev_hal_init - GDEV HAL initialisation (called by trk_gdev_init).
// Runs OS bring-up, seeds the rx buffer base/write cursor, stashes the caller
// context, settles the bus, and clears the EXI2 CSR.
// Address: 0x80397610 | Name: gdev_hal_init
// ---------------------------------------------------------------------------
void gdev_hal_init(void **out_buf, void *ctx)
{
    FUN_8039cca0();
    DAT_80613030 = &DAT_80613034;          // write cursor -> rx buffer base
    *out_buf = &DAT_80613034;              // hand the buffer base back
    DAT_80613020 = ctx;                    // remember caller context
    FUN_8039d068(0x18000);                 // settle
    DAT_cc006828 = 0;                      // clear EXI2 CSR / deselect
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// gdev_hal_open - GDEV HAL open/attach: installs the transfer-complete callback
// and registers the PI-interrupt handler on IRQ 0x19.
// (Reached from trk_gdev_shutdown's teardown/re-arm path.)
// Address: 0x80397688 | Name: gdev_hal_open
// ---------------------------------------------------------------------------
void gdev_hal_open(void)
{
    FUN_8039d068(0x18000);                 // settle
    FUN_8039d068(0x40);
    DAT_80613024 = (void (*)(int))trk_exi_on_complete;  // completion callback
    FUN_8039ccec(0x19, gdev_hal_isr);      // register IRQ 0x19 handler
    FUN_8039d0f0(0x40);                    // settle
}

// ---------------------------------------------------------------------------
// gdev_hal_probe - probe the device and report the negotiated buffer size.
// Reads the device id (command 0x40) and, if the id's low bit is set, the
// status word (command 0x60); when the top status nibble is 0x1f it latches the
// packet size (low 15 bits), marks the device attached, and caches the status.
// Returns the negotiated size (DAT_8061302c), 0 when not yet attached.
// Address: 0x803976dc | Name: gdev_hal_probe
// ---------------------------------------------------------------------------
unsigned int gdev_hal_probe(void)
{
    unsigned int reply[3];

    DAT_80613034 = 0;
    if (DAT_8061302c == 0) {
        FUN_8039cca0();
        trk_exi_cmd_read_id((unsigned char *)reply);
        if ((reply[0] & 1) != 0) {
            trk_exi_cmd_read_status((unsigned char *)reply);
            unsigned int status = reply[0] & 0x1fffffff;
            if ((reply[0] & 0x1f000000) == 0x1f000000) {
                DAT_8061302c = reply[0] & 0x7fff;   // negotiated size
                DAT_80613034 = 1;                   // attached
                DAT_80613028 = status;              // cache status
            }
        }
        FUN_8039ccc8();
    }
    return DAT_8061302c;
}

// ---------------------------------------------------------------------------
// gdev_hal_recv - receive one packet into the caller buffer.
// Reads from the device receive window (0x1e000, or +0x1000 when the status's
// bit 16 is set), rounding the length up to a 4-byte multiple, then clears the
// pending-packet size/flag. Returns 0.
// Address: 0x80397778 | Name: gdev_hal_recv
// ---------------------------------------------------------------------------
int gdev_hal_recv(void *buf, int len)
{
    int window;

    FUN_8039cca0();
    if ((DAT_80613028 & 0x10000) == 0) {
        window = 0;
    } else {
        window = 0x1000;
    }
    trk_exi_read_block(window + 0x1e000, buf, (unsigned int)(len + 3) & 0xfffffffc);
    DAT_8061302c = 0;                      // consume pending packet
    DAT_80613034 = 0;
    FUN_8039ccc8();
    return 0;
}

// ---------------------------------------------------------------------------
// gdev_hal_send - transmit one packet to the device.
// Four EXI phases, each bracketed by the CSR select/deselect (bits 0x405 kept,
// 0xc0 asserted for the transfer): (1) poll status until the "busy" bit (2) of
// the status word clears; (2) write the payload to the double-buffered transmit
// window (0x1c000 / +0x1000, selected by the write-sequence toggle) rounded up
// to a 4-byte multiple, retrying until the block write reports progress;
// (3) poll status again; (4) issue the "commit" command carrying the sequence
// number, the 0x1f marker and the length in the low 29 bits, then poll status
// one final time until both transfers succeed and the busy bit clears. Returns 0.
// Address: 0x80397804 | Name: gdev_hal_send
// ---------------------------------------------------------------------------
int gdev_hal_send(void *buf, unsigned int len)
{
    unsigned char cmd_a[8];
    unsigned int  commit[2];
    unsigned char cmd_b[8];
    unsigned int  status;
    unsigned int  csr;
    int           xfer;
    unsigned int  seq;
    unsigned int  clz_write;

    FUN_8039cca0();

    // Phase 1: wait for the device to be ready to accept a packet.
    do {
        csr = DAT_cc006828;
        DAT_cc006828 = (csr & 0x405) | 0xc0;
        cmd_b[4] = 0x40; cmd_b[5] = 0; cmd_b[6] = 0; cmd_b[7] = 0;
        xfer = trk_exi_transfer(cmd_b + 4, 2, 1);
        countLeadingZeros((unsigned int)xfer);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        trk_exi_transfer((unsigned char *)&status, 4, 0);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        csr = DAT_cc006828;
        DAT_cc006828 = csr & 0x405;
    } while ((status & 2) != 0);

    // Select the transmit window from the write-sequence toggle.
    DAT_806113a8 = DAT_806113a8 + 1;
    unsigned int window = ((DAT_806113a8 & 1) == 0) ? 0u : 0x1000u;

    // Phase 2: push the payload until the block write reports progress.
    unsigned int wrote;
    do {
        wrote = (unsigned int)trk_exi_write_block(window | 0x1c000, buf,
                                                  (len + 3) & 0xfffffffc);
    } while (wrote == 0);

    // Phase 3: poll status again.
    do {
        csr = DAT_cc006828;
        DAT_cc006828 = (csr & 0x405) | 0xc0;
        cmd_b[0] = 0x40; cmd_b[1] = 0; cmd_b[2] = 0; cmd_b[3] = 0;
        xfer = trk_exi_transfer(cmd_b, 2, 1);
        countLeadingZeros((unsigned int)xfer);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        trk_exi_transfer((unsigned char *)&status, 4, 0);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        csr = DAT_cc006828;
        DAT_cc006828 = csr & 0x405;
    } while ((status & 2) != 0);

    // Phase 4a: commit command (sequence << 16 | 0x1f marker | length | select).
    seq = (unsigned int)DAT_806113a8;
    do {
        csr = DAT_cc006828;
        DAT_cc006828 = (csr & 0x405) | 0xc0;
        commit[0] = (seq << 0x10) | 0x1f000000 | (len & 0x1fffffff) | 0xc0000000;
        xfer = trk_exi_transfer((unsigned char *)commit, 4, 1);
        clz_write = countLeadingZeros((unsigned int)xfer);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        csr = DAT_cc006828;
        DAT_cc006828 = csr & 0x405;
    } while ((clz_write >> 5) != 0);

    // Phase 4b: final status poll until both transfers succeed and not busy.
    unsigned int clz_cmd;
    unsigned int clz_read;
    do {
        csr = DAT_cc006828;
        DAT_cc006828 = (csr & 0x405) | 0xc0;
        cmd_a[0] = 0x40; cmd_a[1] = 0; cmd_a[2] = 0; cmd_a[3] = 0;
        xfer = trk_exi_transfer(cmd_a, 2, 1);
        clz_cmd = countLeadingZeros((unsigned int)xfer);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        xfer = trk_exi_transfer((unsigned char *)&status, 4, 0);
        clz_read = countLeadingZeros((unsigned int)xfer);
        do { csr = DAT_cc006834; } while ((csr & 1) != 0);
        csr = DAT_cc006828;
        DAT_cc006828 = csr & 0x405;
    } while (((clz_cmd >> 5) != 0 || (clz_read >> 5) != 0) || ((status & 2) != 0));

    FUN_8039ccc8();
    return 0;
}

// ---------------------------------------------------------------------------
// GDEV HAL no-op flush/settle stubs (installed as the transport flush hooks).
// Address: 0x80397a64 | Name: gdev_hal_flush_a
// Address: 0x80397a68 | Name: gdev_hal_flush_b
// Address: 0x80397a6c | Name: gdev_hal_flush_c
// Address: 0x80397a74 | Name: gdev_hal_stub_d
// ---------------------------------------------------------------------------
void gdev_hal_flush_a(void) { }
void gdev_hal_flush_b(void) { }
void gdev_hal_flush_c(void) { }
void gdev_hal_stub_d(void) { }
