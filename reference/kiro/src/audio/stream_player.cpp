// stream_player.cpp
// Streaming audio/animation player. Plays a DVD-streamed compressed audio track
// (the "VCB" anim/spectral codec, see anim_codec.cpp) by double-buffering the
// disc read, decoding blocks frame-by-frame, converting the decoded float
// samples to 16-bit PCM, and feeding them to the low-level voice mixer
// (FUN_803af1xx / FUN_803ac3xx / FUN_803aexx audio-driver API).
//
// A fixed pool of stream-request slots (base DAT_805ad480, stride 0x4518 bytes)
// each hold a full request context. Per-slot sub-structures are addressed as
// absolute-address computations (slotOff + fixed base), e.g.:
//   slotOff + 0x805be618 : msg packet         (msg_packet_init / _free)
//   slotOff + 0x805be638 : msg stream         (msg_stream_init / _free)
//   slotOff + 0x805be7d0 : anim decode ctx    (anim_decoder_begin, etc.)
//   slotOff + 0x805be7b0 : anim channel ctx
// Per-slot scalar fields live at (&DAT_805ad4xx)[slot*0x4518] and
// (&DAT_805be5xx)[slot*0x4518] (int-indexed, stride 0x4518 ints).
//
// Entry points:
//   stream_player_init            (0x801a1698)  <- music_track_play
//   stream_request_begin          (0x801a1758)  claim slot + kick async load
//   stream_request_pump_header    (0x801a1884)  parse header, prime decode
//   stream_player_decode_mix      (0x801a1b80)  per-frame decode -> PCM -> mix
//   stream_request_close          (0x801a1fec)  <- (indirect) shutdown
//   stream_voice_start            (0x801a2154)  allocate + configure a voice
//   stream_loop_check             (0x801a225c)  loop-point detection
//
// Address range: 0x801a161c - 0x801a225c  (batch_525, subsystem:audio)
#include "common.h"

extern "C" {
// --- per-slot / player state globals (stride 0x4518 ints where indexed) ---
extern int DAT_805ad480;   // slot state (0 free / 1 ready / 5 loading)
extern int DAT_805ad484;   // slot in-use flag
extern int DAT_805ad48c;   // DVD async handle
extern int DAT_805ad490;   // current file read offset
extern int DAT_805ad494;   // total asset size
extern int DAT_805ad498;   // last read byte count
extern int DAT_805ad49c;   // frames per 0x1000 block
extern int DAT_805ad4a0;   // slot aux field
extern int DAT_805ad4a8;   // loop restart offset (-1 = none)
extern int DAT_805ad4ac;   // header-parsed flag
extern int DAT_805ad4b0;   // buffered-block count
extern int DAT_805ad4b4;   // paused flag
extern int DAT_805ad4b8;   // callback-set flag
extern int DAT_805ad4bc;   // callback context a
extern int DAT_805ad4c0;   // callback context b
extern int DAT_805ad4c4;   // decode-armed flag
extern int DAT_805ad4c8;   // end-of-stream flag
extern int DAT_805ad4cc;   // decode-budget-exhausted flag
extern int DAT_805ad4d0;   // frames decoded counter
extern int DAT_805ad4d4;   // active/started flag
extern unsigned int DAT_805ad4dc;  // loop last-position
extern unsigned int DAT_805ad4e4;  // loop prev-position
extern int DAT_805be580;   // PCM ring write cursor (frames)
extern int DAT_805be584;   // voice handle A
extern int DAT_805be588;   // voice handle B
extern int DAT_805be58c;   // per-slot scratch page A
extern int DAT_805be590;   // per-slot scratch page B
extern int DAT_805be594;   // current read dest buffer
extern int DAT_805be598;   // completion callback record
extern int DAT_805be7d4;   // channel count
extern int DAT_805be7d8;   // channel sample rate
extern int DAT_805be7f0;   // per-slot decoded channel state array
extern int DAT_80612518;   // active slot count
extern int DAT_80612374;   // fast-close guard (skip DVD drain when set)
extern int DAT_8061251c;   // last frame time-base sample
extern int DAT_806108e0;   // current frame time-base sample
extern int DAT_806109c0;   // stream sample rate
extern int DAT_800000f8;   // CPU/bus clock (for time-base -> usec)
extern float  FLOAT_80616b10;
extern float  FLOAT_80616b14;
extern float  FLOAT_80616b18;
extern float  FLOAT_80616b1c;
extern float  FLOAT_80616b20;
extern double DOUBLE_80616b28;   // int->double bias subtrahend
extern double DOUBLE_80616b30;   // int->double bias subtrahend

extern int  occ_scratch_alloc(int size, int flag);         // per-slot scratch alloc
extern void memset_wrapper(int dst, int val, int len);
extern int  leaf_31e8(unsigned int dst, unsigned int src, int len); // memmove

// scratch-pool / stream-init (batch_524)
extern unsigned long long anim_scratch_pool_init(unsigned long long a1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5, unsigned long long a6,
    unsigned long long a7, unsigned long long a8, int slotCount, int p10, char *p11, int p12,
    int p13, int p14, int p15, int p16);
extern int  anim_scratch_pool_alloc_pages(int slotCount);
extern void stream_system_lazy_init(unsigned long long a1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5, unsigned long long a6,
    unsigned long long a7, unsigned long long a8, int a9, int a10, char *a11, int a12, int a13,
    int a14, int a15, int a16);
extern int  stream_request_slot_find_free(void);

// --- DVD / async asset loader ---
extern int  asset_get_size(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, unsigned char *path);
extern int  asset_load_async(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, unsigned char *path, int p10, void *p11, int p12, int r7, int r8,
    int r9, int r10);
extern void dvd2_read_wrapper(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, int handle, int dst, int off, int len, int r7, int r8, int r9, int r10);
extern int  dvd2_handle_is_busy(int handle);
extern void dvd2_handle_close(int handle);
extern void *DAT_80610b28;   // async-load completion callback

// --- message packet / stream / codec context callees ---
extern void msg_packet_init(int pkt);
extern void msg_packet_free(int *pkt);
extern void msg_cursor_advance(int pkt, int n);
extern int  msg_recv_pump(int pkt);
extern void msg_parse_header(void);
extern int  msg_scratch_reserve(void);
extern int  msg_stream_consume_chunk(int *stream, int *outChunk);
extern void msg_stream_init(unsigned int *stream, unsigned int hdr);
extern void msg_stream_free(int *stream);
extern void msg_channel_init(int *decoder, int chanCtx);
extern unsigned int bitreader_hdr_field_e(int *br);
extern unsigned char bitreader_hdr_flag2(int *br);

extern void anim_ctx_init_0xe80(int ctx);
extern void anim_ctx_zero_0x10(int ctx);
extern void anim_ctx_destroy(void);
extern void anim_ctx_free_scratch(void);
extern void anim_subctx_destroy(void);
extern void anim_decoder_begin(int decoder, int ctx);
extern void anim_decoder_free(int decoder);
extern int  anim_stream_dispatch_block(int ctx, int chanState, unsigned int *chunk);
extern int  anim_decode_next_block(void);
extern void anim_decode_step(void);
extern int  anim_decode_get_output(int decoder, int *outRows);
extern void anim_decode_skip(int decoder, int frames);

// --- low-level voice / OS driver ---
extern int  os_get_time_base(void);
extern int  audio_linear_to_centibels(int lin);
extern int *FUN_803ac3a8(unsigned int a, int b, int c);        // voice alloc
extern void FUN_803af120(int voice, unsigned int a2, int a3, int a4, int a5, int a6, int a7, int a8); // voice config
extern void FUN_803aeb08(int voice, unsigned int *params);     // voice params (pan/pitch)
extern void FUN_803aedb0(int voice, void *params);             // voice mix params
extern void FUN_803aea60(int voice, short v);                  // voice stop/loop set
extern int  FUN_803a4db4(void);                                // dvd status poll
extern void FUN_803aff9c(int handle);                          // voice free A
extern void FUN_803ac328(int *handle);                         // voice free B

// --- GPR save/restore prologue thunks + misc helpers ---
extern int  __save_gpr_25(void);       // -> (r3:r4) completion-flag : path pair
extern void __restore_gpr_25(void);
extern void FUN_8039cca0(void);        // audio-driver interrupt disable
extern void FUN_8039ccc8(void);        // audio-driver interrupt enable
extern void FUN_8038b510(int *dst, int *src);  // copy callback record

// --- register-pair carriers / spill restores ---
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);
extern unsigned long long FUN_803826d4(void);
extern void FUN_80382720(void);
extern unsigned long long FUN_803826e8(void);
extern void FUN_80382734(void);

// --- forward-referenced helpers defined in later batches ---
extern void FUN_801a2318(void);
extern void FUN_801a2c4c(void);
extern int  FUN_801a23c4(void);
extern int  FUN_801a272c(void);

// forward decls within this TU
void stream_player_pool_init_slots(unsigned long long a1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5, unsigned long long a6,
    unsigned long long a7, unsigned long long a8, int a9, int a10, char *a11, int a12, int a13,
    int a14, int a15, int a16);
} // extern "C"

// slot sub-structure absolute-address bases (slotOff + base). These reproduce
// the decompiler's `iVar + <negative const>` = `iVar + <base>` addressing.
static const unsigned int SLOT_MSG_PKT     = 0x805be618u; // -0x7fa419e8
static const unsigned int SLOT_MSG_STREAM  = 0x805be638u; // -0x7fa419c8
static const unsigned int SLOT_HDR_BR      = 0x805be7a0u; // -0x7fa41860
static const unsigned int SLOT_ANIM_CTX    = 0x805be7d0u; // -0x7fa41830
static const unsigned int SLOT_CHUNK       = 0x805be7b0u; // -0x7fa41850
static const unsigned int SLOT_ANIM_DEC    = 0x805be800u; // -0x7fa41800
static const unsigned int SLOT_ANIM_CHAN   = 0x805be870u; // -0x7fa41790
static const unsigned int SLOT_READ_DST    = 0x805ad580u; // -0x7fa52a80
static const unsigned int SLOT_PCM_SCRATCH = 0x805ad480u; // -0x7fa52b80 base region

// ---------------------------------------------------------------------------
// stream_player_pool_init_slots (0x801a161c)
// Per-slot init loop: for each of the DAT_80612518 slots allocate two scratch
// pages (occ_scratch_alloc 0x32020 each) into the per-slot page fields, then
// initialize the shared codec scratch pool (anim_scratch_pool_init) and the
// streaming subsystem (stream_system_lazy_init). (r3:r4 carrier via FUN_803826f0.)
// ---------------------------------------------------------------------------
void stream_player_pool_init_slots(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    int param_9, int param_10, char *param_11, int param_12, int param_13,
    int param_14, int param_15, int param_16) {
    (void)param_1; (void)param_9; (void)param_10;
    FUN_803826f0();
    for (int i = 0; i < DAT_80612518; i = i + 1) {
        DAT_805be58c = occ_scratch_alloc(0x32020, 0);
        DAT_805be590 = occ_scratch_alloc(0x32020, 0);
    }
    unsigned long long pagePair = (unsigned long long)(unsigned int)DAT_805be590;
    unsigned long long r = anim_scratch_pool_init(param_1, param_2, param_3, param_4, param_5,
        param_6, param_7, param_8, DAT_80612518, (int)pagePair, param_11, param_12, param_13,
        param_14, param_15, param_16);
    stream_system_lazy_init(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
        (int)(r >> 0x20), (int)r, param_11, param_12, param_13, param_14, param_15, param_16);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// stream_player_init (0x801a1698)  <- music_track_play
// Clear the shared player state (0x11460 bytes), reserve the data-page arena
// for `slotCount` slots (anim_scratch_pool_alloc_pages), record the slot count
// (DAT_80612518), then run the per-slot init (stream_player_pool_init_slots).
// ---------------------------------------------------------------------------
void stream_player_init(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    int slotCount, int param_10, int param_11, int param_12, int param_13,
    int param_14, int param_15, int param_16) {
    (void)param_10; (void)param_11;
    char *pageArg = (char *)0x11460;
    memset_wrapper((int)SLOT_PCM_SCRATCH - 0x1000, 0, 0x11460);
    int pages = anim_scratch_pool_alloc_pages(slotCount);
    DAT_80612518 = slotCount;
    stream_player_pool_init_slots(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
        param_8, (int)((unsigned int)pages >> 0x20 & 0), (int)pages, pageArg, param_12, param_13,
        param_14, param_15, param_16);
}

// ---------------------------------------------------------------------------
// stream_player_set_callback (0x801a16ec)
// Register the stream completion callback: copy the callback record
// (FUN_8038b510 into DAT_805be598), store its two context words, and raise the
// callback-set flag DAT_805ad4b8. Always returns 0.
// ---------------------------------------------------------------------------
int stream_player_set_callback(int *cb, int ctxA, int ctxB) {
    FUN_8038b510((int *)&DAT_805be598, cb);
    DAT_805ad4bc = ctxA;
    DAT_805ad4b8 = 1;
    DAT_805ad4c0 = ctxB;
    return 0;
}

// ---------------------------------------------------------------------------
// stream_request_begin (0x801a1758)
// Claim a free stream-request slot, clear its context, mark it in-use, init its
// msg packet, query the asset size (asset_get_size) and kick the async load
// (asset_load_async) + first double-buffered DVD read (dvd2_read_wrapper).
// Marks the slot state 5 (loading) and primes the header pump. (r3:r4 via
// __save_gpr_25 carries the completion flag / path pointer pair.)
// ---------------------------------------------------------------------------
void stream_request_begin(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8) {
    unsigned long long saved = __save_gpr_25();
    unsigned char *path = (unsigned char *)(unsigned int)(saved >> 0x20);
    int completed = (int)saved;

    int slot = stream_request_slot_find_free();
    int off = slot * 0x11460;
    int pageA = (&DAT_805be58c)[slot * 0x4518];
    int pageB = (&DAT_805be590)[slot * 0x4518];
    memset_wrapper((int)(&DAT_805ad480 + slot * 0x4518), 0, 0x11460);
    (&DAT_805be58c)[slot * 0x4518] = pageA;
    (&DAT_805be590)[slot * 0x4518] = pageB;
    (&DAT_805ad484)[slot * 0x4518] = 1;

    msg_packet_init(off + (int)SLOT_MSG_PKT);
    int size = asset_get_size(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, path);
    *(int *)(&DAT_805ad494 + off) = size;
    FUN_8039cca0();
    int handle = asset_load_async(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                  param_8, path, 0, &DAT_80610b28, 0xef, 0, 0, 0, 0);
    *(int *)(&DAT_805ad48c + off) = handle;
    int dst = msg_scratch_reserve();
    *(int *)(&DAT_805be594 + off) = dst;
    dvd2_read_wrapper(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                      *(int *)(&DAT_805ad48c + off), off + (int)SLOT_READ_DST,
                      *(int *)(&DAT_805ad490 + off), 0x1000, 0, 0, 0, 0);
    FUN_8039ccc8();
    if (completed == 0) {
        *(int *)(&DAT_805ad4a8 + off) = 0;
    } else {
        *(int *)(&DAT_805ad4a8 + off) = 1;
    }
    (&DAT_805ad480)[slot * 0x4518] = 5;
    FUN_801a2318();
    FUN_801a2c4c();
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// stream_request_pump_header (0x801a1884)
// Advance a loading request: once the DVD read completes (dvd2_handle_is_busy),
// copy the just-read 0x1000-byte block into the msg buffer, advance cursors,
// and on the first pass parse the header (msg_recv_pump / msg_parse_header),
// init the msg stream + anim ctx, and dispatch the initial blocks. Continues
// pumping until two blocks are buffered, then begins the decoder
// (anim_decoder_begin / msg_channel_init) and marks the slot ready (state 1).
// Kicks the next double-buffered DVD read each pass.
// ---------------------------------------------------------------------------
void stream_request_pump_header(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    int slot) {
    int off = slot * 0x11460;
    if (dvd2_handle_is_busy(*(int *)(&DAT_805ad48c + off)) == 0) { return; }

    leaf_31e8(*(unsigned int *)(&DAT_805be594 + off), (unsigned int)(off + (int)SLOT_READ_DST), 0x1000);
    *(int *)(&DAT_805ad498 + off) = 0x1000;
    *(int *)(&DAT_805ad490 + off) = *(int *)(&DAT_805ad490 + off) + 0x1000;
    if (*(int *)(&DAT_805ad494 + off) < *(int *)(&DAT_805ad490 + off)) {
        *(int *)(&DAT_805ad498 + off) = *(int *)(&DAT_805ad494 + off) - *(int *)(&DAT_805ad490 + off);
    }
    if (*(int *)(&DAT_805ad498 + off) < 0) { *(int *)(&DAT_805ad498 + off) = 0; }
    msg_cursor_advance(off + (int)SLOT_MSG_PKT, *(int *)(&DAT_805ad498 + off));

    if (*(int *)(&DAT_805ad4ac + off) == 0) {
        msg_recv_pump(off + (int)SLOT_MSG_PKT);
        unsigned int hdr = bitreader_hdr_field_e((int *)(off + (int)SLOT_HDR_BR));
        msg_stream_init((unsigned int *)(off + (int)SLOT_MSG_STREAM), hdr);
        anim_ctx_init_0xe80(off + (int)SLOT_ANIM_CTX);
        anim_ctx_zero_0x10((int)(&DAT_805be7f0 + off));
        msg_parse_header();
        msg_stream_consume_chunk((int *)(off + (int)SLOT_MSG_STREAM), (int *)(off + (int)SLOT_CHUNK));
        anim_stream_dispatch_block(off + (int)SLOT_ANIM_CTX, (int)(&DAT_805be7f0 + off),
                                   (unsigned int *)(off + (int)SLOT_CHUNK));
        *(int *)(&DAT_805ad4b0 + off) = 0;
    }

    if (*(int *)(&DAT_805ad4b0 + off) < 2) {
        int r;
        while ((*(int *)(&DAT_805ad4b0 + off) < 2) &&
               ((r = msg_recv_pump(off + (int)SLOT_MSG_PKT)) != 0)) {
            if (r == 1) {
                msg_parse_header();
                while ((*(int *)(&DAT_805ad4b0 + off) < 2) &&
                       (msg_stream_consume_chunk((int *)(off + (int)SLOT_MSG_STREAM),
                                                 (int *)(off + (int)SLOT_CHUNK)) != 0)) {
                    anim_stream_dispatch_block(off + (int)SLOT_ANIM_CTX, (int)(&DAT_805be7f0 + off),
                                               (unsigned int *)(off + (int)SLOT_CHUNK));
                    *(int *)(&DAT_805ad4b0 + off) = *(int *)(&DAT_805ad4b0 + off) + 1;
                }
            }
        }
        int dst = msg_scratch_reserve();
        *(int *)(&DAT_805be594 + off) = dst;
        FUN_8039cca0();
        dvd2_read_wrapper(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                          *(int *)(&DAT_805ad48c + off), off + (int)SLOT_READ_DST,
                          *(int *)(&DAT_805ad490 + off), 0x1000, 0, 0, 0, 0);
        FUN_8039ccc8();
        *(int *)(&DAT_805ad4ac + off) = 1;
    } else {
        int *ch = *(int **)(&DAT_805be7f0 + off);
        while (*ch != 0) { ch = ch + 1; }
        *(int *)(&DAT_805ad49c + off) = 0x1000 / *(int *)(&DAT_805be7d4 + off);
        anim_decoder_begin(off + (int)SLOT_ANIM_DEC, off + (int)SLOT_ANIM_CTX);
        msg_channel_init((int *)(off + (int)SLOT_ANIM_DEC), off + (int)SLOT_ANIM_CHAN);
        (&DAT_805ad480)[slot * 0x4518] = 1;
        if (*(int *)(&DAT_805ad4a8 + off) == 0) {
            *(int *)(&DAT_805ad4a8 + off) = -1;
        } else {
            *(int *)(&DAT_805ad4a8 + off) = *(int *)(&DAT_805ad490 + off);
        }
        FUN_8039cca0();
        dvd2_read_wrapper(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                          *(int *)(&DAT_805ad48c + off), off + (int)SLOT_READ_DST,
                          *(int *)(&DAT_805ad490 + off), 0x1000, 0, 0, 0, 0);
        FUN_8039ccc8();
        *(int *)(&DAT_805ad4c4 + off) = 0;
    }
}

// ---------------------------------------------------------------------------
// stream_player_decode_mix (0x801a1b80)
// The per-frame streaming decode/mix pump for one slot (slot arrives via the
// r3:r4 carrier FUN_803826d4). While the slot is active and not end-of-stream:
// pull/parse the next chunk, decode a block (anim_decode_next_block +
// anim_decode_step), then drain decoded float rows (anim_decode_get_output),
// convert each sample to clamped int16 PCM into the slot's ring region, and
// track how many frames were produced against the elapsed time-base budget so
// decoding stays ahead of playback without running too far. Sets the paused
// bit when the frame budget is exhausted; wraps at loop end.
// ---------------------------------------------------------------------------
// Returns a status consumed by stream_slot_tick: 2 = a buffer was produced and
// the frame budget was reached (keep feeding), 1 = end-of-stream reached, 0
// otherwise (slot idle / nothing to do).
int stream_player_decode_mix(void) {
    long long carrier = (long long)FUN_803826d4();
    int slot = (int)carrier;
    int off = slot * 0x11460;

    unsigned int elapsedRaw = (unsigned int)(DAT_806108e0 - DAT_8061251c);
    double dElapsed = (double)(float)((double)CONCAT44(0x43300000, elapsedRaw) - DOUBLE_80616b28);
    int startTb = os_get_time_base();
    DAT_8061251c = DAT_806108e0;

    if ((&DAT_805ad484)[slot * 0x4518] == 0) { FUN_80382720(); return 0; }

    while (*(int *)(&DAT_805ad4c8 + off) == 0) {
        int r = 1;
        if (*(int *)(&DAT_805ad4cc + off) == 0) { r = msg_recv_pump(off + (int)SLOT_MSG_PKT); }
        if ((r == 0) || (r < 0)) { break; }
        if (*(int *)(&DAT_805ad4cc + off) == 0) { msg_parse_header(); }

        while (true) {
            r = 1;
            if (*(int *)(&DAT_805ad4cc + off) == 0) {
                r = msg_stream_consume_chunk((int *)(off + (int)SLOT_MSG_STREAM),
                                             (int *)(off + (int)SLOT_CHUNK));
            }
            if (r == 0) { break; }
            if (-1 < r) {
                unsigned int rateBits = (unsigned int)DAT_806109c0 ^ 0x80000000u;
                unsigned int chanRateBits = *(unsigned int *)(&DAT_805be7d8 + off) ^ 0x80000000u;
                // frame budget for this chunk from the sample-rate ratio
                int budget = (int)(FLOAT_80616b10 *
                    (float)((double)CONCAT44(0x43300000, chanRateBits) - DOUBLE_80616b30) *
                    (float)((double)(FLOAT_80616b14 /
                        (float)((double)CONCAT44(0x43300000, rateBits) - DOUBLE_80616b30)) * dElapsed));

                if ((*(int *)(&DAT_805ad4cc + off) == 0) && (anim_decode_next_block() == 0)) {
                    anim_decode_step();
                }
                *(int *)(&DAT_805ad4cc + off) = 0;

                double gain      = (double)FLOAT_80616b18;
                double budgetK1  = (double)FLOAT_80616b1c;
                double budgetK2  = (double)FLOAT_80616b20;
                double rateNum   = (double)FLOAT_80616b14;
                double biasA     = DOUBLE_80616b28;
                double biasB     = DOUBLE_80616b30;

                int *rows;
                int nFrames;
                while ((nFrames = anim_decode_get_output(off + (int)SLOT_ANIM_DEC, (int *)&rows)), 0 < nFrames) {
                    if (0x3fff < *(int *)(&DAT_805be580 + off) + nFrames) {
                        while (FUN_801a23c4() == 0) { }
                        while (FUN_801a272c() == 0) { }
                    }
                    int writePos = *(int *)(&DAT_805be580 + off);
                    int rowIdx = 0;
                    int chanByteOff = 0;
                    for (int c = 0; c < *(int *)(&DAT_805be7d4 + off); c = c + 1) {
                        int rowPtr = ((int *)rows)[rowIdx / 4];
                        *(int *)(&DAT_805be580 + off) = writePos;
                        short *pcm = (short *)((int)&DAT_805ad480 + writePos * 2 + 0x1100 + chanByteOff + off);
                        int srcOff = 0;
                        int rem = nFrames;
                        if (0 < nFrames) {
                            do {
                                int s = (int)(gain * (double)*(float *)(rowPtr + srcOff));
                                if (0x7fff < s) { s = 0x7fff; }
                                if (s < -0x8000) { s = -0x8000; }
                                if (pcm < (short *)((int)&DAT_805ad480 + chanByteOff + off + 0x9100)) {
                                    *pcm = (short)s;
                                }
                                pcm = pcm + 1;
                                srcOff = srcOff + 4;
                                *(int *)(&DAT_805be580 + off) = *(int *)(&DAT_805be580 + off) + 1;
                                rem = rem - 1;
                            } while (rem != 0);
                        }
                        chanByteOff = chanByteOff + 0x8000;
                        rowIdx = rowIdx + 4;
                    }
                    anim_decode_skip(off + (int)SLOT_ANIM_DEC, nFrames);
                    *(int *)(&DAT_805ad4d0 + off) = *(int *)(&DAT_805ad4d0 + off) + nFrames;

                    int nowTb = os_get_time_base();
                    unsigned int rateBits2 = (unsigned int)DAT_806109c0 ^ 0x80000000u;
                    double usec = (double)CONCAT44(0x43300000,
                        (unsigned int)((nowTb - startTb) * 8) / (unsigned int)(DAT_800000f8 / 500000));
                    double limit = (double)(float)(budgetK1 * (double)(float)(budgetK2 *
                        (double)(float)(rateNum / (double)(float)((double)CONCAT44(0x43300000, rateBits2) - biasB))));
                    if ((float)limit < (float)(usec - biasA)) {
                        *(int *)(&DAT_805ad4cc + off) = 1;
                        *(int *)(&DAT_805ad4d0 + off) = *(int *)(&DAT_805ad4d0 + off) - budget;
                        FUN_80382720();
                        return 2;   // produced a buffer, hit the frame budget
                    }
                }
            }
        }
        unsigned char eos = bitreader_hdr_flag2((int *)(off + (int)SLOT_HDR_BR));
        if (eos != 0) { *(int *)(&DAT_805ad4c8 + off) = 1; }
    }

    if ((*(int *)(&DAT_805ad494 + off) <= *(int *)(&DAT_805ad490 + off)) &&
        (-1 < *(int *)(&DAT_805ad4a8 + off))) {
        *(int *)(&DAT_805ad4c8 + off) = 0;
        *(int *)(&DAT_805ad490 + off) = *(int *)(&DAT_805ad4a8 + off);
    }
    FUN_80382720();
    return 1;   // end-of-stream reached
}

// ---------------------------------------------------------------------------
// stream_request_set_paused (0x801a1fc8)
// Raise the per-slot paused flag (DAT_805ad4b4).
// ---------------------------------------------------------------------------
void stream_request_set_paused(int slot) {
    (&DAT_805ad4b4)[slot * 0x4518] = 1;
}

// ---------------------------------------------------------------------------
// stream_request_close (0x801a1fec)
// Tear down an active stream slot: drain/close the DVD handle (unless the fast
// close guard DAT_80612374 is set), free the msg stream / anim decoder / codec
// contexts and the msg packet, clear the slot state, then release the two
// voice handles (FUN_803aff9c / FUN_803ac328). No-op if the slot is idle.
// ---------------------------------------------------------------------------
void stream_request_close(int slot) {
    int off = slot * 0x11460;
    if ((&DAT_805ad484)[slot * 0x4518] == 0) { return; }

    if (DAT_80612374 == 0) {
        bool done = false;
        do {
            if (dvd2_handle_is_busy(*(int *)(&DAT_805ad48c + off)) != 0) { break; }
            int st = FUN_803a4db4();
            done = false;
            if (st < 7) {
                if ((st == -1) || ((-2 < st) && (3 < st))) { done = true; }
            } else if (st == 0xb) { done = true; }
        } while (!done);
        FUN_8039cca0();
        dvd2_handle_close(*(int *)(&DAT_805ad48c + off));
        FUN_8039ccc8();
    }
    msg_stream_free((int *)(off + (int)SLOT_MSG_STREAM));
    anim_decoder_free(off + (int)SLOT_ANIM_CHAN);
    anim_ctx_destroy();
    anim_ctx_free_scratch();
    anim_subctx_destroy();
    msg_packet_free((int *)(off + (int)SLOT_MSG_PKT));
    (&DAT_805ad484)[slot * 0x4518] = 0;
    (&DAT_805ad4d4)[slot * 0x4518] = 0;
    (&DAT_805ad4a0)[slot * 0x4518] = 0;
    FUN_8039cca0();
    FUN_803aff9c((&DAT_805be584)[slot * 0x4518]);
    FUN_803ac328((int *)(&DAT_805be584)[slot * 0x4518]);
    FUN_803aff9c((&DAT_805be588)[slot * 0x4518]);
    FUN_803ac328((int *)(&DAT_805be588)[slot * 0x4518]);
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// stream_voice_start (0x801a2154)
// Allocate a mixer voice (FUN_803ac3a8) and configure it: full-scale volume
// (audio_linear_to_centibels(0x3fff)), pan/priority (FUN_803af120), the two
// sample-position words split from param_3/param_4 (>>0x11 and >>1), and the
// mix/param blocks (FUN_803aeb08 / FUN_803aedb0). (r3:r4 carrier via FUN_803826e8
// provides the destination voice-handle slot.)
// ---------------------------------------------------------------------------
void stream_voice_start(int param_1, int param_2, unsigned int param_3, unsigned int param_4,
                        int param_5) {
    (void)param_1; (void)param_2;
    unsigned long long carrier = FUN_803826e8();
    int *pVoiceSlot = (int *)((unsigned long long)carrier >> 0x20);
    int cb = audio_linear_to_centibels(0x3fff);
    int *voice = FUN_803ac3a8(0x1f, -0x7fe5deb0, (int)carrier);
    *pVoiceSlot = (int)voice;
    FUN_803af120(*pVoiceSlot, 0, cb, -0x388, -0x388,
                 ~-(unsigned int)(param_5 != 0) & 0x7f, 0x7f, 0);

    unsigned short posBuf[10];
    posBuf[0] = 1;    // local_28
    posBuf[1] = 10;   // local_26
    posBuf[6] = (unsigned short)(param_3 >> 0x11);  // local_24
    posBuf[7] = (unsigned short)(param_3 >> 1);     // local_22
    posBuf[8] = (unsigned short)(param_4 >> 0x11);  // local_20
    posBuf[9] = (unsigned short)(param_4 >> 1);     // local_1e
    // local_1c/local_1a mirror local_24/local_22
    unsigned short loopBuf[2];
    loopBuf[0] = (unsigned short)(param_3 >> 0x11);
    loopBuf[1] = (unsigned short)(param_3 >> 1);
    FUN_803aeb08(*pVoiceSlot, (unsigned int *)posBuf);

    unsigned short mixBuf[7];
    memset_wrapper((int)mixBuf, 0, 0xe);
    mixBuf[0] = 1;
    mixBuf[1] = 0;
    FUN_803aedb0(*pVoiceSlot, mixBuf);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// stream_loop_check (0x801a225c)
// Loop-point watchdog for slot 0's streaming voices. When the slot is active
// and started and its voice-B read position (DAT_805be588 + 0x1b2) has crossed
// the recorded loop threshold, retrigger both voices (FUN_803aea60) and update
// the loop threshold. Tracks the previous position in DAT_805ad4e4.
// ---------------------------------------------------------------------------
void stream_loop_check(void) {
    unsigned int pos = DAT_805ad4e4;
    if (((DAT_805ad484 != 0) && (DAT_805ad4d4 != 0)) && (DAT_805be588 != 0) &&
        ((pos = *(unsigned int *)(DAT_805be588 + 0x1b2), DAT_805ad4dc <= pos) &&
         ((DAT_805ad4e4 < DAT_805ad4dc) || (pos < DAT_805ad4e4)))) {
        FUN_803aea60(DAT_805be584, 0);
        FUN_803aea60(DAT_805be588, 0);
        DAT_805ad4dc = pos;
    }
    DAT_805ad4e4 = pos;
}

// ===========================================================================
// batch_526 (0x801a2318 - 0x801a2d04)  stream player: double-buffer refill
// state machine + voice-control helpers.
// ===========================================================================
extern "C" {
// additional per-slot / global fields
extern int DAT_805ad4a4;   // resume grace counter
extern int DAT_805ad4d8;   // ring read offset (bytes, within 0x32000 half)
extern unsigned int DAT_805ad4e0;  // last submit position
extern int DAT_805ad568;   // buffers-submitted-this-pass count
extern int DAT_80612020;   // default resume grace value
extern int DAT_80612510;   // global-pause flag
extern int DAT_80612614;   // pause-gate context handle

extern void anim_scratch_noop(void);                     // codec allocator no-op hook
extern unsigned long long FUN_803826bc(void);            // r3:r4 carrier (slot)
extern void FUN_80382708(void);                          // restore paired w/ FUN_803826bc
extern int  FUN_803abe54(unsigned int *cbRec, int tag, int a3, int loop, unsigned int buf,
                         int src, int len, int arena);   // queue/submit voice buffer
extern int  FUN_803affe4(int voice);                     // get voice owner id
extern void FUN_803affbc(int voice, int owner);          // set voice owner id
extern int  FUN_801c2dcc(int ctx);                       // pause-gate poll
extern void FUN_803994a4(int addr, int len);             // dcache range op
extern void FUN_803994d0(unsigned int addr, unsigned int len);  // dcache flush range

// forward decls within this TU (state machine dispatches these)
int  stream_buffer_submit(void);
int  stream_buffer_advance(void);
} // extern "C"

// ---------------------------------------------------------------------------
// stream_voice_pair_start (0x801a2318)
// Start both mixer voices for a slot (slot via r3:r4 carrier FUN_803826f0):
// voice A over scratch page A (DAT_805be58c, +0x32000 loop end) and voice B
// over scratch page B (DAT_805be590), each via stream_voice_start with a unique
// voice id (0x9910991 + slot*0x100 / +0x992). Guards with the audio-driver
// interrupt lock.
// ---------------------------------------------------------------------------
void stream_voice_pair_start(void) {
    int slot = (int)FUN_803826f0();
    FUN_8039cca0();
    stream_voice_start((int)(&DAT_805be584 + slot * 0x4518), slot * 0x100 + 0x9910991,
                       (&DAT_805be58c)[slot * 0x4518], (&DAT_805be58c)[slot * 0x4518] + 0x32000, 1);
    stream_voice_start((int)(&DAT_805be588 + slot * 0x4518), slot * 0x100 + 0x9910992,
                       (&DAT_805be590)[slot * 0x4518], (&DAT_805be590)[slot * 0x4518] + 0x32000, 0);
    FUN_8039ccc8();
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// stream_buffer_submit (0x801a23c4)
// Submit the next decoded PCM half-buffer(s) to the two voices (slot via r3:r4
// carrier FUN_803826bc). Computes the current voice read position vs the ring
// write position to decide whether a fresh block should be enqueued; if so, for
// each channel it flushes the PCM region (FUN_803994d0) and enqueues it on the
// voice (FUN_803abe54), splitting into two segments when the block wraps past
// the 0x32000 half-buffer boundary. Tracks how many contiguous (non-wrapped)
// buffers were queued in DAT_805ad568 and advances the ring read offset
// DAT_805ad4d8. The else branch (armed + finished) retriggers both voices.
// Returns nonzero on progress (via the r3 carrier restore).
// ---------------------------------------------------------------------------
int stream_buffer_submit(void) {
    int slot = (int)FUN_803826bc();
    int off = slot * 0x11460;

    unsigned int writeAhead = (unsigned int)(*(int *)(&DAT_805be580 + off) +
        (((unsigned int)((&DAT_805be590)[slot * 0x4518] + *(int *)(&DAT_805ad4d8 + off))) >> 1) + 1);
    unsigned int voicePos = *(unsigned int *)((&DAT_805be588)[slot * 0x4518] + 0x1b2);
    bool belowHalf = writeAhead < (((unsigned int)(&DAT_805be590)[slot * 0x4518] >> 1) + 0x19000);
    if (!belowHalf) { writeAhead = writeAhead - 0x19000; }
    bool posLE = voicePos <= *(unsigned int *)(&DAT_805ad4e0 + off);

    if ((*(int *)(&DAT_805ad4c4 + off) == 0) ||
        ((belowHalf && posLE) && (voicePos < writeAhead)) ||
        ((!belowHalf && posLE) && (writeAhead < voicePos)) ||
        ((belowHalf && !posLE) && (writeAhead < voicePos)) ||
        ((*(short *)((&DAT_805be588)[slot * 0x4518] + 0x146) == 0) &&
         ((&DAT_805ad4a0)[slot * 0x4518] == 0) &&
         ((DAT_80612510 == 0 || (FUN_801c2dcc(DAT_80612614) != 0)) &&
          ((&DAT_805ad4d4)[slot * 0x4518] != 0)))) {
        unsigned int blockBytes = (unsigned int)*(int *)(&DAT_805be580 + off) << 1;
        if ((*(unsigned int *)(&DAT_805be580 + off) & 0xf) != 0) { blockBytes = blockBytes & 0xffffffe0u; }
        int byteOff = 0;
        (&DAT_805ad568)[slot * 0x4518] = 0;
        int cbIdx = 0;
        int recOff = 0;
        for (int ch = 0; ch < *(int *)(&DAT_805be7d4 + off); ch = ch + 1) {
            int ringOff = *(int *)(&DAT_805ad4d8 + off);
            int page = (ch == 0) ? (&DAT_805be58c)[slot * 0x4518] : (&DAT_805be590)[slot * 0x4518];
            int endPos = ringOff + (int)blockBytes;
            int overflow = 0;
            if (0x31fff < endPos) { overflow = endPos + -0x32000; }
            unsigned int pcmAddr = (unsigned int)((int)&DAT_805ad480 + byteOff + off + 0x1100);
            FUN_803994d0(pcmAddr, blockBytes);
            FUN_8039cca0();
            unsigned int seg1 = blockBytes - overflow;
            FUN_803abe54((unsigned int *)((int)&DAT_805ad480 + recOff + 0x68 + off),
                         slot + 0x4f474759, 0, 1, pcmAddr, ringOff + page, (int)seg1, -0x7fe5ea64);
            if (overflow == 0) {
                (&DAT_805ad568)[slot * 0x4518] = (&DAT_805ad568)[slot * 0x4518] + 1;
            } else {
                int page2 = (ch == 0) ? (&DAT_805be58c)[slot * 0x4518] : (&DAT_805be590)[slot * 0x4518];
                int half = ((int)seg1 >> 1) + (int)((int)seg1 < 0 && (seg1 & 1) != 0);
                FUN_803abe54((unsigned int *)(&DAT_805ad480 + slot * 0x4518 + (cbIdx + 1) * 8 + 0x1a),
                             slot + 0x4f474759, 0, 1,
                             (unsigned int)((int)&DAT_805ad480 + half * 2 + 0x1100 + byteOff + off),
                             page2, overflow, -0x7fe5ea64);
            }
            FUN_8039ccc8();
            byteOff = byteOff + 0x8000;
            cbIdx = cbIdx + 2;
            recOff = recOff + 0x40;
        }
        *(int *)(&DAT_805ad4d8 + off) = *(int *)(&DAT_805ad4d8 + off) + (int)blockBytes;
        if (0x31fff < *(int *)(&DAT_805ad4d8 + off)) {
            *(int *)(&DAT_805ad4d8 + off) = *(int *)(&DAT_805ad4d8 + off) + -0x32000;
        }
    } else if ((*(int *)(&DAT_805ad4c4 + off) != 0) && ((&DAT_805ad4d4)[slot * 0x4518] == 0) &&
               ((DAT_80612510 == 0 || (FUN_801c2dcc(DAT_80612614) != 0)))) {
        FUN_8039cca0();
        FUN_803aea60((&DAT_805be584)[slot * 0x4518], 1);
        FUN_803aea60((&DAT_805be588)[slot * 0x4518], 1);
        FUN_8039ccc8();
        (&DAT_805ad4d4)[slot * 0x4518] = 1;
        if ((&DAT_805ad4a4)[slot * 0x4518] == 0) {
            (&DAT_805ad4a4)[slot * 0x4518] = DAT_80612020;
        }
    }
    FUN_80382708();
    return 1;
}

// ---------------------------------------------------------------------------
// stream_buffer_advance (0x801a272c)
// Once at least 4 buffers have been submitted this pass (DAT_805ad568), record
// the loop/submit positions (DAT_805ad4e0 / DAT_805ad4dc), arm the slot
// (DAT_805ad4c4 = 1), optionally retrigger both voices, and compact the ring:
// memmove the leftover odd-tail PCM back to the front and reset the write
// cursor DAT_805be580. (slot via r3:r4 carrier FUN_803826e8.) Returns nonzero
// once compaction ran.
// ---------------------------------------------------------------------------
int stream_buffer_advance(void) {
    int slot = (int)FUN_803826e8();
    int off = slot * 0x11460;
    if (3 < (&DAT_805ad568)[slot * 0x4518]) {
        int writePos = *(int *)(&DAT_805be580 + off);
        *(unsigned int *)(&DAT_805ad4e0 + off) =
            (unsigned int)((&DAT_805be590)[slot * 0x4518] + *(int *)(&DAT_805ad4d8 + off)) >> 1;
        (&DAT_805ad4dc)[slot * 0x4518] = *(int *)(&DAT_805ad4e0 + off) + -0xc0;
        if ((unsigned int)(&DAT_805ad4dc)[slot * 0x4518] < ((unsigned int)(&DAT_805be590)[slot * 0x4518] >> 1)) {
            (&DAT_805ad4dc)[slot * 0x4518] = (&DAT_805ad4dc)[slot * 0x4518] + 0x19000;
        }
        *(int *)(&DAT_805ad4c4 + off) = 1;
        if (((&DAT_805ad4d4)[slot * 0x4518] != 0) &&
            (*(short *)((&DAT_805be588)[slot * 0x4518] + 0x146) == 0) &&
            ((&DAT_805ad4a0)[slot * 0x4518] == 0) &&
            ((DAT_80612510 == 0 || (FUN_801c2dcc(DAT_80612614) != 0)))) {
            FUN_8039cca0();
            FUN_803aea60((&DAT_805be584)[slot * 0x4518], 1);
            FUN_803aea60((&DAT_805be588)[slot * 0x4518], 1);
            FUN_8039ccc8();
        }
        unsigned int tail = (unsigned int)(writePos << 1) & 0x1e;
        if (tail == 0) {
            *(int *)(&DAT_805be580 + off) = 0;
        } else {
            int i = 0;
            int chOff = 0;
            do {
                leaf_31e8((unsigned int)((int)&DAT_805ad480 + chOff + off + 0x1100),
                          (unsigned int)((int)&DAT_805ad480 + ((writePos << 1) & (int)0xffffffe0) + 0x1100 + chOff + off),
                          (int)tail);
                i = i + 1;
                chOff = chOff + 0x8000;
            } while (i < 2);
            *(int *)(&DAT_805be580 + off) = (int)tail >> 1;
        }
    }
    FUN_80382734();
    return 1;
}

// ---------------------------------------------------------------------------
// stream_slot_tick (0x801a28b8)
// Per-slot playback state machine, run once per frame (slot = param_9). The
// slot state (*(&DAT_805ad480)[slot*0x4518]) drives:
//   1 decode/mix  -> stream_player_decode_mix; 2->state2, 1(EOS)->state6
//   2 submit      -> stream_buffer_submit; done->state3
//   3 dvd refill  -> when read idle, reserve+copy the next 0x1000 block and
//                    kick dvd2_read_wrapper, advance cursors -> state4
//   4 advance     -> stream_buffer_advance; done->state1
//   5 loading     -> stream_request_pump_header
//   6 draining    -> when voice B drained, pause the slot -> state0 (idle)
// Returns nonzero while the slot remains active (state != 0).
// ---------------------------------------------------------------------------
unsigned int stream_slot_tick(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    unsigned int slot) {
    int off = slot * 0x11460;
    int *state = &DAT_805ad480 + slot * 0x4518;
    if ((&DAT_805ad484)[slot * 0x4518] != 0) {
        switch (*state) {
        case 1: {
            int r = stream_player_decode_mix();
            if (r == 2) { *state = 2; }
            else if (r == 1) { *state = 6; }
            break;
        }
        case 2:
            if (stream_buffer_submit() != 0) { *state = 3; }
            break;
        case 3:
            if (*(int *)(&DAT_805ad4c8 + off) == 0) {
                if (dvd2_handle_is_busy(*(int *)(&DAT_805ad48c + off)) != 0) {
                    int dst = msg_scratch_reserve();
                    *(int *)(&DAT_805be594 + off) = dst;
                    FUN_803994a4(off + (int)SLOT_READ_DST, 0x1000);
                    leaf_31e8(*(unsigned int *)(&DAT_805be594 + off), (unsigned int)(off + (int)SLOT_READ_DST), 0x1000);
                    FUN_8039cca0();
                    dvd2_read_wrapper(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                      param_8, *(int *)(&DAT_805ad48c + off), off + (int)SLOT_READ_DST,
                                      *(int *)(&DAT_805ad490 + off), 0x1000, 0, 0, 0, 0);
                    FUN_8039ccc8();
                    msg_cursor_advance(off + (int)SLOT_MSG_PKT, *(int *)(&DAT_805ad498 + off));
                    *(int *)(&DAT_805ad490 + off) = *(int *)(&DAT_805ad490 + off) + 0x1000;
                    *(int *)(&DAT_805ad498 + off) = 0x1000;
                    if (*(int *)(&DAT_805ad494 + off) < *(int *)(&DAT_805ad490 + off)) {
                        *(int *)(&DAT_805ad498 + off) = *(int *)(&DAT_805ad494 + off) - *(int *)(&DAT_805ad490 + off);
                    }
                    if (*(int *)(&DAT_805ad498 + off) < 0) { *(int *)(&DAT_805ad498 + off) = 0; }
                    *state = 4;
                }
            } else {
                *state = 4;
            }
            break;
        case 4:
            if (stream_buffer_advance() != 0) { *state = 1; }
            break;
        case 5:
            stream_request_pump_header(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                       param_8, (int)slot);
            break;
        case 6:
            if (((&DAT_805be588)[slot * 0x4518] == 0) ||
                (*(short *)((&DAT_805be588)[slot * 0x4518] + 0x146) == 0)) {
                stream_request_set_paused((int)slot);
                anim_scratch_noop();
                *state = 0;
            }
            break;
        }
        slot = (unsigned int)(*state != 0);
    }
    return slot;
}

// ---------------------------------------------------------------------------
// stream_slot_is_playing (0x801a2aac)
// Return 1 if the slot is a real (non-sentinel) slot and has started
// (DAT_805ad4d4), else 0.
// ---------------------------------------------------------------------------
int stream_slot_is_playing(int slot) {
    int r = 0;
    if ((slot * 0x11460 != 0x7fa52b80) && ((&DAT_805ad4d4)[slot * 0x4518] != 0)) { r = 1; }
    return r;
}

// ---------------------------------------------------------------------------
// stream_slot_pause (0x801a2ae4)
// Stop both of a slot's voices (FUN_803aea60 ..,0) and set its pause flag
// DAT_805ad4a0. No-op for the sentinel slot.
// ---------------------------------------------------------------------------
void stream_slot_pause(int slot) {
    if (slot * 0x11460 != 0x7fa52b80) {
        if (((&DAT_805be584)[slot * 0x4518] != 0) && ((&DAT_805be588)[slot * 0x4518] != 0)) {
            FUN_8039cca0();
            FUN_803aea60((&DAT_805be584)[slot * 0x4518], 0);
            FUN_803aea60((&DAT_805be588)[slot * 0x4518], 0);
            FUN_8039ccc8();
        }
        (&DAT_805ad4a0)[slot * 0x4518] = 1;
    }
}

// ---------------------------------------------------------------------------
// stream_all_pause (0x801a2b84)
// Stop the given slot's voices and raise the global pause flag DAT_80612510
// (used to pause all streaming audio, e.g. on menu/pause). No-op for sentinel.
// ---------------------------------------------------------------------------
void stream_all_pause(int slot) {
    if (slot * 0x11460 != 0x7fa52b80) {
        if (((&DAT_805be584)[slot * 0x4518] != 0) && ((&DAT_805be588)[slot * 0x4518] != 0)) {
            FUN_8039cca0();
            FUN_803aea60((&DAT_805be584)[slot * 0x4518], 0);
            FUN_803aea60((&DAT_805be588)[slot * 0x4518], 0);
            FUN_8039ccc8();
        }
        DAT_80612510 = 1;
    }
}

// ---------------------------------------------------------------------------
// stream_all_resume (0x801a2c24)
// Clear the global pause flag DAT_80612510. No-op for the sentinel slot.
// ---------------------------------------------------------------------------
void stream_all_resume(int slot) {
    if (slot * 0x11460 == 0x7fa52b80) { return; }
    DAT_80612510 = 0;
}

// ---------------------------------------------------------------------------
// stream_voice_pair_set_owner (0x801a2c4c)
// Reassign the owner id of both of a slot's voices to the value in the r4 half
// of the r3:r4 carrier (FUN_803826f0), when it differs from the current owner
// (FUN_803affe4 get / FUN_803affbc set). No-op for the sentinel slot.
// ---------------------------------------------------------------------------
void stream_voice_pair_set_owner(void) {
    unsigned long long carrier = FUN_803826f0();
    int slot = (int)((unsigned long long)carrier >> 0x20);
    int owner = (int)carrier;
    if (slot * 0x11460 != 0x7fa52b80) {
        FUN_8039cca0();
        if (((&DAT_805be584)[slot * 0x4518] != 0) &&
            (FUN_803affe4((&DAT_805be584)[slot * 0x4518]) != owner)) {
            FUN_803affbc((&DAT_805be584)[slot * 0x4518], owner);
        }
        if (((&DAT_805be588)[slot * 0x4518] != 0) &&
            (FUN_803affe4((&DAT_805be588)[slot * 0x4518]) != owner)) {
            FUN_803affbc((&DAT_805be588)[slot * 0x4518], owner);
        }
        FUN_8039ccc8();
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// stream_slot_resume (0x801a2d04)
// Resume a paused slot: retrigger both voices (when started + not gated by the
// global pause) and clear the pause flag DAT_805ad4a0. No-op for sentinel.
// ---------------------------------------------------------------------------
void stream_slot_resume(int slot) {
    if (slot * 0x11460 != 0x7fa52b80) {
        if (((&DAT_805ad4d4)[slot * 0x4518] != 0) &&
            ((DAT_80612510 == 0 || (FUN_801c2dcc(DAT_80612614) != 0)) &&
             ((&DAT_805be584)[slot * 0x4518] != 0)) && ((&DAT_805be588)[slot * 0x4518] != 0)) {
            FUN_8039cca0();
            FUN_803aea60((&DAT_805be584)[slot * 0x4518], 1);
            FUN_803aea60((&DAT_805be588)[slot * 0x4518], 1);
            FUN_8039ccc8();
        }
        (&DAT_805ad4a0)[slot * 0x4518] = 0;
    }
}

// ===========================================================================
// batch_527 (0x801a2dcc - 0x801a3158)  stream player: per-frame driver,
// subsystem init/shutdown, loop-point predicate, small getters/setters, and
// two padding stubs.
// ===========================================================================
extern "C" {
extern int DAT_805ad488;   // "stop update loop" request flag
extern int DAT_80612514;   // stream subsystem initialized guard
extern int DAT_80612520;   // shared streaming DMA buffer handle
extern unsigned int DAT_80612540;  // current video/display mode id
extern unsigned int DAT_80612538;  // active controller-port mask (bits 0..3)

extern void  FUN_803a0434(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, unsigned int a9, unsigned int a10, char *a11, int a12, int a13,
    int a14, int a15, int a16);                       // OS yield / frame callback
extern unsigned char *FUN_803a0308(int a1, int a2, int a3, int a4);  // buffer register/alloc
extern void  VIWaitForRetrace(void);                  // VI vblank wait
extern int   input_get_buttons_held(int pad);         // poll held buttons for a pad

// forward decl within this TU (init lazily tears down before re-init)
void stream_system_shutdown(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, unsigned int a9, unsigned int a10, char *a11, int a12, int a13,
    int a14, int a15, int a16);
} // extern "C"

// ---------------------------------------------------------------------------
// stream_player_update (0x801a2dcc)
// Per-frame streaming driver loop (slot 0). Runs until the stop flag
// DAT_805ad488 is raised: services a deferred close (DAT_805ad4b4 ->
// stream_request_close) and a deferred begin (DAT_805ad4b8 -> copy the pending
// callback record then stream_request_begin), advances the slot state machine
// (stream_slot_tick), and yields to the OS/frame callback (FUN_803a0434) each
// iteration. (r3:r4 carrier via FUN_803826e8.)
// ---------------------------------------------------------------------------
void stream_player_update(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    unsigned int param_9, unsigned int param_10, char *param_11, int param_12, int param_13,
    int param_14, int param_15, int param_16) {
    (void)param_1; (void)param_9; (void)param_10;
    FUN_803826e8();
    int callbackRec[38];
    while (DAT_805ad488 == 0) {
        FUN_8039cca0();
        int wantBegin = DAT_805ad4b8;
        int wantClose = DAT_805ad4b4;
        FUN_8039ccc8();
        if (wantClose != 0) {
            stream_request_close(0);
            DAT_805ad4b4 = 0;
        }
        if (wantBegin != 0) {
            FUN_8038b510(callbackRec, (int *)&DAT_805be598);
            param_11 = (char *)DAT_805ad4c0;
            stream_request_begin(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            DAT_805ad4b8 = 0;
        }
        unsigned int active = stream_slot_tick(param_1, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, 0);
        FUN_803a0434(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     active, 0, param_11, param_12, param_13, param_14, param_15, param_16);
    }
    DAT_805ad488 = 0;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// stream_system_init (0x801a2e90)
// Lazily initialize the streaming subsystem: if already initialized
// (DAT_80612514) first tear it down (stream_system_shutdown), then clear and
// flush the shared 0x2000 streaming DMA buffer, register it (FUN_803a0308 ->
// DAT_80612520), and raise the initialized guard.
// ---------------------------------------------------------------------------
void stream_system_init(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    unsigned int param_9, unsigned int param_10, char *param_11, int param_12, int param_13,
    int param_14, int param_15, int param_16) {
    if (DAT_80612514 != 0) {
        stream_system_shutdown(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                               param_8, param_9, param_10, param_11, param_12, param_13, param_14,
                               param_15, param_16);
    }
    memset_wrapper((int)0x805be8e0, 0, 0x2000);
    FUN_803994d0(0x805be8e0u, 0x2000);
    DAT_80612520 = (int)FUN_803a0308(0x805c08e0 - 0x100000, 0, 0x805c08e0, 0x2000);
    DAT_80612514 = 1;
}

// ---------------------------------------------------------------------------
// stream_can_loop (0x801a2f0c)
// Predicate: is slot 0 at a point where it can seamlessly loop right now?
// Requires the slot started (DAT_805ad4d4), in use (DAT_805ad484), voice B live
// (DAT_805be588), not in a transitional state (state != 6 and != 3), not paused
// (DAT_805ad4a0 == 0), not gated by the global pause, and the recorded loop
// point (DAT_805ad4dc) within 0xc800 samples of voice B's read position. Called
// by vi_present_frame.
// ---------------------------------------------------------------------------
int stream_can_loop(void) {
    if (((DAT_805ad4d4 == 0) || (DAT_805ad484 == 0) || (DAT_805be588 == 0)) ||
        ((DAT_805ad480 == 6 || (DAT_805ad480 == 3))) ||
        ((DAT_805ad4a0 != 0) ||
         ((DAT_80612510 != 0) && (FUN_801c2dcc(DAT_80612614) == 0))) ||
        ((DAT_805ad4dc == 0) ||
         (0xc7ff < (unsigned int)(DAT_805ad4dc - *(int *)(DAT_805be588 + 0x1b2))))) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// stream_system_shutdown (0x801a2fe0)
// Tear down the streaming subsystem: flush a final frame callback, request a
// pause + close of slot 0 (waiting out any in-flight state on VIWaitForRetrace),
// run one stop-update pass (DAT_805ad488) to drain the driver loop, deregister
// the streaming buffer (FUN_803a0308(0,0,0,0)) and clear the init guard.
// ---------------------------------------------------------------------------
void stream_system_shutdown(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    unsigned int param_9, unsigned int param_10, char *param_11, int param_12, int param_13,
    int param_14, int param_15, int param_16) {
    if (DAT_80612514 == 0) { return; }
    FUN_803a0434(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 param_9, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
    if (DAT_805ad484 != 0) {
        if (DAT_805ad4b4 == 0) {
            stream_request_set_paused(0);
        }
        while (DAT_805ad484 != 0) {
            VIWaitForRetrace();
        }
    }
    DAT_805ad488 = 1;
    while (DAT_805ad488 != 0) {
        FUN_803a0434(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     0x805b0000, 0, param_11, param_12, param_13, param_14, param_15, param_16);
        VIWaitForRetrace();
    }
    unsigned char *buf = FUN_803a0308(0, 0, 0, 0);
    FUN_803a0434(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 (unsigned int)(unsigned long)buf, 0, (char *)0, 0, param_13, param_14,
                 param_15, param_16);
    DAT_80612514 = 0;
}

// ---------------------------------------------------------------------------
// stream_get_resume_grace (0x801a30ac)
// Return the slot-0 resume grace counter (DAT_805ad4a4).
// ---------------------------------------------------------------------------
int stream_get_resume_grace(void) {
    return DAT_805ad4a4;
}

// ---------------------------------------------------------------------------
// stream_set_resume_grace (0x801a30bc)
// Set the slot-0 resume grace counter (DAT_805ad4a4).
// ---------------------------------------------------------------------------
void stream_set_resume_grace(int value) {
    DAT_805ad4a4 = value;
}

// ---------------------------------------------------------------------------
// video_mode_is_progressive (0x801a30cc)
// Return 1 if the current video/display mode (DAT_80612540) is 0xd or 0xe
// (the two progressive-scan modes), else 0. Used by the frame/GX timing code.
// ---------------------------------------------------------------------------
int video_mode_is_progressive(void) {
    if (DAT_80612540 - 0xdu < 2) { return 1; }
    return 0;
}

// ---------------------------------------------------------------------------
// input_poll_all_pads (0x801a30f0)
// Poll the held-button state (input_get_buttons_held) for each of controller
// ports 0..3 that is present in the active-port mask DAT_80612538. (r3:r4
// carrier via FUN_803826f0.)
// ---------------------------------------------------------------------------
void input_poll_all_pads(void) {
    FUN_803826f0();
    int pad = 0;
    do {
        if ((DAT_80612538 & (1u << pad)) != 0) {
            input_get_buttons_held(pad);
        }
        pad = pad + 1;
    } while (pad < 4);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// stream_stub_3154 (0x801a3154)
// Empty routine (alignment / no-op hook).
// ---------------------------------------------------------------------------
void stream_stub_3154(void) {
}

// ---------------------------------------------------------------------------
// stream_stub_3158 (0x801a3158)
// Empty routine (alignment / no-op hook).
// ---------------------------------------------------------------------------
void stream_stub_3158(void) {
}
