// MetroTRK AMC-DDH (host serial) transport driver.
//
// This is the hook set that trk_select_hardware installs for AMC-DDH debug
// hardware (id 0). It drives the AMC HAL (FUN_803b8b6x, in the SDK region) and
// buffers received bytes through a circular receive buffer (DAT_805f2150) that
// trk_ring_* helpers manage. The breakpoint stubs at the top plant/trigger a
// PPC trap via trk_trap and report the outcome.
//
// Batch: batch_1091
// Source module: os/trk_amc_transport.cpp

#include "common.h"

// AMC receive ring-buffer control block (6 header words + payload).
//   [0]=head/write cursor  [2]=base  [3]=capacity  [4]=count  [5]=total
extern unsigned int DAT_805f2150;
extern unsigned char DAT_805f1950;      // ring backing storage (0x800 bytes)
extern int DAT_80613010;                // AMC transport-ready flag

// TRK helpers (defined elsewhere in the trk module).
extern unsigned char trk_get_state_byte(void);
extern int trk_get_output_hook(void);
extern unsigned char trk_trap(void);

// Ring-buffer + AMC glue helpers (defined in a later batch; permissive externs).
extern int  FUN_80396a98(int ring, unsigned int dst, unsigned int len);   // ring push
extern void FUN_80396ba0(unsigned int *ring, void *base, unsigned int cap); // ring init
extern unsigned int FUN_80396bf0(int ring);                                // ring count
extern void FUN_80396f44(void);                                            // AMC poll/service
extern void FUN_80396f94(void);                                            // ring commit hook
extern void FUN_80396fb8(unsigned int *lock);                              // ring lock

// Named memcpy-style leaf helper.
extern void leaf_31e8(unsigned int dst, unsigned int src, unsigned int len);

// Forward declaration (defined at end of this file).
unsigned int trk_amc_ring_drain(unsigned int *ring, unsigned int dst, unsigned int count);

// AMC HAL (SDK region 0x803b8xxx).
extern void FUN_803b8b64(void);         // AMC init
extern void FUN_803b8b68(void);         // AMC shutdown
extern int  FUN_803b8b6c(void);         // AMC bytes-available
extern int  FUN_803b8b74(void);         // AMC receive-into-buffer
extern int  FUN_803b8b7c(void);         // AMC transmit-chunk
extern void FUN_803b8b84(void);         // AMC flush A
extern void FUN_803b8b88(void);         // AMC flush B

// ---------------------------------------------------------------------------
// Address: 0x8039648c | Name: trk_amc_breakpoint_a | Date: 2026-09-14
// Plants/triggers a breakpoint trap when TRK is active and an output hook is
// installed: saves the word at *param_3, issues trk_trap, restores it, and maps
// the trap outcome to a TRK status (0 ok, 2 partial, 1 otherwise).
// ---------------------------------------------------------------------------
unsigned int trk_amc_breakpoint_a(int param_1, int param_2, unsigned int *slot)
{
    (void)param_1; (void)param_2;
    if (trk_get_state_byte() != 0 && trk_get_output_hook() != 0) {
        unsigned int saved = *slot;
        unsigned char rc = trk_trap();
        *slot = saved;
        if (rc != 1) {
            if (rc == 0) return 0;
            if (rc < 3) return 2;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Address: 0x80396548 | Name: trk_amc_breakpoint_b | Date: 2026-09-14
// Identical breakpoint stub (second vector slot).
// ---------------------------------------------------------------------------
unsigned int trk_amc_breakpoint_b(int param_1, int param_2, unsigned int *slot)
{
    (void)param_1; (void)param_2;
    if (trk_get_state_byte() != 0 && trk_get_output_hook() != 0) {
        unsigned int saved = *slot;
        unsigned char rc = trk_trap();
        *slot = saved;
        if (rc != 1) {
            if (rc == 0) return 0;
            if (rc < 3) return 2;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Address: 0x8039664c | Name: trk_amc_shutdown | Date: 2026-09-14
// ---------------------------------------------------------------------------
int trk_amc_shutdown(void)
{
    FUN_803b8b68();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396670 | Name: trk_amc_pump_rx | Date: 2026-09-14
// Reads whatever the AMC HAL has available into a 2KB stack buffer and pushes
// it into the receive ring. Returns 0 when nothing available, the byte count on
// success, or 0xffffd8e7 (-0x2719) if the HAL receive failed.
// ---------------------------------------------------------------------------
unsigned int trk_amc_pump_rx(void)
{
    unsigned char stack_buf[2052];
    unsigned int avail = (unsigned int)FUN_803b8b6c();

    if ((int)avail < 1) {
        avail = 0;
    } else if (FUN_803b8b74() == 0) {
        FUN_80396a98((int)&DAT_805f2150, (unsigned int)stack_buf, avail);
    } else {
        avail = 0xffffd8e7;
    }
    return avail;
}

// ---------------------------------------------------------------------------
// Address: 0x803966e0 | Name: trk_amc_flush_a | Date: 2026-09-14
// ---------------------------------------------------------------------------
int trk_amc_flush_a(void)
{
    FUN_803b8b84();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396704 | Name: trk_amc_flush_b | Date: 2026-09-14
// ---------------------------------------------------------------------------
int trk_amc_flush_b(void)
{
    FUN_803b8b88();
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396728 | Name: trk_amc_send | Date: 2026-09-14
// Transmit hook: when the transport is ready, services the AMC then transmits
// in chunks until the requested length is drained or a chunk transmits nothing.
// Returns 0 on success, 0xffffd8ef (-0x2711) when the transport is not ready.
// ---------------------------------------------------------------------------
unsigned int trk_amc_send(int param_1, int length)
{
    (void)param_1;
    unsigned int result;
    int sent;

    if (DAT_80613010 == 0) {
        FUN_80396f44();
        result = 0xffffd8ef;
    } else {
        FUN_80396f44();
        for (; 0 < length; length = length - sent) {
            FUN_80396f44();
            sent = FUN_803b8b7c();
            if (sent == 0) break;
        }
        result = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Address: 0x803967e8 | Name: trk_amc_recv | Date: 2026-09-14
// Receive hook: services the AMC and pumps RX until the ring holds at least the
// requested count, then drains that many bytes out of the ring into the caller.
// Returns 0 on success, -0x2711 when the transport is not ready, or the HAL
// error code from the pump loop.
// ---------------------------------------------------------------------------
int trk_amc_recv(unsigned int dst, unsigned int count)
{
    unsigned char stack_buf[2052];
    int status = 0;

    if (DAT_80613010 == 0) {
        status = -0x2711;
    } else {
        FUN_80396f44();
        while (FUN_80396bf0((int)&DAT_805f2150) < count) {
            status = 0;
            unsigned int avail = (unsigned int)FUN_803b8b6c();
            if (avail != 0 && (status = FUN_803b8b74()) == 0) {
                FUN_80396a98((int)&DAT_805f2150, (unsigned int)stack_buf, avail);
            }
        }
        if (status == 0) {
            trk_amc_ring_drain((unsigned int *)&DAT_805f2150, dst, count);
        } else {
            FUN_80396f44();
        }
    }
    return status;
}

// ---------------------------------------------------------------------------
// Address: 0x80396908 | Name: trk_amc_init | Date: 2026-09-14
// Brings up the AMC HAL and initialises the receive ring over the 0x800-byte
// backing store at DAT_805f1950.
// ---------------------------------------------------------------------------
int trk_amc_init(void)
{
    FUN_80396f44();
    FUN_803b8b64();
    FUN_80396f44();
    FUN_80396ba0((unsigned int *)&DAT_805f2150, &DAT_805f1950, 0x800);
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80396990 | Name: trk_amc_ring_drain | Date: 2026-09-14
// Copies `count` bytes out of the circular receive ring into `dst`, wrapping at
// the ring end, advancing the read cursor and adjusting the count/total fields.
// Returns 0 on success, 0xffffffff if the ring holds fewer than `count` bytes.
// Ring layout: [0]=read cursor [2]=base [3]=capacity [4]=count [5]=total,
// [6]=lock word.
// ---------------------------------------------------------------------------
unsigned int trk_amc_ring_drain(unsigned int *ring, unsigned int dst, unsigned int count)
{
    unsigned int result;

    if (ring[4] < count) {
        result = 0xffffffff;
    } else {
        FUN_80396fb8(ring + 6);
        unsigned int rd = *ring;
        unsigned int to_end = ring[3] - (rd - ring[2]);
        if (count < to_end) {
            leaf_31e8(dst, rd, count);
            *ring = *ring + count;
        } else {
            leaf_31e8(dst, rd, to_end);
            leaf_31e8(dst + to_end, ring[2], count - to_end);
            *ring = (ring[2] + count) - to_end;
        }
        if (ring[3] == *ring - ring[2]) {
            *ring = ring[2];
        }
        ring[5] = ring[5] + count;
        ring[4] = ring[4] - count;
        FUN_80396f94();
        result = 0;
    }
    return result;
}
