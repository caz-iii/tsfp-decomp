// stream_request.cpp
// Async asset-streaming request pool. A fixed table of stream-request slots
// (base DAT_805ad480, stride 0x4518 bytes/slot, count DAT_80612518) tracks
// in-flight DVD/asset loads. The dispatcher (FUN_801a1758) claims a free slot,
// issues asset_load_async + dvd2_read_wrapper into it, and records the handle
// and reserved scratch. This file holds the slot allocator and the streaming
// subsystem lazy-init thunk.
//
// Slot state byte layout (offsets into a 0x4518-byte slot at DAT_805ad480):
//   [+0x00] : slot state (0 = free; 1 = claimed; 5 = load issued)
//   [+0x04] : in-use flag (DAT_805ad484)
//
// Address range: 0x801a15b4 - 0x801a15fc  (batch_524, subsystem:filesystem)
#include "common.h"

extern "C" {
extern int    DAT_805ad484;    // first slot's in-use flag (stride 0x4518 across slots)
extern int    DAT_80612518;    // stream-request slot count

// streaming lazy-init target (forwarded to by stream_system_lazy_init)
void FUN_801a2e90(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                  unsigned long long a4, unsigned long long a5, unsigned long long a6,
                  unsigned long long a7, unsigned long long a8, unsigned int a9,
                  unsigned int a10, char *a11, unsigned int a12, unsigned int a13,
                  unsigned int a14, unsigned int a15, unsigned int a16);
} // extern "C"

// ---------------------------------------------------------------------------
// stream_request_slot_find_free (FUN_801a15b4)
// Scan the stream-request slot table (DAT_80612518 slots, stride 0x4518 ints)
// for the first slot whose in-use flag (&DAT_805ad484)[slot*0x4518] is 0 and
// return its index, or -1 if all slots are busy.
// ---------------------------------------------------------------------------
int stream_request_slot_find_free(void) {
    int idx = 0;
    int remaining = DAT_80612518;
    if (0 < DAT_80612518) {
        do {
            if ((&DAT_805ad484)[idx * 0x4518] == 0) {
                return idx;
            }
            idx = idx + 1;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// stream_system_lazy_init (FUN_801a15fc)
// Thin forwarder to the streaming subsystem lazy-initializer (FUN_801a2e90),
// which on first call allocates the shared 0x2000 streaming buffer and sets the
// initialized guard. Passes the full 16-slot PowerPC argument frame through
// unchanged.
// ---------------------------------------------------------------------------
void stream_system_lazy_init(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                             unsigned long long a4, unsigned long long a5, unsigned long long a6,
                             unsigned long long a7, unsigned long long a8, unsigned int a9,
                             unsigned int a10, char *a11, unsigned int a12, unsigned int a13,
                             unsigned int a14, unsigned int a15, unsigned int a16) {
    FUN_801a2e90(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
}
