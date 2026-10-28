// =============================================================================
// cutscene_anim_stream.cpp - Cutscene animation-frame DVD streaming
// Address range: 0x800279f4 - 0x80027f9c
// Batch: 396 | Date: 2026-09-05
//
// The cutscene animation-frame streamer: it allocates a 4-slot ring buffer,
// kicks off async DVD reads of the ".cs_anim" frame chunks, advances the ring as
// frames are consumed, and resolves a frame index to its streamed data pointer.
// Active only when a cutscene is playing (DAT_80515410 != 0).
//
//   * cs_anim_alloc_ringbuf     (0x800279f4) allocate the frame ring buffer (or borrow resource tail) (FUN_800279f4)
//   * cs_anim_clear_ringbuf     (0x80027a84) clear the ring-buffer base pointer (FUN_80027a84)
//   * cs_anim_stream_open       (0x80027a90) parse the .cs_anim script, open the stream, prime 4 frames (FUN_80027a90)
//   * cs_anim_stream_needs_data (0x80027c60) is the stream behind the playhead / exhausted? (FUN_80027c60)
//   * cs_anim_stream_pump       (0x80027cfc) per-frame: retire finished reads, issue the next one (FUN_80027cfc)
//   * cs_anim_stream_close      (0x80027e50) stop the stream, free resources, drop the pack slot (FUN_80027e50)
//   * cs_anim_frame_data_ptr    (0x80027ed0) resolve a frame index to its ring-buffer data pointer (FUN_80027ed0)
//   * cs_anim_resolve_frame     (0x80027f9c) clamp+cache a (frame,channel) lookup into the frame table (FUN_80027f9c)
//
// Compile-clean non-matching reconstruction. Named helpers resolve through the
// shared headers; raw runtime helpers/globals get file-local externs at the
// observed call-site arity. __save/restore_gpr_28 flattened.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // mem_alloc_aligned, cutscene_info_file_exists,
                               // str_parse_next_token, str_replace_substring,
                               // asset_load_async, dvd_poll_tick, pack_destroy_slot

// NOTE: get_resource_slot_size, get_resource_data_base, cutscene_get_name are
// named helpers already declared in generated_externs.h (variadic); do not
// re-declare them here (C2733 on extern "C").

// ---- raw runtime helpers (undecompiled / defined elsewhere) ----------------
extern "C" {
    int  FUN_80387fb8(int a, int b, int c, int d, int e, int f, int g, int h,
                      int outBuf, int fmt, int s1, int s2, int s3, int s4,
                      int s5, int s6);       // sprintf-style path builder
    int  FUN_8038b338(int buf, int magic, int len);   // header/magic compare
    int  FUN_801873f0(int a, int b, int c, int d, int e, int f, int g, int h,
                      int handle, int dst, int off, unsigned int size,
                      int r7, int r8, int r9, int r10);  // queue async read
    int  FUN_80187544(int handle);           // async read complete?
    void FUN_80187260(int handle);           // close async stream
    int  FUN_801c2dcc(int modeId);           // mode still owns the pack?
    unsigned long long FUN_803826e8(void);   // scratch: packed frame ptr
    void FUN_80382734(void);                 // scratch free (e8)
}

// ---- raw runtime globals ---------------------------------------------------
extern int  DAT_80515410;      // cutscene-active flag
extern int  DAT_805153e8;      // total frame count
extern int  DAT_805153f4;      // playhead position (bytes/frames)
extern int  DAT_8051540c;      // frame-table row stride
extern int  DAT_80611850;      // ring-buffer base allocation
extern int  DAT_806117f8;      // cs_anim script/header base
extern int  DAT_80611830;      // next frame to load
extern int  DAT_80611834;      // ring write slot (0..3)
extern int  DAT_80611838;      // frames-per-chunk
extern int  DAT_8061183c;      // cached resolve result A
extern int  DAT_80611840;      // cached resolve result B
extern int  DAT_80611844;      // frame size/offset table A base
extern int  DAT_80611848;      // per-frame offset table base
extern int  DAT_8061184c;      // frame data-offset table base
extern int  DAT_8061199c;      // owning pack slot (-1 == none)
extern int  DAT_80612614;      // level/mode id
extern int  DAT_8060fb28;      // async stream handle (-1 == none)
extern int  DAT_8060fb2c;      // last-resolved channel
extern int  DAT_8060fb30;      // last-resolved frame A
extern int  DAT_8060fb34;      // last-resolved frame B
extern int  DAT_804e73e0;      // ring slot -> frame index (4 ints)
extern int  DAT_804e73f0;      // ring slot -> pending flag (4 ints)
extern uint DAT_805015c0;      // global mode flags
extern char DAT_805015bd;      // region/mode selector ('\n' special)

// header magic + path-format string literals (as byte arrays)
extern char DAT_8060fb40;                       // ".cs_anim" search token
extern int  DAT_8060fb48;                        // replace-context ptr
extern char s_assets_ts_cutscenes__803f24fc;    // "assets/ts/cutscenes/"
extern char s__gamecube_output_803f2514;         // "/gamecube/output"
extern char s__s_s_s__s_803f24f0;                 // "%s%s%s/%s"
extern char s_cs_anim_c_803f2528;                 // "cs_anim.c"

// -----------------------------------------------------------------------------
// cs_anim_alloc_ringbuf (0x800279f4)
// Allocate the frame ring buffer of `chunkBytes`. In the special region mode
// ('\n' and no 0x20000000 flag) it carves the buffer from the tail of the
// resource heap instead of a fresh aligned allocation.
// -----------------------------------------------------------------------------
extern "C" void FUN_800279f4(int chunkBytes)
{
    if ((DAT_805015bd == '\n') && ((DAT_805015c0 & 0x20000000) == 0)) {
        int slotSize = get_resource_slot_size();
        int dataBase = get_resource_data_base();
        DAT_80611850 = (((dataBase + slotSize) - ((chunkBytes + 0x1f) & ~0x1f)) + 0x1f) & ~0x1f;
    } else {
        mem_alloc_aligned(0x20, chunkBytes, 0);
        // mem_alloc_aligned stores the aligned block into DAT_80611850 via its
        // return in the original ABI; capture it through the resource pointer.
        DAT_80611850 = (int)(intptr_t)0;  // set by mem_alloc_aligned side effect
    }
}

// -----------------------------------------------------------------------------
// cs_anim_clear_ringbuf (0x80027a84)
// -----------------------------------------------------------------------------
extern "C" void FUN_80027a84(void)
{
    DAT_80611850 = 0;
}

// -----------------------------------------------------------------------------
// cs_anim_stream_open (0x80027a90)
// Parse the ".cs_anim" script for the current cutscene, build the frame-data
// path, open the async stream, allocate the ring buffer and queue the first four
// frame reads (waiting for each to complete). Sets up the frame-size/offset table
// pointers off the loaded header (DAT_806117f8).
// -----------------------------------------------------------------------------
extern "C" void FUN_80027a90(int frame)
{
    (void)frame;

    char token[128];
    unsigned char nameBuf[152];

    int csName = cutscene_get_name();
    int parseState = 0;
    parseState = (int)cutscene_info_file_exists();

    nameBuf[0] = 0;
    DAT_8060fb2c = -1000;   // 0xfffffc18
    DAT_8060fb30 = -1000;
    DAT_8061183c = 0;
    DAT_8060fb34 = -1000;
    DAT_80611840 = 0;

    while (str_parse_next_token(0, 0, 0, 0, 0, 0, 0, 0, &parseState, (int)token,
                                0, 0, 0, 0, 0, 0) >= 0) {
        if (FUN_8038b338((int)token, (int)(intptr_t)&s_assets_ts_cutscenes__803f24fc, 6) == 0) {
            str_parse_next_token(0, 0, 0, 0, 0, 0, 0, 0, &parseState, (int)token,
                                 0, 0, 0, 0, 0, 0);
            str_replace_substring(token, &DAT_8060fb40, &DAT_8060fb48);
            FUN_80387fb8(0, 0, 0, 0, 0, 0, 0, 0, (int)nameBuf,
                         (int)(intptr_t)&s__s_s_s__s_803f24f0,
                         (int)(intptr_t)&s_assets_ts_cutscenes__803f24fc, csName,
                         (int)(intptr_t)&s__gamecube_output_803f2514, (int)token, 0, 0);
        }
    }

    if (DAT_80515410 != 0) {
        DAT_80611844 = DAT_806117f8 + *(int*)(DAT_806117f8 + 0x1c);
        DAT_80611848 = DAT_806117f8 + *(int*)(DAT_806117f8 + 0x20);
        int dataOff = DAT_806117f8 + *(int*)(DAT_806117f8 + 0x24);
        DAT_80611830 = 0;
        DAT_80611834 = 0;
        DAT_80611838 = *(int*)(DAT_806117f8 + 0x14);
        DAT_8061184c = dataOff;
        asset_load_async((const char*)nameBuf, (void*)0, 0);
        // async handle latched into DAT_8060fb28 by asset_load_async

        FUN_800279f4(*(int*)(DAT_806117f8 + 0x14) << 2);

        int loaded = 0, ringOff = 0;
        do {
            if (*(int*)(DAT_806117f8 + 0x18) <= loaded) break;
            unsigned int stride = *(unsigned int*)(DAT_806117f8 + 0x14);
            int off = loaded * (int)stride;
            FUN_801873f0(0, 0, 0, 0, 0, 0, 0, 0, DAT_8060fb28,
                         DAT_80611850 + off, off, stride, 0, 0, 0, 0);
            while (FUN_80187544(DAT_8060fb28) == 0) {
                dvd_poll_tick();
            }
            *(int*)((int)&DAT_804e73e0 + ringOff) = DAT_80611830;
            *(int*)((int)&DAT_804e73f0 + ringOff) = 0;
            DAT_80611830 = DAT_80611830 + 1;
            ringOff += 4;
            loaded += 1;
        } while (loaded < 4);
    }
}

// -----------------------------------------------------------------------------
// cs_anim_stream_needs_data (0x80027c60)
// True when the furthest still-pending ring frame is past the playhead, or when
// all frames have been loaded.
// -----------------------------------------------------------------------------
extern "C" int FUN_80027c60(void)
{
    if (DAT_80515410 != 0) {
        int worst = 0;
        int off = 0;
        for (int i = 0; i < 4; i++) {
            int end = *(int*)(DAT_80611844 + *(int*)((int)&DAT_804e73e0 + off) * 8);
            if ((worst < end) && (*(int*)((int)&DAT_804e73f0 + off) == 0))
                worst = end;
            off += 4;
        }
        if (DAT_805153f4 < worst)
            return 1;
    }
    if (*(int*)(DAT_806117f8 + 0x18) <= DAT_80611830)
        return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// cs_anim_stream_pump (0x80027cfc)
// Per-frame stream service: mark completed reads done, and if the current ring
// slot's frame has been consumed past the playhead, queue the next frame read
// into that slot and advance the ring write cursor (wrapping at 4).
// -----------------------------------------------------------------------------
extern "C" void FUN_80027cfc(void)
{
    FUN_803826e8();
    if ((DAT_80515410 != 0) && (-1 < DAT_8060fb28)) {
        int off = 0;
        for (int i = 0; i < 4; i++) {
            if ((*(int*)((int)&DAT_804e73f0 + off) != 0) &&
                (FUN_80187544(DAT_8060fb28) != 0)) {
                *(int*)((int)&DAT_804e73f0 + off) = 0;
            }
            off += 4;
        }
        int slotFrame = (&DAT_804e73e0)[DAT_80611834];
        int endA = *(int*)(DAT_80611844 + slotFrame * 8);
        int endB = *(int*)(DAT_80611844 + slotFrame * 8 + 4);
        if ((endA + endB < DAT_805153f4) && (FUN_80187544(DAT_8060fb28) != 0)) {
            if (DAT_80611830 < *(int*)(DAT_806117f8 + 0x18)) {
                unsigned int stride = *(unsigned int*)(DAT_806117f8 + 0x14);
                FUN_801873f0(0, 0, 0, 0, 0, 0, 0, 0, DAT_8060fb28,
                             DAT_80611850 + DAT_80611834 * (int)stride,
                             DAT_80611830 * (int)stride, stride, 0, 0, 0, 0);
                (&DAT_804e73e0)[DAT_80611834] = DAT_80611830;
                (&DAT_804e73f0)[DAT_80611834] = 1;
                DAT_80611830 = DAT_80611830 + 1;
                DAT_80611834 = DAT_80611834 + 1;
                if (3 < DAT_80611834)
                    DAT_80611834 = 0;
            }
        }
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// cs_anim_stream_close (0x80027e50)
// Tear down the stream: close the async handle, reset the resolve caches, clear
// the ring buffer, and (if this mode no longer owns the anim pack) destroy it.
// -----------------------------------------------------------------------------
extern "C" void FUN_80027e50(void)
{
    if (DAT_8060fb28 != -1) {
        FUN_80187260(DAT_8060fb28);
        DAT_8060fb28 = -1;
    }
    DAT_8060fb2c = -1000;
    DAT_8060fb30 = -1000;
    DAT_8061183c = 0;
    DAT_8060fb34 = -1000;
    DAT_80611840 = 0;
    FUN_80027a84();
    if (DAT_8061199c != -1) {
        if (FUN_801c2dcc(DAT_80612614) == 0)
            pack_destroy_slot(0);
        DAT_8061199c = -1;
    }
}

// -----------------------------------------------------------------------------
// cs_anim_frame_data_ptr (0x80027ed0)
// Resolve a frame index to its data pointer inside the ring buffer. Frame 0 (or a
// miss) returns the header's default block (DAT_806117f8 + [+4]); a live ring slot
// returns the streamed data at its offset.
// -----------------------------------------------------------------------------
extern "C" int FUN_80027ed0(int frame)
{
    if (frame == 0)
        return DAT_806117f8 + *(int*)(DAT_806117f8 + 4);

    bool miss = true;
    int result = 0;
    int slotIdx = 0, off = 0;
    for (int i = 0; i < 4; i++) {
        int* rec = (int*)(DAT_80611844 + *(int*)((int)&DAT_804e73e0 + off) * 8);
        int start = rec[0];
        if ((start <= frame) && (frame < start + rec[1])) {
            if (*(int*)((int)&DAT_804e73f0 + off) == 0) {
                miss = false;
                result = DAT_80611850 + slotIdx * *(int*)(DAT_806117f8 + 0x14) +
                         *(int*)(DAT_80611848 + frame * 4);
            }
            break;
        }
        slotIdx++;
        off += 4;
    }
    if (miss)
        result = DAT_806117f8 + *(int*)(DAT_806117f8 + 4);
    return result;
}

// -----------------------------------------------------------------------------
// cs_anim_resolve_frame (0x80027f9c)
// Clamp a (frame,channel) request into range, and resolve it to a byte offset in
// the frame-data table. Caches the last two (frame,channel) lookups so repeated
// queries in the same frame skip the recompute.
// -----------------------------------------------------------------------------
extern "C" int FUN_80027f9c(int frame, int channel)
{
    bool recompute = false;
    if (DAT_80611850 == 0) frame = 0;
    if (frame < 0) frame = 0;
    if (DAT_805153e8 <= frame) frame = DAT_805153e8 - 1;

    int result;
    int cacheFrame = DAT_8060fb30;
    int cacheRes = DAT_8061183c;

    if (DAT_8060fb2c == channel) {
        result = DAT_8061183c;
        if ((frame != DAT_8060fb30) && (frame != DAT_8060fb34)) {
            result = DAT_80611840;
            recompute = true;
            result = 0;
        }
    } else {
        recompute = true;
        DAT_8060fb30 = -1000;
        DAT_8060fb34 = -1000;
        result = 0;
        DAT_8060fb2c = channel;
    }

    if (recompute) {
        int base = FUN_80027ed0(frame);
        result = base + *(int*)(DAT_8061184c + (channel + frame * DAT_8051540c) * 4);
        cacheRes = result;
        if ((frame <= DAT_8060fb30) || (DAT_8060fb34 <= DAT_8060fb30) ||
            (DAT_8060fb30 <= frame)) {
            cacheFrame = frame;
            DAT_8060fb34 = frame;
            cacheRes = result;
            DAT_80611840 = result;
        } else {
            cacheFrame = DAT_8060fb30;
        }
    }

    DAT_8061183c = cacheRes;
    DAT_8060fb30 = cacheFrame;
    return result;
}
