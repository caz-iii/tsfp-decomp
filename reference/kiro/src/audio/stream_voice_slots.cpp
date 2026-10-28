// stream_voice_slots.cpp
// Streaming-voice slot manager: the mid layer between the high-level
// stream_voice_* API (stream_voice_system_init / stream_voice_spawn) and the
// low-level hardware AX/DSP voice layer (hwvoice_*).
//
// Owns the 8-entry voice-slot pool at DAT_805ac760 (stride 0x48). Per slot:
//   +0x1c  int   pMixVoiceA   (primary mixer voice handle, 0 = none)
//   +0x20  int   pMixVoiceB   (secondary mixer voice handle, 0 = none)
//   +0x24  float gain         (linear gain scalar)
//   +0x40  u16   flags        (bit 0x200 = active/gated)
//   +0x45  u8   state        (0 idle,1 fill,2 service,3 play,5 release,6/7 defer)
//   +0x46  u8   fadeCmd      (1 = request fade)
// A parallel scratch-buffer pointer table lives at DAT_805ac9a0 (8 x u32),
// each entry a 0x40000-byte streaming buffer reserved from the occ scratch pool.
//
// Batch: batch_489
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// occ scratch-pool reserve: (size, tag) -> buffer pointer.
extern void *occ_scratch_alloc(int size, int tag);

// Global mutex guard pair around slot-table mutation.
extern void FUN_8039cca0(void);   // lock
extern void FUN_8039ccc8(void);   // unlock

// Spawn-frame guard pairs (profiler / task frame markers). The *f0 variant
// returns a packed record: lo word = a value, hi word = slot index.
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void *FUN_803826dc(void);
extern void FUN_80382728(void);
extern void FUN_8039cca0(void);

// Low-level AX/DSP hardware voice layer (defined in hw_voice_stream.cpp).
extern int  hwvoice_find_free_slot(void);
extern void hwvoice_open(void *pSlot, void *pChannelBase, void *pAssetCtx,
                         void *pScratch, unsigned int rate, int prio,
                         int loopA, int loopB);
extern void hwvoice_apply_gain(int pSlot);
// Per-voice hardware state-machine service (advances one active slot per call).
extern unsigned long long hwvoice_service(unsigned long long a1, unsigned long long a2,
                                          unsigned long long a3, unsigned long long a4,
                                          unsigned long long a5, unsigned long long a6,
                                          unsigned long long a7, unsigned long long a8);

// Mixer voice property accessors (AX voice command queue).
extern void         FUN_803affbc(int pVoice, unsigned int routeIdx); // set route/pan
extern unsigned int FUN_803affe4(int pVoice);                        // get route/pan
extern void         FUN_803b0000(int pVoice, int level);             // set level 0..127

// Asset loading helpers.
extern unsigned int asset_load_async(double f1, unsigned long long a2, unsigned long long a3,
                                     unsigned long long a4, unsigned long long a5,
                                     unsigned long long a6, unsigned long long a7,
                                     unsigned long long a8, void *pName, int mode,
                                     const char *pSrcFile, int line, unsigned int flags,
                                     unsigned int a14, unsigned int a15, unsigned int a16);
extern int          asset_get_size(double f1, unsigned long long a2, unsigned long long a3,
                                   unsigned long long a4, unsigned long long a5,
                                   unsigned long long a6, unsigned long long a7,
                                   unsigned long long a8, void *pName);
extern unsigned int asset_pack_entry_is_ready(void);
extern int          asset_get_pack_entry_size(void);

// Compiler-runtime spill helpers (referenced by FUN_8018b514 via saved gpr).
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);

// memset shim.
extern void memset_wrapper(int dst, int val, int len);

// ---- Data ----
extern signed char   DAT_80612490;   // index of highest-priority streaming voice (-1 = none)
extern unsigned char DAT_80612491;   // page-DMA pending count / gate
extern unsigned char DAT_80612492;   // stream flag byte (bit0 = one slot in state 2)
extern int           DAT_805ac760;   // voice-slot pool base (stride 0x48)
extern int           DAT_805ac9a0;   // scratch-buffer pointer table (8 x u32)
extern int           DAT_805a45a0;   // AX channel descriptor base (L/mono)
extern unsigned int  DAT_8060f968;   // asset-loader reentrancy guard
extern char          s_musicGC_c_8041e7b8[]; // __FILE__ literal for asset_load_async
}

// Address of the voice-slot pool base (DAT_805ac760 == -0x7fa538a0).
static int *const kSlotPool = &DAT_805ac760;

// Byte views into a slot record.
static inline unsigned char *slot_state(int idx)   { return (unsigned char *)&DAT_805ac760 + idx * 0x48 + 0x45; }
static inline unsigned char *slot_fadecmd(int idx) { return (unsigned char *)&DAT_805ac760 + idx * 0x48 + 0x46; }
static inline int *slot_voiceA(int idx)  { return (int *)((char *)&DAT_805ac760 + idx * 0x48 + 0x1c); }
static inline int *slot_voiceB(int idx)  { return (int *)((char *)&DAT_805ac760 + idx * 0x48 + 0x20); }
static inline float *slot_gain(int idx)  { return (float *)((char *)&DAT_805ac760 + idx * 0x48 + 0x24); }
static inline unsigned short *slot_flags(int idx) { return (unsigned short *)((char *)&DAT_805ac760 + idx * 0x48 + 0x40); }
static inline unsigned char *slot_priority(int idx) { return (unsigned char *)&DAT_805ac760 + idx * 0x48 + 0x43; }

// ---------------------------------------------------------------------------
// Address: 0x8018b124 | Name: svslot_system_init | Date: 2026-09-06
// Reset the whole slot pool (0x240 bytes = 8 * 0x48), clear the page-DMA
// gates, then reserve 8 x 0x40000 streaming scratch buffers into the
// per-slot scratch-pointer table.
// ---------------------------------------------------------------------------
void svslot_system_init(void)
{
    FUN_803826f0();
    // -0x7fa538a0 == 0x805ac760 : zero the entire 8-slot pool.
    memset_wrapper((int)0x805ac760, 0, 0x240);
    DAT_80612492 = 0;
    DAT_80612491 = 0;

    int *pScratchTab = &DAT_805ac9a0;
    for (int i = 0; i < 8; i++) {
        pScratchTab[i] = (int)occ_scratch_alloc(0x40000, 1);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b19c | Name: svslot_open_stream | Date: 2026-09-06
// Allocate a free slot and open a streaming voice on it. The asset is loaded
// either asynchronously (when a name buffer is supplied) or resolved from an
// already-mounted pack entry. On success the hardware voice is opened against
// the slot's scratch buffer and the slot's gain / active flag are set.
// ---------------------------------------------------------------------------
void svslot_open_stream(unsigned long long param_1, unsigned long long param_2,
                        unsigned long long param_3, unsigned long long param_4,
                        unsigned long long param_5, unsigned long long param_6,
                        unsigned long long param_7, unsigned long long param_8,
                        unsigned int param_9, unsigned int param_10,
                        int loopA, int loopB, unsigned int rate,
                        unsigned int param_14, unsigned int param_15,
                        unsigned int param_16)
{
    void *pName = FUN_803826dc();
    unsigned int savedGuard = DAT_8060f968;

    int slot = hwvoice_find_free_slot();
    if (slot >= 0) {
        unsigned int assetCtx;   // asset ready/handle context
        int          assetSize;

        DAT_8060f968 = 0;
        if (pName == (void *)0x0) {
            assetCtx  = asset_pack_entry_is_ready();
            assetSize = asset_get_pack_entry_size();
        } else {
            assetCtx  = asset_load_async(0.0, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, pName, 0, s_musicGC_c_8041e7b8,
                                         0x6c3, rate, param_14, param_15, param_16);
            assetSize = asset_get_size(0.0, param_2, param_3, param_4, param_5, param_6,
                                       param_7, param_8, pName);
        }
        DAT_8060f968 = savedGuard;

        if (assetSize >= 0) {
            hwvoice_open((char *)&DAT_805ac760 + slot * 0x48, &DAT_805a45a0, &assetCtx,
                         (void *)(&DAT_805ac9a0)[slot], rate, 0x40, loopA, loopB);
            *slot_gain(slot) = 0.0f;
            *slot_flags(slot) = (unsigned short)(*slot_flags(slot) | 0x200);
        }
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b2d8 | Name: svslot_stop | Date: 2026-09-06
// Request a slot to stop: clear the global "servicing" bit if this slot was in
// state 2, move a live slot to the release state (5), and drop its active flag.
// ---------------------------------------------------------------------------
void svslot_stop(int slot)
{
    FUN_8039cca0();
    unsigned char *pState = slot_state(slot);
    if (*pState == 0x02) {
        DAT_80612492 = DAT_80612492 & 0xfe;
    }
    if (*pState != 0x00) {
        *pState = 0x05;
    }
    *slot_flags(slot) = (unsigned short)(*slot_flags(slot) & 0xfdff);
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b35c | Name: svslot_is_active | Date: 2026-09-06
// True while the slot's state byte is non-zero (any live state).
// ---------------------------------------------------------------------------
bool svslot_is_active(int slot)
{
    return (char)(1 - (*slot_state(slot) == 0x00)) != 0;
}

// ---------------------------------------------------------------------------
// Address: 0x8018b37c | Name: svslot_is_stream_done | Date: 2026-09-06
// True once the slot has left the fill(1)/service(2) states - i.e. the stream
// has finished buffering and is playing or has been released.
// ---------------------------------------------------------------------------
unsigned int svslot_is_stream_done(int slot)
{
    unsigned char st = *slot_state(slot);
    if (st != 0x01 && st != 0x02) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x8018b3ac | Name: svslot_route_to_current_player | Date: 2026-09-06
// Re-route both mixer voices of the slot (returned in the frame-guard packed
// record) to the currently-active player/output index, avoiding redundant
// writes when the route already matches.
// ---------------------------------------------------------------------------
void svslot_route_to_current_player(void)
{
    unsigned long long packed = FUN_803826f0();
    int route = (int)packed;
    int slotOff = (int)(packed >> 0x20) * 0x48;

    FUN_8039cca0();
    int *pVoiceA = (int *)((char *)&DAT_805ac760 + slotOff + 0x1c);
    int *pVoiceB = (int *)((char *)&DAT_805ac760 + slotOff + 0x20);

    if (*pVoiceA != 0 && route != (int)FUN_803affe4(*pVoiceA)) {
        FUN_803affbc(*pVoiceA, route);
    }
    if (*pVoiceB != 0 && route != (int)FUN_803affe4(*pVoiceB)) {
        FUN_803affbc(*pVoiceB, route);
    }
    FUN_8039ccc8();
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b448 | Name: svslot_set_level | Date: 2026-09-06
// Set the mixer level (0..127) on the slot's secondary voice, if present.
// ---------------------------------------------------------------------------
void svslot_set_level(int slot, int level)
{
    FUN_8039cca0();
    int voiceB = *slot_voiceB(slot);
    if (voiceB != 0) {
        FUN_803b0000(voiceB, level);
    }
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b4b0 | Name: svslot_set_gain | Date: 2026-09-06
// Store a new linear gain scalar on the slot and push it to the hardware voice.
// ---------------------------------------------------------------------------
void svslot_set_gain(double gain, int slot)
{
    FUN_8039cca0();
    *slot_gain(slot) = (float)gain;
    hwvoice_apply_gain((int)((char *)&DAT_805ac760 + slot * 0x48));
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b514 | Name: svslot_route_to_player | Date: 2026-09-06
// Route the slot's two mixer voices to explicit indices: the primary voice to
// the slot index carried in the saved-gpr frame record, the secondary voice to
// the caller-supplied route (param_3). Redundant writes are skipped.
// ---------------------------------------------------------------------------
void svslot_route_to_player(unsigned int param_1, unsigned int param_2, int routeB)
{
    unsigned long long packed = __save_gpr_28();
    int routeA = (int)packed;
    int slotOff = (int)(packed >> 0x20) * 0x48;

    FUN_8039cca0();
    int *pVoiceA = (int *)((char *)&DAT_805ac760 + slotOff + 0x1c);
    int *pVoiceB = (int *)((char *)&DAT_805ac760 + slotOff + 0x20);

    if (*pVoiceA != 0 && routeA != (int)FUN_803affe4(*pVoiceA)) {
        FUN_803affbc(*pVoiceA, routeA);
    }
    if (*pVoiceB != 0 && routeB != (int)FUN_803affe4(*pVoiceB)) {
        FUN_803affbc(*pVoiceB, routeB);
    }
    FUN_8039ccc8();
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b5b4 | Name: svslot_request_fade | Date: 2026-09-06
// Flag the slot's per-frame service state machine to run a fade (fadeCmd = 1).
// ---------------------------------------------------------------------------
void svslot_request_fade(int slot)
{
    *slot_fadecmd(slot) = 1;
}

// ---------------------------------------------------------------------------
// Address: 0x8018b5d0 | Name: svslot_request_restore | Date: 2026-09-06
// Flag the slot's per-frame service state machine to run a fade-restore
// (fadeCmd = 2). Companion to svslot_request_fade (fadeCmd = 1).
// ---------------------------------------------------------------------------
void svslot_request_restore(int slot)
{
    *slot_fadecmd(slot) = 2;
}

// ---------------------------------------------------------------------------
// Address: 0x8018b5ec | Name: svslot_service_all | Date: 2026-09-06
// Per-frame servicer for the whole voice-slot pool. First, unless a page-DMA
// refill is pending (DAT_80612491), pick the highest-priority streaming voice
// (lowest priority byte +0x43) among slots in play/service states that are not
// flagged 0x20, recording it in DAT_80612490. Then advance the hardware voice
// state machine (hwvoice_service) once for every active slot.
// ---------------------------------------------------------------------------
void svslot_service_all(unsigned long long param_1, unsigned long long param_2,
                        unsigned long long param_3, unsigned long long param_4,
                        unsigned long long param_5, unsigned long long param_6,
                        unsigned long long param_7, unsigned long long param_8)
{
    unsigned long long frame = FUN_803826f0();
    DAT_80612490 = -1;

    if (DAT_80612491 == 0) {
        unsigned char bestPri = 8;   // lower = higher priority; seed above max slot idx
        signed char   idx = 0;
        for (int i = 0; i < 8; i++) {
            unsigned char st = *slot_state(i);
            if (st == 0x02 || st == 0x03) {
                if ((*slot_flags(i) & 0x20) == 0 && *slot_priority(i) < bestPri) {
                    DAT_80612490 = idx;
                    bestPri = *slot_priority(i);
                }
            }
            idx = (signed char)(idx + 1);
        }
    }

    for (int i = 0; i < 8; i++) {
        if (*slot_state(i) != 0x00) {
            frame = hwvoice_service(frame, param_2, param_3, param_4,
                                    param_5, param_6, param_7, param_8);
        }
    }
    FUN_8038273c();
}
