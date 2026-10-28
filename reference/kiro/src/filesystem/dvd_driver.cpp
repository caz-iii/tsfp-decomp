// TSFP GameCube DVD low-level driver (Dolphin SDK DVD layer, from main.dol).
//
// Address: 0x803a24cc | Name: dvd_fst_setup | Date: 2026-09-15
// Address: 0x803a2934 | Name: dvd_cancel    | Date: 2026-09-15
//
// This is the bottom of the DVD file system. dvd_fst_setup (called by DVDInit)
// reads the boot-info pointer to the on-disc File System Table (FST) from the
// low-memory global at 0x80000038, records its base, entry count, and string
// table pointer. dvd_cancel is the thin public wrapper over the internal
// command-block cancel/wait routine (FUN_803a513c), used by the DVD/THP close
// and read paths.
//
// The FST layout (Dolphin standard): the root directory entry at FST[0] stores
// the total entry count in its "next offset" field (word at +8); the string
// table immediately follows the entry array (each entry is 0xC bytes).
//
// NON-MATCHING: low-memory globals and the FST are accessed through per-file
// externs; the FST base/count/string-table are cached into the DVD driver's
// DAT_806131xx state block. Control flow is preserved verbatim.

#include "common.h"

extern "C" {

// ---- low-memory globals ----
extern int DAT_80000038;   // boot-info: pointer to the on-disc FST (or 0)

// ---- DVD driver state block ----
extern unsigned int DAT_80613160;   // FST "valid" sentinel (0x80000000)
extern int          DAT_80613164;   // cached FST base pointer
extern int          DAT_80613168;   // cached FST string-table pointer
extern int          DAT_8061316c;   // cached FST entry count

// ---- internal command-block cancel/wait worker (later batch) ----
extern int FUN_803a513c(int *cmdBlock);

// dvd_fst_setup (0x803a24cc): cache the on-disc FST location from low memory.
// FST base is at 0x80000038; entry count is the word at FST+8; the string table
// begins right after the 0xC-byte entry array.
void dvd_fst_setup(void)
{
    DAT_80613160 = 0x80000000;
    DAT_80613164 = DAT_80000038;
    if (DAT_80000038 == 0) {
        return;
    }
    DAT_8061316c = *(int *)(DAT_80000038 + 8);
    DAT_80613168 = DAT_80000038 + *(int *)(DAT_80000038 + 8) * 0xc;
}

// dvd_cancel (0x803a2934): public wrapper that cancels an in-flight DVD command
// block and waits for it to settle. Always returns 1.
int dvd_cancel(int *cmdBlock)
{
    FUN_803a513c(cmdBlock);
    return 1;
}

// ============================================================================
// batch_1118: FST path resolution + the DVD command/interrupt state machine.
//
// Address: 0x803a2958 | Name: dvd_fst_build_path        | Date: 2026-09-15
// Address: 0x803a2ab8 | Name: dvd_fst_entry_to_path     | Date: 2026-09-15
// Address: 0x803a2c3c | Name: dvd_cmd_invoke_cancel_cb  | Date: 2026-09-15
// Address: 0x803a2c6c | Name: dvd_cmd_get_position      | Date: 2026-09-15
// Address: 0x803a2e48 | Name: dvd_state_advance         | Date: 2026-09-15
// Address: 0x803a2ec8 | Name: dvd_cmd_finish_error      | Date: 2026-09-15
// Address: 0x803a2f74 | Name: dvd_fatal_reset           | Date: 2026-09-15
// Address: 0x803a2fa8 | Name: dvd_request_status        | Date: 2026-09-15
// Address: 0x803a2fd0 | Name: dvd_classify_status       | Date: 2026-09-15
// Address: 0x803a3084 | Name: dvd_status_result_handler | Date: 2026-09-15
//
// dvd_fst_build_path / dvd_fst_entry_to_path reconstruct a slash-delimited path
// for an FST entry by recursing up its parent-directory links (DVDOpen uses
// this). The remaining functions are the DVD command engine that runs off the
// DI (Disc Interface) status register 0xCC006020 and the DSP/AI mailbox
// completion callbacks (aram_cmd_e0/e3): dvd_state_advance is the top-level
// interrupt dispatcher; dvd_status_result_handler classifies the DI result and
// either retries, reports an error, or advances the current command; the
// finish/fatal helpers reset the current command block DAT_80613180 and notify
// its callbacks (cmdblock+0x28 completion, cmdblock+0x38 cancel).
//
// NON-MATCHING: FST entries, the DI register, and the DAT_806131xx command
// state are accessed through per-file externs matching original widths; the
// many peer state-machine functions (FUN_803a3xxx/4xxx/5xxx) are declared as
// permissive externs. Control flow and predicates are preserved verbatim.

// ---- DI (Disc Interface) hardware register ----
extern unsigned int DAT_cc006020;   // DI status / result register
extern unsigned int DAT_cc006018;   // (shared) AI/DI byte-count register

// ---- FST cache (declared above): DAT_80613164 base, DAT_80613168 strings ----
extern int          DAT_80613170;   // FST entry index of the current file

// ---- DVD command engine state ----
extern unsigned char *DAT_80613180;  // current active command block
extern unsigned char  DAT_805f35a0;  // idle/sentinel command block
extern int            DAT_806131a0;  // secondary-completion pending flag
extern void         (*DAT_806131a4)(int, unsigned char *);  // secondary-completion cb
extern int            DAT_806131a8;  // last error code
extern unsigned int   DAT_806131b0;  // last seen DI status code
extern int            DAT_806131b4;  // repeat-status counter
extern int            DAT_80613198;  // reset-completed flag
extern void         (*DAT_806131c8)(unsigned char *);       // custom command handler

// ---- named cross-module helpers (audio ARAM mailbox builders) ----
extern int  aram_cmd_e0(void (*cb)(unsigned int));
extern int  aram_cmd_e3(void (*cb)(unsigned int));
extern int  aram_dma_write_small(unsigned int aramAddr, void (*cb)(unsigned int));

// ---- forward declarations for the mutually-recursive engine functions ----
void dvd_fatal_reset(void);
void dvd_request_status(void);
void dvd_cmd_finish_error(int cause);
void dvd_status_result_handler(unsigned int cause);

// ---- peer state-machine functions (decompiled in later batches) ----
extern void FUN_803a3c98(void);
extern void FUN_803a59e4(void);
extern void FUN_803a5774(unsigned int code);
extern void FUN_803a4d24(void);
extern void FUN_803a33c4(void);
extern void FUN_803a3b84(void);
extern void FUN_803a3a1c(void);
extern void FUN_803a32e8(unsigned int arg);

// dvd_fst_build_path (0x803a2958): recursively build the slash-delimited path
// for FST entry `entryIdx` into `buf` (capacity `cap`), by first recursing into
// the parent directory (word at entry+4 = parent index) then appending
// "/<name>". Returns the number of bytes written.
int dvd_fst_build_path(int entryIdx, int buf, int cap)
{
    int written;
    int parentIdx;
    char *name;
    char *parentName;
    char *dst;
    int rem;

    if (entryIdx == 0) {
        written = 0;
    } else {
        parentIdx = *(int *)(DAT_80613164 + 4 + entryIdx * 0xc);
        name = (char *)(DAT_80613168 + (*(unsigned int *)(DAT_80613164 + entryIdx * 0xc) & 0xffffff));
        if (parentIdx == 0) {
            written = 0;
        } else {
            int poff = parentIdx * 0xc;
            parentName = (char *)(DAT_80613168 + (*(unsigned int *)(DAT_80613164 + poff) & 0xffffff));
            written = dvd_fst_build_path(*(int *)(DAT_80613164 + 4 + poff), buf, cap);
            if (written != cap) {
                int start = written + 1;
                *(unsigned char *)(buf + written) = 0x2f;   // '/'
                dst = (char *)(buf + start);
                rem = cap - start;
                for (; (rem != 0 && (*parentName != '\0')); parentName++) {
                    rem--;
                    *dst = *parentName;
                    dst++;
                }
                written = start + ((cap - start) - rem);
            }
        }
        if (written != cap) {
            int start = written + 1;
            *(unsigned char *)(buf + written) = 0x2f;   // '/'
            dst = (char *)(buf + start);
            rem = cap - start;
            for (; (rem != 0 && (*name != '\0')); name++) {
                rem--;
                *dst = *name;
                dst++;
            }
            written = start + ((cap - start) - rem);
        }
    }
    return written;
}

// dvd_fst_entry_to_path (0x803a2ab8): build the full path of the current FST
// entry (DAT_80613170) into buf/cap and NUL-terminate. Returns 0 when the path
// was truncated to exactly fill the buffer, else 1 (adding a trailing '/' for
// directory entries when room permits).
int dvd_fst_entry_to_path(int buf, int cap)
{
    int cur = DAT_80613170;
    int len = dvd_fst_build_path(DAT_80613170, buf, cap);
    if (len == cap) {
        *(unsigned char *)(buf + cap - 1) = 0;
        return 0;
    }
    if ((*(unsigned int *)(DAT_80613164 + cur * 0xc) & 0xff000000) != 0) {   // directory
        if (len == cap - 1) {
            *(unsigned char *)(buf + len) = 0;
            return 0;
        }
        *(unsigned char *)(buf + len) = 0x2f;   // '/'
        len++;
    }
    *(unsigned char *)(buf + len) = 0;
    return 1;
}

// dvd_cmd_invoke_cancel_cb (0x803a2c3c): call the cancel callback stored at
// cmdblock+0x38 if present.
void dvd_cmd_invoke_cancel_cb(int /*unused*/, int cmdBlock)
{
    void (*cb)(void) = *(void (**)(void))(cmdBlock + 0x38);
    if (cb != (void (*)(void))0) {
        cb();
    }
}

// dvd_cmd_get_position (0x803a2c6c): compute the current transfer position for a
// command block based on its state field (+0xc). For an active read (state 1)
// the live position is start(+0x20) + (length(+0x1c) - remaining count from the
// DI/AI byte-count register). Terminal/idle states return the start position or
// the block pointer itself.
int dvd_cmd_get_position(int cmdBlock)
{
    int state = *(int *)(cmdBlock + 0xc);
    if (state == 2) {
        return 0;
    }
    if (state < 2) {
        if (0 < state) {
            int remaining = (int)DAT_cc006018;
            return *(int *)(cmdBlock + 0x20) + (*(int *)(cmdBlock + 0x1c) - remaining);
        }
        if (state < -1) {
            return cmdBlock;
        }
    } else if (state < 10) {
        if (7 < state) {
            return cmdBlock;
        }
    } else if (0xb < state) {
        return cmdBlock;
    }
    return *(int *)(cmdBlock + 0x20);
}

// dvd_state_advance (0x803a2e48): top-level DVD interrupt dispatcher, installed
// as a DSP/AI mailbox completion callback. cause 0x10 -> fatal reset; even cause
// -> re-request status; odd cause (normal completion) -> re-read the FST, retire
// the current command block (state=0, run its completion cb), and kick the next.
void dvd_state_advance(unsigned int cause)
{
    unsigned char *prev;
    void (*cb)(unsigned int);

    if (cause == 0x10) {
        dvd_fatal_reset();
    } else if ((cause & 1) == 0) {
        dvd_request_status();
    } else {
        DAT_806131b4 = 0;
        dvd_fst_setup();
        prev = DAT_80613180;
        DAT_80613180 = &DAT_805f35a0;
        *(unsigned int *)(prev + 0xc) = 0;
        cb = *(void (**)(unsigned int))(prev + 0x28);
        if (cb != (void (*)(unsigned int))0) {
            cb(0);
        }
        FUN_803a3c98();
    }
}

// dvd_cmd_finish_error (0x803a2ec8): terminate the current command block with an
// error (state = -1). cause 0x10 -> fatal reset; otherwise run the failure
// cleanup, notify the completion callback with -1 and any pending secondary
// callback, then kick the next command.
void dvd_cmd_finish_error(int cause)
{
    unsigned char *prev;

    *(unsigned int *)(DAT_80613180 + 0xc) = 0xffffffff;
    if (cause == 0x10) {
        dvd_fatal_reset();
    } else {
        FUN_803a59e4();
        prev = DAT_80613180;
        DAT_80613180 = &DAT_805f35a0;
        DAT_80613198 = 1;
        {
            void (*cb)(unsigned int, unsigned char *) =
                *(void (**)(unsigned int, unsigned char *))(prev + 0x28);
            if (cb != (void (*)(unsigned int, unsigned char *))0) {
                cb(0xffffffff, prev);
            }
        }
        if (DAT_806131a0 != 0) {
            DAT_806131a0 = 0;
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, prev);
            }
        }
        FUN_803a3c98();
    }
}

// dvd_fatal_reset (0x803a2f74): log a fatal reset code (0x1234568), run the
// hard-reset cleanup, then finish the current command with an error.
void dvd_fatal_reset(void)
{
    FUN_803a5774(0x1234568);
    FUN_803a4d24();
    dvd_cmd_finish_error(0);
}

// dvd_request_status (0x803a2fa8): re-request the DI status by issuing ARAM
// mailbox command E0 with dvd_status_result_handler as the completion callback.
void dvd_request_status(void)
{
    aram_cmd_e0(dvd_status_result_handler);
}

// dvd_classify_status (0x803a2fd0): classify a DI status/error code.
// 0 = fatal (unrecognized), 1 = benign/expected, 2 = retry, 3 = defer.
// Recognizes cover/no-disc/fatal sentinels and de-bounces repeated codes.
int dvd_classify_status(unsigned int status)
{
    unsigned int code;

    if (status == 0x20400) {
        DAT_806131b0 = status;
        return 1;
    }
    code = status & 0xffffff;
    if ((code == 0x62800) || (code == 0x23a00) || (code == 0xb5a01)) {
        return 0;
    }
    DAT_806131b4 = DAT_806131b4 + 1;
    if (DAT_806131b4 == 2) {
        if (code == DAT_806131b0) {
            DAT_806131b0 = code;
            return 1;
        }
        DAT_806131b0 = code;
        return 2;
    }
    DAT_806131b0 = code;
    if ((code != 0x31100) && (*(int *)(DAT_80613180 + 8) != 5)) {
        return 3;
    }
    return 2;
}

// dvd_status_result_handler (0x803a3084): handle the DI status result read from
// 0xCC006020. cause 0x10 -> fatal reset; cause bit1 set -> log + re-request via
// E3. Otherwise classify the status and dispatch: 1 -> re-request E3; 2/3 ->
// per-code error reporting through the secondary callback or a retry; the top
// status byte (0x01/0x02/0x03 000000) selects the error class and the matching
// recovery routine.
void dvd_status_result_handler(unsigned int cause)
{
    bool handled;
    unsigned int status;
    unsigned int topByte;
    unsigned char *cmd;
    int cls;
    int errClass;

    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 2) == 0) {
        status = DAT_cc006020;
        topByte = status & 0xff000000;
        cls = dvd_classify_status(status);
        cmd = DAT_80613180;
        if (cls == 1) {
            FUN_803a5774(status);
            aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
        } else {
            if ((unsigned int)(cls - 2) < 2) {
                errClass = 0;
            } else if (topByte == 0x1000000) {
                errClass = 4;
            } else if (topByte == 0x2000000) {
                errClass = 6;
            } else if (topByte == 0x3000000) {
                errClass = 3;
            } else {
                errClass = 5;
            }
            if (DAT_806131a0 == 0) {
                handled = false;
            } else {
                DAT_806131a0 = 0;
                DAT_80613180 = &DAT_805f35a0;
                DAT_806131a8 = errClass;
                *(unsigned int *)(cmd + 0xc) = 10;
                {
                    void (*cb)(unsigned int, unsigned char *) =
                        *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                    if (cb != (void (*)(unsigned int, unsigned char *))0) {
                        cb(0xfffffffd, cmd);
                    }
                }
                if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                    DAT_806131a4(0, cmd);
                }
                FUN_803a3c98();
                handled = true;
            }
            if (!handled) {
                if (cls == 2) {
                    FUN_803a5774(status);
                    FUN_803a33c4();
                } else if (cls == 3) {
                    if ((status & 0xffffff) == 0x31100) {
                        aram_dma_write_small(*(unsigned int *)(DAT_80613180 + 0x10), FUN_803a32e8);
                    } else {
                        DAT_806131c8(DAT_80613180);
                    }
                } else if (topByte == 0x1000000) {
                    *(unsigned int *)(DAT_80613180 + 0xc) = 5;
                    FUN_803a3b84();
                } else if (topByte == 0x2000000) {
                    *(unsigned int *)(DAT_80613180 + 0xc) = 3;
                    FUN_803a3a1c();
                } else if (topByte == 0x3000000) {
                    *(unsigned int *)(DAT_80613180 + 0xc) = 4;
                    FUN_803a3b84();
                } else {
                    FUN_803a5774(0x1234567);
                    aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
                }
            }
        }
    } else {
        FUN_803a5774(0x1234567);
        aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
    }
}

// ============================================================================
// batch_1119: DVD command-engine callback chain + disc-ID verification.
//
// Address: 0x803a32e8 | Name: dvd_seek_complete       | Date: 2026-09-15
// Address: 0x803a3344 | Name: dvd_error_report        | Date: 2026-09-15
// Address: 0x803a33c4 | Name: dvd_begin_read_data     | Date: 2026-09-15
// Address: 0x803a33ec | Name: dvd_read_data_complete  | Date: 2026-09-15
// Address: 0x803a352c | Name: dvd_verify_disc_id      | Date: 2026-09-15
// Address: 0x803a360c | Name: dvd_reinit_after_id_ok  | Date: 2026-09-15
// Address: 0x803a3640 | Name: dvd_reload_bootinfo     | Date: 2026-09-15
// Address: 0x803a3674 | Name: dvd_bootinfo_complete   | Date: 2026-09-15
// Address: 0x803a36dc | Name: dvd_read_disk_id        | Date: 2026-09-15
// Address: 0x803a3714 | Name: dvd_id_read_complete    | Date: 2026-09-15
//
// These are the mailbox-completion callback nodes that thread the DVD command
// through its phases. Each has the same skeleton: cause 0x10 -> fatal reset;
// the "even/bit set" branch re-requests status via E0/E3; the success branch
// advances the command (setting the state field at cmd+0xc and retiring the
// block via the completion callback at +0x28 and secondary callback
// DAT_806131a4). dvd_verify_disc_id compares the freshly-read disc header
// (DAT_80613184) against the expected magic/ID and either re-reads or reloads
// the boot info; dvd_read_disk_id issues the 0x20-byte ARAM read of the disc-ID
// block into ARAM word 0x420.
//
// NON-MATCHING: hardware regs, the disc-ID buffers, and the DAT_806131xx command
// state are per-file externs; peer engine functions are permissive externs.

// ---- additional DVD engine state ----
extern int            DAT_8061319c;   // current command type/opcode
extern int            DAT_806131b8;   // "id verified / needs remount" latch
extern unsigned char *DAT_80613184;   // disc-header read buffer pointer
extern unsigned char  DAT_805f3560;   // scratch disc-ID DMA buffer (0x20 bytes)
extern unsigned char  DAT_805f3580;   // expected disc-ID/magic string

// ---- additional peers + libc/OS helpers ----
extern void FUN_803a38e8(unsigned int arg);   // id-ok continuation callback
extern void FUN_803a3810(unsigned int arg);   // disc-id DMA completion callback
extern int  FUN_803a57f0(char *a, char *b);    // disc-ID string compare (0 = differ)
extern void leaf_31e8(unsigned char *dst, unsigned int src, int n);  // block copy
extern void os_dc_invalidate_range(unsigned int addr, int len);
extern int  msl_memcmp(int a, unsigned char *b, int n);
extern int  aram_dma_dispatch(unsigned int mainAddrHi, unsigned int mainAddrLo,
                              unsigned int aramAddr, void (*cb)(unsigned int));
extern int  aram_cmd_e4(int setFlag, unsigned int flags, void (*cb)(unsigned int));

// ---- forward declarations for the mutually-recursive callback chain ----
void dvd_seek_complete(unsigned int cause);
void dvd_error_report(unsigned int cause);
void dvd_begin_read_data(void);
void dvd_read_data_complete(unsigned int cause);
void dvd_verify_disc_id(void);
void dvd_reinit_after_id_ok(void);
void dvd_reload_bootinfo(void);
void dvd_bootinfo_complete(unsigned int cause);
void dvd_read_disk_id(void);
void dvd_id_read_complete(unsigned int cause);

// dvd_seek_complete (0x803a32e8): completion cb after a short seek/small write.
// 0x10 -> fatal; even cause -> re-request status via E0; odd -> begin data read.
void dvd_seek_complete(unsigned int cause)
{
    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 1) == 0) {
        aram_cmd_e0(dvd_error_report);
    } else {
        dvd_begin_read_data();
    }
}

// dvd_error_report (0x803a3344): read the DI status (0xCC006020), log it, and
// finish the command with an error via E3.
void dvd_error_report(unsigned int cause)
{
    unsigned int status;

    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 2) == 0) {
        status = DAT_cc006020;
        FUN_803a5774(status);
        aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
    } else {
        FUN_803a5774(0x1234567);
        aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
    }
}

// dvd_begin_read_data (0x803a33c4): kick the data-read phase via E3.
void dvd_begin_read_data(void)
{
    aram_cmd_e3(dvd_read_data_complete);
}

// dvd_read_data_complete (0x803a33ec): data-read completion. 0x10 -> fatal;
// success -> reset retry counter, latch the remount flag for the affected
// command types, then either retire the block through the pending secondary
// callback or move it to state 0xb and continue the engine.
void dvd_read_data_complete(unsigned int cause)
{
    bool handled;
    unsigned char *cmd = DAT_80613180;

    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 2) == 0) {
        DAT_806131b4 = 0;
        if ((DAT_8061319c == 4) || (DAT_8061319c == 5) ||
            (DAT_8061319c == 0xd) || (DAT_8061319c == 0xf)) {
            DAT_806131b8 = 1;
        }
        if (DAT_806131a0 == 0) {
            handled = false;
        } else {
            DAT_806131a8 = 2;
            DAT_80613180 = &DAT_805f35a0;
            DAT_806131a0 = 0;
            *(unsigned int *)(cmd + 0xc) = 10;
            {
                void (*cb)(unsigned int, unsigned char *) =
                    *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                if (cb != (void (*)(unsigned int, unsigned char *))0) {
                    cb(0xfffffffd, cmd);
                }
            }
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, cmd);
            }
            FUN_803a3c98();
            handled = true;
        }
        if (!handled) {
            *(unsigned int *)(DAT_80613180 + 0xc) = 0xb;
            FUN_803a3b84();
        }
    } else {
        FUN_803a5774(0x1234567);
        aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
    }
}

// dvd_verify_disc_id (0x803a352c): compare the freshly-read disc header against
// the expected ID/magic. For command type 3 it does a string compare of the
// stored ID (DAT_805f3580) vs the command's path (cmd+0x24); on match it copies
// the header into the DMA buffer, sets state 1, invalidates the cache range, and
// begins the reload; otherwise it re-reads via E3. For other types it memcmp's
// the header against the expected magic and continues or re-reads.
void dvd_verify_disc_id(void)
{
    int diff;

    if (DAT_8061319c == 3) {
        diff = FUN_803a57f0((char *)&DAT_805f3580, *(char **)(DAT_80613180 + 0x24));
        if (diff == 0) {
            aram_cmd_e3(dvd_id_read_complete);
        } else {
            leaf_31e8(DAT_80613184, 0x805f3580, 0x20);
            *(unsigned int *)(DAT_80613180 + 0xc) = 1;
            os_dc_invalidate_range(0x805f3560, 0x20);
            DAT_806131c8 = (void (*)(unsigned char *))dvd_reload_bootinfo;
            dvd_reload_bootinfo();
        }
    } else {
        diff = msl_memcmp(-0x7fa0ca80, DAT_80613184, 0x20);
        if (diff == 0) {
            DAT_806131c8 = (void (*)(unsigned char *))dvd_reinit_after_id_ok;
            dvd_reinit_after_id_ok();
        } else {
            aram_cmd_e3(dvd_id_read_complete);
        }
    }
}

// dvd_reinit_after_id_ok (0x803a360c): issue an E4 mailbox command carrying the
// disc-header flag byte (DAT_80613184+8), continuing to dvd (FUN_803a38e8).
void dvd_reinit_after_id_ok(void)
{
    aram_cmd_e4((int)*(unsigned char *)(DAT_80613184 + 8), 10, FUN_803a38e8);
}

// dvd_reload_bootinfo (0x803a3640): issue an E4 mailbox command carrying the
// disc-header flag byte, continuing to dvd_bootinfo_complete.
void dvd_reload_bootinfo(void)
{
    aram_cmd_e4((int)*(unsigned char *)(DAT_80613184 + 8), 10, dvd_bootinfo_complete);
}

// dvd_bootinfo_complete (0x803a3674): E4 completion. 0x10 -> fatal; even ->
// re-request status via E0 (dvd_status_result_handler); odd -> reset retry
// counter and read the disc-ID block.
void dvd_bootinfo_complete(unsigned int cause)
{
    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 1) == 0) {
        aram_cmd_e0(dvd_status_result_handler);
    } else {
        DAT_806131b4 = 0;
        dvd_read_disk_id();
    }
}

// dvd_read_disk_id (0x803a36dc): dispatch a 0x20-byte ARAM transfer of the
// disc-ID scratch buffer (&DAT_805f3560) to ARAM word 0x420, completing to
// FUN_803a3810.
void dvd_read_disk_id(void)
{
    aram_dma_dispatch((unsigned int)(unsigned long long)&DAT_805f3560, 0x20, 0x420, FUN_803a3810);
}

// dvd_id_read_complete (0x803a3714): disc-ID read completion. 0x10 -> fatal;
// success -> reset retry counter, then either retire the block through the
// pending secondary callback (error class 1) or set state 6 and continue.
void dvd_id_read_complete(unsigned int cause)
{
    bool handled;
    unsigned char *cmd = DAT_80613180;

    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 2) == 0) {
        handled = false;
        DAT_806131b4 = 0;
        if (DAT_806131a0 != 0) {
            DAT_806131a8 = 1;
            DAT_806131a0 = 0;
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 10;
            {
                void (*cb)(unsigned int, unsigned char *) =
                    *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                if (cb != (void (*)(unsigned int, unsigned char *))0) {
                    cb(0xfffffffd, cmd);
                }
            }
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, cmd);
            }
            FUN_803a3c98();
            handled = true;
        }
        if (!handled) {
            *(unsigned int *)(DAT_80613180 + 0xc) = 6;
            FUN_803a3b84();
        }
    } else {
        FUN_803a5774(0x1234567);
        aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
    }
}

} // extern "C"

// ============================================================================
// batch_1120: DVD disc-change / eject-recovery callbacks + the command-block
// execution state machine (the "run one command block" switch).
//
// Address: 0x803a3810 | Name: dvd_changedisk_fst_reload  | Date: 2026-09-15
// Address: 0x803a38e8 | Name: dvd_eject_recover          | Date: 2026-09-15
// Address: 0x803a39d8 | Name: dvd_reissue_id_read        | Date: 2026-09-15
// Address: 0x803a3a1c | Name: dvd_id_retry_or_defer      | Date: 2026-09-15
// Address: 0x803a3af0 | Name: dvd_read_new_disc_id       | Date: 2026-09-15
// Address: 0x803a3b20 | Name: dvd_new_id_read_complete   | Date: 2026-09-15
// Address: 0x803a3b84 | Name: dvd_begin_reset_recover    | Date: 2026-09-15
// Address: 0x803a3bac | Name: dvd_reset_recover_complete | Date: 2026-09-15
// Address: 0x803a3c98 | Name: dvd_run_next_command       | Date: 2026-09-15
// Address: 0x803a3f80 | Name: dvd_execute_command_block  | Date: 2026-09-15
//
// These continue the DVD command engine. Every mailbox-completion callback in
// this family shares the skeleton: fatal (cause 0x10) -> log + hard-cleanup +
// finish-with-error; even/idle cause -> re-request DI status via aram_cmd_e0;
// odd/set cause -> take the command-specific branch. The disc-change / eject
// path (dvd_changedisk_fst_reload, dvd_eject_recover, dvd_read_new_disc_id,
// dvd_new_id_read_complete) reloads the FST for a swapped disc and re-verifies
// the disc id, deferring through a periodic OSAlarm (dvd_id_retry_or_defer)
// while the drive settles. dvd_run_next_command retires the current command
// block, pulls the next queued block (FUN_803a5500) and dispatches it by its
// pending sub-state (DAT_806131a8) or its type; dvd_execute_command_block is
// the big per-type switch that actually programs the ARAM/DSP mailbox command
// for the block (types 1..0x10: reads, small writes, e1/e2/e3/e4/12 commands).
//
// NON-MATCHING: the DAT_806131xx / DAT_805f35xx driver state, the DI/AI
// register at 0xCC006004, and the low-memory bus-clock word at 0x800000F8 are
// per-file externs; peer state-machine functions and cross-module ARAM/OS
// helpers are declared with matching signatures. Control flow, predicates, and
// the switch cases are preserved verbatim.

// ---- additional DVD driver state ----
extern int          DAT_80613188;   // ChangeDisk: new-disc command block ptr
extern int          DAT_8061318c;   // "no more queued commands" latch
extern int          DAT_80613190;   // deferred-restart flag
extern int          DAT_80613194;   // ARAM streaming-mode selector latch
extern unsigned int DAT_805f3568;   // ChangeDisk: new FST size (bytes)
extern int          DAT_805f3564;   // ChangeDisk: new FST main-mem dest
extern int          DAT_806131c0;   // "drive needs re-spin/settle" flag
extern unsigned int DAT_806131ac;   // pending error/reset code
extern int          DAT_800000f8;   // low-mem: console bus clock (Hz)
extern unsigned int DAT_cc006004;   // AI/DI secondary control register
extern void       (*DAT_80611418)(int, void (*)(unsigned int)); // FST-load hook
extern unsigned char DAT_8061141c;            // OSPanic module-name anchor
extern unsigned char DAT_805f35d0;            // retry/settle OSAlarm object

extern int  DVDChangeDisk;                    // OSPanic caller-name anchor
extern char s_DVDChangeDisk____FST_in_the_new_d_8049a004[];

// ---- peer DVD state-machine functions (this + later batches) ----
extern void dvd_verify_disc_id(void);            // 0x803a352c (batch_1119)
extern void dvd_status_result_handler(unsigned int cause); // 0x803a3084
extern void dvd_cmd_finish_error(int cause);     // 0x803a2ec8 (batch_1118)
extern void FUN_803a5460(void);                  // command-complete cleanup
extern int *FUN_803a5500(void);                  // pop next queued command block
extern int  FUN_803a55a0(void);                  // any queued commands remaining?
extern void FUN_803a42c0(unsigned int cause);    // command-step completion cb

// ---- cross-module ARAM (audio/dsp_hw.cpp) + OS (os/os_alarm.cpp) helpers ----
extern int  aram_cmd_e1(unsigned int flags, unsigned int mainWord,
                        unsigned int aramAddr, void (*cb)(unsigned int));
extern int  aram_cmd_e2(unsigned int flags, void (*cb)(unsigned int));
extern int  aram_cmd_12(unsigned int mainAddrLo, void (*cb)(unsigned int));
extern int  aram_cmd_write_64(unsigned int mainAddrLo, void (*cb)(unsigned int));
extern int  aram_dma_reset_request(void (*cb)(unsigned int));
extern void os_set_alarm(unsigned int *alarm, unsigned int now_arg,
                         unsigned int period_hi, unsigned int period_lo,
                         void *handler);
extern void os_alarm_clear(unsigned int *alarm);
extern void OSPanic(unsigned char *file, int line, const char *fmt, ...);

// forward declarations for the mutually-recursive callbacks below
void dvd_reissue_id_read(void);
void dvd_read_new_disc_id(void);
void dvd_new_id_read_complete(unsigned int cause);
void dvd_run_next_command(void);
void dvd_reset_recover_complete(void);
void dvd_execute_command_block(int cmd);
void dvd_state_advance(unsigned int cause);

// dvd_changedisk_fst_reload (0x803a3810): completion callback used while a new
// disc is being brought online. On a good status it re-arms DVDChangeDisk and,
// after sanity-checking that the new FST fits, DMAs the new FST image from the
// disc into main memory (rounded up to a 0x20 boundary) with dvd_state_advance
// as the completion callback.
void dvd_changedisk_fst_reload(unsigned int cause)
{
    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 1) == 0) {
        aram_cmd_e0((void (*)(unsigned int))dvd_status_result_handler);
    } else {
        DAT_806131c8 = (void (*)(unsigned char *))&DVDChangeDisk;
        DAT_806131b4 = 0;
        if (*(unsigned int *)(DAT_80613188 + 0x3c) < DAT_805f3568) {
            OSPanic(&DAT_8061141c, 0x295,
                    s_DVDChangeDisk____FST_in_the_new_d_8049a004);
        }
        aram_dma_dispatch(*(unsigned int *)(DAT_80613188 + 0x38),
                          (DAT_805f3568 + 0x1f) & 0xffffffe0,
                          (unsigned int)DAT_805f3564,
                          (void (*)(unsigned int))dvd_state_advance);
    }
}

// dvd_eject_recover (0x803a38e8): completion callback after an eject/cover
// event. On good status it either services a pending eject (notify callbacks,
// mark the block state 10, retire it and run the next command) or, if none is
// pending, marks the current block state 1 and (re)executes it.
void dvd_eject_recover(unsigned int cause)
{
    unsigned char *cmd = DAT_80613180;
    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 1) == 0) {
        aram_cmd_e0((void (*)(unsigned int))dvd_status_result_handler);
    } else {
        bool handled = false;
        DAT_806131b4 = 0;
        if (DAT_806131a0 != 0) {
            DAT_806131a8 = 0;
            DAT_806131a0 = 0;
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 10;
            {
                void (*cb)(unsigned int, unsigned char *) =
                    *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                if (cb != (void (*)(unsigned int, unsigned char *))0) {
                    cb(0xfffffffd, cmd);
                }
            }
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, cmd);
            }
            dvd_run_next_command();
            handled = true;
        }
        if (!handled) {
            *(unsigned int *)(DAT_80613180 + 0xc) = 1;
            dvd_execute_command_block((int)DAT_80613180);
        }
    }
}

// dvd_reissue_id_read (0x803a39d8): OSAlarm handler that re-issues the disc-id
// read after the settle delay: clear the DI latch, invalidate the id scratch
// buffer, and kick dvd_read_new_disc_id.
void dvd_reissue_id_read(void)
{
    FUN_803a4d24();
    os_dc_invalidate_range(0x805f3580, 0x20);
    DAT_806131c8 = (void (*)(unsigned char *))dvd_read_new_disc_id;
    dvd_read_new_disc_id();
}

// dvd_id_retry_or_defer (0x803a3a1c): shared "retry or defer" tail. If the
// command's DI sub-state is one of the finalize codes (0xd, 0xf, or 4..5), run
// the complete-cleanup, notify the cancel callback, and retire the block via
// dvd_run_next_command. Otherwise arm a ~1ms periodic OSAlarm that re-issues
// the id read once the drive has settled.
void dvd_id_retry_or_defer(void)
{
    unsigned char *cmd;
    int st = DAT_8061319c;
    bool finalize = (st == 0xd) || (st == 0xf) ||
                    (st < 0xd && st < 6 && 3 < st);
    if (finalize) {
        FUN_803a5460();
        cmd = DAT_80613180;
        DAT_80613180 = &DAT_805f35a0;
        {
            void (*cb)(unsigned int) = *(void (**)(unsigned int))(cmd + 0x28);
            if (cb != (void (*)(unsigned int))0) {
                cb(0xfffffffc);
            }
        }
        dvd_run_next_command();
    } else {
        DAT_806131c0 = 0;
        FUN_803a4d24();
        os_alarm_clear((unsigned int *)&DAT_805f35d0);
        os_set_alarm((unsigned int *)&DAT_805f35d0, 0x10624dd3, 0,
                     (DAT_800000f8 / 4000) * 0x47e, (void *)dvd_reissue_id_read);
    }
}

// dvd_read_new_disc_id (0x803a3af0): kick a 64-byte read of the new disc id
// block into the id scratch buffer, completing to dvd_new_id_read_complete.
void dvd_read_new_disc_id(void)
{
    aram_cmd_write_64((unsigned int)&DAT_805f3580,
                      (void (*)(unsigned int))dvd_new_id_read_complete);
}

// dvd_new_id_read_complete (0x803a3b20): completion of the new disc-id read.
// On good status, verify the freshly read id against the expected value.
void dvd_new_id_read_complete(unsigned int cause)
{
    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
    } else if ((cause & 1) == 0) {
        aram_cmd_e0((void (*)(unsigned int))dvd_status_result_handler);
    } else {
        DAT_806131b4 = 0;
        dvd_verify_disc_id();
    }
}

// dvd_begin_reset_recover (0x803a3b84): request an AI/DMA reset, resuming at
// dvd_reset_recover_complete.
void dvd_begin_reset_recover(void)
{
    aram_dma_reset_request((void (*)(unsigned int))dvd_reset_recover_complete);
}

// dvd_reset_recover_complete (0x803a3bac): after the AI reset, clear the
// secondary control register, mark the current block state 3, then run the
// shared retry-or-defer tail (identical predicate to dvd_id_retry_or_defer).
void dvd_reset_recover_complete(void)
{
    unsigned char *cmd;
    DAT_cc006004 = 0;
    *(unsigned int *)(DAT_80613180 + 0xc) = 3;
    int st = DAT_8061319c;
    bool finalize = (st == 0xd) || (st == 0xf) ||
                    (st < 0xd && st < 6 && 3 < st);
    if (finalize) {
        FUN_803a5460();
        cmd = DAT_80613180;
        DAT_80613180 = &DAT_805f35a0;
        {
            void (*cb)(unsigned int) = *(void (**)(unsigned int))(cmd + 0x28);
            if (cb != (void (*)(unsigned int))0) {
                cb(0xfffffffc);
            }
        }
        dvd_run_next_command();
    } else {
        DAT_806131c0 = 0;
        FUN_803a4d24();
        os_alarm_clear((unsigned int *)&DAT_805f35d0);
        os_set_alarm((unsigned int *)&DAT_805f35d0, 0x10624dd3, 0,
                     (DAT_800000f8 / 4000) * 0x47e, (void *)dvd_reissue_id_read);
    }
}

// dvd_run_next_command (0x803a3c98): retire the current command block and pull
// the next queued one. If nothing is queued, park the driver idle. Otherwise
// pop the next block (FUN_803a5500); if it was already cancelled, notify and
// recurse. Then, based on its pending sub-state (DAT_806131a8) or its "needs
// respin" flag (DAT_806131c0), either program a reset/e3, run the retry-or-defer
// tail, or execute the block via dvd_execute_command_block.
void dvd_run_next_command(void)
{
    int *cmd;
    if (FUN_803a55a0() == 0) {
        DAT_80613180 = (unsigned char *)0;
        return;
    }
    if (DAT_8061318c != 0) {
        DAT_80613180 = (unsigned char *)0;
        DAT_80613190 = 1;
        return;
    }
    DAT_80613180 = (unsigned char *)FUN_803a5500();
    if (DAT_80613198 != 0) {
        cmd = (int *)DAT_80613180;
        cmd[3] = -1;
        DAT_80613180 = &DAT_805f35a0;
        {
            void (*cb)(unsigned int) = (void (*)(unsigned int))cmd[10];
            if (cb != (void (*)(unsigned int))0) {
                cb(0xffffffff);
            }
        }
        dvd_run_next_command();
        return;
    }
    DAT_8061319c = ((int *)DAT_80613180)[2];
    if (DAT_806131a8 != 0) {
        if (DAT_806131a8 == 4) {
            ((int *)DAT_80613180)[3] = 5;
            aram_dma_reset_request(
                (void (*)(unsigned int))dvd_reset_recover_complete);
            DAT_806131a8 = 0;
            return;
        }
        if (DAT_806131a8 < 4) {
            if (DAT_806131a8 == 2) {
                ((int *)DAT_80613180)[3] = 0xb;
                aram_dma_reset_request(
                    (void (*)(unsigned int))dvd_reset_recover_complete);
                DAT_806131a8 = 0;
                return;
            }
            if (1 < DAT_806131a8) {
                ((int *)DAT_80613180)[3] = 4;
                aram_dma_reset_request(
                    (void (*)(unsigned int))dvd_reset_recover_complete);
                DAT_806131a8 = 0;
                return;
            }
            if (DAT_806131a8 < 1) {
                DAT_806131a8 = 0;
                return;
            }
        } else {
            if (7 < DAT_806131a8) {
                DAT_806131a8 = 0;
                return;
            }
            if (DAT_806131a8 < 6) {
                FUN_803a5774(DAT_806131ac);
                aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
                DAT_806131a8 = 0;
                return;
            }
        }
        ((int *)DAT_80613180)[3] = 3;
        {
            int st = DAT_8061319c;
            bool finalize = (st == 0xd) || (st == 0xf) ||
                            (st < 0xd && st < 6 && 3 < st);
            if (finalize) {
                FUN_803a5460();
                cmd = (int *)DAT_80613180;
                DAT_80613180 = &DAT_805f35a0;
                {
                    void (*cb)(unsigned int) = (void (*)(unsigned int))cmd[10];
                    if (cb != (void (*)(unsigned int))0) {
                        cb(0xfffffffc);
                    }
                }
                dvd_run_next_command();
            } else {
                DAT_806131c0 = 0;
                FUN_803a4d24();
                os_alarm_clear((unsigned int *)&DAT_805f35d0);
                os_set_alarm((unsigned int *)&DAT_805f35d0, 0x10624dd3, 0,
                             (DAT_800000f8 / 4000) * 0x47e,
                             (void *)dvd_reissue_id_read);
            }
        }
        DAT_806131a8 = 0;
        return;
    }
    if (DAT_806131c0 == 0) {
        ((int *)DAT_80613180)[3] = 1;
        dvd_execute_command_block((int)DAT_80613180);
        return;
    }
    {
        int st = DAT_8061319c;
        bool finalize = (st == 0xd) || (st == 0xf) ||
                        (st < 0xd && st < 6 && 3 < st);
        if (finalize) {
            FUN_803a5460();
            cmd = (int *)DAT_80613180;
            DAT_80613180 = &DAT_805f35a0;
            {
                void (*cb)(unsigned int) = (void (*)(unsigned int))cmd[10];
                if (cb != (void (*)(unsigned int))0) {
                    cb(0xfffffffc);
                }
            }
            dvd_run_next_command();
        } else {
            DAT_806131c0 = 0;
            FUN_803a4d24();
            os_alarm_clear((unsigned int *)&DAT_805f35d0);
            os_set_alarm((unsigned int *)&DAT_805f35d0, 0x10624dd3, 0,
                         (DAT_800000f8 / 4000) * 0x47e,
                         (void *)dvd_reissue_id_read);
        }
    }
}

// dvd_execute_command_block (0x803a3f80): the per-type command dispatcher. It
// reads the command type (word at +8) of a command block and programs the
// matching ARAM/DSP mailbox command, with FUN_803a42c0 as the step-completion
// callback. Type 1/4 are chunked reads (<=0x80000 per step); 2 is a small
// write; 5 a 64-byte write; 6 an e1/e2 stream setup; 3/0xf/0x10 e3 control; the
// remaining cases are e2/e4/12 variants. The default runs the FST-load hook.
void dvd_execute_command_block(int cmd)
{
    unsigned char *prevCmd = DAT_80613180;
    unsigned int hold;
    unsigned int count;
    void (*cb)(unsigned int, unsigned char *);
    DAT_806131c8 = (void (*)(unsigned char *))dvd_execute_command_block;
    switch (*(int *)(cmd + 8)) {
    default:
        DAT_80611418(cmd, FUN_803a42c0);
        break;
    case 1:
    case 4:
        if (*(int *)(cmd + 0x14) == 0) {
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(prevCmd + 0xc) = 0;
            cb = *(void (**)(unsigned int, unsigned char *))(prevCmd + 0x28);
            if (cb != (void (*)(unsigned int, unsigned char *))0) {
                cb(0, prevCmd);
            }
            dvd_run_next_command();
        } else {
            hold = DAT_cc006004;
            DAT_cc006004 = hold;
            count = *(unsigned int *)(cmd + 0x14) - *(unsigned int *)(cmd + 0x20);
            if (0x80000 < count) {
                count = 0x80000;
            }
            *(unsigned int *)(cmd + 0x1c) = count;
            aram_dma_dispatch(*(unsigned int *)(cmd + 0x18) + *(unsigned int *)(cmd + 0x20),
                              *(unsigned int *)(cmd + 0x1c),
                              *(unsigned int *)(cmd + 0x10) + *(unsigned int *)(cmd + 0x20),
                              (void (*)(unsigned int))FUN_803a42c0);
        }
        break;
    case 2:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_dma_write_small(*(unsigned int *)(cmd + 0x10), FUN_803a42c0);
        break;
    case 3:
        aram_cmd_e3(FUN_803a42c0);
        break;
    case 5:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        *(unsigned int *)(cmd + 0x1c) = 0x20;
        aram_cmd_write_64(*(unsigned int *)(cmd + 0x18), FUN_803a42c0);
        break;
    case 6:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        if (DAT_80613194 == 0) {
            *(unsigned int *)(DAT_80613180 + 0x1c) = 1;
            aram_cmd_e1(0, *(unsigned int *)(cmd + 0x14),
                        *(unsigned int *)(cmd + 0x10), FUN_803a42c0);
        } else {
            *(unsigned int *)(DAT_80613180 + 0x1c) = 0;
            aram_cmd_e2(0, FUN_803a42c0);
        }
        break;
    case 7:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e1(0x10000, 0, 0, FUN_803a42c0);
        break;
    case 8:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        DAT_80613194 = 1;
        aram_cmd_e1(0, 0, 0, FUN_803a42c0);
        break;
    case 9:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e2(0, FUN_803a42c0);
        break;
    case 10:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e2(0x10000, FUN_803a42c0);
        break;
    case 0xb:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e2(0x20000, FUN_803a42c0);
        break;
    case 0xc:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e2(0x30000, FUN_803a42c0);
        break;
    case 0xd:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e4(*(int *)(cmd + 0x10), *(unsigned int *)(cmd + 0x14), FUN_803a42c0);
        break;
    case 0xe:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        *(unsigned int *)(cmd + 0x1c) = 0x20;
        aram_cmd_12(*(unsigned int *)(cmd + 0x18), FUN_803a42c0);
        break;
    case 0xf:
        aram_cmd_e3(FUN_803a42c0);
        break;
    case 0x10:
        hold = DAT_cc006004;
        DAT_cc006004 = hold;
        aram_cmd_e3(FUN_803a42c0);
        break;
    }
}

// ============================================================================
// batch_1121: the DVD command step-completion handler + the public async
// command submitters + the DI hardware reset and status queries.
//
// Address: 0x803a42c0 | Name: dvd_step_complete       | Date: 2026-09-15
// Address: 0x803a4918 | Name: dvd_submit_command      | Date: 2026-09-15
// Address: 0x803a49f4 | Name: dvd_submit_read         | Date: 2026-09-15
// Address: 0x803a4ac4 | Name: dvd_submit_read_diskid  | Date: 2026-09-15
// Address: 0x803a4b98 | Name: dvd_submit_bs_read      | Date: 2026-09-15
// Address: 0x803a4c54 | Name: dvd_submit_audio_stream | Date: 2026-09-15
// Address: 0x803a4d24 | Name: dvd_reset_di            | Date: 2026-09-15
// Address: 0x803a4d68 | Name: dvd_cmd_block_state      | Date: 2026-09-15
// Address: 0x803a4db4 | Name: dvd_get_drive_status     | Date: 2026-09-15
// Address: 0x803a4e60 | Name: dvd_set_autoinvalidate   | Date: 2026-09-15
//
// dvd_step_complete (FUN_803a42c0) is the completion callback threaded through
// every ARAM/DSP mailbox step launched by dvd_execute_command_block. It reads
// the per-command sub-state (DAT_8061319c) to decide how to interpret the DI
// byte-count / result registers (0xCC006018 / 0xCC006020), advances the running
// command's transfer cursor (cmd+0x20 += requested cmd+0x1c minus remaining),
// then either loops the next chunk, finishes the command with its callback
// (cmd+0x28), handles an eject in flight, or re-requests DI status. The five
// dvd_submit_* functions are the public async entry points: they fill a caller
// command block (type at word+2, buffers at +4/+5/+6, callback at +10),
// optionally DC-invalidate the destination for reads, then enqueue the block
// (FUN_803a5498) under an interrupt lock and immediately kick the engine
// (dvd_run_next_command) when the driver is idle. dvd_reset_di pulses the DSP,
// re-arms the AI control register, and clears the pending sub-state. The last
// three are the DVDGetCommandBlockStatus / drive-status / auto-invalidate
// accessors.
//
// NON-MATCHING: the DAT_806131xx driver state, the DI/AI registers, and the
// FST/error-code constants at 0x8049a0xx are per-file externs; peer functions
// and cross-module OS/ARAM helpers are declared with matching signatures.
// Control flow, the sub-state comparisons, and the byte-count math are verbatim.

// ---- additional constants / state ----
extern int          DAT_80611424;   // "requested-transfer" command-type sentinel
extern int          DAT_8049a07c;   // DI result-passthrough command type
extern int          DAT_8049a080;   // DI result-passthrough command type
extern int          DAT_8049a084;   // DI result-passthrough command type
extern int          DAT_80611414;   // auto-invalidate-on-read enable flag
extern unsigned int DAT_cc006000;   // AI/DSP main control register

// ---- peer helpers (this file + later batches) ----
extern int  FUN_803a5498(int queueKind, unsigned int *cmdBlock); // enqueue block

// ---- cross-module OS interrupt + audio-DSP reset helpers ----
extern int  os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void dsp_hw_reset_pulse(void);   // 0x803a2364 (audio/dsp_hw.cpp)

// dvd_step_complete (0x803a42c0): step-completion callback for the active DVD
// command. Interprets the DI byte-count / result registers per sub-state,
// advances the transfer cursor, and drives the command to the next chunk, to
// completion (invoking cmd+0x28 with the transferred count or the raw DI
// result), to an eject-in-flight retirement, or back to a status re-request.
void dvd_step_complete(unsigned int cause)
{
    unsigned char *cmd = DAT_80613180;
    int diResult;
    bool flag;
    unsigned int status;

    if (cause == 0x10) {
        FUN_803a5774(0x1234568);
        FUN_803a4d24();
        dvd_cmd_finish_error(0);
        return;
    }

    if ((DAT_8061319c != 3) && (DAT_8061319c != 0xf)) {
        // count-advancing command types: fold the DI residual into the cursor
        if ((DAT_8061319c == 1) ||
            ((unsigned int)(DAT_8061319c - 4) < 2) || (DAT_8061319c == 0xe)) {
            flag = true;
        } else if (DAT_8061319c == DAT_80611424) {
            flag = true;
        } else {
            flag = false;
        }
        if (flag) {
            diResult = DAT_cc006018;
            *(int *)(DAT_80613180 + 0x20) =
                *(int *)(DAT_80613180 + 0x20) +
                (*(int *)(DAT_80613180 + 0x1c) - diResult);
        }
        cmd = DAT_80613180;

        if ((cause & 8) != 0) {
            // eject/cover event mid-transfer: retire the command
            DAT_806131a0 = 0;
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 10;
            {
                void (*cb)(unsigned int, unsigned char *) =
                    *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                if (cb != (void (*)(unsigned int, unsigned char *))0) {
                    cb(0xfffffffd, cmd);
                }
            }
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, cmd);
            }
            dvd_run_next_command();
            return;
        }

        if ((cause & 1) == 0) {
            // normal (non-error) completion of a step
            if (DAT_8061319c == 0xe) {
                FUN_803a5774(0x1234567);
                aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
                return;
            }
            if (((DAT_8061319c == 1) || (DAT_8061319c == 4) ||
                 (DAT_8061319c == 5) || (DAT_8061319c == 0xe)) &&
                (*(int *)(DAT_80613180 + 0x20) == *(int *)(DAT_80613180 + 0x14))) {
                // whole transfer complete
                if (DAT_806131a0 == 0) {
                    flag = false;
                } else {
                    DAT_806131a8 = 0;
                    DAT_806131a0 = 0;
                    DAT_80613180 = &DAT_805f35a0;
                    *(unsigned int *)(cmd + 0xc) = 10;
                    {
                        void (*cb)(unsigned int, unsigned char *) =
                            *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                        if (cb != (void (*)(unsigned int, unsigned char *))0) {
                            cb(0xfffffffd, cmd);
                        }
                    }
                    if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                        DAT_806131a4(0, cmd);
                    }
                    dvd_run_next_command();
                    flag = true;
                }
                cmd = DAT_80613180;
                if (flag) {
                    return;
                }
                DAT_80613180 = &DAT_805f35a0;
                *(unsigned int *)(cmd + 0xc) = 0;
                {
                    void (*cb)(unsigned int) =
                        *(void (**)(unsigned int))(cmd + 0x28);
                    if (cb != (void (*)(unsigned int))0) {
                        cb(*(unsigned int *)(cmd + 0x20));
                    }
                }
                dvd_run_next_command();
                return;
            }
            aram_cmd_e0((void (*)(unsigned int))dvd_status_result_handler);
            return;
        }

        // error path (cause bit 0 set)
        flag = false;
        DAT_806131b4 = 0;
        if (DAT_8061319c == 0x10) {
            DAT_806131c0 = 1;
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 0;
            {
                void (*cb)(unsigned int) = *(void (**)(unsigned int))(cmd + 0x28);
                if (cb != (void (*)(unsigned int))0) {
                    cb(0);
                }
            }
            dvd_run_next_command();
            return;
        }
        if (DAT_806131a0 != 0) {
            DAT_806131a8 = 0;
            DAT_806131a0 = 0;
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 10;
            {
                void (*cb)(unsigned int, unsigned char *) =
                    *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                if (cb != (void (*)(unsigned int, unsigned char *))0) {
                    cb(0xfffffffd, cmd);
                }
            }
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, cmd);
            }
            dvd_run_next_command();
            flag = true;
        }
        cmd = DAT_80613180;
        if (flag) {
            return;
        }
        if ((DAT_8061319c == 1) || ((unsigned int)(DAT_8061319c - 4) < 2) ||
            (DAT_8061319c == 0xe)) {
            flag = true;
        } else if (DAT_8061319c == DAT_80611424) {
            flag = true;
        } else {
            flag = false;
        }
        if (flag) {
            if (*(int *)(DAT_80613180 + 0x20) != *(int *)(DAT_80613180 + 0x14)) {
                dvd_execute_command_block((int)DAT_80613180);
                return;
            }
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 0;
            {
                void (*cb)(unsigned int) = *(void (**)(unsigned int))(cmd + 0x28);
                if (cb != (void (*)(unsigned int))0) {
                    cb(*(unsigned int *)(cmd + 0x20));
                }
            }
            dvd_run_next_command();
            return;
        }
        // DI-result-passthrough command types
        if ((unsigned int)(DAT_8061319c - 9) < 4) {
            flag = true;
        } else if (DAT_8061319c == DAT_8049a07c) {
            flag = true;
        } else if (DAT_8061319c == DAT_8049a080) {
            flag = true;
        } else if (DAT_8061319c == DAT_8049a084) {
            flag = true;
        } else {
            flag = false;
        }
        if (flag) {
            if ((DAT_8061319c == 0xb) || (DAT_8061319c == 10)) {
                diResult = DAT_cc006020;
                diResult = diResult << 2;
            } else {
                diResult = DAT_cc006020;
            }
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 0;
            {
                void (*cb)(int) = *(void (**)(int))(cmd + 0x28);
                if (cb != (void (*)(int))0) {
                    cb(diResult);
                }
            }
            dvd_run_next_command();
            return;
        }
        if (DAT_8061319c != 6) {
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 0;
            {
                void (*cb)(unsigned int) = *(void (**)(unsigned int))(cmd + 0x28);
                if (cb != (void (*)(unsigned int))0) {
                    cb(0);
                }
            }
            dvd_run_next_command();
            return;
        }
        // sub-state 6: audio-stream status probe
        if (*(int *)(DAT_80613180 + 0x1c) == 0) {
            status = DAT_cc006020;
            if ((status & 1) != 0) {
                DAT_80613180 = &DAT_805f35a0;
                *(unsigned int *)(cmd + 0xc) = 9;
                {
                    void (*cb)(unsigned int) =
                        *(void (**)(unsigned int))(cmd + 0x28);
                    if (cb != (void (*)(unsigned int))0) {
                        cb(0xfffffffe);
                    }
                }
                dvd_run_next_command();
                return;
            }
            DAT_80613194 = 0;
            *(unsigned int *)(DAT_80613180 + 0x1c) = 1;
            aram_cmd_e1(0, *(unsigned int *)(DAT_80613180 + 0x14),
                        *(unsigned int *)(DAT_80613180 + 0x10),
                        (void (*)(unsigned int))dvd_step_complete);
            return;
        }
        DAT_80613180 = &DAT_805f35a0;
        *(unsigned int *)(cmd + 0xc) = 0;
        {
            void (*cb)(unsigned int) = *(void (**)(unsigned int))(cmd + 0x28);
            if (cb != (void (*)(unsigned int))0) {
                cb(0);
            }
        }
        dvd_run_next_command();
        return;
    }

    // sub-state 3 or 0xf: control/seek completion
    if ((cause & 2) == 0) {
        DAT_806131b4 = 0;
        if (DAT_8061319c == 0xf) {
            DAT_806131b8 = 1;
        }
        if (DAT_806131a0 == 0) {
            flag = false;
        } else {
            DAT_806131a8 = 7;
            DAT_806131a0 = 0;
            DAT_80613180 = &DAT_805f35a0;
            *(unsigned int *)(cmd + 0xc) = 10;
            {
                void (*cb)(unsigned int, unsigned char *) =
                    *(void (**)(unsigned int, unsigned char *))(cmd + 0x28);
                if (cb != (void (*)(unsigned int, unsigned char *))0) {
                    cb(0xfffffffd, cmd);
                }
            }
            if (DAT_806131a4 != (void (*)(int, unsigned char *))0) {
                DAT_806131a4(0, cmd);
            }
            dvd_run_next_command();
            flag = true;
        }
        if (!flag) {
            *(unsigned int *)(DAT_80613180 + 0xc) = 7;
            aram_dma_reset_request(
                (void (*)(unsigned int))dvd_reset_recover_complete);
        }
    } else {
        FUN_803a5774(0x1234567);
        aram_cmd_e3((void (*)(unsigned int))dvd_cmd_finish_error);
    }
}

// ---- public async command submitters -------------------------------------
// Each fills the caller's command block (int-indexed: [2]=type, [3]=state,
// [4]=length, [5]=count/len, [6]=dest, [8]=cursor, [10]=callback), optionally
// DC-invalidates the read destination, then enqueues the block and kicks the
// engine if the driver is idle. Returns the enqueue result.

// dvd_submit_command (0x803a4918): generic submit taking an explicit type/queue.
int dvd_submit_command(int *cmdBlock, int dest, int count, int length,
                       void *callback, int queueKind)
{
    int type;
    int result;
    cmdBlock[2] = 1;
    cmdBlock[6] = dest;
    cmdBlock[5] = count;
    cmdBlock[4] = length;
    cmdBlock[8] = 0;
    cmdBlock[10] = (int)callback;
    type = cmdBlock[2];
    if ((DAT_80611414 != 0) &&
        (type == 1 || ((unsigned int)(type - 4) < 2) || (type == 0xe))) {
        os_dc_invalidate_range(cmdBlock[6], cmdBlock[5]);
    }
    os_disable_interrupts();
    cmdBlock[3] = 2;
    result = FUN_803a5498(queueKind, (unsigned int *)cmdBlock);
    if ((DAT_80613180 == (unsigned char *)0) && (DAT_8061318c == 0)) {
        dvd_run_next_command();
    }
    os_get_interrupts_enabled();
    return result;
}

// dvd_submit_read (0x803a49f4): submit a type-4 read (queue kind 2).
int dvd_submit_read(int *cmdBlock, int dest, int count, int length,
                    void *callback)
{
    int type;
    int result;
    cmdBlock[2] = 4;
    cmdBlock[6] = dest;
    cmdBlock[5] = count;
    cmdBlock[4] = length;
    cmdBlock[8] = 0;
    cmdBlock[10] = (int)callback;
    type = cmdBlock[2];
    if ((DAT_80611414 != 0) &&
        (type == 1 || ((unsigned int)(type - 4) < 2) || (type == 0xe))) {
        os_dc_invalidate_range(cmdBlock[6], cmdBlock[5]);
    }
    os_disable_interrupts();
    cmdBlock[3] = 2;
    result = FUN_803a5498(2, (unsigned int *)cmdBlock);
    if ((DAT_80613180 == (unsigned char *)0) && (DAT_8061318c == 0)) {
        dvd_run_next_command();
    }
    os_get_interrupts_enabled();
    return result;
}

// dvd_submit_read_diskid (0x803a4ac4): submit a type-5 32-byte disc-id read.
int dvd_submit_read_diskid(int *cmdBlock, int dest, void *callback)
{
    int type;
    int result;
    cmdBlock[2] = 5;
    cmdBlock[6] = dest;
    cmdBlock[5] = 0x20;
    cmdBlock[4] = 0;
    cmdBlock[8] = 0;
    cmdBlock[10] = (int)callback;
    type = cmdBlock[2];
    if ((DAT_80611414 != 0) &&
        (type == 1 || ((unsigned int)(type - 4) < 2) || (type == 0xe))) {
        os_dc_invalidate_range(cmdBlock[6], cmdBlock[5]);
    }
    os_disable_interrupts();
    cmdBlock[3] = 2;
    result = FUN_803a5498(2, (unsigned int *)cmdBlock);
    if ((DAT_80613180 == (unsigned char *)0) && (DAT_8061318c == 0)) {
        dvd_run_next_command();
    }
    os_get_interrupts_enabled();
    return result;
}

// dvd_submit_bs_read (0x803a4b98): submit a type-7 low-level (BS) command
// (queue kind 1); reuses whatever dest/len are already in the block.
int dvd_submit_bs_read(int *cmdBlock, void *callback)
{
    int type;
    int result;
    cmdBlock[2] = 7;
    cmdBlock[10] = (int)callback;
    type = cmdBlock[2];
    if ((DAT_80611414 != 0) &&
        (type == 1 || ((unsigned int)(type - 4) < 2) || (type == 0xe))) {
        os_dc_invalidate_range(cmdBlock[6], cmdBlock[5]);
    }
    os_disable_interrupts();
    cmdBlock[3] = 2;
    result = FUN_803a5498(1, (unsigned int *)cmdBlock);
    if ((DAT_80613180 == (unsigned char *)0) && (DAT_8061318c == 0)) {
        dvd_run_next_command();
    }
    os_get_interrupts_enabled();
    return result;
}

// dvd_submit_audio_stream (0x803a4c54): submit a type-0xe audio-stream command
// (queue kind 2), a 32-byte transfer into dest.
int dvd_submit_audio_stream(int *cmdBlock, int dest, void *callback)
{
    int type;
    int result;
    cmdBlock[2] = 0xe;
    cmdBlock[6] = dest;
    cmdBlock[5] = 0x20;
    cmdBlock[8] = 0;
    cmdBlock[10] = (int)callback;
    type = cmdBlock[2];
    if ((DAT_80611414 != 0) &&
        (type == 1 || ((unsigned int)(type - 4) < 2) || (type == 0xe))) {
        os_dc_invalidate_range(cmdBlock[6], cmdBlock[5]);
    }
    os_disable_interrupts();
    cmdBlock[3] = 2;
    result = FUN_803a5498(2, (unsigned int *)cmdBlock);
    if ((DAT_80613180 == (unsigned char *)0) && (DAT_8061318c == 0)) {
        dvd_run_next_command();
    }
    os_get_interrupts_enabled();
    return result;
}

// dvd_reset_di (0x803a4d24): reset the DI/DSP interface - pulse the DSP reset
// line, re-arm the AI main control register (0xCC006000 = 0x2a) and secondary
// control register, and clear the abort/pending sub-state latches.
void dvd_reset_di(void)
{
    unsigned int hold;
    dsp_hw_reset_pulse();
    DAT_cc006000 = 0x2a;
    hold = DAT_cc006004;
    DAT_cc006004 = hold;
    DAT_806131b8 = 0;
    DAT_806131a8 = 0;
}

// dvd_cmd_block_state (0x803a4d68): return a command block's public state
// (word at +0xc), mapping the internal "servicing" state 3 to 1, under an
// interrupt lock. (DVDGetCommandBlockStatus core.)
int dvd_cmd_block_state(int cmdBlock)
{
    int state;
    os_disable_interrupts();
    state = *(int *)(cmdBlock + 0xc);
    if (state == 3) {
        state = 1;
    }
    os_get_interrupts_enabled();
    return state;
}

// dvd_get_drive_status (0x803a4db4): return the overall DVD driver status:
// -1 if a reset is queued (DAT_80613198), 8 if a deferred restart is pending
// (DAT_80613190), 0 if idle, else the current command block's mapped state.
int dvd_get_drive_status(void)
{
    unsigned char *cmd;
    int status;
    os_disable_interrupts();
    cmd = DAT_80613180;
    if (DAT_80613198 == 0) {
        if (DAT_80613190 == 0) {
            if (DAT_80613180 == (unsigned char *)0) {
                status = 0;
            } else if (DAT_80613180 == &DAT_805f35a0) {
                status = 0;
            } else {
                os_disable_interrupts();
                status = *(int *)(cmd + 0xc);
                if (status == 3) {
                    status = 1;
                }
                os_get_interrupts_enabled();
            }
        } else {
            status = 8;
        }
    } else {
        status = -1;
    }
    os_get_interrupts_enabled();
    return status;
}

// dvd_set_autoinvalidate (0x803a4e60): set the "auto DC-invalidate read
// destinations" flag and return its previous value. (DVDSetAutoInvalidation.)
int dvd_set_autoinvalidate(int enable)
{
    int prev;
    prev = DAT_80611414;
    DAT_80611414 = enable;
    return prev;
}

// ============================================================================
// batch_1122: DVD command-queue management - the per-priority doubly-linked
// FIFOs, the cancel/wait machinery, the busy/alarm probes, and queue init.
//
// Address: 0x803a4e70 | Name: dvd_end_pause_block    | Date: 2026-09-15
// Address: 0x803a4ec0 | Name: dvd_cancel_block       | Date: 2026-09-15
// Address: 0x803a513c | Name: dvd_cancel_block_wait  | Date: 2026-09-15
// Address: 0x803a51e8 | Name: dvd_cancel_wake        | Date: 2026-09-15
// Address: 0x803a520c | Name: dvd_get_fst_magic      | Date: 2026-09-15
// Address: 0x803a5214 | Name: dvd_is_idle_settled    | Date: 2026-09-15
// Address: 0x803a530c | Name: dvd_cancel_all         | Date: 2026-09-15
// Address: 0x803a5428 | Name: dvd_is_dvd_alarm       | Date: 2026-09-15
// Address: 0x803a5460 | Name: dvd_queue_init         | Date: 2026-09-15
// Address: 0x803a5498 | Name: dvd_queue_insert       | Date: 2026-09-15
//
// The DVD driver holds four priority FIFOs, each a circular doubly-linked list
// whose sentinel head/tail pair lives in the global block at 0x805f35f8 (8
// bytes per queue: [head=next-ptr, tail=prev-ptr]). A command block links in at
// word 0 (next) and word 1 (prev). dvd_queue_insert appends a block to queue
// `kind`; dvd_run_next_command's popper (FUN_803a5500) removes from the head;
// dvd_queue_init resets all four lists to empty (each pointer -> its own
// sentinel). dvd_cancel_block transitions a block out of whatever engine state
// it is in (queued / servicing / mid-reset) and notifies its callback with the
// cancel code 0xfffffffd; dvd_cancel_block_wait wraps it and blocks the caller
// on the wait queue (DAT_80613178) until the block reaches a terminal state;
// dvd_cancel_all cancels every block in the driver. dvd_is_idle_settled reports
// whether the drive is quiescent (idle status + AI control bits clear + no
// pending sub-state). dvd_end_pause_block clears the pause latch and resumes.
//
// NON-MATCHING: the four queue heads are modelled as a flat pointer array
// (dvd_queue_heads == DAT_805f35f8) so the original 2*kind index arithmetic is
// preserved; driver state and the AI control register are per-file externs;
// peer functions and OS helpers are declared with matching signatures.

// ---- command-queue heads + wait queue ----
// Four (head,tail) sentinel pairs, 8 pointer-slots total. The original code
// indexes &DAT_805f35fc (the first "tail") with [kind*2], and &DAT_805f35f8
// (the first "head") as the sentinel address; model both as one flat array.
extern void *DAT_805f35f8;   // queue[0].head sentinel base (array of 8 ptrs)
extern int   DAT_80613178;   // cancel-wait thread queue

// ---- peer functions defined later / in this file ----
extern int   FUN_803a55f8(int *cmdBlock);  // unlink block from its queue (b1123)
int  dvd_cancel_block(int *cmdBlock, void *cancelCb);   // 0x803a4ec0
void dvd_cancel_wake(void);                             // 0x803a51e8
void dvd_queue_init(void);                              // 0x803a5460

// ---- cross-module ARAM / OS helpers ----
extern int  aram_dma_arm_mailbox(void);    // 0x803a2420 (audio/dsp_hw.cpp)
extern int  aram_dma_reset_ack(void);      // 0x803a2434 (audio/dsp_hw.cpp)
extern int  dsp_is_known_alarm(void *alarm);
extern void os_sleep_thread(int *queue);
extern void os_wakeup_thread(int *queue);

// helper: base address of the flat queue-head array
#define DVD_QUEUE ((void **)&DAT_805f35f8)

// dvd_end_pause_block (0x803a4e70): clear the "queue paused" latch (set during
// dvd_cancel_all) and, if a restart was deferred while paused, kick the engine.
void dvd_end_pause_block(void)
{
    os_disable_interrupts();
    DAT_8061318c = 0;
    if (DAT_80613190 != 0) {
        DAT_80613190 = 0;
        dvd_run_next_command();
    }
    os_get_interrupts_enabled();
}

// dvd_cancel_block (0x803a4ec0): cancel a command block, dispatching on its
// engine state (word +0xc, int index [3]). Queued blocks (state 2) are unlinked
// and their callback fired with 0xfffffffd; the currently-servicing block
// (state 1/3) arms a deferred cancel via the eject latch unless it is at a
// non-interruptible sub-state; mid-reset states (4..7,0xb) latch the resume
// sub-state and retire the block. Returns 1 unless a cancel was already pending.
int dvd_cancel_block(int *cmdBlock, void *cancelCb)
{
    int type;
    void (*resetCb)(void);
    os_disable_interrupts();
    switch (cmdBlock[3]) {
    case 1:
        if (DAT_806131a0 != 0) {
            os_get_interrupts_enabled();
            return 0;
        }
        DAT_806131a0 = 1;
        DAT_806131a4 = (void (*)(int, unsigned char *))cancelCb;
        if ((cmdBlock[2] == 4) || (cmdBlock[2] == 1)) {
            DAT_806131a4 = (void (*)(int, unsigned char *))cancelCb;
            aram_dma_arm_mailbox();
        }
        break;
    case 2:
        FUN_803a55f8(cmdBlock);
        cmdBlock[3] = 10;
        if ((void (*)(int, int *))cmdBlock[10] != (void (*)(int, int *))0) {
            ((void (*)(int, int *))cmdBlock[10])(0xfffffffd, cmdBlock);
        }
        if (cancelCb != (void *)0) {
            ((void (*)(int, int *))cancelCb)(0, cmdBlock);
        }
        break;
    case 3:
        type = cmdBlock[2];
        if (type == 0xd ||
            (type < 0xd ? (type < 6 && 3 < type) : (type == 0xf))) {
            if (cancelCb != (void *)0) {
                ((void (*)(int, int *))cancelCb)(0, cmdBlock);
            }
        } else {
            if (DAT_806131a0 != 0) {
                os_get_interrupts_enabled();
                return 0;
            }
            DAT_806131a0 = 1;
            DAT_806131a4 = (void (*)(int, unsigned char *))cancelCb;
        }
        break;
    case 4:
    case 5:
    case 6:
    case 7:
    case 0xb:
        resetCb = (void (*)(void))aram_dma_reset_ack();
        if (resetCb != (void (*)(void))dvd_reset_recover_complete) {
            os_get_interrupts_enabled();
            return 0;
        }
        if (cmdBlock[3] == 4) { DAT_806131a8 = 3; }
        if (cmdBlock[3] == 5) { DAT_806131a8 = 4; }
        if (cmdBlock[3] == 6) { DAT_806131a8 = 1; }
        if (cmdBlock[3] == 0xb) { DAT_806131a8 = 2; }
        if (cmdBlock[3] == 7) { DAT_806131a8 = 7; }
        DAT_80613180 = &DAT_805f35a0;
        cmdBlock[3] = 10;
        if ((void (*)(int, int *))cmdBlock[10] != (void (*)(int, int *))0) {
            ((void (*)(int, int *))cmdBlock[10])(0xfffffffd, cmdBlock);
        }
        if (cancelCb != (void *)0) {
            ((void (*)(int, int *))cancelCb)(0, cmdBlock);
        }
        dvd_run_next_command();
        break;
    case -1:
    case 0:
    case 10:
        if (cancelCb != (void *)0) {
            ((void (*)(int, int *))cancelCb)(0, cmdBlock);
        }
        break;
    }
    os_get_interrupts_enabled();
    return 1;
}

// dvd_cancel_block_wait (0x803a513c): cancel a block and block the calling
// thread on the wait queue until the block reaches a terminal state (state
// -1/0/10, or state 3 at a non-interruptible sub-state). Returns 0 on success,
// -1 if the block could not be cancelled.
int dvd_cancel_block_wait(int *cmdBlock)
{
    int state;
    if (dvd_cancel_block(cmdBlock, (void *)dvd_cancel_wake) == 0) {
        return -1;
    }
    os_disable_interrupts();
    while (((state = cmdBlock[3], 1 < (unsigned int)(state + 1)) && (state != 10)) &&
           ((state != 3) ||
            ((state = cmdBlock[2], 1 < (unsigned int)(state - 4)) &&
             (state != 0xd) && (state != 0xf)))) {
        os_sleep_thread(&DAT_80613178);
    }
    os_get_interrupts_enabled();
    return 0;
}

// dvd_cancel_wake (0x803a51e8): wake any thread blocked in dvd_cancel_block_wait.
void dvd_cancel_wake(void)
{
    os_wakeup_thread(&DAT_80613178);
}

// dvd_get_fst_magic (0x803a520c): return the FST "valid" sentinel constant
// (0x80000000). (Small accessor used by the FST setup path.)
unsigned int dvd_get_fst_magic(void)
{
    return 0x80000000;
}

// dvd_is_idle_settled (0x803a5214): return whether the drive is fully quiescent
// - overall status idle (0 or 8), the AI control register bits (reset/interrupt)
// clear, and no pending sub-state. Blocks with active status return 0.
int dvd_is_idle_settled(void)
{
    unsigned int ctrl;
    int status;
    int settled;
    os_disable_interrupts();
    if (DAT_80613198 == 0) {
        if (DAT_80613190 == 0) {
            if (DAT_80613180 == (unsigned char *)0) {
                status = 0;
            } else if (DAT_80613180 == &DAT_805f35a0) {
                status = 0;
            } else {
                status = *(int *)(DAT_80613180 + 0xc);
            }
        } else {
            status = 8;
        }
    } else {
        status = -1;
    }
    settled = 0;
    switch (status) {
    case 0:
    case 8:
        ctrl = DAT_cc006004;
        if (((ctrl >> 2 & 1) == 0) && ((ctrl & 1) == 0)) {
            settled = (DAT_806131a8 == 0) ? 1 : 0;
        } else {
            settled = 0;
        }
        break;
    case 1:
    case 2:
    case 9:
    case 10:
        settled = 1;
        break;
    case -1:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 0xb:
        settled = 0;
        break;
    }
    os_get_interrupts_enabled();
    return settled;
}

// dvd_cancel_all (0x803a530c): cancel every command block in the driver. Pause
// the queue (DAT_8061318c), drain all queued blocks through dvd_cancel_block,
// cancel the active block, then unpause and resume. The single param is the
// caller's completion callback, invoked once all cancels are done.
void dvd_cancel_all(void *doneCb)
{
    int *block;
    void *savedCb;
    os_disable_interrupts();
    dvd_queue_init();
    savedCb = doneCb;
    if (DAT_806131a0 == 0) {
        if (DAT_80613180 != (unsigned char *)0) {
            ((int *)DAT_80613180)[10] = 0;
        }
        os_disable_interrupts();
        os_disable_interrupts();
        DAT_8061318c = 1;
        if (DAT_80613180 == (unsigned char *)0) {
            DAT_80613190 = 1;
        }
        os_get_interrupts_enabled();
        while ((block = FUN_803a5500()) != (int *)0) {
            dvd_cancel_block(block, (void *)0);
        }
        if (DAT_80613180 == (unsigned char *)0) {
            if (doneCb != (void *)0) {
                ((void (*)(int, int))doneCb)(0, 0);
            }
        } else {
            dvd_cancel_block((int *)DAT_80613180, doneCb);
        }
        os_disable_interrupts();
        DAT_8061318c = 0;
        if (DAT_80613190 != 0) {
            DAT_80613190 = 0;
            dvd_run_next_command();
        }
        os_get_interrupts_enabled();
        os_get_interrupts_enabled();
        savedCb = DAT_806131a4;
    }
    DAT_806131a4 = (void (*)(int, unsigned char *))savedCb;
    os_get_interrupts_enabled();
}

// dvd_is_dvd_alarm (0x803a5428): return whether an alarm object belongs to the
// DVD subsystem - the DVD retry/settle alarm (0x805f35d0) or one of the DSP
// alarms recognised by dsp_is_known_alarm.
int dvd_is_dvd_alarm(void *alarm)
{
    int result;
    if (alarm == (void *)&DAT_805f35d0) {
        result = 1;
    } else {
        result = dsp_is_known_alarm(alarm);
    }
    return result;
}

// dvd_queue_init (0x803a5460): reset all four command FIFOs to empty by
// pointing each queue's head and tail sentinel at itself.
void dvd_queue_init(void)
{
    void **q = DVD_QUEUE;
    // 4 queues * 2 sentinel slots each; head[i]=tail[i]=&head[i]
    q[0] = &q[0]; q[1] = &q[0];   // queue 0
    q[2] = &q[2]; q[3] = &q[2];   // queue 1
    q[4] = &q[4]; q[5] = &q[4];   // queue 2
    q[6] = &q[6]; q[7] = &q[6];   // queue 3
}

// dvd_queue_insert (0x803a5498): append a command block to priority FIFO `kind`
// (0..3). The tail sentinel lives at index kind*2+1; the block's next (word 0)
// points at the head sentinel (index kind*2), its prev (word 1) at the old
// tail. Always returns 1.
int dvd_queue_insert(int kind, unsigned int *cmdBlock)
{
    void **q = DVD_QUEUE;
    os_disable_interrupts();
    *(unsigned int **)(q[kind * 2 + 1]) = cmdBlock;
    cmdBlock[1] = (unsigned int)q[kind * 2 + 1];
    cmdBlock[0] = (unsigned int)&q[kind * 2];
    q[kind * 2 + 1] = cmdBlock;
    os_get_interrupts_enabled();
    return 1;
}

// ============================================================================
// batch_1123: DVD queue pop/scan/unlink + SRAM error-code logging + disc-id
// comparison + the fatal "please turn off the power" error screen hook + the
// disc-boot (BS2 handoff) read state machine.
//
// Address: 0x803a5500 | Name: dvd_queue_pop          | Date: 2026-09-15
// Address: 0x803a55a0 | Name: dvd_queue_any          | Date: 2026-09-15
// Address: 0x803a55f8 | Name: dvd_queue_unlink       | Date: 2026-09-15
// Address: 0x803a5658 | Name: dvd_error_code_index   | Date: 2026-09-15
// Address: 0x803a5774 | Name: dvd_log_error_to_sram  | Date: 2026-09-15
// Address: 0x803a57f0 | Name: dvd_disc_id_match       | Date: 2026-09-15
// Address: 0x803a58e8 | Name: dvd_show_error_screen   | Date: 2026-09-15
// Address: 0x803a5974 | Name: dvd_set_error_hook      | Date: 2026-09-15
// Address: 0x803a59e4 | Name: dvd_run_error_hook      | Date: 2026-09-15
// Address: 0x803a5a14 | Name: dvd_bootinfo_read_step   | Date: 2026-09-15
//
// dvd_queue_pop/any/unlink complete the FIFO layer from batch_1122: pop scans
// the four priority lists in order and removes the head of the first non-empty
// one; any reports whether any queue is non-empty; unlink splices a block out
// by its stored prev/next. dvd_error_code_index maps a DI error code to a
// compact table index (from the tables at 0x8049a138), and dvd_log_error_to_sram
// folds the DI cause byte into a status byte written to SRAM offset 0x12 (so the
// last drive error survives a reset). dvd_disc_id_match compares two 8-byte disc
// IDs (game code / maker code / disc number / version) treating 0/0xff fields as
// wildcards. dvd_show_error_screen builds the arguments for and invokes the OS
// fatal-error screen (os_error_screen_init) with the localised "an error has
// occurred, turn off the power" message; dvd_set_error_hook installs/clears the
// pointer to that screen (DAT_806131d0) and dvd_run_error_hook fires it.
// dvd_bootinfo_read_step is the callback state machine that reads the disc id
// then the apploader/boot-info blocks during a disc (re)boot, re-issuing the
// read on a cover-open (-4) result.
//
// NON-MATCHING: the queue heads reuse the flat array modelled in batch_1122;
// SRAM/font/sound/error-screen helpers resolve through the shared headers
// (generated_decls.h); DI error tables and driver state are per-file externs.

// ---- error-code table + error-screen state ----
extern unsigned int DAT_8049a138;   // base of the DI error-code lookup tables
extern unsigned char *DAT_8061142c; // default localised error message
extern unsigned char *DAT_80611428; // alt (encoding==1) error message
extern void (*DAT_806131d0)();       // installed fatal-error-screen hook
extern int  DAT_8061c018;           // error-screen bg colour word
extern int  DAT_8061c01c;           // error-screen fg colour word
extern unsigned char *PTR_s_An_error_has_occurred__Turn_the_p_803e1e90[]; // per-sound-mode msgs

// ---- disc-boot read state machine state ----
extern int  DAT_806131d8;   // bootinfo read step (0=id,1=id-read,2=data)
extern int  DAT_806131dc;   // bootinfo command-block / header pointer
extern int  DAT_806131e0;   // bootinfo id-read destination

// ---- cross-module SRAM / font / error-screen / string helpers ----
extern unsigned short *os_sram_lock_ex(void);
extern void            os_sram_commit_from(int recompute);
extern short           os_font_detect_encoding(void);
extern unsigned char   os_get_sound_mode(void);
extern int             msl_strncmp(int a, int b, int n);
extern void os_error_screen_init(undefined8 p1, undefined8 p2, undefined8 p3,
                                 undefined8 p4, undefined8 p5, undefined8 p6,
                                 undefined8 p7, undefined8 p8,
                                 undefined4 *bgColor, undefined4 *fgColor,
                                 undefined4 msg, undefined4 p12, undefined4 p13,
                                 undefined4 p14, undefined4 p15, undefined4 p16);

// ---- peer helpers ----
extern int  FUN_803a7b08(void);      // detect TV format (NTSC/PAL/...)
void dvd_show_error_screen(void);    // 0x803a58e8 (installed hook target)
void dvd_bootinfo_read_step(int result, unsigned int *cmdBlock); // 0x803a5a14
char dvd_error_code_index(unsigned int code);                    // 0x803a5658

// dvd_queue_pop (0x803a5500): scan the four priority FIFOs in order and remove
// the head block of the first non-empty queue, splicing it out of the ring and
// clearing its link words. Returns the block, or NULL if all queues are empty.
int *dvd_queue_pop(void)
{
    void **base = DVD_QUEUE;
    int i;
    int idx;
    int *head;
    void **slot;
    os_disable_interrupts();
    idx = 0;
    for (i = 4; i != 0; i--) {
        if (base[idx * 2] != (void *)&base[idx * 2]) {
            os_get_interrupts_enabled();
            os_disable_interrupts();
            slot = &base[idx * 2];
            head = (int *)*slot;
            *slot = (void *)head[0];
            *(void **)(head[0] + 4) = slot;
            os_get_interrupts_enabled();
            head[0] = 0;
            head[1] = 0;
            return head;
        }
        idx = idx + 1;
    }
    os_get_interrupts_enabled();
    return (int *)0;
}

// dvd_queue_any (0x803a55a0): return 1 if any of the four priority FIFOs holds a
// command block, else 0.
int dvd_queue_any(void)
{
    void **base = DVD_QUEUE;
    int i;
    int idx;
    os_disable_interrupts();
    idx = 0;
    for (i = 4; i != 0; i--) {
        if (base[idx * 2] != (void *)&base[idx * 2]) {
            os_get_interrupts_enabled();
            return 1;
        }
        idx = idx + 1;
    }
    os_get_interrupts_enabled();
    return 0;
}

// dvd_queue_unlink (0x803a55f8): splice a command block out of its FIFO using
// its stored prev (word 1) and next (word 0). Returns 1 on success, 0 if the
// block is not linked.
int dvd_queue_unlink(int *cmdBlock)
{
    int *prev;
    int next;
    os_disable_interrupts();
    prev = (int *)cmdBlock[1];
    next = cmdBlock[0];
    if ((prev == (int *)0) || (next == 0)) {
        os_get_interrupts_enabled();
        return 0;
    }
    *prev = next;
    *(int **)(next + 4) = prev;
    os_get_interrupts_enabled();
    return 1;
}

// dvd_error_code_index (0x803a5658): map a 24-bit DI error/status code to a
// compact index by scanning the lookup tables at 0x8049a138 (two rows of nine
// entries). Codes in the 0x100000..0x100008 window map to 0x11; unknown codes
// map to 0x1d.
char dvd_error_code_index(unsigned int code)
{
    unsigned int *tbl;
    char base;
    int rows;
    rows = 2;
    tbl = &DAT_8049a138;
    base = 0;
    while (true) {
        if (code == tbl[0]) { return base; }
        if (code == tbl[1]) { return (char)(base + 1); }
        if (code == tbl[2]) { return (char)(base + 2); }
        if (code == tbl[3]) { return (char)(base + 3); }
        if (code == tbl[4]) { return (char)(base + 4); }
        if (code == tbl[5]) { return (char)(base + 5); }
        if (code == tbl[6]) { return (char)(base + 6); }
        if (code == tbl[7]) { break; }
        if (code == tbl[8]) { return (char)(base + 8); }
        tbl = tbl + 9;
        base = (char)(base + 9);
        rows = rows - 1;
        if (rows == 0) {
            if ((0xfffff < code) && (code < 0x100009)) {
                return 0x11;
            }
            return 0x1d;
        }
    }
    return (char)(base + 7);
}

// dvd_log_error_to_sram (0x803a5774): record the last drive error into SRAM byte
// 0x12 so it survives a reset. The two sentinel causes map to -1/-2; otherwise
// the low 24 bits are indexed (dvd_error_code_index) and the high byte (capped
// at 6) is folded in as a 0x1e-wide "category" multiplier.
void dvd_log_error_to_sram(unsigned int cause)
{
    char status;
    char category;
    unsigned short *sram;
    if (cause == 0x1234567) {
        status = -1;
    } else if (cause == 0x1234568) {
        status = -2;
    } else {
        category = (char)(cause >> 0x18);
        status = dvd_error_code_index(cause & 0xffffff);
        if (5 < (cause >> 0x18)) {
            category = 6;
        }
        status = (char)(status + category * 0x1e);
    }
    sram = os_sram_lock_ex();
    *(char *)(sram + 0x12) = status;
    os_sram_commit_from(1);
}

// dvd_disc_id_match (0x803a57f0): compare two 8-byte disc IDs. Bytes 0..3 are
// the 4-char game code, 4..5 the 2-char maker code, byte 6 the disc number, byte
// 7 the version. A zero game/maker field or a 0xff disc/version field acts as a
// wildcard. Returns 1 on match, 0 otherwise.
int dvd_disc_id_match(char *idA, char *idB)
{
    if ((*idA != '\0') && (*idB != '\0') &&
        (msl_strncmp((int)idA, (int)idB, 4) != 0)) {
        return 0;
    }
    if ((idA[4] == '\0') || (idB[4] == '\0') ||
        (msl_strncmp((int)(idA + 4), (int)(idB + 4), 2) != 0)) {
        return 0;
    }
    if ((idA[6] == -1) || (idB[6] == -1) || (idA[6] == idB[6])) {
        if ((idA[7] == -1) || (idB[7] == -1) || (idA[7] == idB[7])) {
            return 1;
        }
        return 0;
    }
    return 0;
}

// dvd_show_error_screen (0x803a58e8): build the arguments for and invoke the OS
// fatal-error screen. If FUN_803a7b08 (TV-format detect) reports 0, pick the
// localised message by font encoding; otherwise pick the "turn off the power"
// message for the current sound mode. Passes the bg/fg colour words and the
// message to os_error_screen_init.
void dvd_show_error_screen(void)
{
    int tvFormat;
    short encoding;
    unsigned char soundMode;
    unsigned char *msg;
    int bg = DAT_8061c018;
    int fg = DAT_8061c01c;
    int bgLocal;
    int fgLocal;
    tvFormat = FUN_803a7b08();
    if (tvFormat == 0) {
        encoding = os_font_detect_encoding();
        msg = DAT_8061142c;
        if (encoding == 1) {
            msg = DAT_80611428;
        }
    } else {
        soundMode = os_get_sound_mode();
        msg = PTR_s_An_error_has_occurred__Turn_the_p_803e1e90[soundMode];
    }
    fgLocal = fg;
    bgLocal = bg;
    os_error_screen_init(0, 0, 0, 0, 0, 0, 0, 0,
                         (undefined4 *)&fgLocal, (undefined4 *)&bgLocal,
                         (undefined4)(uintptr_t)msg, bg, 0, 0, 0, 0);
}

// dvd_set_error_hook (0x803a5974): install (enable != 0) or clear the fatal
// error-screen hook (DAT_806131d0 -> dvd_show_error_screen). Returns whether a
// hook was previously installed.
int dvd_set_error_hook(int enable)
{
    bool wasSet;
    os_disable_interrupts();
    wasSet = DAT_806131d0 != (void (*)())0;
    if (enable == 0) {
        DAT_806131d0 = (void (*)())0;
    } else {
        DAT_806131d0 = (void (*)())dvd_show_error_screen;
    }
    os_get_interrupts_enabled();
    return wasSet ? 1 : 0;
}

// dvd_run_error_hook (0x803a59e4): invoke the installed fatal error-screen hook
// if one is set.
void dvd_run_error_hook(void)
{
    if (DAT_806131d0 != (void (*)())0) {
        DAT_806131d0();
    }
}

// dvd_bootinfo_read_step (0x803a5a14): callback state machine that reads the
// disc id block then the apploader/boot-info during a disc (re)boot. Step 0
// issues a 0x20-byte read of the disc id; step 1 reads the boot-info header
// (rounded to a 0x20 boundary); a -4 (cover open) result resets the DI and
// re-issues the disc-id read from step 0.
void dvd_bootinfo_read_step(int result, unsigned int *cmdBlock)
{
    if (result < 1) {
        if ((result != -1) && (result == -4)) {
            DAT_806131d8 = 0;
            dvd_reset_di();
            dvd_submit_read_diskid((int *)cmdBlock, DAT_806131e0,
                                   (void *)dvd_bootinfo_read_step);
        }
    } else if (DAT_806131d8 == 1) {
        DAT_806131d8 = 2;
        dvd_submit_read((int *)cmdBlock, *(int *)(DAT_806131dc + 0x10),
                        (*(int *)(DAT_806131dc + 8) + 0x1f) & 0xffffffe0,
                        *(int *)(DAT_806131dc + 4),
                        (void *)dvd_bootinfo_read_step);
    } else if ((DAT_806131d8 < 1) && (-1 < DAT_806131d8)) {
        DAT_806131d8 = 1;
        dvd_submit_read((int *)cmdBlock, DAT_806131dc, 0x20, 0x420,
                        (void *)dvd_bootinfo_read_step);
    }
}
