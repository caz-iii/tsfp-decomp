// MetroTRK GDEV (Emulator/GDEV hardware) transport driver.
//
// The hook set trk_select_hardware installs for GDEV hardware (id 1). Mirrors
// the AMC-DDH driver but talks to the GDEV HAL (FUN_803976xx / FUN_80397axx)
// and buffers into its own receive ring at DAT_805f2670 (0x500-byte backing
// store at DAT_805f2170).
//
// Batch: batch_1092
// Source module: os/trk_gdev_transport.cpp

#include "common.h"

// GDEV receive ring + backing store, and transport-ready flag.
extern unsigned int DAT_805f2670;
extern unsigned char DAT_805f2170;      // 0x500-byte ring backing store
extern int DAT_80613018;                // GDEV transport-ready flag

// Shared ring helpers (trk_ring.cpp).
extern unsigned int trk_ring_push(int ring, unsigned int src, unsigned int len);
extern void trk_ring_init(unsigned int *ring, unsigned int base, unsigned int cap);
extern unsigned int trk_ring_count(int ring);
extern unsigned int trk_amc_ring_drain(unsigned int *ring, unsigned int dst, unsigned int count);
extern void FUN_80396f44(void);         // AMC/GDEV poll-service

// GDEV HAL (SDK region 0x803976xx / 0x80397axx).
extern void FUN_80397688(void);         // GDEV shutdown
extern int  FUN_803976dc(void);         // GDEV bytes-available
extern int  FUN_80397778(void *buf, int len);   // GDEV receive-into-buffer
extern int  FUN_80397804(void *buf, unsigned int len); // GDEV transmit-chunk
extern void FUN_80397610(void *ctx, unsigned int arg); // GDEV init
extern void FUN_80397a64(void);         // GDEV flush A
extern void FUN_80397a68(void);         // GDEV flush B

// ---------------------------------------------------------------------------
// Address: 0x80396bf8 | Name: trk_gdev_shutdown | Date: 2026-09-14
// ---------------------------------------------------------------------------
int trk_gdev_shutdown(void)
{
    FUN_80397688();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396c1c | Name: trk_gdev_pump_rx | Date: 2026-09-14
// Reads available bytes from the GDEV HAL into a stack buffer and pushes them
// into the receive ring. Returns 0 if none, byte count on success, 0xffffd8e7
// on a HAL receive error.
// ---------------------------------------------------------------------------
unsigned int trk_gdev_pump_rx(void)
{
    unsigned int stack_buf[321];
    unsigned int avail = (unsigned int)FUN_803976dc();

    if ((int)avail < 1) {
        avail = 0;
    } else if (FUN_80397778(stack_buf, avail) == 0) {
        trk_ring_push((int)&DAT_805f2670, (unsigned int)stack_buf, avail);
    } else {
        avail = 0xffffd8e7;
    }
    return avail;
}

// ---------------------------------------------------------------------------
// Address: 0x80396c8c | Name: trk_gdev_flush_a | Date: 2026-09-14
// ---------------------------------------------------------------------------
int trk_gdev_flush_a(void)
{
    FUN_80397a64();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396cb0 | Name: trk_gdev_flush_b | Date: 2026-09-14
// ---------------------------------------------------------------------------
int trk_gdev_flush_b(void)
{
    FUN_80397a68();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396cd4 | Name: trk_gdev_send | Date: 2026-09-14
// Transmit hook: when ready, services the GDEV then transmits in chunks via
// FUN_80397804, advancing the source pointer, until the length drains or a
// chunk sends nothing. Returns 0 ok, 0xffffd8ef when not ready.
// ---------------------------------------------------------------------------
unsigned int trk_gdev_send(unsigned int *src, unsigned int length)
{
    unsigned int result;
    int sent;

    if (DAT_80613018 == 0) {
        FUN_80396f44();
        result = 0xffffd8ef;
    } else {
        FUN_80396f44();
        for (; 0 < (int)length; length = length - sent) {
            FUN_80396f44();
            sent = FUN_80397804(src, length);
            if (sent == 0) break;
            src = (unsigned int *)((int)src + sent);
        }
        result = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Address: 0x80396d94 | Name: trk_gdev_recv | Date: 2026-09-14
// Receive hook: services the GDEV and pumps RX until the ring holds at least
// the requested count, then drains that many bytes into the caller. Returns 0
// ok, -0x2711 when not ready, or the HAL error.
// ---------------------------------------------------------------------------
int trk_gdev_recv(unsigned int dst, unsigned int count)
{
    unsigned int stack_buf[320];
    int status = 0;

    if (DAT_80613018 == 0) {
        status = -0x2711;
    } else {
        FUN_80396f44();
        while (trk_ring_count((int)&DAT_805f2670) < count) {
            status = 0;
            unsigned int avail = (unsigned int)FUN_803976dc();
            if (avail != 0 && (status = FUN_80397778(stack_buf, count)) == 0) {
                trk_ring_push((int)&DAT_805f2670, (unsigned int)stack_buf, avail);
            }
        }
        if (status == 0) {
            trk_amc_ring_drain((unsigned int *)&DAT_805f2670, dst, count);
        } else {
            FUN_80396f44();
        }
    }
    return status;
}

// ---------------------------------------------------------------------------
// Address: 0x80396ebc | Name: trk_gdev_init | Date: 2026-09-14
// Brings up the GDEV HAL (FUN_80397610) and initialises the receive ring over
// the 0x500-byte backing store at DAT_805f2170.
// ---------------------------------------------------------------------------
int trk_gdev_init(unsigned int *ctx, unsigned int arg)
{
    FUN_80396f44();
    FUN_80397610(ctx, arg);
    FUN_80396f44();
    trk_ring_init((unsigned int *)&DAT_805f2670, (unsigned int)&DAT_805f2170, 0x500);
    return 0;
}
