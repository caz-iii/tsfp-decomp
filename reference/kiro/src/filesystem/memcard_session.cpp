// memcard_session.cpp
// Memory-card (CARD) save/load *session driver* - the screen-driven state
// machine that sits above the low-level record primitives in memcard_save.cpp.
//
// Entry points here open a save/load session (memcard_session_begin_slot /
// memcard_session_begin_all), then a per-frame worker (memcard_session_run)
// walks the players, pushes the CARD-progress HUD screen (hud_screen_push),
// and sets the session state code DAT_80612540 (0xd = multi-player card op in
// progress, 0x11 = single-slot). The block-level workers build a 0x4000 (16 KB)
// CARD block from a player's slot data, stamp it with the game banner name and
// a CRC32 integrity word, then drive the async CARD command queue
// (gx_cmd_add_type4/type9/type10 + the gx_cmd_list_execute pump) to write or
// read it back, verifying the CRC on load.
//
// The gx_cmd_* names are the shared low-level CARD async command-list helpers
// (see memcard_save.cpp); card_translate_result maps raw CARD status codes.
//
// Address range: 0x801a315c - 0x801a36ac
// Batch: batch_528
#include "common.h"

extern "C" {
// --- CARD async command-list primitives (shared low-level helpers) ---
extern void gx_cmd_add_type4(int h, int a2, int a3, int buf, unsigned int len, int a6, int *rec);
extern void gx_cmd_add_type9(int h, int a2, void *cb, int a4);
extern void gx_cmd_add_type10(int h, int a2, int *rec, int buf, unsigned int len, int a6,
                              int *extra);
extern int  gx_cmd_list_execute(int a1, int *outCode, int *outTag, void *outData);
extern int  card_translate_result(int code);

// --- record / banner build + record helpers ---
extern int  FUN_8038b5c8(int rec);                          // record length query
extern int  FUN_8038b378(unsigned int *a, unsigned int *b); // tag compare (0 == match)
extern int  FUN_80387b68(int field, int expect, int len);   // field validate (0 == match)
extern unsigned long long FUN_8038b510(int *dst, int *src); // string copy (returns dst acc)
extern void FUN_8038b4a0(int dst, int src);                 // string append
extern void FUN_80387fb8(unsigned long long acc, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, int *fmt, int arg0,
                         int r6, int r7, int r8, int r9, int r10);  // sprintf-style banner fmt
extern unsigned int crc32_buffer(void *buf, int len);
extern unsigned int crc32_string(unsigned char *s);
extern int  leaf_31e8(unsigned int dst, unsigned int src, int len);
extern unsigned int FUN_803994a4(int buf, int len);   // cache invalidate range
extern unsigned int FUN_803994d0(unsigned int buf, int len);  // cache flush range

// --- player / hud / notif named helpers (resolved via generated_decls after regen) ---
extern long long player_get_level_mode(int slot);
extern unsigned char *player_slot_get_data(int slot);
extern int  player_get_entity_ptr(int slot);
extern void hud_screen_push(void);
extern void notif_set_player_slot(int slot);
extern void *mem_alloc_aligned(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                               unsigned long long a4, unsigned long long a5, unsigned long long a6,
                               unsigned long long a7, unsigned long long a8, int align, int size,
                               int a11, int r6, int r7, int r8, int r9, int r10);
extern void mem_free_top(int ptr);
extern int  get_string_by_id(int id);

// --- spill-frame prologue/epilogue helpers ---
extern unsigned long long FUN_803826e8(void);   // spill: fetch save frame (hi:lo)
extern unsigned long long FUN_803826f0(void);    // spill: fetch save frame variant
extern void FUN_80382734(void);                  // spill: restore
extern void FUN_8038273c(void);                  // spill: restore variant
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// --- session driver globals ---
extern int   DAT_8061255c;   // session-active flag
extern int   DAT_80612528;   // saved frame hi (session context)
extern int   DAT_8061252c;   // saved frame lo (session context)
extern int   DAT_80612550;   // active player slot for single-slot op
extern int   DAT_80612540;   // session state code (0xd multi / 0x11 single / 0x16 err)
extern int   DAT_80612548;   // per-player pending bitmask
extern float FLOAT_80612558; // session timeout accumulator
extern float FLOAT_80616b38; // session timeout reset value
extern int   DAT_805cc61c;   // current/local player slot
extern int   DAT_805cc60c;   // session flags (bit 0x10000 = deferred/all mode)
extern int   DAT_80611014;   // active player count
extern int   DAT_80612560;   // scan-match found flag
extern int   DAT_80612370;   // expected save-file id blob
extern int   DAT_80610b38;   // expected save-file magic tag
extern int   DAT_80612534;   // load-flush pending flag
extern int   DAT_80612564;   // active CARD command handle
extern unsigned char *DAT_80612568;  // active 0x4000 block buffer
extern int   DAT_80612594;   // load target player slot
extern int   DAT_8060f948;   // build variant id (region/mode)
extern int   DAT_805c08e0;   // per-handle command context A (stride 0x10)
extern int   DAT_805c08e4;   // per-handle command context B (stride 0x10)
extern int   DAT_80610a14;   // scan result value
extern int   DAT_80610a18;   // scan result valid flag
extern int   DAT_80610b40;   // banner comment format string
extern int   DAT_8041e9e8;   // default banner icon/palette blob (0x800 bytes)
extern int   s_TimeSplitters_Futuro_Perfecto_8042dfc0;  // ES title
extern int   s_TimeSplitters_Future_Perfect_8042dfa0;   // EN title
extern int   PTR_FUN_804771e8;                          // deferred-all callback table
} // extern "C"

// The deferred/all-slot session opener (FUN_801a39a8) lives just past this
// batch; forward-declare it here so the begin_slot/begin_all forwarders resolve.
// (C++ linkage - it will be defined as a normal C++ function in a later batch.)
void memcard_session_begin_all_ex(int frameHi, int callbackTable, int slot);

// Forward decls for the two intra-file session openers (defined further down)
// so begin_slot can call begin_all before its definition.
void memcard_session_begin_all(int frameHi, int callbackTable);

// ---------------------------------------------------------------------------
// memcard_session_nop (FUN_801a315c)
// Empty alignment stub.
// ---------------------------------------------------------------------------
void memcard_session_nop(void) {
}

// ---------------------------------------------------------------------------
// memcard_session_hash_name (FUN_801a3160)
// Thin forwarder that hashes a save-file name string via crc32_string and
// returns the resulting CRC (callers use it as the record name-hash).
// ---------------------------------------------------------------------------
unsigned int memcard_session_hash_name(unsigned char *name) {
    return crc32_string(name);
}

// ---------------------------------------------------------------------------
// memcard_session_flush_load (FUN_801a3180)
// If a load flush is pending (DAT_80612534), pump the CARD command list until
// the terminal tag 0xe (or an error), then clear the pending flag.
// ---------------------------------------------------------------------------
void memcard_session_flush_load(void) {
    if (DAT_80612534 != 0) {
        int rc = 0;
        int outTag = 0;
        int outCode = 0;
        int outData[4];
        while (rc == 0 && outTag != 0xe) {
            rc = gx_cmd_list_execute(3, &outCode, &outTag, outData);
        }
        DAT_80612534 = 0;
    }
}

// ---------------------------------------------------------------------------
// memcard_session_begin_slot (FUN_801a31e8)
// Open a save session for the local player (DAT_805cc61c). If that player is a
// live participant (level_mode == 2) push the CARD HUD screen in single-slot
// state (0x11); otherwise mark the deferred/all-mode flag and hand off to the
// multi-slot session opener (memcard_session_begin_all).
// ---------------------------------------------------------------------------
void memcard_session_begin_slot(int frameHi, int frameLo) {
    DAT_8061255c = 1;
    DAT_80612528 = frameHi;
    DAT_8061252c = frameLo;
    long long mode = player_get_level_mode(DAT_805cc61c);
    if (mode == 2) {
        DAT_80612550 = DAT_805cc61c;
        hud_screen_push();
        DAT_80612540 = 0x11;
        FLOAT_80612558 = FLOAT_80616b38;
    } else {
        DAT_805cc60c = DAT_805cc60c | 0x10000;
        memcard_session_begin_all_ex(DAT_80612528, (int)&PTR_FUN_804771e8, DAT_805cc61c);
        DAT_805cc60c = DAT_805cc60c & 0xfffeffff;
    }
}

// ---------------------------------------------------------------------------
// memcard_session_scan_match_cb (FUN_801a32ac)
// CARD directory scan callback: validate the entry's magic tag (DAT_80610b38)
// and the game-id fields (DAT_80612370); on a full match latch the found flag
// (DAT_80612560) and return whether a match has been seen (via countLeadingZeros
// to turn the flag into 0/1).
// ---------------------------------------------------------------------------
unsigned int memcard_session_scan_match_cb(int entry) {
    int recLen = FUN_8038b5c8(entry);
    int rc = FUN_8038b378((unsigned int *)(entry + recLen + -4), (unsigned int *)&DAT_80610b38);
    if (rc == 0
        && FUN_80387b68(entry + 0x28, DAT_80612370, 4) == 0
        && FUN_80387b68(entry + 0x2c, DAT_80612370 + 4, 2) == 0) {
        DAT_80612560 = 1;
    }
    unsigned int clz = countLeadingZeros((unsigned int)DAT_80612560);
    return clz >> 5;
}

// ---------------------------------------------------------------------------
// memcard_session_scan (FUN_801a3338)
// Kick a CARD directory scan (gx_cmd_add_type9 with memcard_session_scan_match_cb)
// on handle param_1 and pump the command list to the terminal tag 0xe. On success
// returns the latched scan result (DAT_80610a14 gated by DAT_80610a18); on failure
// clears the per-handle command context and returns the negated translated code.
// ---------------------------------------------------------------------------
unsigned int memcard_session_scan(int handle) {
    bool done = false;
    int outTag = 0;
    int outCode = 0;
    int outData[2];
    notif_set_player_slot(0);
    gx_cmd_add_type9(handle, handle, (void *)memcard_session_scan_match_cb, 0);
    while (true) {
        if (done) {
            return (unsigned int)DAT_80610a14 & (unsigned int)(-(int)(DAT_80610a18 != 0));
        }
        int rc = gx_cmd_list_execute(handle, &outCode, &outTag, outData);
        if (rc != 0) break;
        if (outTag == 0xe) done = true;
    }
    (&DAT_805c08e4)[handle * 4] = 0;
    (&DAT_805c08e0)[handle * 4] = 0;
    int translated = card_translate_result(outCode);
    return (unsigned int)(-translated);
}

// ---------------------------------------------------------------------------
// memcard_session_begin_all (FUN_801a3404)
// Open a save session across all local players (slot == -1 sentinel).
// ---------------------------------------------------------------------------
void memcard_session_begin_all(int frameHi, int callbackTable) {
    memcard_session_begin_all_ex(frameHi, callbackTable, -1);
}

// ---------------------------------------------------------------------------
// memcard_session_save_block (FUN_801a3428)
// Build and write one player's 0x4000 (16 KB) CARD save block. Allocates an
// aligned block from the top of the heap, copies the player's slot data + entity
// name into the banner region, stamps the region-specific game title
// ("TimeSplitters: Future Perfect" / ES "Futuro Perfecto"), copies the 0x1edc
// player-slot payload, writes the payload size + a CRC32 integrity word, flushes
// the buffer, and issues the async CARD write (gx_cmd_add_type10).
// ---------------------------------------------------------------------------
void memcard_session_save_block(unsigned long long f1, unsigned long long f2,
                                unsigned long long f3, unsigned long long f4,
                                unsigned long long f5, unsigned long long f6,
                                unsigned long long f7, unsigned long long f8) {
    (void)f2; (void)f3; (void)f4; (void)f5; (void)f6; (void)f7; (void)f8;
    int aiStack_50[20];

    int slot = __save_gpr_28();
    int handle = DAT_80612564;
    unsigned char *buf = (unsigned char *)mem_alloc_aligned(
        f1, f2, f3, f4, f5, f6, f7, f8, 0x20, 0x4000, 0, 0, 0, 0, 0, 0);
    int *slotData = (int *)player_slot_get_data(slot);
    int bannerHi = *(int *)(*slotData + 0x20);
    int bannerLo = *(int *)(*slotData + 0x24);
    int local_58 = bannerHi;
    int local_54 = bannerLo;
    DAT_80612568 = buf;

    notif_set_player_slot(2);
    int *entity = (int *)player_get_entity_ptr(slot);
    FUN_8038b510(aiStack_50, entity);
    FUN_8038b4a0((int)aiStack_50, -0x7f9ef4c8);
    leaf_31e8((unsigned int)(buf + 0x40), 0x8041e9e8, 0x800);

    unsigned long long titleAcc;
    if (DAT_8060f948 == 4) {
        titleAcc = FUN_8038b510((int *)buf, (int *)&s_TimeSplitters_Futuro_Perfecto_8042dfc0);
    } else {
        titleAcc = FUN_8038b510((int *)buf, (int *)&s_TimeSplitters_Future_Perfect_8042dfa0);
    }
    int label = get_string_by_id(0x1940);
    FUN_80387fb8(titleAcc, 0, 0, 0, 0, 0, 0, 0, (int)(buf + 0x20), &DAT_80610b40, label,
                 0, 0, 0, 0, 0);

    int entityPtr = player_get_entity_ptr(slot);
    FUN_8038b4a0((int)(buf + 0x20), entityPtr);
    FUN_8038b4a0((int)(buf + 0x20), -0x7f9ef4b8);

    buf[0x840] = 0;
    buf[0x841] = 0;
    buf[0x842] = 0x40;
    buf[0x843] = 0;
    buf[0x848] = 0;
    buf[0x849] = 0;
    buf[0x84a] = 0;
    buf[0x84b] = 0;

    unsigned char *payloadSrc = player_slot_get_data(slot);
    leaf_31e8((unsigned int)(buf + 0x850), (unsigned int)payloadSrc, 0x1edc);
    buf[0x84c] = 0;
    buf[0x84d] = 0;
    buf[0x84e] = 0;
    buf[0x84f] = 2;

    unsigned int crc = crc32_buffer(buf, 0x4000);
    *(unsigned int *)(buf + 0x848) = crc;
    FUN_803994d0((unsigned int)DAT_80612568, 0x4000);
    gx_cmd_add_type10(handle, handle, aiStack_50, (int)DAT_80612568, 0x4000, 0, &local_58);
    (void)local_54;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// memcard_session_save_poll (FUN_801a35ac)
// Pump the pending save write once. On the terminal tag 0xe free the block and
// return -1 (done); on a still-running pump return 0; on error free the block
// and return the translated CARD status.
// ---------------------------------------------------------------------------
int memcard_session_save_poll(void) {
    int outTag = 0;
    int outCode = 0;
    int outData[4];
    int rc = gx_cmd_list_execute(DAT_80612564, &outCode, &outTag, outData);
    if (rc == 0) {
        if (outTag == 0xe) {
            mem_free_top((int)DAT_80612568);
            return -1;
        }
        return 0;
    }
    mem_free_top((int)DAT_80612568);
    return card_translate_result(outCode);
}

// ---------------------------------------------------------------------------
// memcard_session_begin_load (FUN_801a3618)
// Allocate the aligned 0x4000 load block, invalidate its cache range, and issue
// the async CARD read (gx_cmd_add_type4) into it; records the load target slot
// context for the completion poll.
// ---------------------------------------------------------------------------
void memcard_session_begin_load(unsigned long long f1, unsigned long long f2,
                                unsigned long long f3, unsigned long long f4,
                                unsigned long long f5, unsigned long long f6,
                                unsigned long long f7, unsigned long long f8) {
    (void)f2; (void)f3; (void)f4; (void)f5; (void)f6; (void)f7; (void)f8;
    unsigned long long frame = FUN_803826f0();
    int handle = DAT_80612564;
    DAT_80612568 = (unsigned char *)mem_alloc_aligned(
        f1, f2, f3, f4, f5, f6, f7, f8, 0x20, 0x4000, 0, 0, 0, 0, 0, 0);
    notif_set_player_slot(1);
    FUN_803994a4((int)DAT_80612568, 0x4000);
    gx_cmd_add_type4(handle, handle, (int)frame, (int)DAT_80612568, 0x4000, 0,
                     &DAT_805c08e0 + DAT_80612564 * 4);
    DAT_80612594 = (int)(frame >> 0x20);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// memcard_session_load_poll (FUN_801a36ac)
// Pump the pending load read once. On the terminal tag 0xe re-validate the block:
// invalidate cache, recompute the CRC32 over the block with the stored checksum
// zeroed, and if it matches (and the record type field == 2) copy the payload
// into the target player slot and return -1 (success); on mismatch return 0x16.
// On a still-running pump return 0; on error return the translated status.
// ---------------------------------------------------------------------------
int memcard_session_load_poll(void) {
    int outTag = 0;
    int outCode = 0;
    int outData[4];
    int rc = gx_cmd_list_execute(DAT_80612564, &outCode, &outTag, outData);
    unsigned char *buf = DAT_80612568;
    if (rc == 0) {
        if (outTag == 0xe) {
            FUN_803994a4((int)DAT_80612568, 0x4000);
            unsigned int storedCrc = *(unsigned int *)(buf + 0x848);
            buf[0x848] = 0;
            buf[0x849] = 0;
            buf[0x84a] = 0;
            buf[0x84b] = 0;
            unsigned int calcCrc = crc32_buffer(DAT_80612568, 0x4000);
            *(unsigned int *)(buf + 0x848) = storedCrc;
            if (*(unsigned int *)(buf + 0x848) == calcCrc && *(int *)(buf + 0x84c) == 2) {
                unsigned char *dst = player_slot_get_data(DAT_80612594);
                leaf_31e8((unsigned int)dst, (unsigned int)(DAT_80612568 + 0x850), 0x1edc);
                mem_free_top((int)DAT_80612568);
                return -1;
            }
            mem_free_top((int)DAT_80612568);
            return 0x16;
        }
        return 0;
    }
    mem_free_top((int)DAT_80612568);
    return card_translate_result(outCode);
}

// ===========================================================================
// batch_529 additions: the memory-card session *state machine* and its scan/
// find/teardown/controller helpers. Address range 0x801a3794 - 0x801a4f44.
// This is the per-frame HUD-screen driver (memcard_session_update) that walks
// players, drives the CARD save/load ops built in the batch_528 block above,
// and the multi-slot directory scan + record-match helpers feeding it.
// ===========================================================================

extern "C" {
// --- additional CARD command-list primitives ---
extern unsigned long long gx_cmd_add_type0(int h, int a2);
extern void gx_cmd_add_type5(int h, int a2, void *cb, int a4);
extern void gx_cmd_add_type6(int h, int a2, int *rec);
extern void gx_cmd_add_type7(int h, int a2, int recPtr, int *ctx);

// --- session-driver state-machine globals (batch_529) ---
extern int   DAT_80612530;   // active scan bank index (0/1 double buffer)
extern int   DAT_806125a4;   // per-bank matched-record count (stride 4)
extern int   DAT_805c0900;   // matched-record table base (stride 0x38, bank *0x690)
extern int   DAT_805c0928;   // matched-record entry-id field (base+0x28)
extern int   DAT_805c08e8;   // per-handle command result C (stride 0x10)
extern int   DAT_80612d58;   // scan scratch table base
extern int   DAT_80612554;   // active record index within bank
extern int   DAT_80612544;   // saved CARD handle for retry
extern int   DAT_8061254c;   // active multi-slot player index
extern int   DAT_8061255c_isMulti; // (alias not used; real DAT_8061255c above)
extern int   DAT_8061259c;   // return/resume state code
extern int   DAT_80612598;   // pending resume-op code
extern int   DAT_806125ac;   // keyboard-entry accept flag
extern int   DAT_806125b0;   // keyboard-entry commit flag
extern int   DAT_806125b4;   // pending loaded-record ptr
extern int   DAT_80612538;   // active pad mask
extern int   DAT_8061253c;   // session-busy flag
extern int   DAT_8061237c;   // force-refresh flag
extern int   DAT_80610aa0;   // display sync mode
extern int   DAT_806109c0;   // frame timer raw
extern int   DAT_80610b4c;   // last-updated state code
extern int   DAT_80610b30;   // deferred hud focus id (-1 none)
extern int   DAT_80610b34;   // last card result code
extern int   DAT_80611a54;   // sound-enabled flag
extern int   DAT_80611a58;   // sound-id table ptr
extern float FLOAT_806108ec; // per-frame timeout delta scale
extern float FLOAT_80616b40; // timer numerator
extern float FLOAT_80616b44; // input-repeat threshold
extern float FLOAT_80616b48; // error-screen input threshold
extern double DOUBLE_80616b50; // timer bias (2^52 magic)

// --- named helpers used by the state machine (extern until defined) ---
extern int  video_mode_is_progressive(void);
extern unsigned int input_poll_all_pads(void);
extern void input_stub_nop(void);
extern int  input_is_connected(int pad);
extern unsigned int input_get_buttons_held(int pad);
extern int  input_find_controller_index(int pad);
extern int  input_get_controller_id(int slot);
extern void input_set_pad_mapping(int slot, int pad);
extern void hud_set_focus_by_id(int screen, int id);
extern void hud_screen_pop_current(void);
extern void hud_screen_reset_scroll(int screen);
extern void hud_screen_navigate_to(int frameHi, int frameLo, int arg);
extern int  player_slot_bind_entity(int slot, void *data, int arg);
extern void player_slot_set_properties(int slot, int a2, int a3, int a4, unsigned int hash,
                                       int idx);
extern int  player_get_character_id(int slot);
extern int  player_get_weapon_id(int slot);
extern void keyboard_input_update(int screen);
extern void sound_init_play_params(int params);
extern void sound_play_request(int *params);
extern unsigned long long sound_reset_streams(void);
extern void music_queue_track(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                              unsigned long long f4, unsigned long long f5, unsigned long long f6,
                              unsigned long long f7, unsigned long long f8, int hi, int lo,
                              char *s, int *r6, int r7, int r8, int r9, int r10);
extern void stats_merge_all_players(void);
extern void VIWaitForRetrace(void);
extern unsigned int FUN_8039def4(void);
extern void FUN_8039dcf4(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                         unsigned long long f4, unsigned long long f5, unsigned long long f6,
                         unsigned long long f7, unsigned long long f8, int a9, unsigned int a10,
                         int a11, int *r6, int r7, int r8, int r9, int r10);
extern void FUN_803ac020(void);
extern void FUN_803a78ac(int a);
extern void FUN_803a7710(void);
extern unsigned long long FUN_803826e4(void);   // spill: fetch entry frame
extern void FUN_80382730(void);                  // spill: restore entry frame
extern int  FUN_801a66f4(void);                  // keyboard-entry busy poll
extern int  DAT_805c1620;                        // keyboard-entry name buffer
} // extern "C"

// Intra-file forward decls for the batch_529 helpers the state machine calls
// before their definitions appear.
unsigned int memcard_session_scan_slot(void);
void memcard_session_scan_slot_cb(int frameHi, int frameLo, int ctxA, int ctxB);
int  memcard_session_find_record(void);
int  memcard_session_find_by_hash(void);
void memcard_session_finish(void);
void memcard_session_next_player(void);
void memcard_session_reassign_pad(void);
void memcard_session_resume_op(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                               unsigned long long f4, unsigned long long f5, unsigned long long f6,
                               unsigned long long f7, unsigned long long f8);

// ---------------------------------------------------------------------------
// memcard_session_scan_slot_cb (FUN_801a3794)
// Per-entry callback for the per-slot CARD directory scan. Validates the entry
// magic tag (DAT_80610b38) and game-id fields (DAT_80612370); on a full match
// appends it to the active scan bank's record table
// (DAT_805c0900 + bank*0x690 + count*0x38), stashing size + command context.
// ---------------------------------------------------------------------------
void memcard_session_scan_slot_cb(int frameHi, int frameLo, int ctxA, int ctxB) {
    (void)frameHi; (void)frameLo;
    unsigned long long frame = FUN_803826e4();
    int *entry = (int *)(unsigned int)(frame >> 0x20);
    int recLen = FUN_8038b5c8((int)entry);
    if (FUN_8038b378((unsigned int *)((int)entry + recLen + -4), (unsigned int *)&DAT_80610b38) == 0
        && FUN_80387b68((int)(entry + 10), DAT_80612370, 4) == 0
        && FUN_80387b68((int)(entry + 0xb), DAT_80612370 + 4, 2) == 0) {
        int count = *(int *)(&DAT_806125a4 + DAT_80612530 * 4);
        int bankOff = DAT_80612530 * 0x690;
        *(int *)(&DAT_806125a4 + DAT_80612530 * 4) = count + 1;
        int *slot = (int *)(&DAT_805c0900 + count * 0x38 + bankOff);
        FUN_8038b510(slot, entry);
        *(unsigned char *)((int)slot + recLen + -4) = 0;
        slot[8] = entry[8];
        slot[9] = entry[9];
        slot[10] = (int)frame;
        slot[0xd] = ctxB;
        slot[0xc] = ctxA;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// memcard_session_scan_slot (FUN_801a38b0)
// Kick a per-slot CARD directory scan (gx_cmd_add_type5 w/ scan_slot_cb) on the
// active bank handle (DAT_80612530) and pump the command list to the terminal
// tag 0xe. Latches the resulting per-handle command context and returns 1 on
// success, else the translated CARD status.
// ---------------------------------------------------------------------------
unsigned int memcard_session_scan_slot(void) {
    bool done = false;
    int outTag = 0;
    int outCode = 0;
    int outCtxA = 0;
    int outCtxB = 0;
    notif_set_player_slot(0);
    *(int *)(DAT_80612d58 + 8) = 0;
    gx_cmd_add_type5(DAT_80612530, 3, (void *)memcard_session_scan_slot_cb, 0);
    while (true) {
        if (done) return 1;
        int rc = gx_cmd_list_execute(3, &outCode, &outTag, &outCtxA);
        if (rc != 0) break;
        if (outTag == 0xe) {
            done = true;
            (&DAT_805c08e4)[DAT_80612530 * 4] = outCtxB;
            (&DAT_805c08e0)[DAT_80612530 * 4] = outCtxA;
        }
    }
    *(int *)(DAT_80612d58 + 8) = 0;
    (&DAT_805c08e4)[DAT_80612530 * 4] = outCtxB;
    (&DAT_805c08e0)[DAT_80612530 * 4] = outCtxA;
    return (unsigned int)card_translate_result(outCode);
}

// ---------------------------------------------------------------------------
// memcard_session_begin_all_ex (FUN_801a39a8)
// The deferred/all-slot session opener. Builds the pending-player bitmask
// (DAT_80612548): for slot == -1, every live participant (level_mode != 2) up
// to DAT_80611014; otherwise just the one slot. If any players are pending and
// deferred/all-mode is active, push the multi-slot HUD screen (state 0xd);
// otherwise clear the all-mode flag and push the single HUD screen.
// ---------------------------------------------------------------------------
void memcard_session_begin_all_ex(int frameHi, int callbackTable, int slot) {
    (void)callbackTable;
    unsigned long long frame = FUN_803826e8();
    DAT_80612528 = (int)(frame >> 0x20);
    DAT_8061252c = (int)frame;
    DAT_80612548 = 0;
    (void)frameHi;
    if ((DAT_805cc60c & 0x10000) != 0) {
        if (slot == -1) {
            int i = 0;
            while (true) {
                DAT_80612528 = (int)(frame >> 0x20);
                DAT_8061252c = (int)frame;
                if (DAT_80611014 <= i) break;
                long long mode = player_get_level_mode(i);
                if (mode != 2) {
                    DAT_80612548 = DAT_80612548 | (1 << i);
                }
                i++;
            }
        } else {
            long long mode = player_get_level_mode(slot);
            if (mode != 2) {
                DAT_80612548 = DAT_80612548 | (1 << slot);
            }
        }
        DAT_80612528 = (int)(frame >> 0x20);
        DAT_8061252c = (int)frame;
        if (DAT_80612548 != 0) {
            hud_screen_push();
            DAT_80612540 = 0xd;
            FLOAT_80612558 = FLOAT_80616b38;
            FUN_80382734();
            return;
        }
    }
    DAT_80612528 = (int)(frame >> 0x20);
    DAT_8061252c = (int)frame;
    DAT_805cc60c = DAT_805cc60c & 0xfffeffff;
    hud_screen_push();
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// memcard_session_find_record (FUN_801a3ad4)
// Linear scan of the active bank's record table for an entry whose tag matches
// the incoming frame's tag (FUN_8038b378 == 0). Used to check if the current
// player's save already exists on the card.
// ---------------------------------------------------------------------------
int memcard_session_find_record(void) {
    unsigned int *needle = (unsigned int *)FUN_803826e8();
    int i = 0;
    int off = 0;
    while (i < *(int *)(&DAT_806125a4 + DAT_80612564 * 4)
           && FUN_8038b378((unsigned int *)(&DAT_805c0900 + off + DAT_80612564 * 0x690), needle)
              != 0) {
        i++;
        off += 0x38;
    }
    FUN_80382734();
    return i;
}

// ---------------------------------------------------------------------------
// memcard_session_find_by_hash (FUN_801a3b60)
// Linear scan of the active bank's record table for an entry whose name-hash
// (memcard_session_hash_name) matches the incoming frame value. Returns the
// matching index.
// ---------------------------------------------------------------------------
int memcard_session_find_by_hash(void) {
    int needle = (int)FUN_803826e8();
    int i = 0;
    int off = 0;
    while (i < *(int *)(&DAT_806125a4 + DAT_80612564 * 4)
           && needle != (int)memcard_session_hash_name(
                            (unsigned char *)(&DAT_805c0900 + off + DAT_80612564 * 0x690))) {
        i++;
        off += 0x38;
    }
    FUN_80382734();
    return i;
}

// ---------------------------------------------------------------------------
// memcard_session_finish (FUN_801a3c00)
// Session teardown: merge per-player stats, reset the active slot, drain any
// pending load-flush command, and clear the session-busy/state flags.
// ---------------------------------------------------------------------------
void memcard_session_finish(void) {
    stats_merge_all_players();
    DAT_80612550 = 0;
    if (DAT_80612534 != 0) {
        int outTag = 0;
        int outCode = 0;
        int outData[4];
        int rc = 0;
        while (rc == 0 && outTag != 0xe) {
            rc = gx_cmd_list_execute(3, &outCode, &outTag, outData);
        }
        DAT_80612534 = 0;
    }
    DAT_8061253c = 0;
    DAT_80612540 = 0;
    FLOAT_80612558 = FLOAT_80616b38;
}

// ---------------------------------------------------------------------------
// memcard_session_next_player (FUN_801a3c88)
// Advance a single-slot (non-multi) session to the next player and re-enter the
// scan state (1); when past the last player, or in multi mode, push the HUD
// screen and finish the session.
// ---------------------------------------------------------------------------
void memcard_session_next_player(void) {
    if (DAT_8061255c == 0) {
        DAT_80612550 = DAT_80612550 + 1;
        if (DAT_80612550 < DAT_80611014) {
            DAT_80612540 = 1;
            FLOAT_80612558 = FLOAT_80616b38;
            DAT_80612538 = ~((1 << DAT_80612550) - 1U) & 0xf;
            return;
        }
        DAT_80612538 = 1;
        hud_screen_push();
        memcard_session_finish();
        return;
    }
    DAT_80612538 = 1;
    hud_screen_push();
    memcard_session_finish();
}

// ---------------------------------------------------------------------------
// memcard_session_reassign_pad (FUN_801a3d30)
// If a controller other than the active slot's is holding the confirm button,
// swap that pad's mapping onto the active save slot (DAT_80612550) so the
// player physically pressing the button drives the card op. Updates the active
// pad mask (DAT_80612538).
// ---------------------------------------------------------------------------
void memcard_session_reassign_pad(void) {
    FUN_803826f0();
    int pad = 0;
    while (true) {
        if (input_is_connected(pad) != 0 && (input_get_buttons_held(pad) & 0x80) != 0) {
            int otherIdx = input_find_controller_index(pad);
            int activeId = input_get_controller_id(DAT_80612550);
            input_set_pad_mapping(DAT_80612550, pad);
            input_set_pad_mapping(otherIdx, activeId);
            DAT_80612538 = 1 << DAT_80612550;
            break;
        }
        pad++;
        if (3 < pad) break;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// memcard_session_resume_op (FUN_801a4f44)
// Re-issue the CARD command matching the pending resume-op code (DAT_80612598)
// after an error screen was dismissed: load (0xc), save (10/0xa), status-get
// (>8), or delete (0xf); then jump the state machine to the resume state.
// ---------------------------------------------------------------------------
void memcard_session_resume_op(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                               unsigned long long f4, unsigned long long f5, unsigned long long f6,
                               unsigned long long f7, unsigned long long f8) {
    if (DAT_80612598 == 0xc) {
        memcard_session_begin_load(f1, f2, f3, f4, f5, f6, f7, f8);
    } else if (DAT_80612598 < 0xc) {
        if (DAT_80612598 != 10) {
            if (DAT_80612598 < 10) {
                if (8 < DAT_80612598) {
                    gx_cmd_add_type6(DAT_80612564, DAT_80612564,
                                     &DAT_805c08e0 + DAT_80612564 * 4);
                }
            } else {
                memcard_session_save_block(f1, f2, f3, f4, f5, f6, f7, f8);
            }
        }
    } else if (DAT_80612598 == 0xf) {
        gx_cmd_add_type7(DAT_80612564, DAT_80612564,
                         *(int *)(&DAT_805c0928 + DAT_80612564 * 0x690 + DAT_80612554 * 0x38),
                         &DAT_805c08e0 + DAT_80612564 * 4);
    }
    DAT_80612540 = DAT_80612598;
    FLOAT_80612558 = FLOAT_80616b38;
}

// ---------------------------------------------------------------------------
// memcard_session_update (FUN_801a3dd8)
// The memory-card session per-frame state machine. Advances the current HUD
// screen (focus id DAT_80610b30), accumulates the input-repeat timer, services
// the deferred command-list swap (double-buffered banks DAT_80612530), then
// dispatches on the session state code DAT_80612540:
//   1  arm scan            2  browse/select record (input)      3  name entry begin
//   4  name entry (keyboard)  5  name entry commit-wait
//   7  error/confirm screen (per-op branch on DAT_80610b34)
//   8  overwrite confirm    9  status-get pump   10 delete confirm
//   0xb save-poll   0xc load-poll   0xd multi-slot save driver
//   0xe multi-slot save-poll  0xf delete-pump  0x10 delete confirm-2
//   0x11 single-slot top menu
// Uses the CARD ops built above (begin_load/save_block/save_poll/load_poll) and
// the scan/find/finish/next-player helpers. FP args f1..f8 are register-spill
// context threaded into the varargs-style music/render/save helpers.
// ---------------------------------------------------------------------------
void memcard_session_update(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                            unsigned long long f4, unsigned long long f5, unsigned long long f6,
                            unsigned long long f7, unsigned long long f8) {
    int local_cc, local_d0, local_b0, local_ac;
    int local_d4, local_d8; int local_b8, local_b4;
    int local_dc, local_e0; int auStack_c0[2];
    int local_e4, local_e8; int local_c8, local_c4;
    int local_a8[12], local_78[12], local_48[12];

    int screen = __save_gpr_28();

    if (DAT_80610b4c != DAT_80612540 && DAT_80610b30 != -1) {
        hud_set_focus_by_id(screen, DAT_80610b30);
        DAT_80610b30 = -1;
    }

    if (DAT_8061237c != 0 || video_mode_is_progressive() != 0 || DAT_80610aa0 == -1) {
        // input-repeat timer accumulate
        unsigned int rawXor = (unsigned int)DAT_806109c0 ^ 0x80000000;
        double timeF = (double)FLOAT_80616b40 /
                       ((double)(long long)(0x4330000000000000ULL | rawXor) - DOUBLE_80616b50);
        DAT_80610b4c = DAT_80612540;
        FLOAT_80612558 = (float)((double)(float)timeF * (double)FLOAT_806108ec
                                 + (double)FLOAT_80612558);
        input_stub_nop();

        // deferred command-list swap between the two scan banks
        if (DAT_80612534 == 0 && ((unsigned int)(DAT_80612540 - 1) < 2 || DAT_80612540 == 0x11)) {
            gx_cmd_add_type0(DAT_80612530, 3);
            DAT_80612534 = 1;
        } else if (DAT_80612534 != 0) {
            int rc = gx_cmd_list_execute(3, &local_cc, &local_d0, &local_b0);
            int bank = DAT_80612530;
            if ((rc != 0 || local_d0 == 0xe) && local_cc != -1) {
                if (rc == 0) {
                    int r = -local_cc + -3;
                    (&DAT_805c08e4)[DAT_80612530 * 4] = local_ac;
                    *(&DAT_805c08e0 + DAT_80612530 * 4) = local_b0;
                    *(int *)(&DAT_806125a4 + bank * 4) = 0;
                    (&DAT_805c08e8)[bank * 4] = r - ((r == 0 ? 1 : 0) + -local_cc + -4);
                } else {
                    if ((&DAT_805c08e4)[DAT_80612530 * 4] != 0
                        || *(&DAT_805c08e0 + DAT_80612530 * 4) != 0) {
                        *(int *)(&DAT_806125a4 + DAT_80612530 * 4) = 0;
                    }
                    int r = -local_cc + -3;
                    (&DAT_805c08e4)[bank * 4] = 0;
                    *(&DAT_805c08e0 + DAT_80612530 * 4) = 0;
                    (&DAT_805c08e8)[bank * 4] = r - ((r == 0 ? 1 : 0) + -local_cc + -4);
                }
                DAT_80612534 = 0;
                DAT_80612530 = 1 - DAT_80612530;
            }
        }

        switch (DAT_80612540) {
        case 1:
            DAT_8061259c = 1;
            DAT_80612540 = 2;
            FLOAT_80612558 = FLOAT_80616b38;
            break;
        case 2: {
            DAT_8061259c = 1;
            if (FLOAT_80616b44 <= *(float *)(screen + 0x1b4)) {
                if (*(int *)(screen + 0x188) == -1) {
                    hud_set_focus_by_id(screen, -6);
                }
                if ((input_poll_all_pads() & 0x10) != 0) {
                    if (DAT_80612550 == 0) {
                        hud_screen_pop_current();
                    } else {
                        DAT_80612550 = DAT_80612550 - 1;
                        DAT_80612538 = ~((1 << DAT_80612550) - 1U) & 0xf;
                    }
                }
                if ((input_poll_all_pads() & 0x180) != 0 || (input_poll_all_pads() & 0x40) != 0) {
                    unsigned int held = input_poll_all_pads();
                    int focus = *(int *)(screen + 0x188);
                    bool guard = (held & 0x40) == 0
                        || (((*(unsigned int *)(*(int *)(screen + 0x194) + 0x28) & 1) == 0
                             && focus != -6 && focus != -0x3ee && focus != -10 && -1 < focus));
                    if (guard) {
                        if ((input_poll_all_pads() & 0x40) == 0) {
                            if ((*(unsigned int *)(*(int *)(screen + 0x194) + 0x28) & 1) == 0) {
                                int f = *(int *)(screen + 0x188);
                                if (f == -10) {
                                    memcard_session_reassign_pad();
                                    player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                                    memcard_session_next_player();
                                } else if (f == -6 || f == -0x3ee) {
                                    DAT_80612540 = 3;
                                    FLOAT_80612558 = FLOAT_80616b38;
                                    memcard_session_reassign_pad();
                                } else if (-1 < f) {
                                    if (DAT_80611a54 != 0) {
                                        int sid = *(int *)(DAT_80611a58 + 8);
                                        sound_init_play_params((int)local_a8);
                                        local_a8[0] = sid;
                                        sound_play_request(local_a8);
                                    }
                                    hud_screen_reset_scroll(screen);
                                    DAT_80612564 = *(int *)(screen + 0x188) / 1000;
                                    DAT_80612554 = *(int *)(screen + 0x188) % 1000;
                                    int *ent = (int *)player_slot_bind_entity(
                                        DAT_80612550,
                                        (void *)(&DAT_805c0900 + f * 0x38 + (f / 1000) * -0xd430),
                                        0);
                                    int e = *ent;
                                    *(int *)(e + 0x24) = (&DAT_805c08e4)[DAT_80612564 * 4];
                                    *(int *)(e + 0x20) = (&DAT_805c08e0)[DAT_80612564 * 4];
                                    *(short *)(*ent + 0x2e) = (short)DAT_80612564;
                                    DAT_806125b4 = *ent;
                                    memcard_session_begin_load(f1, f2, f3, f4, f5, f6, f7, f8);
                                    DAT_80612540 = 0xc;
                                    FLOAT_80612558 = FLOAT_80616b38;
                                    memcard_session_reassign_pad();
                                }
                            } else if (DAT_80611a54 != 0) {
                                int sid = *(int *)(DAT_80611a58 + 0xc);
                                sound_init_play_params((int)local_78);
                                local_78[0] = sid;
                                sound_play_request(local_78);
                            }
                        } else {
                            memcard_session_reassign_pad();
                            DAT_80612564 = *(int *)(screen + 0x188) / 1000;
                            DAT_80612554 = *(int *)(screen + 0x188) % 1000;
                            int nameId = get_string_by_id(DAT_80612550 + 0x396);
                            player_slot_bind_entity(DAT_80612550, (void *)(unsigned int)nameId, 0);
                            player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                            DAT_80612540 = 0x10;
                            FLOAT_80612558 = FLOAT_80616b38;
                            DAT_80610b30 = 2;
                        }
                    } else if (DAT_80611a54 != 0) {
                        int sid = *(int *)(DAT_80611a58 + 0xc);
                        sound_init_play_params((int)local_48);
                        local_48[0] = sid;
                        sound_play_request(local_48);
                    }
                }
            }
            break;
        }
        case 3: {
            DAT_806125ac = 0;
            DAT_806125b0 = 0;
            hud_screen_reset_scroll(screen);
            int nameId = get_string_by_id(DAT_80612550 + 0x396);
            FUN_8038b510((int *)&DAT_805c1620, (int *)(unsigned int)nameId);
            DAT_80612540 = 4;
            DAT_80610b30 = 0;
            FLOAT_80612558 = FLOAT_80616b38;
            DAT_8061259c = 4;
            break;
        }
        case 4:
            if (DAT_806125b0 == 0) {
                if ((input_poll_all_pads() & 0x10) != 0) {
                    DAT_806125ac = 0;
                    DAT_806125b0 = 0;
                    DAT_80612540 = 1;
                    FLOAT_80612558 = FLOAT_80616b38;
                }
            } else {
                player_slot_bind_entity(DAT_80612550, &DAT_805c1620, 0);
                if (FUN_801a66f4() != 0) {
                    DAT_80612598 = 5;
                    DAT_806125ac = 0;
                    DAT_806125b0 = 0;
                    break;
                }
                DAT_806125ac = 0;
                DAT_806125b0 = 0;
                DAT_80612540 = 0x11;
                FLOAT_80612558 = FLOAT_80616b38;
            }
            keyboard_input_update(screen);
            break;
        case 5:
            if (FUN_801a66f4() == 0) {
                DAT_80612540 = 0x11;
                FLOAT_80612558 = FLOAT_80616b38;
            } else {
                DAT_80612598 = 5;
            }
            break;
        case 7:
            if (FLOAT_80616b48 <= *(float *)(screen + 0x1b4)) {
                if ((input_poll_all_pads() & 0x10) != 0 && DAT_8061255c == 0) {
                    DAT_80612540 = DAT_8061259c;
                    FLOAT_80612558 = FLOAT_80616b38;
                }
                if ((input_poll_all_pads() & 0x18000000000ULL) != 0) {
                    switch (DAT_80610b34) {
                    default:
                        DAT_80612540 = DAT_8061259c;
                        FLOAT_80612558 = FLOAT_80616b38;
                        break;
                    case 2:
                    case 0x15: {
                        int f = *(int *)(screen + 0x188);
                        if (f == 2) {
                            sound_reset_streams();
                            music_queue_track(f1, f2, f3, f4, f5, f6, f7, f8, 0, 0,
                                              (char *)0, (int *)0, 0, 0, 0, 0);
                            FUN_803ac020();
                            FUN_803a78ac(1);
                            FUN_803a7710();
                            VIWaitForRetrace();
                            unsigned int mode = FUN_8039def4();
                            FUN_8039dcf4(f1, f2, f3, f4, f5, f6, f7, f8, 1, mode, 1,
                                         (int *)0, 0, 0, 0, 0);
                        } else if (f < 2) {
                            if (0 < f) memcard_session_resume_op(f1, f2, f3, f4, f5, f6, f7, f8);
                        } else if (f < 4) {
                            player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                            if (DAT_8061259c == 0xd) {
                                DAT_80612548 = DAT_80612548 & ~(1 << DAT_8061254c);
                                DAT_80612540 = DAT_8061259c;
                                FLOAT_80612558 = FLOAT_80616b38;
                            } else {
                                memcard_session_next_player();
                            }
                        }
                        break;
                    }
                    case 4:
                    case 0xd:
                    case 0xf:
                    case 0x12:
                    case 0x13: {
                        int f = *(int *)(screen + 0x188);
                        if (f == 2) {
                            player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                            if (DAT_8061259c == 0xd) {
                                DAT_80612548 = DAT_80612548 & ~(1 << DAT_8061254c);
                                DAT_80612540 = DAT_8061259c;
                                FLOAT_80612558 = FLOAT_80616b38;
                            } else {
                                memcard_session_next_player();
                            }
                        } else if (f < 2 && 0 < f) {
                            memcard_session_resume_op(f1, f2, f3, f4, f5, f6, f7, f8);
                        }
                        break;
                    }
                    case 0xe:
                    case 0x11: {
                        int f = *(int *)(screen + 0x188);
                        if (f == 2) {
                            DAT_80612564 = DAT_80612544;
                            DAT_80612540 = 10;
                            FLOAT_80612558 = FLOAT_80616b38;
                        } else if (f < 2) {
                            if (0 < f) memcard_session_resume_op(f1, f2, f3, f4, f5, f6, f7, f8);
                        } else if (f < 4) {
                            player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                            if (DAT_8061259c == 0xd) {
                                DAT_80612548 = DAT_80612548 & ~(1 << DAT_8061254c);
                                DAT_80612540 = DAT_8061259c;
                                FLOAT_80612558 = FLOAT_80616b38;
                            } else {
                                memcard_session_next_player();
                            }
                        }
                        break;
                    }
                    case 0x14: {
                        int f = *(int *)(screen + 0x188);
                        if (f == 2) {
                            local_e8 = 0xf;
                            int rc = 0;
                            local_c4 = 0;
                            local_c8 = 0;
                            memcard_session_flush_load();
                            gx_cmd_add_type0(DAT_80612564, DAT_80612564);
                            while (rc == 0 && local_e8 != 0xe) {
                                int *sd = (int *)player_slot_get_data(DAT_80612550);
                                int e = *sd;
                                *(int *)(e + 0x24) = local_c4;
                                *(int *)(e + 0x20) = local_c8;
                                rc = gx_cmd_list_execute(DAT_80612564, &local_e4, &local_e8,
                                                         &local_c8);
                                int h = DAT_80612564;
                                (&DAT_805c08e4)[DAT_80612564 * 4] = local_c4;
                                int r = -local_e4 + -3;
                                (&DAT_805c08e0)[DAT_80612564 * 4] = local_c8;
                                (&DAT_805c08e8)[h * 4] = r - ((r == 0 ? 1 : 0) + -local_e4 + -4);
                            }
                            if (rc == 0) {
                                if (DAT_80612598 == 0xb) {
                                    memcard_session_resume_op(f1, f2, f3, f4, f5, f6, f7, f8);
                                } else if (DAT_80612598 == 0xc) {
                                    DAT_80612540 = 1;
                                    FLOAT_80612558 = FLOAT_80616b38;
                                } else {
                                    DAT_80612540 = DAT_80612598;
                                    FLOAT_80612558 = FLOAT_80616b38;
                                }
                            } else {
                                DAT_80610b34 = card_translate_result(local_e4);
                            }
                        } else if (f < 2 && 0 < f) {
                            memcard_session_resume_op(f1, f2, f3, f4, f5, f6, f7, f8);
                        }
                        break;
                    }
                    case 0x16: {
                        int f = *(int *)(screen + 0x188);
                        if (f == 3) {
                            DAT_80612540 = 1;
                            FLOAT_80612558 = FLOAT_80616b38;
                        } else if (f < 3) {
                            if (f == 1) {
                                memcard_session_begin_load(f1, f2, f3, f4, f5, f6, f7, f8);
                                DAT_80612540 = 0xc;
                                FLOAT_80612558 = FLOAT_80616b38;
                            } else if (0 < f) {
                                DAT_80612540 = 0x10;
                                FLOAT_80612558 = FLOAT_80616b38;
                                DAT_80610b30 = 2;
                            }
                        } else if (f < 5) {
                            player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                            memcard_session_next_player();
                        }
                        break;
                    }
                    }
                }
            }
            break;
        case 8:
            if (FLOAT_80616b48 <= *(float *)(screen + 0x1b4)) {
                if ((input_poll_all_pads() & 0x180) == 0) {
                    if ((input_poll_all_pads() & 0x10) != 0) {
                        DAT_80612540 = DAT_8061259c;
                        FLOAT_80612558 = FLOAT_80616b38;
                    }
                } else if (*(int *)(screen + 0x188) == 1) {
                    int *sd = (int *)player_slot_get_data(DAT_80612550);
                    int e = *sd;
                    *(int *)(e + 0x24) = (&DAT_805c08e4)[DAT_80612564 * 4];
                    *(int *)(e + 0x20) = (&DAT_805c08e0)[DAT_80612564 * 4];
                    *(short *)(*sd + 0x2e) = (short)DAT_80612564;
                    memcard_session_save_block(f1, f2, f3, f4, f5, f6, f7, f8);
                    DAT_80612540 = 0xb;
                    FLOAT_80612558 = FLOAT_80616b38;
                } else {
                    DAT_80612540 = DAT_8061259c;
                    FLOAT_80612558 = FLOAT_80616b38;
                }
            }
            break;
        case 9:
            if (FLOAT_80616b44 <= FLOAT_80612558) {
                int rc = gx_cmd_list_execute(DAT_80612564, &local_d4, &local_d8, &local_b8);
                if (rc == 0) {
                    if (local_d8 == 0xe) {
                        (&DAT_805c08e4)[DAT_80612564 * 4] = local_b4;
                        DAT_80612540 = DAT_8061259c;
                        (&DAT_805c08e0)[DAT_80612564 * 4] = local_b8;
                        FLOAT_80612558 = FLOAT_80616b38;
                    }
                } else {
                    DAT_80610b34 = card_translate_result(local_d4);
                    DAT_80612540 = 7;
                    FLOAT_80612558 = FLOAT_80616b38;
                }
            }
            break;
        case 10:
            if (FLOAT_80616b44 <= *(float *)(screen + 0x1b4)) {
                if ((input_poll_all_pads() & 0x180) == 0) {
                    if ((input_poll_all_pads() & 0x10) != 0) {
                        DAT_80612540 = 7;
                        FLOAT_80612558 = FLOAT_80616b38;
                    }
                } else {
                    int f = *(int *)(screen + 0x188);
                    if (f == 2) {
                        DAT_80612540 = 7;
                        FLOAT_80612558 = FLOAT_80616b38;
                    } else if (f < 2 && 0 < f) {
                        DAT_80612540 = 9;
                        FLOAT_80612558 = FLOAT_80616b38;
                        gx_cmd_add_type6(DAT_80612564, DAT_80612564,
                                         &DAT_805c08e0 + DAT_80612564 * 4);
                    }
                }
            }
            break;
        case 0xb:
            if (FLOAT_80616b44 <= FLOAT_80612558) {
                int r = memcard_session_save_poll();
                if (r < 1) {
                    if (r == -1) {
                        unsigned char *ent = (unsigned char *)player_get_entity_ptr(DAT_80612550);
                        unsigned int h = memcard_session_hash_name(ent);
                        player_slot_set_properties(DAT_80612550, DAT_80612564 * 0x10,
                                                   (&DAT_805c08e0)[DAT_80612564 * 4],
                                                   (&DAT_805c08e4)[DAT_80612564 * 4], h,
                                                   (short)DAT_80612564);
                        memcard_session_next_player();
                    }
                } else {
                    DAT_80612544 = DAT_80612564;
                    DAT_80612598 = 0xb;
                    DAT_80612540 = 7;
                    FLOAT_80612558 = FLOAT_80616b38;
                    DAT_80610b34 = r;
                }
            }
            break;
        case 0xc:
            if (FLOAT_80616b44 <= FLOAT_80612558) {
                int r = memcard_session_load_poll();
                DAT_80612598 = 0xc;
                if (r == -1) {
                    int *sd = (int *)player_slot_get_data(DAT_80612550);
                    *sd = DAT_806125b4;
                    *(int **)(DAT_806125b4 + 0x38) = sd;
                    unsigned char *ent = (unsigned char *)player_get_entity_ptr(DAT_80612550);
                    unsigned int h = memcard_session_hash_name(ent);
                    player_slot_set_properties(DAT_80612550, DAT_80612564 * 0x10,
                                               (&DAT_805c08e0)[DAT_80612564 * 4],
                                               (&DAT_805c08e4)[DAT_80612564 * 4], h,
                                               (short)DAT_80612564);
                    memcard_session_next_player();
                } else if (r != 0) {
                    DAT_80612544 = DAT_80612564;
                    DAT_80612540 = 7;
                    FLOAT_80612558 = FLOAT_80616b38;
                    DAT_80610b34 = r;
                }
            }
            break;
        case 0xd: {
            DAT_8061255c = 1;
            bool started = false;
            DAT_8061259c = 0xd;
            int i = 0;
            int n = 4;
            do {
                if ((DAT_80612548 & (1 << i)) != 0) {
                    DAT_80612564 = player_get_character_id(i);
                    player_get_weapon_id(i);
                    DAT_80612554 = memcard_session_find_by_hash();
                    memcard_session_save_block(f1, f2, f3, f4, f5, f6, f7, f8);
                    DAT_80612540 = 0xe;
                    started = true;
                    FLOAT_80612558 = FLOAT_80616b38;
                    DAT_8061254c = i;
                    DAT_80612550 = i;
                    break;
                }
                i++;
                n--;
            } while (n != 0);
            if (!started) {
                DAT_8061255c = 0;
                hud_screen_navigate_to(DAT_80612528, DAT_8061252c, -1);
                memcard_session_finish();
            }
            break;
        }
        case 0xe:
            if (FLOAT_80616b44 <= FLOAT_80612558) {
                int r = memcard_session_save_poll();
                if (r == -1) {
                    DAT_80612548 = DAT_80612548 & ~(1 << DAT_8061254c);
                    DAT_80612540 = 0xd;
                    FLOAT_80612558 = FLOAT_80616b38;
                } else if (r != 1 && r != 0) {
                    DAT_80612544 = DAT_80612564;
                    DAT_8061259c = 0xd;
                    DAT_80612598 = 0xd;
                    DAT_80612540 = 7;
                    FLOAT_80612558 = FLOAT_80616b38;
                    DAT_80610b34 = r;
                }
            }
            break;
        case 0xf:
            if (FLOAT_80616b44 <= FLOAT_80612558) {
                int rc = gx_cmd_list_execute(DAT_80612564, &local_dc, &local_e0, auStack_c0);
                DAT_80612598 = 0xf;
                if (rc == 0) {
                    if (local_e0 == 0xe) {
                        DAT_80612540 = DAT_8061259c;
                        FLOAT_80612558 = FLOAT_80616b38;
                    }
                } else {
                    DAT_80612544 = DAT_80612564;
                    DAT_80610b34 = card_translate_result(local_dc);
                    DAT_80612540 = 7;
                    FLOAT_80612558 = FLOAT_80616b38;
                }
            }
            break;
        case 0x10:
            if (FLOAT_80616b48 <= *(float *)(screen + 0x1b4)) {
                if ((input_poll_all_pads() & 0x180) == 0) {
                    if ((input_poll_all_pads() & 0x10) != 0) {
                        DAT_80612540 = DAT_8061259c;
                        FLOAT_80612558 = FLOAT_80616b38;
                    }
                } else {
                    int f = *(int *)(screen + 0x188);
                    if (f == 2) {
                        DAT_80612540 = DAT_8061259c;
                        FLOAT_80612558 = FLOAT_80616b38;
                    } else if (f < 2 && 0 < f) {
                        DAT_80612540 = 0xf;
                        FLOAT_80612558 = FLOAT_80616b38;
                        gx_cmd_add_type7(DAT_80612564, DAT_80612564,
                                         *(int *)(&DAT_805c0928 + DAT_80612564 * 0x690
                                                  + DAT_80612554 * 0x38),
                                         &DAT_805c08e0 + DAT_80612564 * 4);
                    }
                }
            }
            break;
        case 0x11:
            DAT_806125ac = 0;
            DAT_806125b0 = 0;
            if (DAT_8061259c != 0xd) {
                DAT_8061259c = 0x11;
            }
            if (*(int *)(screen + 0x188) == -1) {
                hud_set_focus_by_id(screen, 1);
            }
            if (FLOAT_80616b44 <= FLOAT_80612558) {
                if ((input_poll_all_pads() & 0x18000000000ULL) == 0
                    || (*(unsigned int *)(*(int *)(screen + 0x194) + 0x28) & 1) != 0) {
                    if ((input_poll_all_pads() & 0x10) != 0) {
                        if (DAT_8061255c == 0) {
                            DAT_80612540 = 4;
                            FLOAT_80612558 = FLOAT_80616b38;
                        } else {
                            hud_screen_push();
                            memcard_session_finish();
                        }
                    }
                } else {
                    int f = *(int *)(screen + 0x188);
                    if (f == 3) {
                        int *sd = (int *)player_slot_get_data(DAT_80612550);
                        int e = *sd;
                        *(int *)(e + 0x24) = 0;
                        *(int *)(e + 0x20) = 0;
                        player_slot_set_properties(DAT_80612550, 0, 0, 2, 0, 0xffff);
                        memcard_session_next_player();
                    } else if (f < 3 && 0 < f) {
                        DAT_80612564 = f - 1;
                        if (memcard_session_find_record() == 0) {
                            DAT_80612544 = DAT_80612564;
                            DAT_80612540 = 8;
                            FLOAT_80612558 = FLOAT_80616b38;
                        } else {
                            int *sd = (int *)player_slot_get_data(DAT_80612550);
                            int e = *sd;
                            *(int *)(e + 0x24) = (&DAT_805c08e4)[DAT_80612564 * 4];
                            *(int *)(e + 0x20) = (&DAT_805c08e0)[DAT_80612564 * 4];
                            *(short *)(*sd + 0x2e) = (short)DAT_80612564;
                            memcard_session_save_block(f1, f2, f3, f4, f5, f6, f7, f8);
                            DAT_80612540 = 0xb;
                            FLOAT_80612558 = FLOAT_80616b38;
                        }
                    }
                }
            }
            break;
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// batch_530 additions: the session HUD builder/entry layer plus the standalone
// memory-card *presence-check* screen. Address range 0x801a5040 - 0x801a6c7c.
//   memcard_session_can_confirm / _build_menu / _build_screen / _reset /
//   _scan_all  drive the save/load session HUD (state machine in _update).
//   memcard_check_* are a separate boot/profile "is a card present & valid"
//   screen (own globals DAT_806125xx / magic DAT_80610b6c, notif_* driven).
// ===========================================================================

extern "C" {
// --- HUD element / list builders (extern until defined) ---
extern int  hud_find_child_by_id(int screen, int id);
extern int  hud_add_list_item(void);
extern int  hud_add_callback_item(double t);
extern void hud_item_add_spacer(double t, int flags);
extern void hud_item_set_locked(int item, int locked);
extern double hud_item_calc_center_x(int item);
extern double hud_item_calc_center_y(int item);
extern void hud_screen_register_handler_global(int slot, int strId, int a3, int a4);
extern void hud_screen_remove_handler(int screen, int slot);
extern void hud_screen_rebuild_items(void);
extern void hud_element_init_from_layout(unsigned int flags, int *layout, int a3, int a4);
extern unsigned long long hud_element_scroll_horizontal(double t, int a2);
extern int  hud_element_get_dimension(int el, int axis);
extern void hud_element_apply_scroll(void);
extern void hud_element_apply_center_xy(int a);
extern void hud_element_finalize_frame(void);
extern int  hud_element_is_closing(int el);

// --- text / keyboard / format helpers ---
extern void text_set_font_table(int table);
extern void text_get_char_width(int *s);
extern unsigned long long keyboard_open_inline(int screen, int *buf, int maxLen, int a4, int flags,
                                               void *cb, int a7);
extern void format_string_varargs(double f1, unsigned long long f2, unsigned long long f3,
                                  unsigned long long f4, unsigned long long f5,
                                  unsigned long long f6, unsigned long long f7,
                                  unsigned long long f8, int dst, int fmt, int *arg0, int arg1,
                                  int arg2, int r8, int r9, int r10);

// --- notif (notification-feed) driver used by the presence-check screen ---
extern void notif_set_active(void);
extern int  notif_is_visible(void);
extern unsigned long long notif_set_player_data(int slot);
extern void input_reset_pad(int pad);

// --- presence-check screen helpers (FUN_8033xxxx) ---
extern void FUN_8033a784(unsigned long long acc, unsigned long long f2, unsigned long long f3,
                         unsigned long long f4, unsigned long long f5, unsigned long long f6,
                         unsigned long long f7, unsigned long long f8, int screen, int code,
                         int a11, int a12, int a13, int a14, int a15, int a16);
extern unsigned int FUN_8033ad24(void);
extern void FUN_8033be18(double f1, double f2, double f3, unsigned long long f4,
                         unsigned long long f5, unsigned long long f6, unsigned long long f7,
                         unsigned long long f8, int screen, int a10, int a11, int a12, int a13,
                         int a14, int a15, int a16);
extern void FUN_8033c2e0(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                         unsigned long long f4, unsigned long long f5, unsigned long long f6,
                         unsigned long long f7, unsigned long long f8, int screen);
extern int  FUN_803826bc(void);   // spill: fetch screen frame
extern void FUN_80382708(void);   // spill: restore screen frame

// --- batch_530 new globals ---
extern int   DAT_80610b68;   // scan-all arm flag
extern int   DAT_80610b6c;   // presence-check expected magic tag
extern int   DAT_806125a0;   // default error-string id
extern int   DAT_80612d10;   // card-present latched flag
extern int   DAT_8061256c;   // presence-check active flag
extern int   DAT_80612580;   // presence-check scan-match flag
extern int   DAT_80612584;   // presence-check active handle
extern int   DAT_80612588;   // presence-check active slot
extern int   DAT_8061258c;   // presence-check result/state
extern int   DAT_80612590;   // presence-check phase
extern int   DAT_805c08f8;   // per-handle command context (slot 1 group)
extern int   DAT_805c1634;   // browse-name work ptr
extern int   DAT_805c1654;   // format scratch buffer
extern int   DAT_805c1854;   // screen layout descriptor
extern int   DAT_806119e0;   // active screen root element
extern int   DAT_80610b50;   // "%s %d" browse-title format
extern int   DAT_80610b5c;   // "%c" slot-letter format
extern int   DAT_80610b60;   // overwrite-confirm format
extern int   DAT_80610b64;   // name-entry font glyph probe
extern int   s__s__d_80610b50; // "%s %d" string
extern int   PTR_FUN_8042df80; // save-menu screen vtable A
extern int   PTR_FUN_8042df90; // save-menu screen vtable B
extern char  LAB_801a3d24;     // keyboard-commit callback trampoline
extern float FLOAT_80616b3c;
extern float FLOAT_80616b48;
extern float FLOAT_80616b58;
extern float FLOAT_80616b5c;
extern float FLOAT_80616b60;
extern float FLOAT_80616b64;
extern float FLOAT_80616b68;
extern float FLOAT_80616b6c;
extern float FLOAT_80616b70;
extern float FLOAT_80616b8c;
extern float FLOAT_80616b90;
extern float FLOAT_8061257c;
extern float FLOAT_80616b40;
extern double DOUBLE_80616b50;
} // extern "C"

// intra-file forward decls (batch_530)
int  memcard_session_scan_all(void);   // FUN_801a66f4 returns void but callers ignore
void memcard_session_build_menu(unsigned long long f1, unsigned long long f2,
                                unsigned long long f3, unsigned long long f4,
                                unsigned long long f5, unsigned long long f6,
                                unsigned long long f7, unsigned long long f8);
unsigned int memcard_check_scan(void);
void memcard_check_scan_cb(int entry);
void memcard_check_dismiss(void);

// ---------------------------------------------------------------------------
// memcard_session_can_confirm (FUN_801a5040)
// Predicate: for the two save-menu screen vtables, decide whether the current
// focus item is a confirmable card action. In name-entry state (4) defers to
// FUN_8038b5c8; otherwise requires a live, unlocked, real (>= 0) focus id that
// is not one of the reserved nav ids (-6, -0x3ee, -10). Returns 1 confirmable,
// 0 not, -1 wrong screen.
// ---------------------------------------------------------------------------
int memcard_session_can_confirm(int screen) {
    if (*(void ***)(screen + 4) == (void **)&PTR_FUN_8042df80
        || *(void ***)(screen + 4) == (void **)&PTR_FUN_8042df90) {
        if (DAT_80612540 == 4) {
            return FUN_8038b5c8(-0x7fa3e9e0);
        }
        int r = 0;
        if (*(int *)(screen + 0x194) != 0
            && (*(unsigned int *)(*(int *)(screen + 0x194) + 0x28) & 1) == 0) {
            int focus = *(int *)(screen + 0x188);
            if (focus != -6 && focus != -0x3ee && focus != -10 && -1 < focus) {
                r = 1;
            }
        }
        return r;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// memcard_session_scan_all (FUN_801a66f4)
// Session busy/arming poll. Drains any pending load-flush command, then (when
// armed, DAT_80610b68) scans both card slots (memcard_session_scan) and tallies
// per-slot outcomes into no-card (result 4), other-error, and full/ok counts.
// Depending on the tally drives the state machine to the error screen (7) with
// the appropriate result code (no-card 4, generic ok/duplicate 0x15).
// ---------------------------------------------------------------------------
int memcard_session_scan_all(void) {
    FUN_803826e8();
    if (DAT_80612534 != 0) {
        int rc = 0, tag = 0, code = 0; int outData[8];
        while (rc == 0 && tag != 0xe) {
            rc = gx_cmd_list_execute(3, &code, &tag, outData);
        }
        DAT_80612534 = 0;
    }
    if (DAT_80610b68 != 0) {
        DAT_80610b34 = -1;
        int full = 0, errCount = 0, noCard = 0;
        DAT_80612544 = 0;
        int handle = 0;
        DAT_80612560 = 0;
        do {
            unsigned int r = (unsigned int)memcard_session_scan(handle);
            if ((int)r < 0x4000) {
                if ((int)r < 0) {
                    if ((unsigned int)(-(int)r) == 4) {
                        noCard++;
                    } else {
                        errCount++;
                        if (DAT_80610b34 == -1) {
                            DAT_80610b34 = -(int)r;
                            DAT_80612544 = handle;
                        }
                    }
                } else if (DAT_80610b34 == -1) {
                    errCount++;
                    DAT_80610b34 = 2;
                    DAT_80612544 = handle;
                }
            } else {
                full++;
            }
            handle++;
        } while (handle < 2);
        if (noCard == 2) {
            DAT_80610b34 = 4;
            DAT_8061259c = 0x11;
            DAT_80612540 = 7;
            FLOAT_80612558 = FLOAT_80616b38;
        } else if (noCard + errCount == 2 && full == 0 && DAT_80612560 == 0) {
            DAT_8061259c = 0x11;
            DAT_80612540 = 7;
            FLOAT_80612558 = FLOAT_80616b38;
        } else if (noCard + errCount == 2 && full == 0 && DAT_80612560 != 0) {
            DAT_80610b34 = 0x15;
            DAT_80612540 = 7;
            FLOAT_80612558 = FLOAT_80616b38;
        }
    }
    FUN_80382734();
    return 0;
}

// ---------------------------------------------------------------------------
// memcard_session_reset (FUN_801a661c)
// Reset all session state and (re)enter the save/load session at state 1 (arm
// scan): clear the pad, set the callback table, clear result/flags, drain any
// pending flush, then push the session HUD screen.
// ---------------------------------------------------------------------------
void memcard_session_reset(int frameHi) {
    input_reset_pad(0);
    DAT_8061252c = (int)&PTR_FUN_804771e8;
    DAT_8061253c = 0;
    DAT_8061255c = 0;
    DAT_80612550 = 0;
    DAT_80612560 = 0;
    DAT_80610b34 = -1;
    DAT_80612598 = 1;
    DAT_80612564 = 0;
    DAT_80612544 = 0;
    DAT_80612538 = 1;
    DAT_80612528 = frameHi;
    if (DAT_80612534 != 0) {
        int rc = 0, tag = 0, code = 0; int outData[4];
        while (rc == 0 && tag != 0xe) {
            rc = gx_cmd_list_execute(3, &code, &tag, outData);
        }
        DAT_80612534 = 0;
    }
    DAT_80612540 = 1;
    FLOAT_80612558 = FLOAT_80616b38;
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// memcard_session_build_menu (FUN_801a50ec)
// Per-state HUD list builder for the save/load session screen. Builds the item
// list matching the current state DAT_80612540: state 2 lists both card slots'
// saved records (locking those already in use), 3/4 the name-entry prompt,
// 7 the error/confirm text (per DAT_80610b34), 8/10 overwrite/delete confirm,
// 9/0xb/0xc/0xf progress messages, 0xd/0xe multi-slot progress, 0x11 the top
// menu. Sets the default focus (DAT_80610b30) for each.
// ---------------------------------------------------------------------------
void memcard_session_build_menu(unsigned long long f1, unsigned long long f2,
                                unsigned long long f3, unsigned long long f4,
                                unsigned long long f5, unsigned long long f6,
                                unsigned long long f7, unsigned long long f8) {
    (void)f1;
    int screen = FUN_803826bc();
    switch (DAT_80612540) {
    case 2: {
        int sid = get_string_by_id(0x395);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9cc, (int)&s__s__d_80610b50,
                              (int *)(unsigned int)sid, DAT_80612550 + 1, 0, 0, 0, 0);
        int item = hud_find_child_by_id(screen, -3);
        *(int **)(item + 0x20) = &DAT_805c1634;
        *(float *)(item + 0x40) = (float)hud_item_calc_center_y(item);
        *(float *)(item + 0x3c) = (float)hud_item_calc_center_x(item);
        int back = hud_find_child_by_id(screen, -4);
        if (back == 0) {
            hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
            hud_screen_register_handler_global(3, get_string_by_id(0x966), 0, 0);
        } else {
            *(int *)(back + 0x20) = get_string_by_id(0x390);
            *(int *)(back + 0x28) = 4;
            *(float *)(back + 0x3c) = (float)hud_item_calc_center_x(back);
            *(float *)(back + 0x40) = (float)hud_item_calc_center_y(back);
        }
        hud_item_add_spacer((double)FLOAT_80616b58, 0x20400);
        get_string_by_id(0x410);
        hud_add_list_item();
        double centerY = (double)FLOAT_80616b68;
        int bank = 0, ctxOff = 0, recBase = 0, countIdx = 0;
        do {
            hud_item_add_spacer((double)FLOAT_80616b58, 0x20400);
            int cb;
            if (bank == 0) {
                cb = hud_add_callback_item((double)FLOAT_80616b38);
                *(float *)(cb + 100) = *(float *)(cb + 0x6c) * FLOAT_80616b48 - FLOAT_80616b60;
                *(float *)(cb + 0x6c) = FLOAT_80616b64;
                countLeadingZeros(*(unsigned int *)((int)&DAT_805c08e8 + ctxOff));
                get_string_by_id(0x1954);
                hud_add_list_item();
                cb = hud_add_callback_item((double)FLOAT_80616b38);
                *(float *)(cb + 100) = *(float *)(cb + 0x6c) * FLOAT_80616b48 - FLOAT_80616b60;
                *(float *)(cb + 0x6c) = FLOAT_80616b64;
            } else {
                cb = hud_add_callback_item((double)FLOAT_80616b38);
                *(float *)(cb + 100) = *(float *)(cb + 0x6c) * FLOAT_80616b48 - FLOAT_80616b60;
                *(float *)(cb + 0x6c) = FLOAT_80616b64;
                countLeadingZeros(*(unsigned int *)((int)&DAT_805c08e8 + ctxOff));
                get_string_by_id(0x1955);
                hud_add_list_item();
                cb = hud_add_callback_item((double)FLOAT_80616b38);
                *(float *)(cb + 100) = *(float *)(cb + 0x6c) * FLOAT_80616b48 - FLOAT_80616b60;
                *(float *)(cb + 0x6c) = FLOAT_80616b64;
            }
            int recOff = 0;
            for (int j = 0; j < *(int *)(&DAT_806125a4 + countIdx); j++) {
                int item2 = hud_add_list_item();
                unsigned int h = memcard_session_hash_name(
                    (unsigned char *)(&DAT_805c0900 + recOff + recBase));
                for (int k = 0; k < DAT_80612550; k++) {
                    int wid = player_get_weapon_id(k);
                    if ((int)h == wid
                        && player_get_level_mode(k)
                           == *(long long *)((int)&DAT_805c08e0 + ctxOff)) {
                        hud_item_set_locked(item2, 1);
                    }
                }
                *(float *)(item2 + 0x3c) = (float)centerY;
                recOff += 0x38;
            }
            if (*(int *)((int)&DAT_805c08e8 + ctxOff) == 0) {
                get_string_by_id(0x1981);
                hud_add_list_item();
            } else if (*(int *)(&DAT_806125a4 + countIdx) == 0) {
                if (*(int *)((int)&DAT_805c08e4 + ctxOff) == 0
                    && *(int *)((int)&DAT_805c08e0 + ctxOff) == 0) {
                    get_string_by_id(0x1982);
                    hud_add_list_item();
                } else {
                    get_string_by_id(0x1980);
                    hud_add_list_item();
                }
            }
            bank++;
            recBase += 0x690;
            ctxOff += 0x10;
            countIdx += 4;
        } while (bank < 2);
        DAT_80610b30 = -6;
        break;
    }
    case 3:
    case 4: {
        int item = hud_find_child_by_id(screen, -3);
        *(int *)(item + 0x20) = get_string_by_id(0x40f);
        *(float *)(item + 0x40) = (float)hud_item_calc_center_y(item);
        *(float *)(item + 0x3c) = (float)hud_item_calc_center_x(item);
        DAT_80610b30 = 0;
        int back = hud_find_child_by_id(screen, -4);
        if (back == 0) {
            hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
            hud_screen_register_handler_global(3, get_string_by_id(0x966), 0, 0);
        } else {
            *(int *)(back + 0x20) = get_string_by_id(0xe59);
            *(int *)(back + 0x28) = 4;
            *(float *)(back + 0x3c) = (float)hud_item_calc_center_x(back);
        }
        break;
    }
    case 7: {
        int item = hud_find_child_by_id(screen, -3);
        DAT_8061253c = 1;
        if (item != 0) *(int *)(item + 0x20) = 0;
        if (DAT_8061259c == 0xd) {
            player_get_entity_ptr(DAT_8061254c);
            hud_add_list_item();
            hud_add_callback_item((double)FLOAT_80616b3c);
        }
        switch (DAT_80610b34) {
        default: {
            int s = get_string_by_id(DAT_806125a0);
            format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                                  DAT_80612544 + 0x41, 0, 0, 0, 0);
            hud_add_list_item();
            break;
        }
        case 2:
        case 0x15: {
            int strId = (DAT_80610b34 == 0x15) ? 0x195d : 0x195b;
            int s = get_string_by_id(strId);
            format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, (int)&DAT_805c1654, s,
                                  &DAT_80610b5c, DAT_80612544 + 0x41, 2, 0, 0, 0);
            hud_add_list_item();
            hud_add_list_item();
            get_string_by_id(0x196f); hud_add_list_item();
            get_string_by_id(0x1951); hud_add_list_item();
            get_string_by_id(0x196b); hud_add_list_item();
            break;
        }
        case 4:  case 0xd: case 0xf: case 0x11: case 0x12: case 0x13: case 0x14: case 0x16:
        case 0xe: {
            // per-code error message body (string id table)
            int msgId;
            switch (DAT_80610b34) {
            case 4:    msgId = 0x1945; break;
            case 0xd:  msgId = 0x194a; break;
            case 0xe:  msgId = 0x1947; break;
            case 0xf:  case 0x12: msgId = 0x1949; break;
            case 0x11: msgId = 0x1948; break;
            case 0x13: msgId = 0x194c; break;
            case 0x14: msgId = 0x1957; break;
            default:   msgId = 0x1958; break; // 0x16
            }
            int s = get_string_by_id(msgId);
            format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                                  DAT_80612544 + 0x41, 0, 0, 0, 0);
            hud_add_list_item();
            hud_add_list_item();
            get_string_by_id(0x196f); hud_add_list_item();
            if (DAT_80610b34 == 0xe || DAT_80610b34 == 0x11) {
                get_string_by_id(0x1952); hud_add_list_item();
            }
            if (DAT_80610b34 == 0x14) {
                get_string_by_id(0x197d); hud_add_list_item();
            } else if (DAT_80610b34 == 0x16) {
                get_string_by_id(0x196b); hud_add_list_item();
                get_string_by_id(0x197e); hud_add_list_item();
            } else {
                get_string_by_id(0x196b); hud_add_list_item();
            }
            break;
        }
        }
        DAT_80610b30 = 1;
        break;
    }
    case 8: {
        int ent = player_get_entity_ptr(DAT_80612550);
        int s = get_string_by_id(0x1964);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, (int)&DAT_805c1654, s, &DAT_80610b60,
                              ent, DAT_80612544 + 0x41, 0, 0, 0);
        hud_add_list_item();
        hud_add_list_item();
        get_string_by_id(0x357); hud_add_list_item();
        get_string_by_id(0x358); hud_add_list_item();
        DAT_80610b30 = 2;
        break;
    }
    case 9: {
        int s = get_string_by_id(0x1966);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                              DAT_80612564 + 0x41, 0, 0, 0, 0);
        hud_add_list_item();
        break;
    }
    case 10: {
        int s = get_string_by_id(0x1967);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                              DAT_80612564 + 0x41, 0, 0, 0, 0);
        hud_add_list_item();
        hud_add_list_item();
        get_string_by_id(0x357); hud_add_list_item();
        get_string_by_id(0x358); hud_add_list_item();
        DAT_80610b30 = 2;
        break;
    }
    case 0xb: {
        int s = get_string_by_id(0x196c);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                              DAT_80612564 + 0x41, 0, 0, 0, 0);
        hud_add_list_item();
        break;
    }
    case 0xc: {
        int s = get_string_by_id(0x196d);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                              DAT_80612564 + 0x41, 0, 0, 0, 0);
        hud_add_list_item();
        break;
    }
    case 0xd:
    case 0xe:
        if (DAT_80612548 != 0 || DAT_8061253c == 0) {
            int s = get_string_by_id(0x196c);
            format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                                  DAT_80612564 + 0x41, 0, 0, 0, 0);
            player_get_entity_ptr(DAT_8061254c);
            hud_add_list_item();
            hud_add_callback_item((double)FLOAT_80616b3c);
            hud_add_list_item();
        }
        break;
    case 0xf: {
        int s = get_string_by_id(0x1971);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                              DAT_80612564 + 0x41, 0, 0, 0, 0);
        hud_add_list_item();
        break;
    }
    case 0x10: {
        int s = get_string_by_id(0x1970);
        format_string_varargs(0, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e9ac, s, (int *)0,
                              DAT_80612564 + 0x41, 0, 0, 0, 0);
        hud_add_list_item();
        hud_add_list_item();
        get_string_by_id(0xa44); hud_add_list_item();
        get_string_by_id(0xe26); hud_add_list_item();
        DAT_80610b30 = 2;
        break;
    }
    case 0x11: {
        int item = hud_find_child_by_id(screen, -3);
        *(int *)(item + 0x20) = get_string_by_id(0x1962);
        *(float *)(item + 0x40) = (float)hud_item_calc_center_y(item);
        *(float *)(item + 0x3c) = (float)hud_item_calc_center_x(item);
        countLeadingZeros((unsigned int)DAT_805c08e8);
        get_string_by_id(0x1954); hud_add_list_item();
        countLeadingZeros((unsigned int)DAT_805c08f8);
        get_string_by_id(0x1955); hud_add_list_item();
        get_string_by_id(0x196b); hud_add_list_item();
        DAT_80610b30 = 1;
        break;
    }
    }
    FUN_80382708();
}

// ---------------------------------------------------------------------------
// memcard_session_build_screen (FUN_801a6230)
// Top-level session screen builder called each rebuild. Sets the title (region
// aware), builds the base layout element, opens the inline keyboard for the
// name-entry states (3/4), calls memcard_session_build_menu to fill the list,
// wires the confirm/back/nav handlers per state, and positions the screen.
// ---------------------------------------------------------------------------
void memcard_session_build_screen(unsigned long long f1, unsigned long long f2,
                                  unsigned long long f3, unsigned long long f4,
                                  unsigned long long f5, unsigned long long f6,
                                  unsigned long long f7, unsigned long long f8, int screen) {
    (void)f1;
    bool mapmaker = false;
    if ((DAT_8060f948 == 1 || DAT_8060f948 == 4) && DAT_80612540 == 0x11) {
        mapmaker = true;
    }
    float titleScale = mapmaker ? FLOAT_80616b6c : FLOAT_80616b70;
    unsigned int flags = 0;
    int s = get_string_by_id(0x35d);
    format_string_varargs((double)titleScale, f2, f3, f4, f5, f6, f7, f8, -0x7fa3e7ac, s,
                          (int *)0x805c0000, DAT_80612550 + 1, 0, 0, 0, 0);
    if (DAT_80612540 == 4 || DAT_8061255c != 0) {
        flags = 0x20000000;
    }
    hud_element_init_from_layout(flags | 0x28000, &DAT_805c1854, 0, 0);

    if (DAT_80612540 == 0x11 || (unsigned int)(DAT_80612540 - 2) < 3 || DAT_80612540 == 1) {
        hud_add_list_item();
        hud_add_callback_item((double)FLOAT_80616b3c);
    }

    unsigned int rawXor = ((DAT_8060f948 == 1) ? 0xd : 0xb) ^ 0x80000000;
    double scrollT = (double)FLOAT_80616b5c
                     * (double)(float)((double)(long long)(0x4330000000000000ULL | rawXor)
                                       - DOUBLE_80616b50);
    unsigned long long acc = hud_element_scroll_horizontal((double)(float)scrollT, 0);

    if (DAT_80612540 == 4 || DAT_80612540 == 3) {
        int dim = hud_element_get_dimension(screen, 0);
        text_set_font_table(dim);
        text_get_char_width(&DAT_80610b64);
        if (DAT_80612540 == 3) {
            int nameId = get_string_by_id(DAT_80612550 + 0x396);
            FUN_8038b510((int *)&DAT_805c1620, (int *)(unsigned int)nameId);
        }
        acc = keyboard_open_inline(screen, &DAT_805c1620, 0x13, 0,
                                   -(int)(DAT_80612540 == 3) & 0x200, (void *)&LAB_801a3d24, 2);
    }
    memcard_session_build_menu(acc, (unsigned long long)(double)titleScale, f3, f4, f5, f6, f7, f8);
    hud_element_apply_scroll();
    hud_screen_remove_handler(screen, 2);

    bool statusRange = !(DAT_80612540 != 10
                         && (DAT_80612540 < 10 ? DAT_80612540 <= 8 : DAT_80612540 >= 0x10));
    if (statusRange) {
        hud_screen_remove_handler(screen, 0);
        hud_screen_remove_handler(screen, 1);
        hud_screen_register_handler_global(0, get_string_by_id(0x19c7), 2, 0);
    } else {
        hud_screen_register_handler_global(0, get_string_by_id(0x19c7), 0, 0);
        if (DAT_8061255c == 0 || DAT_80612540 != 7) {
            hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
        } else {
            hud_screen_remove_handler(screen, 1);
        }
    }
    *(float *)(screen + 0x5c) = FLOAT_80616b8c;
    *(float *)(screen + 0x60) = FLOAT_80616b90;
    *(int *)(screen + 0x3c) = DAT_80612538;
    hud_element_apply_center_xy(DAT_806119e0);
}

// ---------------------------------------------------------------------------
// memcard_check_scan_cb (FUN_801a68cc)
// Presence-check dir-scan callback: validate the entry magic tag (DAT_80610b6c)
// and game-id fields; on a full match latch DAT_80612580. Returns 0/1.
// ---------------------------------------------------------------------------
void memcard_check_scan_cb(int entry) {
    int recLen = FUN_8038b5c8(entry);
    int rc = FUN_8038b378((unsigned int *)(recLen + entry + -4), (unsigned int *)&DAT_80610b6c);
    if (rc == 0
        && FUN_80387b68(entry + 0x28, DAT_80612370, 4) == 0
        && FUN_80387b68(entry + 0x2c, DAT_80612370 + 4, 2) == 0) {
        DAT_80612580 = 1;
    }
    unsigned int clz = countLeadingZeros((unsigned int)DAT_80612580);
    (void)(clz >> 5);
}

// ---------------------------------------------------------------------------
// memcard_check_scan (FUN_801a6958)
// Pump a presence-check dir scan (gx_cmd_add_type5 w/ memcard_check_scan_cb) on
// handle DAT_80612584 to the terminal tag 0xe. Returns 1 on success else the
// translated CARD status.
// ---------------------------------------------------------------------------
unsigned int memcard_check_scan(void) {
    bool done = false;
    int outTag = 0, outCode = 0; int outData[3];
    notif_set_player_slot(0);
    gx_cmd_add_type5(DAT_80612584, 3, (void *)memcard_check_scan_cb, 0);
    while (true) {
        if (done) return 1;
        int rc = gx_cmd_list_execute(3, &outCode, &outTag, outData);
        if (rc != 0) break;
        if (outTag == 0xe) done = true;
    }
    return (unsigned int)card_translate_result(outCode);
}

// ---------------------------------------------------------------------------
// memcard_check_dismiss (FUN_801a6c7c)
// Dismiss the presence-check screen: clear the card-present latch, hand control
// back to the notification feed, and push the next HUD screen.
// ---------------------------------------------------------------------------
void memcard_check_dismiss(void) {
    DAT_80612d10 = 0;
    notif_set_active();
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// memcard_check_update (FUN_801a69e8)
// Per-frame update for the presence-check screen. Phase < 0: scan both slots,
// classify to a card-present latch (DAT_80612d10) + result code, or dismiss if
// both cards are full. Phase 1: hand the result to the notif renderer, checking
// the alternate slot on duplicate. Phase 2: run the notif animation timer and
// rebuild the item list.
// ---------------------------------------------------------------------------
void memcard_check_update(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                          unsigned long long f4, unsigned long long f5, unsigned long long f6,
                          unsigned long long f7, unsigned long long f8, int screen, int a10,
                          int a11, int a12, int a13, int a14, int a15, int a16) {
    (void)f1;
    if (hud_element_is_closing(screen) != 0 || notif_is_visible()) {
        return;
    }
    if (DAT_80612590 == 1) {
        unsigned long long acc = notif_set_player_data(DAT_80612588);
        if (DAT_8061258c == 2) {
            DAT_80612580 = 0;
            DAT_80612584 = DAT_80612588;
            memcard_check_scan();
            if (DAT_80612580 == 0) {
                DAT_80612584 = 1 - DAT_80612588;
                memcard_check_scan();
                if (DAT_80612580 != 0) {
                    DAT_8061258c = 0x15;
                    DAT_80612588 = 1 - DAT_80612588;
                }
            } else {
                DAT_8061258c = 0x15;
            }
        }
        FUN_8033a784(acc, f2, f3, f4, f5, f6, f7, f8, screen, DAT_8061258c, a11, a12, a13, a14,
                     a15, a16);
    } else if (DAT_80612590 < 1) {
        if (-1 < DAT_80612590) {
            DAT_80612560 = 1;
            unsigned int r0 = memcard_session_scan(0);
            unsigned int r1 = memcard_session_scan(1);
            if ((int)r0 < 0xa000 && (int)r1 < 0xa000) {
                DAT_80612590 = 1;
                DAT_80612d10 = 1;
                DAT_80612588 = 0;
                if ((int)r0 < 0) {
                    if ((int)r1 < 0) {
                        if (r0 == 0xfffffffc && r1 != 0xfffffffc) {
                            DAT_8061258c = -(int)r1;
                            DAT_80612588 = 1;
                        } else {
                            DAT_8061258c = -(int)r0;
                        }
                    } else {
                        DAT_80612588 = 1;
                        DAT_8061258c = 2;
                    }
                } else {
                    DAT_8061258c = 2;
                }
            } else {
                memcard_check_dismiss();
            }
        }
    } else if (DAT_80612590 < 3) {
        double num = (double)FLOAT_80616b40;
        double t = (double)FLOAT_8061257c
                   + (double)(float)(num / (double)(float)((double)(long long)(
                         0x4330000000000000ULL | ((unsigned int)DAT_806109c0 ^ 0x80000000))
                       - DOUBLE_80616b50));
        FLOAT_8061257c = (float)t;
        if (FUN_8033ad24() == 0) {
            FUN_8033be18(DOUBLE_80616b50, num, t, f4, f5, f6, f7, f8, screen, a10, a11, a12, a13,
                         a14, a15, a16);
        } else {
            double num2 = (double)FLOAT_80616b40;
            double t2 = (double)FLOAT_8061257c
                        + (double)(float)(num2 / (double)(float)((double)(long long)(
                              0x4330000000000000ULL | ((unsigned int)DAT_806109c0 ^ 0x80000000))
                            - DOUBLE_80616b50));
            FLOAT_8061257c = (float)t2;
            if (num2 < (double)FLOAT_8061257c) {
                FUN_8033be18(DOUBLE_80616b50, num2, t2, f4, f5, f6, f7, f8, screen, a10, a11, a12,
                             a13, a14, a15, a16);
            }
        }
        hud_screen_rebuild_items();
    }
}

// ---------------------------------------------------------------------------
// memcard_check_render (FUN_801a6c1c)
// Presence-check screen render: in phase 2 forward to the notif renderer
// (FUN_8033c2e0); otherwise, if the screen is active, draw the base layout
// frame and center it.
// ---------------------------------------------------------------------------
void memcard_check_render(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                          unsigned long long f4, unsigned long long f5, unsigned long long f6,
                          unsigned long long f7, unsigned long long f8, int screen) {
    if (DAT_80612590 == 2) {
        FUN_8033c2e0(f1, f2, f3, f4, f5, f6, f7, f8, screen);
    } else if (DAT_8061256c != 0) {
        hud_element_init_from_layout(0x20000, (int *)0, 0, 0);
        hud_element_finalize_frame();
        hud_element_apply_center_xy(DAT_806119e0);
    }
}

// ===========================================================================
// batch_531 additions: presence-check screen entry/init helpers, the CARD
// scratch-buffer allocator, and the card-swap detector. Address range
// 0x801a6cb0 - 0x801a6dec.
// ===========================================================================

extern "C" {
extern void gx_cmd_list_alloc(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                              unsigned long long f4, unsigned long long f5, unsigned long long f6,
                              unsigned long long f7, unsigned long long f8);
extern int  FUN_803b5d40(int handle, unsigned int *out1, unsigned int *out2);  // CARD probe/status

extern float FLOAT_80612570;  // presence-check re-arm timer
extern int   DAT_80612574;    // presence-check context frame lo
extern int   DAT_80612578;    // presence-check context frame hi2
extern int   DAT_80610b78;    // per-slot last CARD probe result (stride 4)
extern int   DAT_806125b8;    // players-need-recheck bitmask
extern int   DAT_805c18b8;    // CARD 0x8000 scratch buffer table (stride 4)
extern int   DAT_805c18c8;    // CARD 0x4000 scratch buffer table (stride 4)
} // extern "C"

// ---------------------------------------------------------------------------
// memcard_check_set_phase2 (FUN_801a6cb0)
// Advance the presence-check screen to phase 2 (notif animation) and reset the
// animation timer.
// ---------------------------------------------------------------------------
void memcard_check_set_phase2(void) {
    DAT_80612590 = 2;
    FLOAT_8061257c = FLOAT_80616b38;
}

// ---------------------------------------------------------------------------
// memcard_check_restart (FUN_801a6cc4)
// Reset the presence-check screen to phase 0 and push it.
// ---------------------------------------------------------------------------
void memcard_check_restart(void) {
    DAT_80612590 = 0;
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// memcard_check_enter (FUN_801a6cf8)
// Enter the presence-check screen: reset the re-arm timer, phase 0, stash the
// caller's context frame, and push the screen.
// ---------------------------------------------------------------------------
void memcard_check_enter(int frameHi, int frameLo, int frameHi2) {
    FLOAT_80612570 = FLOAT_80616b38;
    DAT_80612590 = 0;
    DAT_80612528 = frameHi;
    DAT_80612574 = frameLo;
    DAT_80612578 = frameHi2;
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// memcard_check_enter_active (FUN_801a6d40)
// Mark the presence-check screen active (DAT_8061256c) then enter it.
// ---------------------------------------------------------------------------
void memcard_check_enter_active(int frameHi, int frameLo, int frameHi2) {
    DAT_8061256c = 1;
    memcard_check_enter(frameHi, frameLo, frameHi2);
}

// ---------------------------------------------------------------------------
// memcard_alloc_buffers (FUN_801a6d68)
// Allocate the CARD scratch buffers: four 0x8000 work buffers (DAT_805c18b8[])
// and four 0x4000 block buffers (DAT_805c18c8[]) from the bottom of the heap,
// then allocate the async command list (gx_cmd_list_alloc).
// ---------------------------------------------------------------------------
void memcard_alloc_buffers(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                           unsigned long long f4, unsigned long long f5, unsigned long long f6,
                           unsigned long long f7, unsigned long long f8) {
    (void)f1;
    FUN_803826e8();
    int off = 0;
    for (int i = 0; i < 4; i++) {
        *(int *)((int)&DAT_805c18b8 + off) = (int)mem_alloc_aligned(
            f1, f2, f3, f4, f5, f6, f7, f8, 0x20, 0x8000, 1, 0, 0, 0, 0, 0);
        *(int *)((int)&DAT_805c18c8 + off) = (int)mem_alloc_aligned(
            f1, f2, f3, f4, f5, f6, f7, f8, 0x20, 0x4000, 1, 0, 0, 0, 0, 0);
        off += 4;
    }
    gx_cmd_list_alloc(f1, f2, f3, f4, f5, f6, f7, f8);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// card_mount_probe (FUN_801a6dec)
// Poll both CARD slots' probe status (FUN_803b5d40); if a slot's status changed
// from the last recorded value (DAT_80610b78[slot]) - i.e. a card was inserted
// or removed - flag every active player for a save re-check (DAT_806125b8).
// Records the new status per slot. Called by card_mount_poll.
// ---------------------------------------------------------------------------
void card_mount_probe(void) {
    FUN_803826f0();
    int off = 0;
    for (int slot = 0; slot < 2; slot++) {
        unsigned int a[5];
        unsigned int b;
        int status = FUN_803b5d40(slot, a, &b);
        if (status != *(int *)(&DAT_80610b78 + off)) {
            int remaining = DAT_80611014;
            int bit = 0;
            while (0 < remaining) {
                DAT_806125b8 = DAT_806125b8 | (1 << bit);
                bit++;
                remaining--;
            }
        }
        *(int *)(&DAT_80610b78 + off) = status;
        off += 4;
    }
    FUN_8038273c();
}
