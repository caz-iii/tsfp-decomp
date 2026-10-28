// ARQ - ARAM Queue: request queue built on top of the DSP ARAM DMA engine.
// The DSP driver (audio/dsp.cpp) can only run one ARAM<->main-mem transfer at a
// time; ARQ serializes callers' transfers through a two-priority linked list
// (high list + low list) and chops each request into <= DAT_80613300-byte
// chunks via aram_dma_queue_advance / aram_dma_queue_service.
//
// Address: 0x803abde4..0x803abfb0 | Module: audio/arq.cpp | Date: 2026-09-15

#include "common.h"

// ---- ARQ request block (0x20 bytes) --------------------------------------
// [0] next pointer (link within its priority list)
// [1] prev pointer
// [2] transfer type/direction (0 = read to main mem, else ARAM write)
// [4] main-memory address
// [5] ARAM address
// [6] length in bytes
// [7] done-callback (defaults to the no-op at 0x803abd14)
struct ArqRequest {
    struct ArqRequest* next;   // [0]
    struct ArqRequest* prev;   // [1]
    int   type;                // [2]
    int   pad3;                // [3]
    u32   mmem_addr;           // [4]
    u32   aram_addr;           // [5]
    u32   length;              // [6]
    void* done_cb;             // [7]
};

// ---- SDK version registration -------------------------------------------
extern int os_register_version(u32, u32, u32, u32, u32, u32, u32, u32,
                               void*, u32, u32, u32, u32, u32, u32, u32);
extern void* DAT_80611480;        // "<< Dolphin SDK - ARQ\trelease build: ..."

// ---- DSP ARAM DMA driver hooks (audio/dsp.cpp) ---------------------------
extern void dsp_aram_dma_setup(int type, u32 addr_a, u32 addr_b, u32 length);
extern void aram_dma_queue_advance(void);
extern void aram_dma_queue_service(void);       // AI/ARAM DMA completion callback
extern int  ai_set_callback(void* cb);
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);

// ---- ARQ queue state -----------------------------------------------------
extern ArqRequest* DAT_806132e0;   // low-priority list head
extern ArqRequest* DAT_806132e4;   // low-priority list tail
extern ArqRequest* DAT_806132e8;   // high-priority list head
extern ArqRequest* DAT_806132ec;   // high-priority list tail
extern ArqRequest* DAT_806132f0;   // in-flight request
extern int         DAT_806132f4;   // busy flag
extern void*       DAT_806132f8;   // in-flight done-callback
extern int         DAT_806132fc;
extern u32         DAT_80613300;   // per-chunk max transfer size (0x1000)
extern int         DAT_80613304;   // one-time-init guard
extern int         DAT_803abd14;   // no-op done-callback target

// ARQInit: one-time init of the ARQ layer. Registers the ARQ SDK version,
// clears the pending lists and cursors, sets the default chunk size, and
// installs the ARAM DMA completion service routine.
void ARQInit(u32 p1, u32 p2, u32 p3, u32 p4, u32 p5, u32 p6, u32 p7, u32 p8,
             u32 p10, u32 p11, u32 p12, u32 p13, u32 p14, u32 p15, u32 p16) {
    if (DAT_80613304 != 1) {
        os_register_version(p1, p2, p3, p4, p5, p6, p7, p8, DAT_80611480,
                            p10, p11, p12, p13, p14, p15, p16);
        DAT_806132e8 = 0;      // high list head
        DAT_806132e0 = 0;      // low list head
        DAT_80613300 = 0x1000; // default chunk size
        ai_set_callback((void*)aram_dma_queue_service);
        DAT_806132f0 = 0;
        DAT_806132f4 = 0;
        DAT_806132f8 = 0;
        DAT_806132fc = 0;
        DAT_80613304 = 1;
    }
}

// ARQPostRequest: fill and enqueue an ARAM DMA request block. 'priority'
// selects the high (==1) or low (<=0) list; the block is appended at the
// list tail. If the engine is idle, the head request is kicked immediately.
void ARQPostRequest(ArqRequest* req, u32 type, u32 arg3, int priority,
                    u32 mmem, u32 aram, u32 length, void* done_cb) {
    ArqRequest* lo_head;
    ArqRequest* lo_tail;
    ArqRequest* hi_head;
    ArqRequest* hi_tail;

    req->next      = 0;
    req->type      = (int)type;
    req->pad3      = (int)arg3;
    req->mmem_addr = mmem;
    req->aram_addr = aram;
    req->length    = length;
    req->done_cb   = (done_cb == 0) ? (void*)&DAT_803abd14 : done_cb;

    os_disable_interrupts();
    hi_head = DAT_806132e8;
    hi_tail = DAT_806132ec;
    if (priority == 1) {
        lo_head = req;
        lo_tail = req;
        if (DAT_806132e0 != 0) {
            DAT_806132e4->next = req;
            lo_head = DAT_806132e0;
            lo_tail = req;
            hi_head = DAT_806132e8;
            hi_tail = DAT_806132ec;
        }
    } else {
        lo_head = DAT_806132e0;
        lo_tail = DAT_806132e4;
        if ((priority < 1) && (-1 < priority)) {
            hi_head = req;
            hi_tail = req;
            if (DAT_806132e8 != 0) {
                DAT_806132ec->next = req;
                lo_head = DAT_806132e0;
                lo_tail = DAT_806132e4;
                hi_head = DAT_806132e8;
                hi_tail = req;
            }
        }
    }
    DAT_806132ec = hi_tail;
    DAT_806132e8 = hi_head;
    DAT_806132e4 = lo_tail;
    DAT_806132e0 = lo_head;

    if ((DAT_806132f0 == 0) && (DAT_806132f4 == 0)) {
        if (DAT_806132e0 != 0) {
            if (DAT_806132e0->type == 0) {
                dsp_aram_dma_setup(0, DAT_806132e0->mmem_addr,
                                   DAT_806132e0->aram_addr, DAT_806132e0->length);
            } else {
                dsp_aram_dma_setup((short)DAT_806132e0->type,
                                   DAT_806132e0->aram_addr,
                                   DAT_806132e0->mmem_addr, DAT_806132e0->length);
            }
            DAT_806132f8 = DAT_806132e0->done_cb;
            DAT_806132f0 = DAT_806132e0;
            DAT_806132e0 = DAT_806132e0->next;
        }
        if (DAT_806132f0 == 0) {
            aram_dma_queue_advance();
        }
    }
    os_get_interrupts_enabled();
}
