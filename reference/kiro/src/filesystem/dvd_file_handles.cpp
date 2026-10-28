// ============================================================================
// Address range: 0x80186acc - 0x80187004 | batch_480 | Date: 2026-09-06
// Subsystem: filesystem (DVD open-file handle table) + two streaming-scratch
//            accessors + the disc-error boot tick.  tag dvd-handles.
//
// A small fixed table of 0x14 open DVD file handles (each a 0x48-byte record:
// DVDFileInfo block at +0x00, entrynum at +0x00 of the shadow copy DAT_805a2c98,
// size/base at DAT_805a2cd0, cursor at DAT_805a2cd8, in-use state at +0x40
// DAT_805a2cdc).  Provides open-by-path, aligned read-at-cursor, seek, close,
// plus the entrynum getter.  Sits under the pack/asset loader
// (dvd_get_file_size / dvd_read_file_* / pack_open call in).  Two leftover
// accessors report streaming-scratch usage in KB; boot_disc_error_tick polls the
// drive state and drives the disc-error / cover / retry screen.
//
//   stream_scratch_get_usage_kb   0x80186acc  report scratch used/total in KB
//   dvd_handle_table_reset_find_free 0x80186b00 one-time clear + find first free slot
//   dvd_path_skip_to_leaf         0x80186b78  advance a path past the last '/'
//   boot_disc_error_tick          0x80186bbc  poll drive state -> error/message screen
//   dvd_read_aligned              0x80186d98  32-byte-aligned + tail DVD read
//   dvd_handle_get_entrynum       0x80186e58  entrynum for an open handle (or -1)
//   dvd_handle_open               0x80186e94  resolve path -> entrynum -> open a slot
//   dvd_handle_read               0x80186f34  read at the handle cursor, advance it
//   dvd_handle_seek               0x80186fa4  seek a handle (SET / CUR from base)
//   dvd_handle_close              0x80187004  close a handle, free the slot
//
// Structurally-faithful non-matching port.  The per-slot DVDFileInfo pointer is
// (&slotState) - 0x40, expressed here as the ROM's constant base -0x7fa5d364
// (== 0x805a2c9c) plus slot*0x48.  Spawn-frame register-pair helpers carry the
// path / dst pointers; the leading double params on the read paths are FP-arg
// ABI passthrough artifacts, preserved in signature but unused.  The
// __save_gpr_25/28 spill stubs are dropped.

#include "common.h"
#include "generated_decls.h"

// ---- spawn-frame register-pair helpers --------------------------------------
extern void FUN_803826e4(void);   // returns r3:r4 (dst / dvdfileinfo) pair
typedef unsigned long long (*FramePairFn)(void);
#define FRAME_PAIR_E4 ((FramePairFn)FUN_803826e4)
extern void FUN_803826f0(void);   // returns r3 (path ptr)
typedef char *(*FramePtrFn)(void);
#define FRAME_PTR_F0 ((FramePtrFn)FUN_803826f0)
extern void FUN_8038273c(void);   // spawn-frame epilogue
extern void FUN_80382730(void);   // spawn-frame epilogue

// ---- DVD / CRT helpers -------------------------------------------------------
extern char *FUN_8038b258(char *s, char *cset);   // strrchr-alike (last of cset)
extern void  FUN_803a2934(int *dvdFileInfo);       // DVDClose-alike
extern int   FUN_803a4db4(void);                   // query drive/cover state
extern void  leaf_31e8(int dst, unsigned int src, int len);   // small memcpy
// dvd_async_read_6cac, DVDConvertPathToEntrynum, dvd_open_entrynum,
// get_free_heap_size, loadscreen_is_active, loadscreen_render_deferred,
// boot_render_message_screen resolve through generated_decls.

// ---- streaming scratch globals (shared with the streamer) -------------------
extern int DAT_80612420;   // scratch base
extern int DAT_80612428;   // scratch write cursor
extern int DAT_8061242c;   // scratch region size

// ---- DVD handle table (0x14 slots, stride 0x48) -----------------------------
// Element bases; the DVDFileInfo block lives at (state - 0x40) == base -0x7fa5d364.
extern int  DAT_805a2c98;   // slot entrynum          (stride 0x48)
extern int  DAT_805a2cd0;   // slot base offset       (stride 0x48)
extern int  DAT_805a2cd8;   // slot read cursor       (stride 0x48)
extern int  DAT_805a2cdc;   // slot in-use state      (stride 0x48, indexed *0x12 words)

// ---- disc-error / boot state ------------------------------------------------
extern unsigned char DAT_80610ac0;   // handle-table needs-clear latch
extern char          DAT_80610ac4;   // path separator char ('/')
extern int           DAT_80610aa0;   // last drive-error code
extern int           DAT_80612478;   // in-error latch (last frame)

// -----------------------------------------------------------------------------
// stream_scratch_get_usage_kb (0x80186acc)
// Report the streaming scratch usage: *usedKb = (cursor - base) >> 10,
// *totalKb = regionSize >> 10.  Either pointer may be null.  Caller
// get_display_dimensions (debug HUD).
// -----------------------------------------------------------------------------
void stream_scratch_get_usage_kb(unsigned int *usedKb, unsigned int *totalKb)
{
    if (usedKb != (unsigned int *)0) {
        *usedKb = (unsigned int)(DAT_80612428 - DAT_80612420) >> 10;
    }
    if (totalKb == (unsigned int *)0) {
        return;
    }
    *totalKb = (unsigned int)DAT_8061242c >> 10;
}

// -----------------------------------------------------------------------------
// dvd_handle_table_reset_find_free (0x80186b00)
// One-time clear of all 0x14 slot states (when the needs-clear latch is set),
// then return the index of the first free slot (state == 0), or -1 if the table
// is full.
// -----------------------------------------------------------------------------
int dvd_handle_table_reset_find_free(void)
{
    if (DAT_80610ac0 != '\0') {
        int off = 0;
        for (int n = 0x14; n != 0; n--) {
            *(int *)((int)&DAT_805a2cdc + off) = 0;
            off += 0x48;
        }
        DAT_80610ac0 = '\0';
    }

    int idx = 0;
    int off = 0;
    for (int n = 0x14; n != 0; n--) {
        if (*(int *)((int)&DAT_805a2cdc + off) == 0) {
            return idx;
        }
        idx = idx + 1;
        off += 0x48;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// dvd_path_skip_to_leaf (0x80186b78)
// Return the pointer just past the last path-separator (DAT_80610ac4 = '/') in
// `path`, i.e. the leaf filename; or `path` unchanged if there is no separator.
// -----------------------------------------------------------------------------
char *dvd_path_skip_to_leaf(char *path)
{
    char *sep = FUN_8038b258(path, &DAT_80610ac4);
    if (sep != (char *)0) {
        path = sep + 1;
    }
    return path;
}

// -----------------------------------------------------------------------------
// boot_disc_error_tick (0x80186bbc)
// Poll the drive/cover state (FUN_803a4db4) and drive the appropriate screen:
// error states (4,5,6,0xb,-1) show the disc-error message screen (suppressed for
// the "no disc" case 5 when the spill flag bit is set); otherwise clear the
// error code and, if a load screen is active, render it deferred.  Stores the
// raw state through the frame's out-pointer and latches the error flag.  Caller
// game_resource_loader_21bc.
// -----------------------------------------------------------------------------
void boot_disc_error_tick(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                          unsigned long long f4, unsigned long long f5, unsigned long long f6,
                          unsigned long long f7, unsigned long long f8)
{
    // The frame pair's low word is the out-pointer for the raw state; the high
    // bit (0x1_0000_0000) is a "already showing message" guard.
    unsigned long long frame = FRAME_PAIR_E4();
    int isError = 0;

    int state = FUN_803a4db4();
    switch (state) {
    case 4:
    case 5:
    case 6:
    case 0xb:
    case -1:
        isError = 1;
        break;
    }

    if (isError == 0) {
        DAT_80610aa0 = -1;
        if ((DAT_80612478 != 0) && (loadscreen_is_active() != 0)) {
            (void)f1; (void)f2; (void)f3; (void)f4; (void)f5; (void)f6; (void)f7; (void)f8;
            loadscreen_render_deferred();
        }
    }
    else {
        bool show = true;
        if (((frame & 0x100000000ULL) != 0) && (state != 5)) {
            show = false;
        }
        if (show) {
            boot_render_message_screen();
        }
    }

    DAT_80612478 = isError;
    *(int *)(unsigned int)frame = state;
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// dvd_read_aligned (0x80186d98)
// Read `len` bytes at `offset` from the DVDFileInfo in the frame's high word,
// clamped to the file size (record +0x34).  Reads the 32-byte-aligned bulk
// directly, then reads any sub-32-byte tail into a stack bounce buffer and
// copies it out.  The dst base comes from the frame's low word.
// -----------------------------------------------------------------------------
int dvd_read_aligned(unsigned long long f1, double d2, double d3, unsigned long long f4,
                     unsigned long long f5, unsigned long long f6, unsigned long long f7,
                     unsigned long long f8, unsigned int a9, unsigned int a10,
                     unsigned int len, unsigned int offset, unsigned int a13,
                     unsigned int a14, unsigned int a15, unsigned int a16)
{
    unsigned char bounce[64];

    unsigned long long frame = FRAME_PAIR_E4();
    int fileInfo = (int)(frame >> 0x20);

    if (*(int *)(fileInfo + 0x34) < (int)(len + offset)) {
        len = (*(int *)(fileInfo + 0x34) - offset) + 0x1f & 0xffffffe0;
    }
    unsigned int bulk = len & 0xffffffe0;
    int ret = 0;
    if (bulk != 0) {
        ret = dvd_async_read_6cac((unsigned long long)ret, d2, d3, f4, f5, f6, f7, f8,
                                  fileInfo, (int)frame, bulk, offset, a13, a14, a15, a16);
    }
    if ((len & 0x1f) != 0) {
        int tailRc = dvd_async_read_6cac((unsigned long long)ret, d2, d3, f4, f5, f6, f7, f8,
                                         fileInfo, (int)(unsigned int)bounce, 0x20, offset + bulk,
                                         a13, a14, a15, a16);
        if (tailRc != 0) {
            leaf_31e8((int)frame + bulk, (unsigned int)bounce, len & 0x1f);
        }
    }
    FUN_80382730();
    return ret;
}

// -----------------------------------------------------------------------------
// dvd_handle_get_entrynum (0x80186e58)
// Return the DVD entrynum backing an open handle, or -1 if the index is out of
// range or the slot is not open.
// -----------------------------------------------------------------------------
int dvd_handle_get_entrynum(int handle)
{
    if ((-1 < handle) && (handle < 0x14) && ((&DAT_805a2cdc)[handle * 0x12] != 0)) {
        return *(int *)(&DAT_805a2c98 + handle * 0x48);
    }
    return -1;
}

// -----------------------------------------------------------------------------
// dvd_handle_open (0x80186e94)
// Open the file named by the frame's path pointer: strip to the leaf name, grab
// a free slot, resolve the path to an entrynum (DVDConvertPathToEntrynum), open
// it (dvd_open_entrynum into the slot's DVDFileInfo at base -0x7fa5d364), and on
// success record the entrynum, mark the slot in-use, and reset its cursor.
// -----------------------------------------------------------------------------
void dvd_handle_open(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                     unsigned long long f4, unsigned long long f5, unsigned long long f6,
                     unsigned long long f7, unsigned long long f8)
{
    char *path = FRAME_PTR_F0();
    path = dvd_path_skip_to_leaf(path);

    int slot = dvd_handle_table_reset_find_free();
    if (slot != -1) {
        int off = slot * 0x48;
        unsigned int entrynum = DVDConvertPathToEntrynum(f1, f2, f3, f4, f5, f6, f7, f8, path);
        if ((entrynum != 0xffffffff) &&
            (dvd_open_entrynum(entrynum, off + -0x7fa5d364) != 0)) {
            *(unsigned int *)(&DAT_805a2c98 + off) = entrynum;
            (&DAT_805a2cdc)[slot * 0x12] = 1;
            *(int *)(&DAT_805a2cd8 + off) = 0;
        }
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// dvd_handle_read (0x80186f34)
// Read `len` bytes from an open handle at its current cursor via dvd_read_aligned
// (against the slot's DVDFileInfo), advance the cursor by the bytes read, and
// return the count (or -1 on a bad/closed handle).
// -----------------------------------------------------------------------------
int dvd_handle_read(unsigned long long f1, double d2, double d3, unsigned long long f4,
                    unsigned long long f5, unsigned long long f6, unsigned long long f7,
                    unsigned long long f8, int handle, unsigned int a10, unsigned int len,
                    unsigned int a12, unsigned int a13, unsigned int a14, unsigned int a15,
                    unsigned int a16)
{
    int ret;
    if ((handle < 0) || (0x13 < handle) || ((&DAT_805a2cdc)[handle * 0x12] == 0)) {
        ret = -1;
    }
    else {
        int off = handle * 0x48;
        ret = dvd_read_aligned(f1, d2, d3, f4, f5, f6, f7, f8, off + -0x7fa5d364, a10, len,
                               *(unsigned int *)(&DAT_805a2cd8 + off), a13, a14, a15, a16);
        *(int *)(&DAT_805a2cd8 + off) = *(int *)(&DAT_805a2cd8 + off) + ret;
    }
    return ret;
}

// -----------------------------------------------------------------------------
// dvd_handle_seek (0x80186fa4)
// Seek an open handle's cursor: whence 0 = relative to the slot base
// (DAT_805a2cd0 + offset), whence 1 = absolute.  Returns the new cursor, or -1
// on a bad/closed handle.
// -----------------------------------------------------------------------------
int dvd_handle_seek(int handle, int offset, int whence)
{
    if ((-1 < handle) && (handle < 0x14) && ((&DAT_805a2cdc)[handle * 0x12] != 0)) {
        int off = handle * 0x48;
        if (whence == 0) {
            *(int *)(&DAT_805a2cd8 + off) = *(int *)(&DAT_805a2cd0 + off) + offset;
        }
        else if (whence == 1) {
            *(int *)(&DAT_805a2cd8 + off) = offset;
        }
        return *(int *)(&DAT_805a2cd8 + off);
    }
    return -1;
}

// -----------------------------------------------------------------------------
// dvd_handle_close (0x80187004)
// Close an open handle: if it holds a real DVDFileInfo (state != -2 sentinel)
// close it (FUN_803a2934), then mark the slot free.  Returns 1 on success, -1 on
// a bad/already-free handle.
// -----------------------------------------------------------------------------
int dvd_handle_close(int handle)
{
    if ((-1 < handle) && (handle < 0x14)) {
        int state = (&DAT_805a2cdc)[handle * 0x12];
        if (state != 0) {
            if (state != -2) {
                FUN_803a2934((int *)(handle * 0x48 + -0x7fa5d364));
            }
            (&DAT_805a2cdc)[handle * 0x12] = 0;
            return 1;
        }
    }
    return -1;
}

// ============================================================================
// Address range: 0x80187080 - 0x80187590 | batch_481 | Date: 2026-09-06
// Subsystem: filesystem (SECOND DVD open-file handle table + async streamer).
//            tag dvd-handles.
//
// A larger, streaming-oriented DVD handle table sitting alongside the batch_480
// table.  0x1c (28) slots, stride 0x54.  Parallel byte flag arrays track slot
// state:  DAT_805a3288 = in-use, DAT_805a328a = busy/read-in-flight,
// DAT_805a3289 = read-complete.  The DVDFileInfo/file-handle block for a slot is
// at &DAT_805a3238 + slot*0x54; the shadow entrynum/id lives at DAT_805a3278
// (stride 0x54, i.e. *0x15 words), the clamp size at DAT_805a326c, and the async
// read cursors at DAT_805a3274 / 3280 / 3284 / 327c.  A one-time table-clear
// latch is DAT_80610ac1.
//
//   dvd2_table_reset           0x80187080  clear all 28 slots + clear the latch
//   dvd2_handle_open_entrynum  0x801870c4  open by entrynum into a free slot
//   dvd2_handle_open_path      0x80187198  open by path (DVDOpen) into a free slot
//   dvd2_handle_close          0x80187260  spin until idle, DVDClose, free slot
//   dvd2_read_async_cb         0x80187320  DVDReadAsync completion callback (chunks)
//   dvd2_read_wrapper          0x801873f0  thunk -> dvd2_handle_read_async(prio=0)
//   dvd2_handle_read_async     0x80187414  start a chunked async read on a slot
//   dvd2_handle_is_busy        0x80187544  0 iff slot in-use && busy flag set
//   dvd2_nop                   0x8018758c  empty
//   dvd2_slot_entry_addr       0x80187590  addr into the batch_480 table (stride 0x48)
//
// Structurally-faithful non-matching port.  Spawn-frame register-pair helpers
// (FUN_803826e8 hi/lo) carry the path/id and slot index; the leading FP-arg /
// GPR passthrough params are ABI artifacts preserved in the signatures.  Reads
// are broken into 0x40000-byte (256 KiB) chunks, re-armed from the completion
// callback.  dvd2_slot_entry_addr indexes the batch_480 stride-0x48 table
// (DAT_805a2ccc), linking the two handle tables.

// ---- spawn-frame register-pair helper (hi = path/id/slot, lo = dst) ---------
extern void FUN_803826e8(void);
typedef unsigned long long (*FramePairE8Fn)(void);
#define FRAME_PAIR_E8 ((FramePairE8Fn)FUN_803826e8)
extern void FUN_80382734(void);   // spawn-frame epilogue

// ---- DVD services -----------------------------------------------------------
extern int DVDOpen(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                   unsigned long long f4, unsigned long long f5, unsigned long long f6,
                   unsigned long long f7, unsigned long long f8, char *name, int dvdFileInfo,
                   int a11, int a12, int a13, int a14, int a15, int a16);
extern int DVDReadAsync(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                        unsigned long long f4, unsigned long long f5, unsigned long long f6,
                        unsigned long long f7, unsigned long long f8, void *dvdFileInfo,
                        int addr, unsigned int len, unsigned int offset, void *cb,
                        int prio, int a15, int a16);
// FUN_803a4db4 (drive state) and FUN_803a2934 (DVDClose) are self-externed above
// in the batch_480 block.  dvd_open_entrynum, dvd_handle_get_entrynum and
// dvd_path_skip_to_leaf resolve through generated_decls / the batch_480 defs.

// ---- second DVD handle table (0x1c slots, stride 0x54) ----------------------
extern int          DAT_805a3238;   // slot DVDFileInfo / handle block (stride 0x54)
extern int          DAT_805a326c;   // slot clamp size                 (stride 0x54)
extern int          DAT_805a3274;   // async chunk length              (stride 0x54)
extern int          DAT_805a3278;   // slot entrynum / id shadow       (stride 0x54)
extern int          DAT_805a327c;   // async bytes remaining           (stride 0x54)
extern int          DAT_805a3280;   // async source offset             (stride 0x54)
extern int          DAT_805a3284;   // async dst address               (stride 0x54)
extern unsigned char DAT_805a3288;  // slot in-use flag                (stride 0x54)
extern unsigned char DAT_805a3289;  // slot read-complete flag         (stride 0x54)
extern unsigned char DAT_805a328a;  // slot busy / read-in-flight flag (stride 0x54)
extern unsigned char DAT_80610ac1;  // second table needs-clear latch

// ---- batch_480 table base (stride 0x48) for dvd2_slot_entry_addr ------------
extern int DAT_805a2ccc;

// forward decls for within-file callers
void dvd2_table_reset(void);
int  dvd2_handle_is_busy(int slot);
void dvd2_read_async_cb(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                        unsigned long long f4, unsigned long long f5, unsigned long long f6,
                        unsigned long long f7, unsigned long long f8, int completedLen,
                        unsigned int *req);

// -----------------------------------------------------------------------------
// dvd2_table_reset (0x80187080)
// Clear all 0x1c slots: mark not-in-use (flag+0), not-busy (flag+2), and
// read-complete (flag+1 = 1); then clear the one-time table-clear latch.
// -----------------------------------------------------------------------------
void dvd2_table_reset(void)
{
    int off = 0;
    for (int n = 0x1c; n != 0; n--) {
        (&DAT_805a3288)[off] = 0;
        (&DAT_805a328a)[off] = 0;
        (&DAT_805a3289)[off] = 1;
        off += 0x54;
    }
    DAT_80610ac1 = 0;
}

// -----------------------------------------------------------------------------
// dvd2_handle_open_entrynum (0x801870c4)
// Open a DVD file by the entrynum resolved from the frame's high word.  Applies
// the one-time table reset (latch), finds the first free slot, resolves the
// caller's id to a real entrynum (dvd_handle_get_entrynum), opens it into the
// slot's file block (dvd_open_entrynum), and records the id + slot flags.
// -----------------------------------------------------------------------------
void dvd2_handle_open_entrynum(void)
{
    unsigned long long frame = FRAME_PAIR_E8();

    if (DAT_80610ac1 != '\0') {
        dvd2_table_reset();
    }

    int off = 0;
    for (int n = 0x1c; n != 0; n--) {
        if ((&DAT_805a3288)[off] == '\0') {
            unsigned int entrynum = dvd_handle_get_entrynum((int)(frame >> 0x20));
            if (((int)entrynum > -1) &&
                (dvd_open_entrynum(entrynum, (int)(&DAT_805a3238 + off)) != 0)) {
                (&DAT_805a3288)[off] = 1;
                *(int *)((int)&DAT_805a3278 + off) = (int)frame;
                (&DAT_805a328a)[off] = 0;
                (&DAT_805a3289)[off] = 1;
            }
            break;
        }
        off += 0x54;
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// dvd2_handle_open_path (0x80187198)
// Open a DVD file by path.  Applies the one-time table reset, finds the first
// free slot, strips the path to its leaf name, and opens it via DVDOpen into the
// slot's file block, recording the caller's id + slot flags on success.  The
// leading undefined8 params are FP/GPR ABI passthrough.
// -----------------------------------------------------------------------------
void dvd2_handle_open_path(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                           unsigned long long f4, unsigned long long f5, unsigned long long f6,
                           unsigned long long f7, unsigned long long f8, int a9, int a10,
                           int a11, int a12, int a13, int a14, int a15, int a16)
{
    unsigned long long frame = FRAME_PAIR_E8();

    if (DAT_80610ac1 != '\0') {
        dvd2_table_reset();
    }

    int off = 0;
    for (int n = 0x1c; n != 0; n--) {
        if ((&DAT_805a3288)[off] == '\0') {
            char *leaf = dvd_path_skip_to_leaf((char *)(unsigned int)(frame >> 0x20));
            int rc = DVDOpen(f1, f2, f3, f4, f5, f6, f7, f8, leaf,
                             (int)(&DAT_805a3238 + off), a11, a12, a13, a14, a15, a16);
            if (rc != 0) {
                (&DAT_805a3288)[off] = 1;
                *(int *)((int)&DAT_805a3278 + off) = (int)frame;
                (&DAT_805a328a)[off] = 0;
                (&DAT_805a3289)[off] = 1;
            }
            break;
        }
        off += 0x54;
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// dvd2_handle_close (0x80187260)
// Close an open slot: spin until the slot's read is idle (dvd2_handle_is_busy)
// OR the drive reaches a terminal error/cover state (FUN_803a4db4 returns -1, or
// 4..6, or 0xb), then DVDClose the file block and free the slot.  Returns 1.
// Called by the batch_477 texture-streamer close path.
// -----------------------------------------------------------------------------
int dvd2_handle_close(int slot)
{
    if ((-1 < slot) && (slot < 0x1c)) {
        int off = slot * 0x54;
        if ((&DAT_805a3288)[off] != '\0') {
            for (;;) {
                if (dvd2_handle_is_busy(slot) != 0) {
                    break;
                }
                int st = FUN_803a4db4();
                bool terminal = false;
                if (st < 7) {
                    if ((st == -1) || ((-2 < st) && (3 < st))) {
                        terminal = true;
                    }
                }
                else if (st == 0xb) {
                    terminal = true;
                }
                if (terminal) {
                    break;
                }
            }
            FUN_803a2934((int *)(&DAT_805a3238 + off));
            (&DAT_805a3288)[off] = 0;
        }
    }
    return 1;
}

// -----------------------------------------------------------------------------
// dvd2_read_async_cb (0x80187320)
// DVDReadAsync completion callback.  If the completed length matches the pending
// chunk (req[0xf]), mark the request busy (+0x51 = 1) and, while bytes remain
// (req[0x11] > 0), issue the next 0x40000-byte chunk (re-arming this callback),
// advancing the dst/src/remaining cursors (req[0x13]/[0x12]/[0x11]) and storing
// the chunk length in req[0xf].  On issue failure clears the busy byte; on
// completion clears the done byte (+0x52).
// -----------------------------------------------------------------------------
void dvd2_read_async_cb(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                        unsigned long long f4, unsigned long long f5, unsigned long long f6,
                        unsigned long long f7, unsigned long long f8, int completedLen,
                        unsigned int *req)
{
    if (completedLen == (int)req[0xf]) {
        *(unsigned char *)((int)req + 0x51) = 1;
        int remaining = (int)req[0x11];
        if (remaining < 1) {
            *(unsigned char *)((int)req + 0x52) = 0;
        }
        else {
            int chunk = 0x40000;
            if (remaining < 0x40001) {
                chunk = remaining;
            }
            int rc = DVDReadAsync(f1, f2, f3, f4, f5, f6, f7, f8, req,
                                  (int)req[0x13], (unsigned int)chunk, req[0x12],
                                  (void *)dvd2_read_async_cb, 2, 0, 0);
            req[0x13] = req[0x13] + chunk;
            req[0x12] = req[0x12] + chunk;
            req[0x11] = req[0x11] - chunk;
            req[0xf]  = chunk;
            if (rc == 0) {
                *(unsigned char *)((int)req + 0x51) = 0;
            }
        }
    }
    else {
        *(unsigned char *)((int)req + 0x51) = 0;
    }
}

// -----------------------------------------------------------------------------
// dvd2_read_wrapper (0x801873f0)
// Thunk to dvd2_handle_read_async with priority selector (param13) = 0.
// -----------------------------------------------------------------------------
void dvd2_handle_read_async(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                            unsigned long long f4, unsigned long long f5, unsigned long long f6,
                            unsigned long long f7, unsigned long long f8, int a9, int a10,
                            int baseOffset, unsigned int len, int prioSel, int a14,
                            int a15, int a16);

void dvd2_read_wrapper(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                       unsigned long long f4, unsigned long long f5, unsigned long long f6,
                       unsigned long long f7, unsigned long long f8, int a9, int a10,
                       int baseOffset, unsigned int len, int a13, int a14, int a15, int a16)
{
    dvd2_handle_read_async(f1, f2, f3, f4, f5, f6, f7, f8, a9, a10, baseOffset, len,
                           0, a14, a15, a16);
}

// -----------------------------------------------------------------------------
// dvd2_handle_read_async (0x80187414)
// Start a chunked async read on the slot identified by the frame's high word.
// Validates the slot (in-use && not busy), computes the source offset from the
// caller's baseOffset plus the slot's stored id, clamps `len` to the slot size
// (DAT_805a326c), then kicks off the first 0x40000-byte chunk via DVDReadAsync
// with dvd2_read_async_cb re-arming it; priority is 2, or 1 when prioSel == 100.
// The busy/done flags (DAT_805a328a / 3289) reflect whether the read is running.
// Called by the batch_477 streamer via 0x80187414.
// -----------------------------------------------------------------------------
void dvd2_handle_read_async(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                            unsigned long long f4, unsigned long long f5, unsigned long long f6,
                            unsigned long long f7, unsigned long long f8, int a9, int a10,
                            int baseOffset, unsigned int len, int prioSel, int a14,
                            int a15, int a16)
{
    unsigned long long frame = FRAME_PAIR_E8();
    int slot = (int)(frame >> 0x20);

    if (((long long)frame >= 0) && (frame < 0x1c00000000ULL)) {
        int off = slot * 0x54;
        if (((&DAT_805a3288)[off] != '\0') && (dvd2_handle_is_busy(slot) != 0)) {
            unsigned int srcOffset = baseOffset + (&DAT_805a3278)[slot * 0x15];
            if (*(int *)(&DAT_805a326c + off) < (int)(len + srcOffset)) {
                len = (*(int *)(&DAT_805a326c + off) - srcOffset) + 0x1f & 0xffffffe0;
            }
            if (-1 < (int)len) {
                unsigned int chunk = 0x40000;
                if ((int)len < 0x40001) {
                    chunk = len;
                }
                *(unsigned int *)(&DAT_805a3274 + off) = chunk;
                *(unsigned int *)(&DAT_805a327c + off) = len - chunk;
                *(unsigned int *)(&DAT_805a3284 + off) = (int)frame + chunk;
                *(unsigned int *)(&DAT_805a3280 + off) = srcOffset + chunk;
                int rc = DVDReadAsync(f1, f2, f3, f4, f5, f6, f7, f8,
                                      (void *)(&DAT_805a3238 + off), (int)frame, chunk, srcOffset,
                                      (void *)dvd2_read_async_cb, 2 - (prioSel == 100), a15, a16);
                if (rc == 0) {
                    (&DAT_805a328a)[off] = 0;
                    (&DAT_805a3289)[off] = 0;
                }
                else {
                    (&DAT_805a328a)[off] = 1;
                    (&DAT_805a3289)[off] = 1;
                }
            }
        }
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// dvd2_handle_is_busy (0x80187544)
// Return 0 iff the slot is valid, in-use (DAT_805a3288) and busy
// (DAT_805a328a); otherwise 1 (i.e. "not busy / can proceed").
// -----------------------------------------------------------------------------
int dvd2_handle_is_busy(int slot)
{
    if ((-1 < slot) && (slot < 0x1c) &&
        ((&DAT_805a3288)[slot * 0x54] != '\0') &&
        ((&DAT_805a328a)[slot * 0x54] != '\0')) {
        return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// dvd2_nop (0x8018758c) — empty.
// -----------------------------------------------------------------------------
void dvd2_nop(void)
{
}

// -----------------------------------------------------------------------------
// dvd2_slot_entry_addr (0x80187590)
// Return *(batch_480 table[slot]) + off — indexes the stride-0x48 handle table
// (DAT_805a2ccc), linking the two DVD handle tables.
// -----------------------------------------------------------------------------
int dvd2_slot_entry_addr(int slot, int off)
{
    return *(int *)(&DAT_805a2ccc + slot * 0x48) + off;
}

// ============================================================================
// dvd_async_read_6cac (0x80186cac) | batch_504
// Blocking "read a whole file region" helper built on DVDReadAsync.  It loops:
//   - issue DVDReadAsync until it is accepted (returns non-zero);
//   - pump the disc-error / loading state: FUN_8039e020 polls for a fatal drive
//     abort (latches DAT_80612374, the loader-ready flag); while ready it keeps
//     presenting loading frames (gx_present_and_advance); FUN_803a4d68 polls the
//     async transfer's completion, and boot_disc_error_tick services the disc
//     swap / retry UI.  When the transfer completes it checks the transferred
//     length (FUN_803a2c6c) against the requested length and, on a short read,
//     waits ~0x78 frames (2s @ 60Hz) and retries the whole read.
//
// The destination buffer and the DVDFileInfo pointer arrive in the FUN_803826e4
// spill frame (the decompiler surfaced them as the r3:r4 pair); they are modelled
// here as explicit parameters (dstBuf / fileInfo) per the compile-clean standard.
//
// Non-matching: the doubly-nested do/while retry structure is preserved; the
// paired-single/float spill temporaries the decompiler surfaced are irrelevant to
// the integer control flow and dropped.
// ============================================================================
// gx_present_and_advance + os_get_frame_count resolve through generated_decls.h;
// not re-declared here to avoid linkage/return-type conflicts.
extern "C" {
extern unsigned int FUN_8039e020(void);            // poll fatal drive-abort
extern int  FUN_803a4d68(int fileInfo);            // poll async transfer complete (0/-1/1)
extern int  FUN_803a2c6c(int fileInfo);            // transferred byte count
extern int  DAT_80612374;                          // loader-ready flag
} // extern "C"

void dvd_async_read_6cac(unsigned long long param_1, double param_2, double param_3,
                         unsigned long long param_4, unsigned long long param_5,
                         unsigned long long param_6, unsigned long long param_7,
                         unsigned long long param_8, int dstBuf, int fileInfo,
                         int reqLen, unsigned int srcOffset)
{
    FUN_803a4db4();   // prime drive-state query

    for (;;) {
        int done;
        do {
            // issue the async read until the queue accepts it
            // DVDReadAsync(f1..f8, dvdFileInfo, addr, len, offset, cb, prio, a15, a16)
            int accepted;
            do {
                accepted = DVDReadAsync(param_1, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8, (void *)(unsigned int)fileInfo,
                                        dstBuf, (unsigned int)reqLen, srcOffset,
                                        (void *)0, 2, 0, 0);
            } while (accepted == 0);

            // pump completion + disc-error UI
            do {
                if (FUN_8039e020() == 0) {
                    if (DAT_80612374 != 0) {
                        gx_present_and_advance(param_1, param_2, param_3, param_4, param_5,
                                               param_6, param_7, param_8);
                    }
                } else {
                    DAT_80612374 = 1;
                }
                done = FUN_803a4d68(fileInfo);
                boot_disc_error_tick(param_1, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8);
            } while (done != 0 && done != -1);
        } while (done != 0);

        // completed: verify full length, else wait ~0x78 frames and retry
        if (FUN_803a2c6c(fileInfo) == reqLen) break;
        int t0 = os_get_frame_count();
        while ((unsigned int)os_get_frame_count() < (unsigned int)(t0 + 0x78)) { }
    }

    FUN_80382730();
}
