// Address: 0x803b1ca0 | Name: si_device | Date: 2026-09-15
//
// batch_1141: the SI (Serial Interface) device-protocol layer. This sits on top
// of the low-level SI channel transfer engine (FUN_803b8c90 / FUN_803b9xxx,
// which drive the SI registers at 0xCC006800, per-channel stride 0x14) and
// implements the command/response protocol for an SI peripheral (command bytes
// 0x81 set-mode, 0x83 read-status, 0x85 read-id, 0x89 clear-status, plus the
// 0xF1/0xF3/0xF4 transfer/"R"ead-block ops). Each of the (up to 3) SI channels
// has a device state block in the array based at DAT_8060e700, stride 0x110:
//   +0x000 (word)  active flag
//   +0x004 (word)  last result / error code
//   +0x094 (byte)  current command byte
//   +0x0a0 (word)  block length          +0x0a4 (word) callback-arg / -1 sentinel
//   +0x0a8 (word)  retry counter
//   +0x0b4 (word)  transfer buffer ptr
//   +0x0c4/+0x0c8/+0x0cc/+0x0dc (fn ptrs) command completion callbacks
//   +0x0e0 (alarm) OS alarm object for the command timeout
//   +0x110 stride to the next channel's block
// A parallel array at DAT_8060e780 (stride 0x44 words == 0x110 bytes) holds the
// per-channel data pointers. Command routines build a small request buffer,
// push it through the transfer engine, and validate every step with Ghidra's
// `countLeadingZeros(rc) >> 5 == 0` (rc != 0 success) idiom.
//
// NON-MATCHING: the flat DAT_8060e7xx byte-offset pointer arithmetic and the
// clz-based success tests are preserved verbatim from the decompiler. Per-file
// externs are declared for every raw FUN_/DAT_ symbol at the call-site arity.

#include "common.h"

// ---- SI device state arrays (base 0x8060e700) -----------------------------
extern int    DAT_8060e700;   // [ch*0x44] active flag   (word view, stride 0x110 bytes)
extern int    DAT_8060e704;   // [ch*0x44] last result code
extern byte   DAT_8060e70a;   // [+0x0a] response length short (low byte)
extern int    DAT_8060e70c;   // [+0x0c] timing word
extern short  DAT_8060e710;   // [+0x10] period short
extern int    DAT_8060e714;   // [+0x14] read-block length
extern int    DAT_8060e724;   // [+0x24] phase/step counter
extern int    DAT_8060e780;   // [ch*0x44] per-channel data pointer array
extern byte   DAT_8060e794;   // [+0x94] current command byte
extern int    DAT_8060e7a0;   // [+0xa0] block length
extern int    DAT_8060e7a4;   // [+0xa4] callback arg / -1 sentinel
extern int    DAT_8060e7a8;   // [+0xa8] retry counter
extern uint   DAT_8060e7b4;   // [+0xb4] transfer buffer ptr
extern int    DAT_8060e7c4;   // [+0xc4] fn ptr (secondary complete)
extern int    DAT_8060e7c8;   // [+0xc8] fn ptr (xfer complete)
extern int    DAT_8060e7cc;   // [+0xcc] fn ptr (command complete)
extern int    DAT_8060e7dc;   // [+0xdc] fn ptr (reset complete)
extern int    DAT_8060e7e0;   // [+0xe0] OS alarm object
extern int    DAT_8060e810;   // second channel state block base
extern int    DAT_800000f8;   // OS bus clock (Hz)

// ---- low-level SI transfer / command helpers (defined elsewhere) ----------
extern int  FUN_803b8c90(int ch, byte* buf, int len, int dir, undefined4 cb);   // queue SI xfer
extern int  FUN_803b8eec(int ch, byte* buf, int len, int flag);                 // read block
extern int  FUN_803b8f8c(int ch, uint buf, uint len, int arg, void* cb);        // start read w/ cb
extern int  FUN_803b9078(int ch);                                               // poll status
extern int  FUN_803b930c(int ch, int a);                                        // channel reset
extern int  FUN_803b94fc(int ch);                                               // check id ready
extern int  FUN_803b97f8(int ch, int a, int b);                                 // begin command
extern int  FUN_803b9924(int ch);                                               // finish/poll
extern int  FUN_803b9fb8(int ch, int a, int b);                                 // probe present
extern void FUN_803ba0ac(int ch);                                               // abort/cleanup

// ---- OS alarm helpers ------------------------------------------------------
extern void os_cancel_alarm(int* alarm);
extern void os_set_alarm(undefined4* alarm, undefined4 t_hi, undefined4 t_lo, int period, void* cb);
extern int  os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);

// forward decls (defined later in this file)
undefined4 sidev_cmd_read_status(int ch, byte* out);
undefined4 sidev_cmd_clear_status(int ch);
undefined4 sidev_issue_command(int ch);
void       sidev_xfer_complete(int ch);
void       sidev_alarm_timeout(undefined* alarm);

// sidev_abort (0x803b1ca0): tear down an in-flight command on a channel. Clears
// the active flag, resets the channel, cancels the timeout alarm, and fires the
// command-complete and secondary-complete callbacks with the aborted code
// (0xfffffffd), latching the last-result field to the same unless already -1.
void sidev_abort(int ch)
{
    void (*cb)(int, uint);
    int base;

    base = ch * 0x110;
    if ((&DAT_8060e700)[ch * 0x44] != 0) {
        (&DAT_8060e700)[ch * 0x44] = 0;
        FUN_803b930c(ch, 0);
        os_cancel_alarm((int*)(&DAT_8060e7e0 + base));
        cb = *(void(**)(int, uint))(&DAT_8060e7cc + base);
        if (cb != (void(*)(int, uint))0x0) {
            *(undefined4*)(&DAT_8060e7cc + base) = 0;
            (*cb)(ch, 0xfffffffd);
        }
        if ((&DAT_8060e704)[ch * 0x44] != -1) {
            (&DAT_8060e704)[ch * 0x44] = 0xfffffffd;
        }
        cb = *(void(**)(int, uint))(&DAT_8060e7c4 + base);
        if ((cb != (void(*)(int, uint))0x0) && (6 < *(int*)(&DAT_8060e724 + base))) {
            *(undefined4*)(&DAT_8060e7c4 + base) = 0;
            (*cb)(ch, 0xfffffffd);
        }
    }
}

// sidev_read_status_complete (0x803b1d78): the command-timeout / status-poll
// continuation. Cancels the alarm, then (if the channel is still active) probes
// the device, reads its status byte and clears status; on a busy/again bit it
// retries via sidev_issue_command until the retry counter runs out, otherwise
// aborts the transfer and fires the command-complete callback with the result.
void sidev_read_status_complete(int ch)
{
    void (*cb)(int, int);
    int rc;
    int retry;
    int base;
    byte status[12];

    base = ch * 0x110;
    os_cancel_alarm((int*)(&DAT_8060e7e0 + base));
    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        return;
    }
    rc = FUN_803b9fb8(ch, 0, 0);
    if (rc == 0) {
        rc = -0x80;
    } else {
        rc = sidev_cmd_read_status(ch, status);
        if ((-1 < rc) && (rc = sidev_cmd_clear_status(ch), -1 < rc)) {
            if ((status[0] & 0x18) == 0) {
                rc = 0;
            } else {
                rc = -5;
            }
            if ((rc == -5) &&
               (retry = *(int*)(&DAT_8060e7a8 + base), *(int*)(&DAT_8060e7a8 + base) = retry + -1,
                0 < retry + -1)) {
                rc = sidev_issue_command(ch);
                if (-1 < rc) {
                    return;
                }
                goto LAB_fire_cb;
            }
        }
        FUN_803ba0ac(ch);
    }
LAB_fire_cb:
    cb = *(void(**)(int, int))(&DAT_8060e7cc + base);
    if (cb != (void(*)(int, int))0x0) {
        *(undefined4*)(&DAT_8060e7cc + base) = 0;
        (*cb)(ch, rc);
    }
}

// sidev_xfer_complete (0x803b1e90): data-transfer completion callback. Polls
// the finish status, cleans up, and fires the xfer-complete callback with 0 on
// success (finish OK and id-ready), else the aborted code 0xfffffffd.
void sidev_xfer_complete(int ch)
{
    bool ok;
    uint clz;
    void (*cb)(int, uint);
    undefined4 rc;
    int idReady;

    rc = FUN_803b9924(ch);
    clz = countLeadingZeros(rc);
    FUN_803ba0ac(ch);
    cb = *(void(**)(int, uint))(&DAT_8060e7c8 + ch * 0x110);
    if (cb != (void(*)(int, uint))0x0) {
        ok = false;
        *(undefined4*)(&DAT_8060e7c8 + ch * 0x110) = 0;
        if ((clz >> 5 == 0) && (idReady = FUN_803b94fc(ch), idReady != 0)) {
            ok = true;
        }
        if (ok) {
            rc = 0;
        } else {
            rc = 0xfffffffd;
        }
        (*cb)(ch, rc);
    }
}

// sidev_reset_complete (0x803b1f38): reset-completion callback. Fires the
// reset-complete callback with 1 when the id becomes ready, else 0xfffffffd.
void sidev_reset_complete(int ch)
{
    void (*cb)(int, uint);
    int idReady;
    undefined4 rc;

    cb = *(void(**)(int, uint))(&DAT_8060e7dc + ch * 0x110);
    if (cb != (void(*)(int, uint))0x0) {
        *(undefined4*)(&DAT_8060e7dc + ch * 0x110) = 0;
        idReady = FUN_803b94fc(ch);
        if (idReady == 0) {
            rc = 0xfffffffd;
        } else {
            rc = 1;
        }
        (*cb)(ch, rc);
    }
}

// sidev_cmd_set_mode (0x803b1fbc): issue the 0x81 set-mode command (0x81000000
// or 0x81010000 depending on the mode arg), then poll status and finish. All
// three steps must succeed (clz idiom) or returns the aborted code.
undefined4 sidev_cmd_set_mode(int ch, int mode)
{
    uint c1;
    uint c2;
    uint c3;
    int begin;
    undefined4 rc;
    undefined4 buf[2];

    begin = FUN_803b97f8(ch, 0, 4);
    if (begin == 0) {
        rc = 0xfffffffd;
    } else {
        if (mode == 0) {
            buf[0] = 0x81000000;
        } else {
            buf[0] = 0x81010000;
        }
        rc = FUN_803b8c90(ch, (byte*)buf, 2, 1, 0);
        c1 = countLeadingZeros(rc);
        rc = FUN_803b9078(ch);
        c2 = countLeadingZeros(rc);
        rc = FUN_803b9924(ch);
        c3 = countLeadingZeros(rc);
        if ((c1 >> 5 == 0 && c2 >> 5 == 0) && c3 >> 5 == 0) {
            rc = 0;
        } else {
            rc = 0xfffffffd;
        }
    }
    return rc;
}

// sidev_cmd_read_status (0x803b207c): issue the 0x83 read-status command,
// read one status byte into out[0], poll and finish. Success only when all five
// steps succeed.
undefined4 sidev_cmd_read_status(int ch, byte* out)
{
    uint c1;
    uint c2;
    uint c3;
    uint c4;
    uint c5;
    int begin;
    undefined4 rc;
    byte buf[12];

    begin = FUN_803b97f8(ch, 0, 4);
    if (begin == 0) {
        rc = 0xfffffffd;
    } else {
        buf[0] = 0x83;
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        rc = FUN_803b8c90(ch, buf, 2, 1, 0);
        c1 = countLeadingZeros(rc);
        rc = FUN_803b9078(ch);
        c2 = countLeadingZeros(rc);
        rc = FUN_803b8c90(ch, out, 1, 0, 0);
        c3 = countLeadingZeros(rc);
        rc = FUN_803b9078(ch);
        c4 = countLeadingZeros(rc);
        rc = FUN_803b9924(ch);
        c5 = countLeadingZeros(rc);
        if ((((c1 >> 5 == 0 && c2 >> 5 == 0) && c3 >> 5 == 0) && c4 >> 5 == 0) && c5 >> 5 == 0) {
            rc = 0;
        } else {
            rc = 0xfffffffd;
        }
    }
    return rc;
}

// sidev_cmd_read_id (0x803b216c): issue the 0x85 read-id command, read two id
// bytes into out, poll and finish. Success only when all five steps succeed.
undefined4 sidev_cmd_read_id(int ch, byte* out)
{
    uint c1;
    uint c2;
    uint c3;
    uint c4;
    uint c5;
    int begin;
    undefined4 rc;
    byte buf[12];

    begin = FUN_803b97f8(ch, 0, 4);
    if (begin == 0) {
        rc = 0xfffffffd;
    } else {
        buf[0] = 0x85;
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        rc = FUN_803b8c90(ch, buf, 2, 1, 0);
        c1 = countLeadingZeros(rc);
        rc = FUN_803b9078(ch);
        c2 = countLeadingZeros(rc);
        rc = FUN_803b8c90(ch, out, 2, 0, 0);
        c3 = countLeadingZeros(rc);
        rc = FUN_803b9078(ch);
        c4 = countLeadingZeros(rc);
        rc = FUN_803b9924(ch);
        c5 = countLeadingZeros(rc);
        if ((((c1 >> 5 == 0 && c2 >> 5 == 0) && c3 >> 5 == 0) && c4 >> 5 == 0) && c5 >> 5 == 0) {
            rc = 0;
        } else {
            rc = 0xfffffffd;
        }
    }
    return rc;
}

// sidev_cmd_clear_status (0x803b225c): issue the 0x89 clear-status command,
// poll and finish. Success only when all three steps succeed.
undefined4 sidev_cmd_clear_status(int ch)
{
    uint c1;
    uint c2;
    uint c3;
    int begin;
    undefined4 rc;
    byte buf[4];

    begin = FUN_803b97f8(ch, 0, 4);
    if (begin == 0) {
        rc = 0xfffffffd;
    } else {
        buf[0] = 0x89;
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        rc = FUN_803b8c90(ch, buf, 1, 1, 0);
        c1 = countLeadingZeros(rc);
        rc = FUN_803b9078(ch);
        c2 = countLeadingZeros(rc);
        rc = FUN_803b9924(ch);
        c3 = countLeadingZeros(rc);
        if ((c1 >> 5 == 0 && c2 >> 5 == 0) && c3 >> 5 == 0) {
            rc = 0;
        } else {
            rc = 0xfffffffd;
        }
    }
    return rc;
}

// sidev_alarm_timeout (0x803b2308): OS alarm callback (shared by all channels).
// Resolves the channel from the alarm object address, and if that channel is
// still active resets it and fires the command-complete callback with the
// timeout code 0xfffffffb.
void sidev_alarm_timeout(undefined* alarm)
{
    void (*cb)(int, uint);
    int* state;
    int ch;

    ch = 0;
    state = &DAT_8060e700;
    if (alarm != (undefined*)&DAT_8060e7e0) {
        ch = 1;
        state = &DAT_8060e810;
        if (alarm != (undefined*)0x8060e8f0) {
            ch = 2;
            state = &DAT_8060e810;
        }
    }
    if (*state != 0) {
        FUN_803b930c(ch, 0);
        cb = (void(*)(int, uint))state[0x33];
        if (cb != (void(*)(int, uint))0x0) {
            state[0x33] = 0;
            (*cb)(ch, 0xfffffffb);
        }
    }
}

// sidev_issue_command (0x803b23ac): drive one command/transfer for a channel.
// Begins the command, cancels any pending alarm, then arms a per-command
// timeout alarm sized from the device timing words (DAT_8060e70c/e710) and the
// OS bus clock DAT_800000f8, using the command byte DAT_8060e794 to select the
// timeout profile (0xF1/0xF3 immediate, 0xF4 short, others clocked). It then
// sends the command header block, and for a 'R'ead-block command streams the
// 0x200-byte data block, finally arming the transfer-complete callback
// (sidev_xfer_complete). Returns 0 on success, 0xfffffffd on any failure.
undefined4 sidev_issue_command(int ch)
{
    byte cmd;
    int rc;
    undefined4 result;
    int base;
    uint clocks;
    uint scaled;

    base = ch * 0x110;
    rc = FUN_803b97f8(ch, 0, 4);
    if (rc == 0) {
        FUN_803ba0ac(ch);
        return 0xfffffffd;
    }
    os_cancel_alarm((int*)(&DAT_8060e7e0 + base));
    cmd = (&DAT_8060e794)[base];
    if (cmd == 0xf3) goto LAB_send;
    if (cmd < 0xf3) {
        if (cmd != 0xf1) {
            if (0xf0 < cmd) {
                os_set_alarm((undefined4*)(&DAT_8060e7e0 + base), 0x10624dd3, 0,
                             (DAT_800000f8 / 4000) * 100, sidev_alarm_timeout);
            }
            goto LAB_send;
        }
    } else {
        if (0xf4 < cmd) goto LAB_send;
        if (0x80 < *(ushort*)(&DAT_8060e70a + base)) {
            scaled = (DAT_800000f8 >> 2) * 2;
            result = (undefined4)
                     ((ulonglong)(uint)((int)(uint)*(ushort*)(&DAT_8060e710 + base) >> 6) *
                      (ulonglong)scaled >> 0x20);
            os_set_alarm((undefined4*)(&DAT_8060e7e0 + base), result, result,
                         ((int)(uint)*(ushort*)(&DAT_8060e710 + base) >> 6) * scaled,
                         sidev_alarm_timeout);
            goto LAB_send;
        }
    }
    clocks = *(uint*)(&DAT_8060e70c + base);
    scaled = (DAT_800000f8 >> 2) * 2;
    clocks = ((int)clocks >> 0xd) + (uint)((int)clocks < 0 && (clocks & 0x1fff) != 0);
    rc = ((int)clocks >> 0x1f) * scaled + (int)((ulonglong)clocks * (ulonglong)scaled >> 0x20);
    os_set_alarm((undefined4*)(&DAT_8060e7e0 + base), rc, rc, clocks * scaled, sidev_alarm_timeout);
LAB_send:
    rc = FUN_803b8eec(ch, &DAT_8060e794 + base, *(int*)(&DAT_8060e7a0 + base), 1);
    if (rc == 0) {
        FUN_803b9924(ch);
        FUN_803ba0ac(ch);
        result = 0xfffffffd;
    } else if (((&DAT_8060e794)[base] == 'R') &&
              (rc = FUN_803b8eec(ch, (byte*)((&DAT_8060e780)[ch * 0x44] + 0x200),
                                 *(int*)(&DAT_8060e714 + base), 1), rc == 0)) {
        FUN_803b9924(ch);
        FUN_803ba0ac(ch);
        result = 0xfffffffd;
    } else if (*(int*)(&DAT_8060e7a4 + base) == -1) {
        FUN_803b9924(ch);
        FUN_803ba0ac(ch);
        result = 0;
    } else {
        if ((&DAT_8060e794)[base] == 'R') {
            clocks = 0x200;
        } else {
            clocks = (uint)*(ushort*)(&DAT_8060e70a + base);
        }
        rc = FUN_803b8f8c(ch, *(uint*)(&DAT_8060e7b4 + base), clocks,
                          *(int*)(&DAT_8060e7a4 + base), (void*)sidev_xfer_complete);
        if (rc == 0) {
            FUN_803b9924(ch);
            FUN_803ba0ac(ch);
            result = 0xfffffffd;
        } else {
            result = 0;
        }
    }
    return result;
}

// ===========================================================================
// batch_1142: the SI device driver's public command API and one-time init,
// continuing src/input/si_device.cpp. These build the command header bytes
// (command + 24-bit block address split across DAT_8060e795..+0x98) for the
// read (0x52 'R'), write (0xF2) and erase (0xF1) operations, register the
// per-command completion callbacks, arm the timeout, and kick the transfer via
// the low-level engine. sidev_init brings up the whole SI/DSP/alarm stack once.
//
// NON-MATCHING: the flat DAT_8060e7xx byte-offset arithmetic and the 24-bit
// address bit-slicing are preserved verbatim. Per-file externs added below.
// ===========================================================================

extern uint   DAT_8060e7b0;   // [+0xb0] block address (24-bit, sliced into cmd bytes)
extern byte   DAT_8060e795;   // [+0x95] cmd addr byte 2
extern byte   DAT_8060e796;   // [+0x96] cmd addr byte 1
extern byte   DAT_8060e797;   // [+0x97] cmd addr byte 0 (2 bits)
extern byte   DAT_8060e798;   // [+0x98] cmd addr byte
extern int    DAT_8060e7d0;   // [+0xd0] per-channel result-latch clear slot
extern int    DAT_8060e80c;   // work-area ptr A (also 2nd-channel state at ch*0x44)
extern int    DAT_8060e91c;   // work-area ptr B
extern int    DAT_8060e920;   // default work-area buffer
extern short  DAT_80613410;   // detected OS font encoding
extern int    DAT_806114a0;   // OS version-register arg
extern int    DAT_80000000;   // low-mem / boot info base

// ---- OS init / registration helpers ---------------------------------------
extern undefined8 os_register_version(undefined8, undefined8, undefined8, undefined8, undefined8,
                                      undefined8, undefined8, undefined8, int, undefined4,
                                      undefined4, undefined4, undefined4, undefined4, undefined4,
                                      undefined4);
extern void  dsp_sched_init(undefined8, undefined8, undefined8, undefined8, undefined8, undefined8,
                            undefined8, undefined8);
extern short os_font_detect_encoding(void);
extern void  os_alarm_init(void);
extern void  os_alarm_clear(undefined4* alarm);
extern void  os_init_thread_queue(undefined4* q);
extern void  os_register_reset_function(int fn);

// forward decls (defined later / cross-referenced)
int sidev_begin_command(int ch, int xferCb, int cmdCb);
void sidev_reset_then_issue(int ch, int result);
void sidev_set_workarea(undefined* wa);

// sidev_reset_then_issue (0x803b264c): reset-completion continuation that, on a
// successful id-ready reset, issues the pending command; on failure it routes
// the error to the appropriate completion callback based on the command byte
// (0x52 'R' -> xfer callback +0xc8; the 0xF1..0xF4 ops -> command callback +0xcc).
void sidev_reset_then_issue(int ch, int result)
{
    byte cmd;
    int rc;
    int base;
    void (*cb)(int);

    base = ch * 0x110;
    if (-1 < result) {
        *(void(**)(int, int))(&DAT_8060e7dc + base) = sidev_reset_then_issue;
        rc = FUN_803b9fb8(ch, 0, -0x7fc4e0c8);
        if (rc == 0) {
            result = 0;
        } else {
            *(undefined4*)(&DAT_8060e7dc + base) = 0;
            result = sidev_issue_command(ch);
        }
    }
    if (result < 0) {
        cmd = (&DAT_8060e794)[base];
        if (cmd != 0xf3) {
            if (cmd < 0xf3) {
                if (cmd == 0x52) {
                    cb = *(void(**)(int))(&DAT_8060e7c8 + base);
                    if (cb == (void(*)(int))0x0) {
                        return;
                    }
                    *(undefined4*)(&DAT_8060e7c8 + base) = 0;
                    (*cb)(ch);
                    return;
                }
                if (cmd < 0x52) {
                    return;
                }
                if (cmd < 0xf1) {
                    return;
                }
            } else if (0xf4 < cmd) {
                return;
            }
            cb = *(void(**)(int))(&DAT_8060e7cc + base);
            if (cb != (void(*)(int))0x0) {
                *(undefined4*)(&DAT_8060e7cc + base) = 0;
                (*cb)(ch);
            }
        }
    }
}

// sidev_begin_command (0x803b275c): common command-start path. Under the
// interrupt lock, registers the optional xfer (+0xc8) and command (+0xcc)
// completion callbacks, ensures the device is present (re-arming the reset
// continuation if not), begins the command, cancels any pending alarm, and arms
// a fresh per-command timeout alarm sized from the timing words and the OS bus
// clock (command byte selects the profile). Returns 0, or -1 when the device
// needs an async reset first, or 0xfffffffd/0xffffffff on failure.
int sidev_begin_command(int ch, int xferCb, int cmdCb)
{
    byte cmd;
    int rc;
    uint clocks;
    int base;
    uint scaled;
    undefined4 result;

    os_disable_interrupts();
    base = ch * 0x110;
    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        result = 0xfffffffd;
        goto LAB_done;
    }
    if (xferCb != 0) {
        *(int*)(&DAT_8060e7c8 + base) = xferCb;
    }
    if (cmdCb != 0) {
        *(int*)(&DAT_8060e7cc + base) = cmdCb;
    }
    *(void(**)(int, int))(&DAT_8060e7dc + base) = sidev_reset_then_issue;
    rc = FUN_803b9fb8(ch, 0, -0x7fc4e0c8);
    if (rc == 0) {
        result = 0xffffffff;
        goto LAB_done;
    }
    *(undefined4*)(&DAT_8060e7dc + base) = 0;
    rc = FUN_803b97f8(ch, 0, 4);
    if (rc == 0) {
        FUN_803ba0ac(ch);
        result = 0xfffffffd;
        goto LAB_done;
    }
    os_cancel_alarm((int*)(&DAT_8060e7e0 + base));
    cmd = (&DAT_8060e794)[base];
    if (cmd != 0xf3) {
        if (cmd < 0xf3) {
            if (cmd == 0xf1) {
LAB_clocked:
                clocks = *(uint*)(&DAT_8060e70c + base);
                scaled = (DAT_800000f8 >> 2) * 2;
                clocks = ((int)clocks >> 0xd) + (uint)((int)clocks < 0 && (clocks & 0x1fff) != 0);
                rc = ((int)clocks >> 0x1f) * scaled + (int)((ulonglong)clocks * (ulonglong)scaled >> 0x20);
                os_set_alarm((undefined4*)(&DAT_8060e7e0 + base), rc, rc, clocks * scaled,
                             sidev_alarm_timeout);
            } else if (0xf0 < cmd) {
                os_set_alarm((undefined4*)(&DAT_8060e7e0 + base), 0x10624dd3, 0,
                             (DAT_800000f8 / 4000) * 100, sidev_alarm_timeout);
            }
        } else if (cmd < 0xf5) {
            if (*(ushort*)(&DAT_8060e70a + base) < 0x81) goto LAB_clocked;
            scaled = (DAT_800000f8 >> 2) * 2;
            result = (undefined4)
                     ((ulonglong)(uint)((int)(uint)*(ushort*)(&DAT_8060e710 + base) >> 6) *
                      (ulonglong)scaled >> 0x20);
            os_set_alarm((undefined4*)(&DAT_8060e7e0 + base), result, result,
                         ((int)(uint)*(ushort*)(&DAT_8060e710 + base) >> 6) * scaled,
                         sidev_alarm_timeout);
        }
    }
    result = 0;
LAB_done:
    os_get_interrupts_enabled();
    return result;
}

// sidev_read_block (0x803b2980): issue a 0x52 'R' read-block command. Builds the
// 5-byte command header (0x52 + 24-bit address from DAT_8060e7b0), begins the
// command, then streams the command header, the 0x200-byte payload, and arms the
// xfer-complete callback. Returns 0 on success, -3 on transfer setup failure.
int sidev_read_block(int ch, int xferCb)
{
    int rc;
    int base;

    base = ch * 0x110;
    (&DAT_8060e794)[base] = 0x52;
    (&DAT_8060e795)[base] = (byte)(*(uint*)(&DAT_8060e7b0 + base) >> 0x11) & 0x7f;
    (&DAT_8060e796)[base] = (char)(*(uint*)(&DAT_8060e7b0 + base) >> 9);
    (&DAT_8060e797)[base] = (byte)(*(uint*)(&DAT_8060e7b0 + base) >> 7) & 3;
    (&DAT_8060e798)[base] = (byte)*(undefined4*)(&DAT_8060e7b0 + base) & 0x7f;
    *(undefined4*)(&DAT_8060e7a0 + base) = 5;
    *(undefined4*)(&DAT_8060e7a4 + base) = 0;
    *(undefined4*)(&DAT_8060e7a8 + base) = 0;
    rc = sidev_begin_command(ch, xferCb, 0);
    if (rc == -1) {
        rc = 0;
    } else if (-1 < rc) {
        rc = FUN_803b8eec(ch, &DAT_8060e794 + base, *(int*)(&DAT_8060e7a0 + base), 1);
        if (((rc == 0) ||
            (rc = FUN_803b8eec(ch, (byte*)((&DAT_8060e780)[ch * 0x44] + 0x200),
                               *(int*)(&DAT_8060e714 + base), 1), rc == 0)) ||
           (rc = FUN_803b8f8c(ch, *(uint*)(&DAT_8060e7b4 + base), 0x200,
                              *(int*)(&DAT_8060e7a4 + base), (void*)sidev_xfer_complete), rc == 0)) {
            *(undefined4*)(&DAT_8060e7c8 + base) = 0;
            FUN_803b9924(ch);
            FUN_803ba0ac(ch);
            rc = -3;
        } else {
            rc = 0;
        }
    }
    return rc;
}

// sidev_write_block (0x803b2ab4): issue a 0xF2 write-block command. Builds the
// header (0xF2 + 24-bit address, high address bit set when the response length
// DAT_8060e70a exceeds 0x80), begins the command, streams the header, and arms
// the xfer-complete callback. Returns 0 / -3.
int sidev_write_block(int ch, int cmdCb)
{
    int rc;
    int base;

    base = ch * 0x110;
    (&DAT_8060e794)[base] = 0xf2;
    if (*(ushort*)(&DAT_8060e70a + base) < 0x81) {
        (&DAT_8060e795)[base] = (byte)(*(uint*)(&DAT_8060e7b0 + base) >> 0x11) & 0x7f;
    } else {
        (&DAT_8060e795)[base] = (byte)(*(uint*)(&DAT_8060e7b0 + base) >> 0x11) & 0x7f | 0x80;
    }
    (&DAT_8060e796)[base] = (char)(*(uint*)(&DAT_8060e7b0 + base) >> 9);
    (&DAT_8060e797)[base] = (byte)(*(uint*)(&DAT_8060e7b0 + base) >> 7) & 3;
    (&DAT_8060e798)[base] = (byte)*(undefined4*)(&DAT_8060e7b0 + base) & 0x7f;
    *(undefined4*)(&DAT_8060e7a0 + base) = 5;
    *(undefined4*)(&DAT_8060e7a4 + base) = 1;
    *(undefined4*)(&DAT_8060e7a8 + base) = 3;
    rc = sidev_begin_command(ch, 0, cmdCb);
    if (rc == -1) {
        rc = 0;
    } else if (-1 < rc) {
        rc = FUN_803b8eec(ch, &DAT_8060e794 + base, *(int*)(&DAT_8060e7a0 + base), 1);
        if ((rc == 0) ||
           (rc = FUN_803b8f8c(ch, *(uint*)(&DAT_8060e7b4 + base),
                              (uint)*(ushort*)(&DAT_8060e70a + base),
                              *(int*)(&DAT_8060e7a4 + base), (void*)sidev_xfer_complete), rc == 0)) {
            *(undefined4*)(&DAT_8060e7cc + base) = 0;
            FUN_803b9924(ch);
            FUN_803ba0ac(ch);
            rc = -3;
        } else {
            rc = 0;
        }
    }
    return rc;
}

// sidev_erase_sector (0x803b2bf0): issue a 0xF1 erase command for a 24-bit block
// address. Small devices (response length < 0x81) build the 3-byte header and
// run the command; larger devices just invoke the callback with success. Returns
// 0 / -3.
int sidev_erase_sector(int ch, uint blockAddr, undefined* cmdCb)
{
    int rc;
    int base;

    base = ch * 0x110;
    if (*(ushort*)(&DAT_8060e70a + base) < 0x81) {
        (&DAT_8060e794)[base] = 0xf1;
        (&DAT_8060e795)[base] = (byte)(blockAddr >> 0x11) & 0x7f;
        (&DAT_8060e796)[base] = (char)(blockAddr >> 9);
        *(undefined4*)(&DAT_8060e7a0 + base) = 3;
        *(undefined4*)(&DAT_8060e7a4 + base) = 0xffffffff;
        *(undefined4*)(&DAT_8060e7a8 + base) = 3;
        rc = sidev_begin_command(ch, 0, (int)cmdCb);
        if (rc == -1) {
            rc = 0;
        } else if (-1 < rc) {
            rc = FUN_803b8eec(ch, &DAT_8060e794 + base, *(int*)(&DAT_8060e7a0 + base), 1);
            if (rc == 0) {
                *(undefined4*)(&DAT_8060e7cc + base) = 0;
                rc = -3;
            } else {
                rc = 0;
            }
            FUN_803b9924(ch);
            FUN_803ba0ac(ch);
        }
    } else {
        if (cmdCb != (undefined*)0x0) {
            (*(void(*)(int, int))cmdCb)(ch, 0);
        }
        rc = 0;
    }
    return rc;
}

// sidev_init (0x803b2d00): one-time SI device-driver bring-up (guarded by the
// work-area pointers). Detects the OS font encoding, registers the OS version,
// initializes the DSP scheduler and OS alarm system, resets both channel state
// blocks (result -3, thread queue + alarm cleared), points the driver at the
// default work area, and registers the reset handler.
void sidev_init(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4,
                undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8,
                undefined4 param_9, undefined4 param_10, undefined4 param_11, undefined4 param_12,
                undefined4 param_13, undefined4 param_14, undefined4 param_15, undefined4 param_16)
{
    int i;
    undefined4* state;
    undefined8 ver;

    state = (undefined4*)&DAT_8060e700;
    if ((DAT_8060e80c == 0) || (DAT_8060e91c == 0)) {
        DAT_80613410 = os_font_detect_encoding();
        ver = os_register_version(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                  param_8, DAT_806114a0, param_10, param_11, param_12, param_13,
                                  param_14, param_15, param_16);
        dsp_sched_init(ver, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        os_alarm_init();
        i = 0;
        do {
            state[1] = 0xfffffffd;
            os_init_thread_queue(state + 0x23);
            os_alarm_clear(state + 0x38);
            i = i + 1;
            state = state + 0x44;
        } while (i < 2);
        sidev_set_workarea((undefined*)&DAT_80000000);
        os_register_reset_function(-0x7fb60d18);
    }
}

// sidev_get_font_encoding (0x803b2dac): return the detected OS font encoding
// (DAT_80613410).
undefined2 sidev_get_font_encoding(void)
{
    return DAT_80613410;
}

// sidev_set_workarea (0x803b2db4): set the driver work-area pointers
// (DAT_8060e80c / DAT_8060e91c), defaulting a NULL arg to the built-in buffer
// DAT_8060e920.
void sidev_set_workarea(undefined* wa)
{
    DAT_8060e80c = (int)wa;
    if (wa == (undefined*)0x0) {
        DAT_8060e80c = (int)&DAT_8060e920;
    }
    if (wa == (undefined*)0x0) {
        wa = (undefined*)&DAT_8060e920;
    }
    DAT_8060e91c = (int)wa;
}

// sidev_mount (0x803b2dec): attach/claim a channel (0..1). Fails if the channel
// index is out of range or has no work-area; otherwise, under the interrupt
// lock, marks the channel result busy (-1), clears the reset-latch, and returns
// the channel's state block pointer through out. Returns 0 / -0x80 / -1 / -3.
undefined4 sidev_mount(int ch, undefined4* out)
{
    undefined4 rc;

    if (((ch < 0) || (1 < ch)) || ((&DAT_8060e80c)[ch * 0x44] == 0)) {
        rc = 0xffffff80;
    } else {
        os_disable_interrupts();
        if ((&DAT_8060e700)[ch * 0x44] == 0) {
            rc = 0xfffffffd;
        } else if ((&DAT_8060e704)[ch * 0x44] == -1) {
            rc = 0xffffffff;
        } else {
            (&DAT_8060e704)[ch * 0x44] = 0xffffffff;
            rc = 0;
            *(undefined4*)(&DAT_8060e7d0 + ch * 0x110) = 0;
            *out = (undefined4)(&DAT_8060e700 + ch * 0x44);
        }
        os_get_interrupts_enabled();
    }
    return rc;
}

// sidev_set_result_if_idle (0x803b2ea4): latch a result code into a channel
// state block's result field (block[1]); when the block is active (block[0]!=0)
// it always writes, otherwise only if the field was the -1 sentinel.
int sidev_set_result_if_idle(int* block, int result)
{
    os_disable_interrupts();
    if (*block == 0) {
        if (block[1] == -1) {
            block[1] = result;
        }
    } else {
        block[1] = result;
    }
    os_get_interrupts_enabled();
    return result;
}

// ===========================================================================
// batch_1143 (0x803b2f08 .. 0x803b4058): SI-device query/capacity API, the
// blocking result wait, the ID-unlock descramble handshake and its DSP-mailbox
// reset callbacks. This continues the SI device driver: sidev_query_capacity
// reads the device geometry to report total/free bytes, sidev_probe_id runs the
// scrambled-ID unlock handshake (an LFSR/Galois bit-scramble seeded from an LCG
// on the OS time base), and the two DSP callbacks (sidev_dsp_reset_cb /
// sidev_dsp_probe_cb) drive the mailbox unlock sequence.
//
// NON-MATCHING: the LFSR/bit-permutation loops and the flat DAT_8060e7xx
// byte-offset arithmetic are preserved verbatim from the decompiler; per-file
// externs added at call-site arity for the DSP-mailbox / OS-cache / OS-thread
// helpers and the extra channel-state DATs. C4311/C4312 ptr-width warnings only.
// ---------------------------------------------------------------------------

// ---- additional channel-state DATs (base 0x8060e700, stride 0x110) --------
extern int    DAT_8060e72c;   // [+0x2c] scrambled ID / rolling LFSR state
extern int    DAT_8060e734;   // [+0x34] read-config: sector count / 0xff
extern int    DAT_8060e73c;   // [+0x3c] read-config word (0x49f300)
extern int    DAT_8060e740;   // [+0x40] read-config word (0x160)
extern int    DAT_8060e744;   // [+0x44] read-config word (0)
extern short  DAT_8060e754;   // [+0x54] read-config short (0x10)
extern int    DAT_8060e758;   // [+0x58] callback fn ptr (reset cb)
extern int    DAT_8060e75c;   // [+0x5c] callback arg
extern int    DAT_8060e760;   // [+0x60] callback fn ptr (probe cb)
extern int    DAT_8060e764;   // [+0x64] callback arg
extern int    DAT_8060e78c;   // [+0x8c] per-channel OS thread queue
extern int    DAT_80613412;   // font-encoding-detected flag
extern int    DAT_806114a8;   // global LCG state (rand seed)

// ---- helpers introduced this batch ----------------------------------------
int         sidev_geom_field_88(int block);  // read geometry field (block+0x88), defined in batch_1144
extern int  FUN_803b4a80(int block);     // read geometry field (block+0x84)
extern int  FUN_803b66cc(int ch);        // detach a channel
void sidev_mount_continue(int ch, int rc); // mount continuation callback, defined in batch_1146
void        sidev_dsp_probe_cb(int arg); // DSP probe/unlock continuation, defined in batch_1144

// ---- DSP mailbox + OS cache/thread/time helpers ---------------------------
extern void   dsp_mailbox_write(uint value);
extern ushort dsp_mailbox_read_status(void);
extern void   dsp_task_reset(undefined4* task);
extern void   os_sleep_thread(int* queue);
extern int    os_get_time_base(void);
extern void   os_dc_flush_range(uint addr, int len);
extern void   os_dc_invalidate_range(uint addr, int len);
extern void   memset_wrapper(int dst, int val, int len);   // decompiler memset intrinsic

// forward decl (defined later in this batch)
void sidev_dsp_reset_cb(int arg);

// sidev_get_result (0x803b2f08): return a channel's last-result word
// (block[1] == (&DAT_8060e704)[ch*0x44]); -0x80 for a bad channel index.
undefined4 sidev_get_result(int ch)
{
    if ((-1 < ch) && (ch < 2)) {
        return (&DAT_8060e704)[ch * 0x44];
    }
    return 0xffffff80;
}

// sidev_query_capacity (0x803b2f38): mount the channel, read the device
// geometry blocks (FUN_803b467c/4a80), and report total size (out_total =
// sectors * bytes-per-sector) and free block count (out_free = count of 0xff
// directory entries). Latches a -6 (not-ready) result if geometry is missing.
int sidev_query_capacity(int ch, int* out_total, int* out_free)
{
    int rc;
    int geomA;
    int geomB;
    ushort i;
    int* block[3];

    rc = sidev_mount(ch, (undefined4*)block);
    if (-1 < rc) {
        geomA = sidev_geom_field_88((int)block[0]);
        geomB = FUN_803b4a80((int)block[0]);
        if ((geomA == 0) || (geomB == 0)) {
            os_disable_interrupts();
            if (*block[0] == 0) {
                if (block[0][1] == -1) {
                    block[0][1] = -6;
                }
            } else {
                block[0][1] = -6;
            }
            os_get_interrupts_enabled();
            rc = -6;
        } else {
            if (out_total != (int*)0x0) {
                *out_total = block[0][3] * (uint)*(ushort*)(geomA + 6);
            }
            if (out_free != (int*)0x0) {
                *out_free = 0;
                for (i = 0; i < 0x7f; i = i + 1) {
                    if (*(char*)(geomB + 8) == -1) {
                        *out_free = *out_free + 1;
                    }
                    geomB = geomB + 0x40;
                }
            }
            os_disable_interrupts();
            if (*block[0] == 0) {
                if (block[0][1] == -1) {
                    block[0][1] = 0;
                }
            } else {
                block[0][1] = 0;
            }
            os_get_interrupts_enabled();
            rc = 0;
        }
    }
    return rc;
}

// sidev_wait_result (0x803b3088): block the calling thread until a channel's
// result word is no longer the -1 "busy" sentinel, sleeping on the channel's OS
// thread queue between polls. Returns the final result (or -0x80 on bad index).
int sidev_wait_result(int ch)
{
    int rc;

    os_disable_interrupts();
    while (true) {
        if ((ch < 0) || (1 < ch)) {
            rc = -0x80;
        } else {
            rc = (&DAT_8060e704)[ch * 0x44];
        }
        if (rc != -1) {
            break;
        }
        os_sleep_thread((int*)(&DAT_8060e78c + ch * 0x110));
    }
    os_get_interrupts_enabled();
    return rc;
}

// sidev_detach_all (0x803b3120): on channel 0, detach both channels 0 and 1
// (FUN_803b66cc); returns 0 only if both detaches failed, else 1.
undefined4 sidev_detach_all(int ch)
{
    int rc;
    undefined4 ok;

    if ((ch == 0) &&
        ((rc = FUN_803b66cc(0), rc == -1 || (rc = FUN_803b66cc(1), rc == -1)))) {
        ok = 0;
    } else {
        ok = 1;
    }
    return ok;
}

// sidev_is_encoding_set (0x803b3170): return 1 if the OS font encoding has been
// detected (DAT_80613412 != 0), else 0.
undefined4 sidev_is_encoding_set(void)
{
    if (DAT_80613412 != 0) {
        return 1;
    }
    return 0;
}

// sidev_bit_reverse (0x803b318c): the ID-scramble bit permutation. Maps each of
// the 32 input bits to a fixed output position: the low 16 bits fan out toward
// the high end (spacing that grows by one per bit), the high bits (except bit
// 0x1f, which stays) shift down by an odd stride. Fully unrolled 4-at-a-time.
uint sidev_bit_reverse(uint v)
{
    uint sign;
    uint out;
    uint bit;
    uint nb;
    int loShift;
    int hiShift;
    int iter;

    iter = 8;
    sign = v >> 0x1f;
    loShift = 0;
    hiShift = 1;
    out = 0;
    bit = 0;
    do {
        if (bit < 0x10) {
            out = out | (v & 1 << bit) << (0x1f - bit) - loShift;
            loShift = loShift + 1;
        } else if (bit == 0x1f) {
            out = out | sign;
        } else {
            out = out | (v & 1 << bit) >> hiShift;
            hiShift = hiShift + 2;
        }
        nb = bit + 1;
        if (nb < 0x10) {
            out = out | (v & 1 << nb) << (0x1f - nb) - loShift;
            loShift = loShift + 1;
        } else if (nb == 0x1f) {
            out = out | sign;
        } else {
            out = out | (v & 1 << nb) >> hiShift;
            hiShift = hiShift + 2;
        }
        nb = bit + 2;
        if (nb < 0x10) {
            out = out | (v & 1 << nb) << (0x1f - nb) - loShift;
            loShift = loShift + 1;
        } else if (nb == 0x1f) {
            out = out | sign;
        } else {
            out = out | (v & 1 << nb) >> hiShift;
            hiShift = hiShift + 2;
        }
        nb = bit + 3;
        if (nb < 0x10) {
            out = out | (v & 1 << nb) << (0x1f - nb) - loShift;
            loShift = loShift + 1;
        } else if (nb == 0x1f) {
            out = out | sign;
        } else {
            out = out | (v & 1 << nb) >> hiShift;
            hiShift = hiShift + 2;
        }
        bit = bit + 4;
        iter = iter + -1;
    } while (iter != 0);
    return out;
}

// sidev_read_block_scrambled (0x803b32f8): issue the 0x52 "R" read-block
// command. Builds the 5-byte command header (op 0x52 + a 24-bit block address
// sliced across the header bytes, layout depending on large_device), streams
// the fixed 0x200-byte scratch header, then the caller's payload, and validates
// each transfer with the clz success idiom. Returns 0 / 0xfffffffd.
undefined4 sidev_read_block_scrambled(int ch, uint block_addr, byte* payload,
                                      int payload_len, int large_device)
{
    uint c1;
    uint c2;
    uint c3;
    uint c4;
    int begun;
    undefined4 rc;
    byte hdr[2];
    undefined1 hdr2;
    byte hdr3;
    byte hdr4;

    begun = FUN_803b97f8(ch, 0, 4);
    if (begun == 0) {
        rc = 0xfffffffd;
    } else {
        memset_wrapper((int)&hdr, 0, 5);
        hdr[0] = 0x52;
        hdr[1] = (byte)(block_addr >> 0x18);
        if (large_device == 0) {
            hdr[1] = hdr[1] >> 5 & 3;
            hdr2 = (undefined1)(block_addr >> 0x15);
            hdr3 = (byte)(block_addr >> 0x13) & 3;
            hdr4 = (byte)(block_addr >> 0xc) & 0x7f;
        } else {
            hdr2 = (undefined1)(block_addr >> 0x10);
        }
        (void)hdr2; (void)hdr3; (void)hdr4;
        rc = FUN_803b8eec(ch, hdr, 5, 1);
        c1 = countLeadingZeros(rc);
        rc = FUN_803b8eec(ch, (byte*)((&DAT_8060e780)[ch * 0x44] + 0x200),
                          *(int*)(&DAT_8060e714 + ch * 0x110), 1);
        c2 = countLeadingZeros(rc);
        rc = FUN_803b8eec(ch, payload, payload_len, 0);
        c3 = countLeadingZeros(rc);
        rc = FUN_803b9924(ch);
        c4 = countLeadingZeros(rc);
        if (((c1 >> 5 == 0 && c2 >> 5 == 0) && c3 >> 5 == 0) && c4 >> 5 == 0) {
            rc = 0;
        } else {
            rc = 0xfffffffd;
        }
    }
    return rc;
}

// sidev_random_delay_count (0x803b343c): derive a small (4..~35) pseudo-random
// count from an LCG over the OS time base (state DAT_806114a8), used to vary the
// scramble padding length in the ID handshake. Retries up to 10 times to obtain
// a value >= 4, folding fresh time-base samples into the shift each iteration.
uint sidev_random_delay_count(void)
{
    int tb;
    uint n;
    uint tries;
    uint shift;

    shift = 1;
    tb = os_get_time_base();
    for (tries = 0;
         (DAT_806114a8 = tb * 0x41c64e6d + 0x3039, n = (DAT_806114a8 >> 0x10 & 0x1f) + 1,
          n < 4 && (tries < 10)); tries = tries + 1) {
        tb = os_get_time_base();
        tb = tb << shift;
        shift = shift + 1;
        if (0x10 < shift) {
            shift = 1;
        }
    }
    if (n < 4) {
        n = 4;
    }
    return n;
}

// sidev_probe_id (0x803b3500): the device ID-unlock / descramble handshake.
// Seeds a scramble word from the LCG, runs the "R" read-block command with a
// random padding count, then advances a Galois-LFSR (poly taps 0x17/0xf/0/7)
// forward over the response to descramble it in 32-bit groups, XORing each
// recovered word with the returned data. Publishes the recovered rolling state
// into the channel block (+0x2c), fills the DSP read-config words and the reset/
// probe callbacks, primes the DSP task, and returns the three descrambled ID
// words via out[0..2]. Returns 0 on success, 0xfffffffd on a transfer failure.
undefined4 sidev_probe_id(int ch, uint* out)
{
    uint* dataptr;
    uint* aligned;
    int tb;
    uint pad;
    undefined4 rc;
    uint word;
    uint total;
    int base;
    uint lfsr;
    uint acc;
    uint tmpA;
    uint tmpB;
    uint idA;
    uint idB;
    uint idC;
    uint idD;
    uint idE;

    base = ch * 0x110;
    dataptr = (uint*)(&DAT_8060e780)[ch * 0x44];
    aligned = (uint*)((int)dataptr + 0x2fU & 0xffffffe0);
    tb = os_get_time_base();
    DAT_806114a8 = tb * 0x41c64e6d + 0x3039;
    lfsr = DAT_806114a8 >> 0x10 & 0x7000 | 0x7fec8000;
    pad = sidev_random_delay_count();
    tb = sidev_read_block_scrambled(ch, lfsr, (byte*)&idA, pad, 0);
    if (tb < 0) {
        rc = 0xfffffffd;
    } else {
        word = pad * 8 + 1;
        acc = 0;
        if (word != 0) {
            if ((8 < word) && (tmpA = pad & 0x1fffffff, pad * 8 != 7)) {
                do {
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    acc = acc + 8;
                    tmpA = tmpA - 1;
                } while (tmpA != 0);
            }
            tb = word - acc;
            if (acc < word) {
                do {
                    lfsr = lfsr >> 1 | (~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) & 1) << 0x1e;
                    tb = tb + -1;
                } while (tb != 0);
            }
        }
        *(uint*)(&DAT_8060e72c + base) =
            lfsr | ~(lfsr >> 0x17 ^ lfsr >> 0xf ^ lfsr ^ lfsr >> 7) << 0x1f;
        lfsr = sidev_bit_reverse(*(uint*)(&DAT_8060e72c + base));
        *(uint*)(&DAT_8060e72c + base) = lfsr;
        pad = sidev_random_delay_count();
        tb = sidev_read_block_scrambled(ch, 0, (byte*)&idA, pad + 0x14, 1);
        if (tb < 0) {
            rc = 0xfffffffd;
        } else {
            word = *(uint*)(&DAT_8060e72c + base);
            tb = 4;
            acc = idA ^ word;
            do {
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                tb = tb + -1;
            } while (tb != 0);
            *(uint*)(&DAT_8060e72c + base) =
                word | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1f;
            tb = 4;
            word = *(uint*)(&DAT_8060e72c + base);
            total = idB ^ word;
            do {
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                tb = tb + -1;
            } while (tb != 0);
            *(uint*)(&DAT_8060e72c + base) =
                word | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1f;
            tb = 4;
            word = *(uint*)(&DAT_8060e72c + base);
            tmpA = idC ^ word;
            do {
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                tb = tb + -1;
            } while (tb != 0);
            *(uint*)(&DAT_8060e72c + base) =
                word | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1f;
            tb = 4;
            word = *(uint*)(&DAT_8060e72c + base);
            idD = idD ^ word;
            do {
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                tb = tb + -1;
            } while (tb != 0);
            *(uint*)(&DAT_8060e72c + base) =
                word | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1f;
            lfsr = lfsr * 8;
            idE = *(uint*)(&DAT_8060e72c + base);
            word = 0;
            idE = idE ^ word; // preserve decompiler local shuffle (idE recomputed below)
            idE = *(uint*)(&DAT_8060e72c + base);
            word = 0;
            {
                uint carry = idE;
                if (lfsr != 0) {
                    if ((8 < lfsr) && (tmpB = lfsr - 1 >> 3, lfsr != 8)) {
                        do {
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            word = word + 8;
                            tmpB = tmpB - 1;
                        } while (tmpB != 0);
                    }
                    tb = lfsr - word;
                    if (word < lfsr) {
                        do {
                            carry = carry << 1 | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1e & 2;
                            tb = tb + -1;
                        } while (tb != 0);
                    }
                }
                *(uint*)(&DAT_8060e72c + base) =
                    carry | ~(carry << 0x17 ^ carry << 0xf ^ carry ^ carry << 7) >> 0x1f;
            }
            tb = 4;
            lfsr = 0;
            word = *(uint*)(&DAT_8060e72c + base);
            do {
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                lfsr = lfsr + 8;
                tb = tb + -1;
            } while (tb != 0);
            tb = 0x21 - lfsr;
            if (lfsr < 0x21) {
                do {
                    word = word << 1 | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1e & 2;
                    tb = tb + -1;
                } while (tb != 0);
            }
            *(uint*)(&DAT_8060e72c + base) =
                word | ~(word << 0x17 ^ word << 0xf ^ word ^ word << 7) >> 0x1f;
            *aligned = idD;
            aligned[1] = idE;
            *dataptr = (uint)aligned;
            dataptr[1] = 8;
            dataptr[3] = (uint)(aligned + 8);
            dataptr[2] = 0;
            os_dc_flush_range((uint)aligned, 8);
            os_dc_invalidate_range((uint)(aligned + 8), 4);
            os_dc_flush_range((uint)dataptr, 0x10);
            *(undefined4*)(&DAT_8060e734 + base) = 0xff;
            *(undefined4*)(&DAT_8060e73c + base) = 0x49f300;
            *(undefined4*)(&DAT_8060e740 + base) = 0x160;
            *(undefined4*)(&DAT_8060e744 + base) = 0;
            *(undefined2*)(&DAT_8060e754 + base) = 0x10;
            *(void(**)(int))(&DAT_8060e758 + base) = sidev_dsp_reset_cb;
            *(undefined4*)(&DAT_8060e75c + base) = 0;
            *(void(**)(int))(&DAT_8060e760 + base) = sidev_dsp_probe_cb;
            *(undefined4*)(&DAT_8060e764 + base) = 0;
            dsp_task_reset((undefined4*)(base + -0x7f9f18d0));
            *out = acc;
            rc = 0;
            out[1] = total;
            out[2] = tmpA;
        }
    }
    return rc;
}

// sidev_dsp_reset_cb (0x803b4058): DSP-mailbox reset step for the SI-device
// scrambled read task. Selects the channel state block (0x8060e700 for the arg
// -0x7f9f18d0, else 0x8060e810), writes the reset opcode 0xff000000 to the DSP
// mailbox (spinning until the mailbox drains), then writes the saved config word
// block[0x20]. Registered as the reset callback in sidev_probe_id.
void sidev_dsp_reset_cb(int arg)
{
    ushort status;
    undefined4* block;
    undefined4 cfg;

    block = (undefined4*)&DAT_8060e700;
    if (arg != -0x7f9f18d0) {
        block = (undefined4*)&DAT_8060e810;
    }
    cfg = block[0x20];
    dsp_mailbox_write(0xff000000);
    do {
        status = dsp_mailbox_read_status();
    } while (status != 0);
    dsp_mailbox_write(cfg);
    do {
        status = dsp_mailbox_read_status();
    } while (status != 0);
    return;
}

// ===========================================================================
// batch_1144 (0x803b40c8 .. 0x803b4938): the multi-block transfer engine and
// the FAT/directory allocation layer on top of the SI device. sidev_read /
// sidev_write chunk a caller buffer into 0x200-byte device blocks and drive the
// per-block completion callbacks (sidev_read_block_complete /
// sidev_write_block_complete) that advance the address/length cursors and chain
// the next block until the count runs out. Above that, sidev_fat_alloc /
// sidev_fat_free walk the on-device FAT chain (a table of 16-bit next-links in
// the mounted directory block) and sidev_fat_write_next / sidev_fat_flush_complete
// double-buffer and write the updated FAT/directory sector back. sidev_dsp_probe_cb
// is the DSP-mailbox continuation of the ID-unlock handshake.
//
// NON-MATCHING: the LFSR unlock loop and the flat DAT_8060e7xx byte-offset
// arithmetic are preserved verbatim; per-file externs added for the FAT/CRC/
// cache helpers and the transfer-cursor DATs. C4311/C4312 ptr-width warnings.
// ---------------------------------------------------------------------------

// ---- additional transfer-cursor / FAT DATs (base 0x8060e700, stride 0x110) -
extern int    DAT_8060e7ac;   // [+0xac] remaining block count
extern int    DAT_8060e7b8;   // [+0xb8] secondary buffer cursor
extern short  DAT_8060e7be;   // [+0xbe] FAT alloc head link
extern int    DAT_8060e7d4;   // [+0xd4] transfer-complete user callback
extern int    DAT_8060e7d8;   // [+0xd8] FAT-op user callback
extern int    DAT_8060e788;   // [+0x88] active FAT/directory block ptr (double-buffered)

// ---- FAT / CRC / cache helpers --------------------------------------------
extern void leaf_31e8(uint dst, uint src, int len);          // block copy
extern void os_dc_store_range(uint addr, int len);
// promoted to real definitions in batch_1145:
void sidev_block_checksum(ushort* buf, uint nbytes, short* sum_out, short* inv_out);
void sidev_fat_writeback(int ch, short* fatblk, undefined4 cb);

// forward decls (defined later in this batch)
void sidev_read_block_complete(int ch, int rc);
void sidev_write_block_complete(int ch, int rc);
void sidev_fat_flush_complete(int ch, int rc);
void sidev_fat_write_next(int ch, int rc);

// sidev_dsp_probe_cb (0x803b40c8): DSP-mailbox continuation of the ID-unlock
// handshake. Selects the channel block, advances the rolling scramble word
// (block[0xb]) with two randomized "R" reads whose block address is derived
// from the DSP-config word XORed with the scramble state, then reads the device
// status; a clear "ready" bit (0x40) with a good transfer completes the unlock,
// otherwise it aborts. Registered as the probe callback in sidev_probe_id.
void sidev_dsp_probe_cb(int arg)
{
    uint lfsr;
    uint pad;
    int rc;
    uint acc;
    int total;
    uint word;
    undefined4* block;
    uint cfg;
    int ch;
    byte status[4];
    byte scratch[68];

    ch = 0;
    block = (undefined4*)&DAT_8060e700;
    if (arg != -0x7f9f18d0) {
        ch = 1;
        block = (undefined4*)&DAT_8060e810;
        if (arg != -0x7f9f17c0) {
            ch = 2;
            block = (undefined4*)&DAT_8060e810;
        }
    }
    cfg = *(uint*)((block[0x20] + 0x2f & 0xffffffe0) + 0x20);
    pad = sidev_random_delay_count();
    rc = sidev_read_block_scrambled(ch, (cfg ^ block[0xb]) & 0xffff0000, scratch, pad, 1);
    if (rc < 0) {
        FUN_803ba0ac(ch);
        sidev_mount_continue(ch, -3);
    } else {
        acc = 0;
        lfsr = block[0xb];
        pad = pad + block[5] + 4;
        total = pad * 8;
        word = total + 1;
        if (word != 0) {
            if ((8 < word) && (pad = pad & 0x1fffffff, total != 7)) {
                do {
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    acc = acc + 8;
                    pad = pad - 1;
                } while (pad != 0);
            }
            rc = word - acc;
            if (acc < word) {
                do {
                    lfsr = lfsr << 1 | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1e & 2;
                    rc = rc + -1;
                } while (rc != 0);
            }
        }
        block[0xb] = lfsr | ~(lfsr << 0x17 ^ lfsr << 0xf ^ lfsr ^ lfsr << 7) >> 0x1f;
        pad = sidev_random_delay_count();
        rc = sidev_read_block_scrambled(ch, (cfg << 0x10 ^ block[0xb]) & 0xffff0000, scratch, pad, 1);
        if (rc < 0) {
            FUN_803ba0ac(ch);
            sidev_mount_continue(ch, -3);
        } else {
            rc = sidev_cmd_read_status(ch, status);
            if (FUN_803b94fc(ch) == 0) {
                FUN_803ba0ac(ch);
                sidev_mount_continue(ch, -3);
            } else {
                if ((rc == 0) && ((status[0] & 0x40) == 0)) {
                    FUN_803ba0ac(ch);
                    rc = -5;
                }
                sidev_mount_continue(ch, rc);
            }
        }
    }
    return;
}

// sidev_read_block_complete (0x803b43ec): per-block read-complete callback.
// On success advance the source/dest cursors by 0x200 and, while blocks remain,
// chain the next read; on the final block (or error) latch the result and fire
// the user transfer-complete callback (+0xd4).
void sidev_read_block_complete(int ch, int rc)
{
    void (*cb)(int, int);
    int remain;
    int base;

    base = ch * 0x110;
    if (-1 < rc) {
        *(int*)(&DAT_8060e7b8 + base) = *(int*)(&DAT_8060e7b8 + base) + 0x200;
        *(int*)(&DAT_8060e7b0 + base) = *(int*)(&DAT_8060e7b0 + base) + 0x200;
        *(int*)(&DAT_8060e7b4 + base) = *(int*)(&DAT_8060e7b4 + base) + 0x200;
        remain = *(int*)(&DAT_8060e7ac + base);
        *(int*)(&DAT_8060e7ac + base) = remain + -1;
        if ((0 < remain + -1) && (rc = sidev_read_block(ch, -0x7fc4bc14), -1 < rc)) {
            return;
        }
    }
    if (*(int*)(&DAT_8060e7d0 + base) == 0) {
        sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    }
    cb = *(void(**)(int, int))(&DAT_8060e7d4 + base);
    if (cb != (void(*)(int, int))0x0) {
        *(undefined4*)(&DAT_8060e7d4 + base) = 0;
        (*cb)(ch, rc);
    }
    return;
}

// sidev_read (0x803b44c8): start a multi-block read of param_3 bytes into the
// caller's buffer. Records the user callback (+0xd4), sets the block count
// (bytes >> 9 == /0x200), source address and buffer, and kicks the first block.
int sidev_read(int ch, undefined4 blockAddr, uint nbytes, undefined4 buf, undefined4 userCb)
{
    int rc;

    rc = ch * 0x110;
    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        rc = -3;
    } else {
        *(undefined4*)(&DAT_8060e7d4 + rc) = userCb;
        *(uint*)(&DAT_8060e7ac + rc) = nbytes >> 9;
        *(undefined4*)(&DAT_8060e7b0 + rc) = blockAddr;
        *(undefined4*)(&DAT_8060e7b4 + rc) = buf;
        rc = sidev_read_block(ch, -0x7fc4bc14);
    }
    return rc;
}

// sidev_write_block_complete (0x803b452c): per-block write-complete callback.
// Same shape as the read path but the cursor stride is the device response
// length (+0x0a short), chaining sidev_write_block until the count runs out.
void sidev_write_block_complete(int ch, int rc)
{
    void (*cb)(int, int);
    int remain;
    int base;

    base = ch * 0x110;
    if (-1 < rc) {
        *(uint*)(&DAT_8060e7b8 + base) =
            *(int*)(&DAT_8060e7b8 + base) + (uint)*(ushort*)(&DAT_8060e70a + base);
        *(uint*)(&DAT_8060e7b0 + base) =
            *(int*)(&DAT_8060e7b0 + base) + (uint)*(ushort*)(&DAT_8060e70a + base);
        *(uint*)(&DAT_8060e7b4 + base) =
            *(int*)(&DAT_8060e7b4 + base) + (uint)*(ushort*)(&DAT_8060e70a + base);
        remain = *(int*)(&DAT_8060e7ac + base);
        *(int*)(&DAT_8060e7ac + base) = remain + -1;
        if ((0 < remain + -1) && (rc = sidev_write_block(ch, -0x7fc4bad4), -1 < rc)) {
            return;
        }
    }
    if (*(int*)(&DAT_8060e7d0 + base) == 0) {
        sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    }
    cb = *(void(**)(int, int))(&DAT_8060e7d4 + base);
    if (cb != (void(*)(int, int))0x0) {
        *(undefined4*)(&DAT_8060e7d4 + base) = 0;
        (*cb)(ch, rc);
    }
    return;
}

// sidev_write (0x803b4614): start a multi-block write of param_3 bytes. Block
// count is bytes / device-response-length (+0x0a short); records the user
// callback, source address and buffer, and kicks the first block.
int sidev_write(int ch, undefined4 blockAddr, int nbytes, undefined4 buf, undefined4 userCb)
{
    int rc;

    rc = ch * 0x110;
    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        rc = -3;
    } else {
        *(undefined4*)(&DAT_8060e7d4 + rc) = userCb;
        *(int*)(&DAT_8060e7ac + rc) = nbytes / (int)(uint)*(ushort*)(&DAT_8060e70a + rc);
        *(undefined4*)(&DAT_8060e7b0 + rc) = blockAddr;
        *(undefined4*)(&DAT_8060e7b4 + rc) = buf;
        rc = sidev_write_block(ch, -0x7fc4bad4);
    }
    return rc;
}

// sidev_geom_field_88 (0x803b467c): return the device geometry word at
// block+0x88 (used by sidev_query_capacity for the sectors-per-device value).
int sidev_geom_field_88(int block)
{
    return *(int*)(block + 0x88);
}

// sidev_fat_flush_complete (0x803b4684): completion of the FAT/directory sector
// write-back. Ping-pongs the active directory block between the +0x6000 and
// +0x8000 scratch halves and copies the freshly-written 0x2000 block into the
// now-inactive half, then latches the result and fires the FAT-op callback (+0xd8).
void sidev_fat_flush_complete(int ch, int rc)
{
    void (*cb)(int, int);
    uint halfA;
    uint halfB;
    int base;

    base = ch * 0x110;
    if (-1 < rc) {
        halfA = (&DAT_8060e780)[ch * 0x44] + 0x6000;
        halfB = (&DAT_8060e780)[ch * 0x44] + 0x8000;
        if (*(uint*)(&DAT_8060e788 + base) == halfA) {
            *(uint*)(&DAT_8060e788 + base) = halfB;
            leaf_31e8(halfB, halfA, 0x2000);
        } else {
            *(uint*)(&DAT_8060e788 + base) = halfA;
            leaf_31e8(halfA, halfB, 0x2000);
        }
    }
    if (*(int*)(&DAT_8060e7d0 + base) == 0) {
        sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    }
    cb = *(void(**)(int, int))(&DAT_8060e7d8 + base);
    if (cb != (void(*)(int, int))0x0) {
        *(undefined4*)(&DAT_8060e7d8 + base) = 0;
        (*cb)(ch, rc);
    }
    return;
}

// sidev_fat_write_next (0x803b4758): after erasing the FAT sector, write the
// updated directory block back at the device address computed from the active
// block's offset (timing word +0x0c * (offset >> 0xd)); on failure latch the
// result and fire the FAT-op callback.
void sidev_fat_write_next(int ch, int rc)
{
    void (*cb)(int, int);
    int base;

    base = ch * 0x110;
    if ((rc < 0) ||
        (rc = sidev_write(ch, *(int*)(&DAT_8060e70c + base) *
                              ((uint)(*(int*)(&DAT_8060e788 + base) -
                                      (&DAT_8060e780)[ch * 0x44]) >> 0xd), 0x2000,
                          *(int*)(&DAT_8060e788 + base), (undefined4)sidev_fat_flush_complete),
         rc < 0)) {
        if (*(int*)(&DAT_8060e7d0 + base) == 0) {
            sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
        }
        cb = *(void(**)(int, int))(&DAT_8060e7d8 + base);
        if (cb != (void(*)(int, int))0x0) {
            *(undefined4*)(&DAT_8060e7d8 + base) = 0;
            (*cb)(ch, rc);
        }
    }
    return;
}

// sidev_fat_alloc (0x803b4820): allocate nblocks free entries in the on-device
// FAT chain held in the active directory block. Walks the FAT starting after
// the reserved head (index 5), threading each newly-claimed entry (set to -1)
// onto the chain, updating the free count (block[3]) and the search cursor
// (block[4]); records the chain head at +0xbe and writes the block back via
// FUN_803b49d4. Returns -0xa on FAT exhaustion / -9 on over-request.
undefined4 sidev_fat_alloc(int ch, uint nblocks, undefined4 userCb)
{
    undefined4 rc;
    int base;
    short* fat;
    ushort cursor;
    ushort head;
    ushort prev;
    uint guard;
    ushort chainHead;

    base = ch * 0x110;
    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        rc = 0xfffffffd;
    } else {
        fat = *(short**)(&DAT_8060e788 + base);
        if ((ushort)fat[3] < nblocks) {
            rc = 0xfffffff7;
        } else {
            fat[3] = fat[3] - (short)nblocks;
            cursor = fat[4];
            guard = 0;
            head = 0xffff;
            prev = 0;
            while (nblocks != 0) {
                guard = guard + 1;
                if ((int)(*(ushort*)(&DAT_8060e710 + base) - 5) < (int)(guard & 0xffff)) {
                    return 0xfffffffa;
                }
                cursor = cursor + 1;
                if ((cursor < 5) || ((uint)*(ushort*)(&DAT_8060e710 + base) <= (uint)cursor)) {
                    cursor = 5;
                }
                if (fat[cursor] == 0) {
                    chainHead = cursor;
                    if (head != 0xffff) {
                        fat[prev] = cursor;
                        chainHead = head;
                    }
                    fat[cursor] = -1;
                    nblocks = nblocks - 1;
                    prev = cursor;
                    head = chainHead;
                }
            }
            fat[4] = cursor;
            *(ushort*)(&DAT_8060e7be + base) = head;
            sidev_fat_writeback(ch, fat, userCb);
            rc = 0;
        }
    }
    return rc;
}

// sidev_fat_free (0x803b4938): free a FAT chain starting at link `start`,
// following each entry's next-link, clearing it to 0 and bumping the free count
// (block[3]), until the end-of-chain sentinel 0xffff; then writes the directory
// block back via FUN_803b49d4. Returns -0xa on a corrupt/out-of-range link.
undefined4 sidev_fat_free(int ch, ushort start, undefined4 userCb)
{
    uint link;
    undefined4 rc;
    short* fat;

    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        rc = 0xfffffffd;
    } else {
        fat = *(short**)(&DAT_8060e788 + ch * 0x110);
        while (start != 0xffff) {
            link = (uint)start;
            if ((link < 5) || (*(ushort*)(&DAT_8060e710 + ch * 0x110) <= link)) {
                return 0xfffffffa;
            }
            start = fat[link];
            fat[link] = 0;
            fat[3] = fat[3] + 1;
        }
        sidev_fat_writeback(ch, fat, userCb);
        rc = 0;
    }
    return rc;
}

// ===========================================================================
// batch_1145 (0x803b49d4 .. 0x803b55dc): the SI-device (memory-card) integrity
// / mount-verification layer plus the second (directory) double-buffered
// write-back bank. sidev_block_checksum is the additive 16-bit sum + inverse
// sum used on every 0x2000 block. sidev_fat_writeback / sidev_dir_writeback
// stamp a block with a fresh checksum and erase+rewrite it (each bank has its
// own +0x2000/+0x4000 vs +0x6000/+0x8000 scratch pair). sidev_verify_header
// authenticates the header block with an SRAM-keyed LCG stream, and
// sidev_verify_dir / sidev_verify_fat validate the two directory/FAT bank
// copies and pick the newer valid one; sidev_verify_all is the mount gate.
//
// NON-MATCHING: the flat block-offset arithmetic, the SRAM LCG auth stream
// (__sar2i 64-bit arithmetic-shift intrinsic), and the additive checksum loops
// are preserved verbatim. Per-file externs for the OS SRAM helpers; the prior
// FUN_803b49d4/FUN_803b4ce4 externs are promoted to real definitions.
// ---------------------------------------------------------------------------

extern int  DAT_8060e784;   // [+0x84] active directory-bank block ptr (double-buffered)

// ---- OS SRAM (protected system memory) helpers ----------------------------
extern byte* os_sram_lock_ex(void);       // lock+return the SRAM image
extern void  os_sram_commit_from(int offset);
extern u64   __sar2i(s32 hiValue, u32 loValue, int shift);  // Ghidra 64-bit arithmetic shift

// forward decls (defined later in this batch)
undefined4 sidev_verify_header(int block);
int        sidev_verify_dir(int block, uint* out_sel);
int        sidev_verify_fat(int block, uint* out_sel);

// sidev_fat_writeback (0x803b49d4): stamp the FAT block with a bumped sequence
// (word[2]) and a fresh checksum, flush it from cache, then erase and rewrite
// its device sector, chaining sidev_fat_write_next as the erase-complete cb.
void sidev_fat_writeback(int ch, short* fatblk, undefined4 cb)
{
    fatblk[2] = fatblk[2] + 1;
    sidev_block_checksum((ushort*)(fatblk + 2), 0x1ffc, fatblk, fatblk + 1);
    os_dc_store_range((uint)fatblk, 0x2000);
    *(undefined4*)(&DAT_8060e7d8 + ch * 0x110) = cb;
    sidev_erase_sector(ch, *(int*)(&DAT_8060e70c + ch * 0x110) *
                           ((uint)((int)fatblk - (&DAT_8060e780)[ch * 0x44]) >> 0xd),
                       (undefined*)sidev_fat_write_next);
    return;
}

// sidev_geom_field_84 (0x803b4a80): return the device geometry word at
// block+0x84 (the free-block / directory-count field).
undefined4 sidev_geom_field_84(int block)
{
    return *(undefined4*)(block + 0x84);
}

// sidev_dir_flush_complete (0x803b4a88): completion of the directory-bank
// sector write-back. Ping-pongs the active directory block between the +0x2000
// and +0x4000 scratch halves, copies the fresh block into the inactive half,
// latches the result, and fires the dir-op callback (+0xd8).
void sidev_dir_flush_complete(int ch, int rc)
{
    uint halfB;
    void (*cb)(int, int);
    uint halfA;
    int base;

    base = ch * 0x110;
    if (-1 < rc) {
        halfA = (&DAT_8060e780)[ch * 0x44] + 0x2000;
        halfB = (&DAT_8060e780)[ch * 0x44] + 0x4000;
        if (*(uint*)(&DAT_8060e784 + base) == halfA) {
            *(uint*)(&DAT_8060e784 + base) = halfB;
            leaf_31e8(halfB, halfA, 0x2000);
        } else {
            *(uint*)(&DAT_8060e784 + base) = halfA;
            leaf_31e8(halfA, halfB, 0x2000);
        }
    }
    if (*(int*)(&DAT_8060e7d0 + base) == 0) {
        sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    }
    cb = *(void(**)(int, int))(&DAT_8060e7d8 + base);
    if (cb != (void(*)(int, int))0x0) {
        *(undefined4*)(&DAT_8060e7d8 + base) = 0;
        (*cb)(ch, rc);
    }
    return;
}

// sidev_dir_write_next (0x803b4b58): after erasing the directory sector, write
// the updated directory block back at the device address computed from the
// active block's offset; on failure latch the result and fire the callback.
void sidev_dir_write_next(int ch, int rc)
{
    void (*cb)(int, int);
    int base;

    base = ch * 0x110;
    if ((rc < 0) ||
        (rc = sidev_write(ch, *(int*)(&DAT_8060e70c + base) *
                              ((uint)(*(int*)(&DAT_8060e784 + base) -
                                      (&DAT_8060e780)[ch * 0x44]) >> 0xd), 0x2000,
                          *(int*)(&DAT_8060e784 + base), (undefined4)sidev_dir_flush_complete),
         rc < 0)) {
        if (*(int*)(&DAT_8060e7d0 + base) == 0) {
            sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
        }
        cb = *(void(**)(int, int))(&DAT_8060e7d8 + base);
        if (cb != (void(*)(int, int))0x0) {
            *(undefined4*)(&DAT_8060e7d8 + base) = 0;
            (*cb)(ch, rc);
        }
    }
    return;
}

// sidev_dir_writeback (0x803b4c20): stamp the active directory block (seq word
// at +0xffd, checksum at +0/+1... actually laid out at the block start) with a
// fresh checksum, flush it, and erase+rewrite the sector via sidev_dir_write_next.
int sidev_dir_writeback(int ch, undefined4 cb)
{
    int base;
    ushort* dir;

    base = ch * 0x110;
    if ((&DAT_8060e700)[ch * 0x44] == 0) {
        base = -3;
    } else {
        dir = *(ushort**)(&DAT_8060e784 + base);
        dir[0xffd] = dir[0xffd] + 1;
        sidev_block_checksum(dir, 0x1ffc, (short*)(dir + 0xffe), (short*)(dir + 0xfff));
        os_dc_store_range((uint)dir, 0x2000);
        *(undefined4*)(&DAT_8060e7d8 + base) = cb;
        base = sidev_erase_sector(ch, *(int*)(&DAT_8060e70c + base) *
                                      ((uint)((int)dir - (&DAT_8060e780)[ch * 0x44]) >> 0xd),
                                  (undefined*)sidev_dir_write_next);
    }
    return base;
}

// sidev_block_checksum (0x803b4ce4): the SI-device block checksum over nbytes
// (as 16-bit words): sum_out = additive sum of the words, inv_out = additive
// sum of their bitwise-inverses; a resulting 0xffff is folded to 0. Fully
// unrolled 8-words-at-a-time with a scalar tail. Used on every 0x2000 block.
void sidev_block_checksum(ushort* buf, uint nbytes, short* sum_out, short* inv_out)
{
    ushort* p;
    ushort last;
    uint nwords;
    uint blocks;

    *inv_out = 0;
    nwords = ((int)nbytes >> 1) + (uint)((int)nbytes < 0 && (nbytes & 1) != 0);
    *sum_out = 0;
    if (0 < (int)nwords) {
        blocks = nwords >> 3;
        if (blocks != 0) {
            do {
                *sum_out = *sum_out + buf[0];
                *inv_out = *inv_out + ~buf[0];
                *sum_out = *sum_out + buf[1];
                *inv_out = *inv_out + ~buf[1];
                *sum_out = *sum_out + buf[2];
                *inv_out = *inv_out + ~buf[2];
                *sum_out = *sum_out + buf[3];
                *inv_out = *inv_out + ~buf[3];
                *sum_out = *sum_out + buf[4];
                *inv_out = *inv_out + ~buf[4];
                *sum_out = *sum_out + buf[5];
                *inv_out = *inv_out + ~buf[5];
                *sum_out = *sum_out + buf[6];
                *inv_out = *inv_out + ~buf[6];
                *sum_out = *sum_out + buf[7];
                p = buf + 7;
                buf = buf + 8;
                *inv_out = *inv_out + ~*p;
                blocks = blocks - 1;
            } while (blocks != 0);
            nwords = nwords & 7;
            if (nwords == 0) goto done;
        }
        do {
            *sum_out = *sum_out + *buf;
            last = *buf;
            buf = buf + 1;
            *inv_out = *inv_out + ~last;
            nwords = nwords - 1;
        } while (nwords != 0);
    }
done:
    if (*sum_out == -1) {
        *sum_out = 0;
    }
    if (*inv_out == -1) {
        *inv_out = 0;
        return;
    }
    return;
}

// sidev_verify_header (0x803b4e94): validate and authenticate the card header
// block (at *(block+0x80)). Checks the format markers ([0x10]==0, [0x11]==the
// device serial at block+8), the additive block checksum (sum at [0xfe]/[0xff]),
// then authenticates the 12-byte owner key: an LCG stream seeded from words at
// [6]/[8] XORed with a per-channel 6-byte pad read from OS SRAM must reproduce
// the stored key bytes. Finally checks the stored font encoding matches the
// system encoding. Returns 0, -0xa (bad checksum/auth) or -0xd (encoding mismatch).
undefined4 sidev_verify_header(int block)
{
    ushort word;
    ushort* hdr;
    undefined4 rc;
    byte* pad;
    ushort sum;
    ushort inv;
    ushort* p;
    uint lo;
    byte* keybase;
    int hi;
    int i;
    longlong lv;
    undefined8 uv;

    hdr = *(ushort**)(block + 0x80);
    if ((hdr[0x10] == 0) && (hdr[0x11] == *(ushort*)(block + 8))) {
        inv = 0;
        sum = 0;
        i = 0x1f;
        p = hdr;
        do {
            sum = sum + p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7];
            inv = inv + ~p[0] + ~p[1] + ~p[2] + ~p[3] + ~p[4] + ~p[5] + ~p[6] + ~p[7];
            p = p + 8;
            i = i + -1;
        } while (i != 0);
        i = 6;
        do {
            word = *p;
            p = p + 1;
            sum = sum + word;
            inv = inv + ~word;
            i = i + -1;
        } while (i != 0);
        if (sum == 0xffff) {
            sum = 0;
        }
        if (inv == 0xffff) {
            inv = 0;
        }
        if ((hdr[0xfe] == sum) && (hdr[0xff] == inv)) {
            hi = *(int*)(hdr + 6);
            lo = *(uint*)(hdr + 8);
            pad = os_sram_lock_ex();
            i = (block + 0x7f9f1900) / 0x110 + (block + 0x7f9f1900 >> 0x1f);
            keybase = (byte*)(pad + (i - (i >> 0x1f)) * 6);
            i = 0;
            p = hdr;
            do {
                lv = __sar2i(hi * 0x41c64e6d + (int)((ulonglong)lo * 0x41c64e6d >> 0x20) +
                             (uint)(0xffffcfc6 < lo * 0x41c64e6d), lo * 0x41c64e6d + 0x3039, 0x10);
                if ((uint)*(byte*)p != ((int)lv + (uint)*keybase & 0xff)) {
                    os_sram_commit_from(0);
                    return 0xfffffffa;
                }
                lv = lv * 0x41c64e6d + 0x3039;
                uv = __sar2i((int)((ulonglong)lv >> 0x20), (uint)lv, 0x10);
                i = i + 1;
                lo = (uint)uv & 0x7fff;
                hi = 0;
                keybase = keybase + 1;
                p = (ushort*)((int)p + 1);
            } while (i < 0xc);
            os_sram_commit_from(0);
            sum = sidev_get_font_encoding();
            if (hdr[0x12] == sum) {
                rc = 0;
            } else {
                rc = 0xfffffff3;
            }
        } else {
            rc = 0xfffffffa;
        }
    } else {
        rc = 0xfffffffa;
    }
    return rc;
}

// sidev_verify_dir (0x803b5118): validate the two directory-bank copies (at
// header+0x2000 and +0x4000). For each, recompute the additive checksum and
// compare against the stored footer; a mismatch invalidates the active pointer
// (block+0x84) and counts a bad copy. If both are good, select the newer copy
// by the sequence field (+0x3a), copy it over the older, and record its index.
// Returns the count of bad copies (0 = both valid); *out_sel = chosen index.
int sidev_verify_dir(int block, uint* out_sel)
{
    ushort word;
    uint idx;
    ushort* p;
    uint* addrp;
    uint* footp;
    short sum;
    short inv;
    uint sel;
    int bad;
    int i;
    uint addrs[6];

    addrp = addrs + 2;
    footp = addrs;
    bad = 0;
    sel = 0;
    idx = 0;
    do {
        *addrp = *(int*)(block + 0x80) + (idx + 1) * 0x2000;
        inv = 0;
        *footp = *addrp + 0x1fc0;
        sum = 0;
        p = (ushort*)*addrp;
        i = 0x1ff;
        do {
            sum = sum + p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7];
            inv = inv + ~p[0] + ~p[1] + ~p[2] + ~p[3] + ~p[4] + ~p[5] + ~p[6] + ~p[7];
            p = p + 8;
            i = i + -1;
        } while (i != 0);
        i = 6;
        do {
            word = *p;
            p = p + 1;
            sum = sum + word;
            inv = inv + ~word;
            i = i + -1;
        } while (i != 0);
        if (sum == -1) {
            sum = 0;
        }
        if (inv == -1) {
            inv = 0;
        }
        if ((sum != *(short*)(*footp + 0x3c)) || (inv != *(short*)(*footp + 0x3e))) {
            *(undefined4*)(block + 0x84) = 0;
            bad = bad + 1;
            sel = idx;
        }
        idx = idx + 1;
        addrp = addrp + 1;
        footp = footp + 1;
    } while ((int)idx < 2);
    if (bad == 0) {
        if (*(int*)(block + 0x84) == 0) {
            sel = (uint)(-1 < (int)*(short*)(addrs[0] + 0x3a) - (int)*(short*)(addrs[1] + 0x3a));
            *(uint*)(block + 0x84) = addrs[sel + 2];
            leaf_31e8(addrs[sel + 2], addrs[(sel ^ 1) + 2], 0x2000);
        } else {
            sel = (uint)(*(int*)(block + 0x84) != addrs[2]);
        }
    }
    if (out_sel != (uint*)0x0) {
        *out_sel = sel;
    }
    return bad;
}

// sidev_verify_fat (0x803b5358): validate the two FAT-bank copies (at
// header+0x6000 and +0x8000). Recomputes the additive checksum against the
// footer, and additionally recounts the free entries (0 links from index 5 to
// block[0x10]) against the stored free count [3]. A mismatch invalidates the
// active FAT pointer (block+0x88). If both are valid, select the newer copy by
// its sequence field (+4), copy it over the older, and record its index.
int sidev_verify_fat(int block, uint* out_sel)
{
    uint* addrp;
    uint idx;
    ushort* p;
    short* fatp;
    short* fat;
    ushort word;
    short sum;
    short inv;
    uint sel;
    int bad;
    int i;
    uint addrs[3];

    idx = 0;
    bad = 0;
    sel = 0;
    addrp = addrs;
    do {
        fat = (short*)(*(int*)(block + 0x80) + (idx + 3) * 0x2000);
        *addrp = (uint)fat;
        p = (ushort*)(fat + 2);
        inv = 0;
        sum = 0;
        i = 0x1ff;
        do {
            sum = sum + p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7];
            inv = inv + ~p[0] + ~p[1] + ~p[2] + ~p[3] + ~p[4] + ~p[5] + ~p[6] + ~p[7];
            p = p + 8;
            i = i + -1;
        } while (i != 0);
        i = 6;
        do {
            word = *p;
            p = p + 1;
            sum = sum + word;
            inv = inv + ~word;
            i = i + -1;
        } while (i != 0);
        if (sum == -1) {
            sum = 0;
        }
        if (inv == -1) {
            inv = 0;
        }
        if ((*fat == sum) && (fat[1] == inv)) {
            fatp = fat + 5;
            sum = 0;
            for (word = 5; word < *(ushort*)(block + 0x10); word = word + 1) {
                if (*fatp == 0) {
                    sum = sum + 1;
                }
                fatp = fatp + 1;
            }
            if (sum != fat[3]) {
                *(undefined4*)(block + 0x88) = 0;
                bad = bad + 1;
                sel = idx;
            }
        } else {
            *(undefined4*)(block + 0x88) = 0;
            bad = bad + 1;
            sel = idx;
        }
        idx = idx + 1;
        addrp = addrp + 1;
    } while ((int)idx < 2);
    if (bad == 0) {
        if (*(uint*)(block + 0x88) == 0) {
            sel = (uint)(-1 < (int)*(short*)(addrs[0] + 4) - (int)*(short*)(addrs[1] + 4));
            *(uint*)(block + 0x88) = addrs[sel];
            leaf_31e8(addrs[sel], addrs[sel ^ 1], 0x2000);
        } else {
            sel = (uint)(*(uint*)(block + 0x88) != addrs[0]);
        }
    }
    if (out_sel != (uint*)0x0) {
        *out_sel = sel;
    }
    return bad;
}

// sidev_verify_all (0x803b55dc): the mount validity gate. Authenticates the
// header, then validates both directory and FAT banks; the combined bad-copy
// count must be 0 (fully clean) to mount OK, exactly 1 bad copy is recoverable-
// but-reported as -6, and any header failure propagates. Returns 0 / -6 / <0.
int sidev_verify_all(int block)
{
    int rc;
    int rc2;

    rc = sidev_verify_header(block);
    if (-1 < rc) {
        rc = sidev_verify_dir(block, (uint*)0x0);
        rc2 = sidev_verify_fat(block, (uint*)0x0);
        rc = rc + rc2;
        if (rc == 1) {
            rc = -6;
        } else if ((rc < 1) && (-1 < rc)) {
            rc = 0;
        } else {
            rc = -6;
        }
    }
    return rc;
}

// ===========================================================================
// batch_1146 (0x803b5668 .. 0x803b6630): the SI-device (memory-card) mount
// state machine and the fsck/repair pass. sidev_attach is the public
// attach/mount entry; it primes the channel state and drives sidev_mount_continue,
// a self-chaining callback that probes for the card, retries, and on success
// runs sidev_mount_sequence (detect capacity, read the device id, authenticate
// the owner key against OS SRAM, set the transfer mode, and page the card image
// into the work area). Once mounted, sidev_fsck walks the directory + FAT to
// repair the free-block reference counts and rewrite the affected bank.
// sidev_finish tears the mount down with a result code.
//
// NON-MATCHING: the flat DAT_8060e7xx byte-offset arithmetic, the additive
// checksum recompute, and the SRAM owner-key byte fixups are preserved verbatim.
// Per-file externs added for the low-level probe/SI helpers and the device-info
// tables (DAT_8049f460/480). C4311/C4312 ptr-width warnings only.
// ---------------------------------------------------------------------------

// ---- device-info tables + status flags ------------------------------------
extern int    DAT_8049f460;   // capacity -> bytes-per-block table (index (code>>9)&0x1c)
extern int    DAT_8049f480;   // capacity -> read-block-length table (index (code>>6)&0x1c)
extern byte   DAT_800030e3;   // OS reset/shutdown flags (bit 0x80 = shutting down)
extern short  DAT_806114b0;   // expected device id for the built-in EXI slot (-1 = any)
extern byte   DAT_80000004;   // low-memory sentinel address value (EXI type marker)
extern int    DAT_803b1c68;   // default no-op completion callback

// ---- extra channel-state DATs (base 0x8060e700, stride 0x110) -------------
extern ushort DAT_8060e708;   // [+0x08] device serial / size code (short)
extern short  DAT_8060e712;   // [+0x12] read-id result (2 bytes) / font marker
extern int    DAT_8060e718;   // [+0x18] probed owner-id words (12 bytes)
extern int    DAT_8060e719;
extern int    DAT_8060e71a;
extern int    DAT_8060e71b;
extern int    DAT_8060e71c;
extern int    DAT_8060e71d;
extern int    DAT_8060e71e;
extern int    DAT_8060e71f;
extern int    DAT_8060e808;   // [+0x108] detected EXI device address

// ---- low-level probe / SI-engine helpers ----------------------------------
extern uint FUN_803ba188(int ch);                    // read EXI/attach status bits
extern int  FUN_803ba1c8(int ch, int a, byte* out);  // probe device id/type
extern int  FUN_803b957c(int ch);                    // poll insertion state
extern int  FUN_803b9630(int ch, void* cb);          // register extract callback
extern int  FUN_803b973c(int ch);                    // release channel
extern void dsp_task_wakeup_thread(int arg);         // OS thread wakeup (used as completion cb)

// forward decls (defined later in this batch)
int  sidev_fsck(int ch, undefined4* out_bytes, undefined* cb);
int  sidev_mount_sequence(int ch);
void sidev_finish(int ch, undefined4 result);

// sidev_fsck (0x803b5668): filesystem check/repair. Mounts the channel,
// re-authenticates the header, validates the directory + FAT banks (recovering
// a single bad copy), then walks every directory entry building a block
// reference-count map, verifying each file's FAT chain length against its size,
// and rebuilding the free-block links + free count in the FAT; if anything was
// repaired the affected bank checksum is recomputed and the bank is written back.
// out_bytes (if given) receives 0x2000 when a write-back is queued. Returns 0 /
// -6 (inconsistent) / a latched error.
int sidev_fsck(int ch, undefined4* out_bytes, undefined* cb)
{
    ushort* pw;
    uint cnt;
    bool fatRepaired;
    byte dirRepaired;
    bool dirRewrite;
    int rc;
    int rc2;
    ushort link;
    int base;
    ushort* fatp;
    short* sp;
    char* entry;
    short* sp2;
    ushort i;
    short freeCount;
    uint scratch;
    uint dirSel;
    uint fatSel;
    uint banks[4];
    int* block[3];

    dirRepaired = 0;
    dirRewrite = false;
    fatRepaired = false;
    if (out_bytes != (undefined4*)0x0) {
        *out_bytes = 0;
    }
    rc = sidev_mount(ch, (undefined4*)block);
    if (-1 < rc) {
        rc = sidev_verify_header((int)block[0]);
        if (rc < 0) {
            rc = sidev_set_result_if_idle(block[0], rc);
        } else {
            rc = sidev_verify_dir((int)block[0], &dirSel);
            rc2 = sidev_verify_fat((int)block[0], &fatSel);
            if (rc + rc2 < 2) {
                base = block[0][0x20];
                banks[2] = base + 0x2000;
                banks[0] = base + 0x6000;
                banks[3] = base + 0x4000;
                banks[1] = base + 0x8000;
                if (rc + rc2 == 1) {
                    if (block[0][0x21] == 0) {
                        block[0][0x21] = banks[dirSel + 2];
                        leaf_31e8(banks[dirSel + 2], banks[(dirSel ^ 1) + 2], 0x2000);
                        dirRewrite = true;
                    } else {
                        block[0][0x22] = banks[fatSel];
                        leaf_31e8(banks[fatSel], banks[fatSel ^ 1], 0x2000);
                        dirRepaired = 1;
                    }
                }
                scratch = banks[fatSel ^ 1];
                memset_wrapper(scratch, 0, 0x2000);
                rc2 = 0x7f;
                rc = 0;
                do {
                    entry = (char*)(block[0][0x21] + rc);
                    if (*entry != -1) {
                        link = *(ushort*)(entry + 0x36);
                        for (i = 0; (link != 0xffff && (i < *(ushort*)(entry + 0x38))); i = i + 1) {
                            cnt = (uint)link;
                            if ((cnt < 5) || (*(ushort*)(block[0] + 4) <= cnt)) {
                                return sidev_set_result_if_idle(block[0], -6);
                            }
                            base = cnt * 2;
                            link = *(short*)(scratch + base) + 1;
                            *(ushort*)(scratch + base) = link;
                            if (1 < link) {
                                return sidev_set_result_if_idle(block[0], -6);
                            }
                            link = *(ushort*)(block[0][0x22] + base);
                        }
                        if ((i != *(ushort*)(entry + 0x38)) || (link != 0xffff)) {
                            return sidev_set_result_if_idle(block[0], -6);
                        }
                    }
                    rc = rc + 0x40;
                    rc2 = rc2 + -1;
                } while (rc2 != 0);
                sp = (short*)(scratch + 10);
                freeCount = 0;
                rc = 10;
                for (i = 5; i < *(ushort*)(block[0] + 4); i = i + 1) {
                    link = *(ushort*)(block[0][0x22] + rc);
                    if (*sp == 0) {
                        if (link != 0) {
                            *(ushort*)(block[0][0x22] + rc) = 0;
                            fatRepaired = true;
                        }
                        freeCount = freeCount + 1;
                    } else if (((link < 5) || (*(ushort*)(block[0] + 4) <= link)) && (link != 0xffff)) {
                        return sidev_set_result_if_idle(block[0], -6);
                    }
                    rc = rc + 2;
                    sp = sp + 1;
                }
                if (freeCount != *(short*)(block[0][0x22] + 6)) {
                    *(short*)(block[0][0x22] + 6) = freeCount;
                    fatRepaired = true;
                }
                if (fatRepaired) {
                    sp = (short*)block[0][0x22];
                    sp[1] = 0;
                    sp2 = sp + 1;
                    pw = (ushort*)(sp + 2);
                    *sp = 0;
                    rc = 0x1ff;
                    do {
                        *sp = *sp + pw[0];
                        *sp2 = *sp2 + ~pw[0];
                        *sp = *sp + pw[1];
                        *sp2 = *sp2 + ~pw[1];
                        *sp = *sp + pw[2];
                        *sp2 = *sp2 + ~pw[2];
                        *sp = *sp + pw[3];
                        *sp2 = *sp2 + ~pw[3];
                        *sp = *sp + pw[4];
                        *sp2 = *sp2 + ~pw[4];
                        *sp = *sp + pw[5];
                        *sp2 = *sp2 + ~pw[5];
                        *sp = *sp + pw[6];
                        *sp2 = *sp2 + ~pw[6];
                        *sp = *sp + pw[7];
                        pw = pw + 8;
                        *sp2 = *sp2 + ~pw[-1];
                        rc = rc + -1;
                    } while (rc != 0);
                    rc = 6;
                    do {
                        *sp = *sp + *pw;
                        link = *pw;
                        pw = pw + 1;
                        *sp2 = *sp2 + ~link;
                        rc = rc + -1;
                    } while (rc != 0);
                    if (*sp == -1) {
                        *sp = 0;
                    }
                    if (*sp2 == -1) {
                        *sp2 = 0;
                    }
                }
                leaf_31e8(banks[fatSel ^ 1], banks[fatSel], 0x2000);
                if (dirRewrite) {
                    if (out_bytes != (undefined4*)0x0) {
                        *out_bytes = 0x2000;
                    }
                    rc = sidev_dir_writeback(ch, (undefined4)cb);
                } else if ((bool)(dirRepaired | fatRepaired)) {
                    if (out_bytes != (undefined4*)0x0) {
                        *out_bytes = 0x2000;
                    }
                    sidev_fat_writeback(ch, (short*)block[0][0x22], (undefined4)cb);
                    rc = 0;
                } else {
                    sidev_set_result_if_idle(block[0], 0);
                    if (cb != (undefined*)0x0) {
                        os_disable_interrupts();
                        (*(void(*)(int, int))cb)(ch, 0);
                        os_get_interrupts_enabled();
                    }
                    rc = 0;
                }
            } else {
                rc = sidev_set_result_if_idle(block[0], -6);
            }
        }
    }
    return rc;
}

// sidev_check_async (0x803b5bf8): fire-and-forget fsck (ignore the bytes-out).
void sidev_check_async(int ch, undefined* cb)
{
    undefined4 bytesOut[2];

    sidev_fsck(ch, bytesOut, cb);
    return;
}

// sidev_check (0x803b5c20): blocking fsck; kicks the async check with the OS
// thread-wakeup completion callback and waits for the result.
void sidev_check(int ch)
{
    int rc;
    undefined4 bytesOut[2];

    rc = sidev_fsck(ch, bytesOut, (undefined*)dsp_task_wakeup_thread);
    if (-1 < rc) {
        sidev_wait_result(ch);
    }
    return;
}

// sidev_is_valid_capacity (0x803b5c74): validate a device address / capacity
// code. Rejects addresses outside the low EXI window (unless the built-in slot
// sentinel), misaligned addresses, unknown capacity codes (must be one of
// 4/8/0x10/0x20/0x40/0x80), and capacities whose bytes-per-block table entry
// would give fewer than 8 blocks. Returns 1 if usable, else 0.
undefined4 sidev_is_valid_capacity(undefined1* addr)
{
    uint code;

    if ((((uint)addr & 0xffff0000) != 0) && ((addr != &DAT_80000004 || (DAT_806114b0 == -1)))) {
        return 0;
    }
    if (((uint)addr & 3) != 0) {
        return 0;
    }
    code = (uint)addr & 0xfc;
    if (code != 0x20) {
        if (code < 0x20) {
            if (code != 8) {
                if (code < 8) {
                    if (code != 4) {
                        return 0;
                    }
                } else if (code != 0x10) {
                    return 0;
                }
            }
        } else if (code != 0x80) {
            if (0x7f < code) {
                return 0;
            }
            if (code != 0x40) {
                return 0;
            }
        }
    }
    if (*(uint*)(&DAT_8049f460 + ((uint)addr >> 9 & 0x1c)) == 0) {
        return 0;
    }
    if ((((uint)addr & 0xfc) << 0x11) / *(uint*)(&DAT_8049f460 + ((uint)addr >> 9 & 0x1c)) < 8) {
        return 0;
    }
    return 1;
}

// sidev_get_status (0x803b5d40): query a channel's insertion/size status. When
// no card is mounted, probes the slot (FUN_803ba188/1c8) and validates the
// reported capacity; when mounted, reports the cached size code (+0x08) and
// bytes-per-block (+0x0c). *out_size / *out_bpb receive the size code and bytes-
// per-block. Returns 0 ok, -1 not-inserted, -2 unsupported, -3 busy/shutdown, -0x80 bad index.
undefined4 sidev_get_status(int ch, uint* out_size, undefined4* out_bpb)
{
    int inserted;
    uint status;
    int base;
    undefined4 rc;
    undefined1* probe[2];

    if ((-1 < ch) && (ch < 2)) {
        if ((DAT_800030e3 & 0x80) != 0) {
            return 0xfffffffd;
        }
        base = ch * 0x110;
        os_disable_interrupts();
        inserted = FUN_803b957c(ch);
        if (inserted == -1) {
            rc = 0xfffffffd;
        } else if (inserted == 0) {
            rc = 0xffffffff;
        } else if ((&DAT_8060e700)[ch * 0x44] == 0) {
            status = FUN_803ba188(ch);
            if ((status & 8) == 0) {
                inserted = FUN_803ba1c8(ch, 0, (byte*)probe);
                if (inserted == 0) {
                    rc = 0xffffffff;
                } else {
                    inserted = sidev_is_valid_capacity(probe[0]);
                    if (inserted == 0) {
                        rc = 0xfffffffe;
                    } else {
                        if (out_size != (uint*)0x0) {
                            *out_size = (uint)probe[0] & 0xfc;
                        }
                        if (out_bpb != (undefined4*)0x0) {
                            *out_bpb = *(undefined4*)(&DAT_8049f460 + ((uint)probe[0] >> 9 & 0x1c));
                        }
                        rc = 0;
                    }
                }
            } else {
                rc = 0xfffffffe;
            }
        } else if (*(int*)(&DAT_8060e724 + base) < 1) {
            rc = 0xffffffff;
        } else {
            if (out_size != (uint*)0x0) {
                *out_size = (uint)*(ushort*)(&DAT_8060e708 + base);
            }
            if (out_bpb != (undefined4*)0x0) {
                *out_bpb = *(undefined4*)(&DAT_8060e70c + base);
            }
            rc = 0;
        }
        os_get_interrupts_enabled();
        return rc;
    }
    return 0xffffff80;
}

// sidev_mount_sequence (0x803b5ebc): the multi-stage mount worker, driven by
// the phase counter (+0x24). Phase 0: detect the card, validate capacity, read
// the device id, derive geometry (bytes-per-block/response-length/block count),
// read the id + status, and either authenticate the owner id against OS SRAM
// (probing + storing a fresh id) or verify a stored id checksum. Phases 1..N:
// set the transfer mode once, then page each 0x2000 block of the card image into
// the work area. Errors route through sidev_finish; the read completion re-enters
// via sidev_mount_continue.
int sidev_mount_sequence(int ch)
{
    char c1, c2, c3, c4, c5, c6, c7;
    short id;
    byte sum;
    int rc;
    undefined2* sram;
    int i;
    undefined2* keyslot;
    int base;
    char* dst;
    char* src;
    byte status[4];
    undefined1* probe[2];

    base = ch * 0x110;
    if (*(int*)(&DAT_8060e724 + base) == 0) {
        rc = FUN_803ba1c8(ch, 0, (byte*)probe);
        if (rc == 0) {
            rc = -3;
        } else {
            rc = sidev_is_valid_capacity(probe[0]);
            if (rc == 0) {
                rc = -2;
            } else {
                rc = 0;
            }
        }
        if (-1 < rc) {
            *(undefined1**)(&DAT_8060e808 + base) = probe[0];
            *(ushort*)(&DAT_8060e708 + base) = (ushort)probe[0] & 0xfc;
            *(undefined4*)(&DAT_8060e70c + base) =
                *(undefined4*)(&DAT_8049f460 + ((uint)probe[0] >> 9 & 0x1c));
            *(short*)(&DAT_8060e710 + base) =
                (short)(((int)((uint)*(ushort*)(&DAT_8060e708 + base) << 0x14) >> 3) /
                        *(int*)(&DAT_8060e70c + base));
            *(undefined4*)(&DAT_8060e714 + base) =
                *(undefined4*)(&DAT_8049f480 + ((uint)probe[0] >> 6 & 0x1c));
            rc = sidev_cmd_read_id(ch, (byte*)(&DAT_8060e712 + base));
            if (-1 < rc) {
                rc = sidev_is_encoding_set();
                if ((rc == 0) || ((int)(uint)*(ushort*)(&DAT_8060e712 + base) >> 8 != 0xec)) {
                    *(undefined2*)(&DAT_8060e70a + base) = 0x80;
                } else {
                    *(undefined2*)(&DAT_8060e70a + base) = 0x200;
                }
                rc = sidev_cmd_clear_status(ch);
                if ((-1 < rc) && (rc = sidev_cmd_read_status(ch, status), -1 < rc)) {
                    rc = FUN_803b94fc(ch);
                    if (rc == 0) {
                        rc = -3;
                    } else if ((status[0] & 0x40) == 0) {
                        rc = sidev_probe_id(ch, (uint*)(&DAT_8060e718 + base));
                        if (-1 < rc) {
                            sram = (undefined2*)os_sram_lock_ex();
                            keyslot = sram + ch * 6;
                            *(char*)keyslot = (&DAT_8060e718)[base];
                            c1 = (&DAT_8060e718)[base];
                            *((char*)keyslot + 1) = (&DAT_8060e719)[base];
                            i = 8;
                            c2 = (&DAT_8060e719)[base];
                            *(char*)(keyslot + 1) = (&DAT_8060e71a)[base];
                            c3 = (&DAT_8060e71a)[base];
                            *((char*)keyslot + 3) = (&DAT_8060e71b)[base];
                            c4 = (&DAT_8060e71b)[base];
                            *(char*)(keyslot + 2) = (&DAT_8060e71c)[base];
                            c5 = (&DAT_8060e71c)[base];
                            *((char*)keyslot + 5) = (&DAT_8060e71d)[base];
                            c6 = (&DAT_8060e71d)[base];
                            *(char*)(keyslot + 3) = (&DAT_8060e71e)[base];
                            c7 = (&DAT_8060e71e)[base];
                            *((char*)keyslot + 7) = (&DAT_8060e71f)[base];
                            sum = c1 + c2 + c3 + c4 + c5 + c6 + c7 + (&DAT_8060e71f)[base];
                            dst = (char*)(keyslot + 4);
                            {
                                int off = base;
                                i = 4;
                                do {
                                    src = (char*)((int)(&DAT_8060e700 + ch * 0x44) + 8 + off + 0x18);
                                    off = off + 1;
                                    *dst = *src;
                                    dst = dst + 1;
                                    sum = sum + *src;
                                    i = i + -1;
                                } while (i != 0);
                            }
                            *((char*)sram + ch + 0x26) = ~sum;
                            os_sram_commit_from(1);
                            return rc;
                        }
                    } else {
                        *(undefined4*)(&DAT_8060e724 + base) = 1;
                        sram = (undefined2*)os_sram_lock_ex();
                        {
                            char* p = (char*)(sram + ch * 6);
                            sum = p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7];
                            p = p + 8;
                            i = 4;
                            do {
                                c1 = *p;
                                p = p + 1;
                                sum = sum + c1;
                                i = i + -1;
                            } while (i != 0);
                        }
                        os_sram_commit_from(0);
                        if (*((char*)sram + ch + 0x26) == (char)~sum) goto page_in;
                        rc = -5;
                    }
                }
            }
        }
fail:
        FUN_803ba0ac(ch);
        sidev_finish(ch, rc);
    } else {
page_in:
        if (*(int*)(&DAT_8060e724 + base) == 1) {
            if (*(undefined1**)(&DAT_8060e808 + base) == &DAT_80000004) {
                sram = (undefined2*)os_sram_lock_ex();
                id = sram[ch * 6];
                os_sram_commit_from(0);
                if ((DAT_806114b0 == -1) || (id != DAT_806114b0)) {
                    rc = -2;
                    goto fail;
                }
            }
            *(undefined4*)(&DAT_8060e724 + base) = 2;
            rc = sidev_cmd_set_mode(ch, 1);
            if (rc < 0) goto fail;
            FUN_803b930c(ch, -0x7fc4e288);
            FUN_803ba0ac(ch);
            os_dc_invalidate_range((&DAT_8060e780)[ch * 0x44], 0xa000);
        }
        rc = sidev_read(ch, *(int*)(&DAT_8060e70c + base) *
                            (*(int*)(&DAT_8060e724 + base) + -2), 0x2000,
                        (&DAT_8060e780)[ch * 0x44] +
                        (*(int*)(&DAT_8060e724 + base) + -2) * 0x2000,
                        (undefined4)sidev_mount_continue);
        if (rc < 0) {
            sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
        }
    }
    return rc;
}

// sidev_mount_continue (0x803b6310): the mount-continuation callback. On a
// probe-again result (1) re-arms the extract-probe and re-runs the mount
// sequence; on a page-in step completion advances the phase counter and reads
// the next block, finishing with sidev_verify_all once all banks are paged.
// Terminal errors go to sidev_finish; the channel result-latch clear callback
// (+0xd0) is always fired at the end.
void sidev_mount_continue(int ch, int rc)
{
    int phase;
    int base;
    void (*cb)(int, int);

    base = ch * 0x110;
    if (rc != -3) {
        if (-4 < rc) {
            if (rc == 1) {
                *(void(**)(int, int))(&DAT_8060e7dc + base) = sidev_mount_continue;
                if (FUN_803b9fb8(ch, 0, -0x7fc4e0c8) == 0) {
                    return;
                }
                *(undefined4*)(&DAT_8060e7dc + base) = 0;
                rc = sidev_mount_sequence(ch);
            } else {
                if ((0 < rc) || (rc < 0)) goto done;
                phase = *(int*)(&DAT_8060e724 + base);
                *(int*)(&DAT_8060e724 + base) = phase + 1;
                if (6 < phase + 1) {
                    rc = sidev_verify_all((int)(&DAT_8060e700 + ch * 0x44));
                    goto done;
                }
                rc = sidev_mount_sequence(ch);
            }
            if (-1 < rc) {
                return;
            }
            goto done;
        }
        if (rc != -5) goto done;
    }
    sidev_finish(ch, rc);
done:
    cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
    *(undefined4*)(&DAT_8060e7d0 + base) = 0;
    sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    (*cb)(ch, rc);
    return;
}

// sidev_attach (0x803b6448): public attach/mount entry for a channel. Validates
// the index and shutdown state, rejects if a card is already claimed or absent,
// stores the caller's work-area ptr / detach callback / completion callback,
// resets the phase counter and channel state, registers the extract-abort
// handler, then arms the extract probe and enters the mount sequence. Returns 0
// on a queued mount, or a negative error.
int sidev_attach(int ch, undefined4 workarea, undefined4 detachCb, undefined* doneCb)
{
    int rc;
    uint status;
    int reg;
    int* block;

    if ((ch < 0) || (1 < ch)) {
        rc = -0x80;
    } else if ((DAT_800030e3 & 0x80) == 0) {
        rc = ch * 0x110;
        block = &DAT_8060e700 + ch * 0x44;
        os_disable_interrupts();
        if ((&DAT_8060e704)[ch * 0x44] == -1) {
            os_get_interrupts_enabled();
            rc = -1;
        } else if ((*block == 0) && (status = FUN_803ba188(ch), (status & 8) != 0)) {
            os_get_interrupts_enabled();
            rc = -2;
        } else {
            (&DAT_8060e704)[ch * 0x44] = 0xffffffff;
            (&DAT_8060e780)[ch * 0x44] = workarea;
            *(undefined4*)(&DAT_8060e7c4 + rc) = detachCb;
            if (doneCb == (undefined*)0x0) {
                doneCb = (undefined*)&DAT_803b1c68;
            }
            *(undefined**)(&DAT_8060e7d0 + rc) = doneCb;
            *(undefined4*)(&DAT_8060e7cc + rc) = 0;
            if ((*block == 0) && (reg = FUN_803b9630(ch, (void*)sidev_abort), reg == 0)) {
                (&DAT_8060e704)[ch * 0x44] = 0xfffffffd;
                os_get_interrupts_enabled();
                rc = -3;
            } else {
                *(undefined4*)(&DAT_8060e724 + rc) = 0;
                *block = 1;
                FUN_803b930c(ch, 0);
                os_cancel_alarm((int*)(&DAT_8060e7e0 + rc));
                *(undefined4*)(&DAT_8060e784 + rc) = 0;
                *(undefined4*)(&DAT_8060e788 + rc) = 0;
                os_get_interrupts_enabled();
                *(void(**)(int, int))(&DAT_8060e7dc + rc) = sidev_mount_continue;
                reg = FUN_803b9fb8(ch, 0, -0x7fc4e0c8);
                if (reg == 0) {
                    rc = 0;
                } else {
                    *(undefined4*)(&DAT_8060e7dc + rc) = 0;
                    rc = sidev_mount_sequence(ch);
                }
            }
        }
    } else {
        rc = -3;
    }
    return rc;
}

// sidev_attach_sync (0x803b65e8): blocking attach; queues the mount with the OS
// thread-wakeup completion callback and waits for the result.
void sidev_attach_sync(int ch, undefined4 workarea, undefined4 detachCb)
{
    int rc;

    rc = sidev_attach(ch, workarea, detachCb, (undefined*)dsp_task_wakeup_thread);
    if (-1 < rc) {
        sidev_wait_result(ch);
    }
    return;
}

// sidev_finish (0x803b6630): tear down a mount. Under the interrupt lock, if the
// channel is active it resets the channel, releases it, cancels the alarm,
// clears the active flag, latches the result code, and resets the phase counter.
void sidev_finish(int ch, undefined4 result)
{
    os_disable_interrupts();
    if ((&DAT_8060e700)[ch * 0x44] != 0) {
        FUN_803b930c(ch, 0);
        FUN_803b973c(ch);
        os_cancel_alarm((int*)(&DAT_8060e7e0 + ch * 0x110));
        (&DAT_8060e700)[ch * 0x44] = 0;
        (&DAT_8060e704)[ch * 0x44] = result;
        *(undefined4*)(&DAT_8060e724 + ch * 0x110) = 0;
    }
    os_get_interrupts_enabled();
    return;
}

// ============================================================================
// batch_1147: SI-device (memory-card) format pass + directory-entry lookup /
// access-control API. Appended to src/input/si_device.cpp, continuing the
// batch_1141..1146 driver from 0x803b6630.
//
// sidev_format_card rebuilds every on-card bank from scratch: it wipes and
// stamps the header block (format markers, SRAM-derived owner key, font
// encoding, timestamp, additive checksum), the two directory copies and the
// two FAT copies, then erases+rewrites the whole 0xA000 region through the
// multi-sector write-back callback sidev_format_write_next. sidev_format is
// the public "reformat this card" entry; sidev_format_default reformats with
// the system font encoding.
//
// The direntry helpers walk the mounted directory block (0x7f entries of 0x40
// bytes, base = geometry field +0x84). Each entry carries a 6-byte owner key
// at +0x00, a 0x20-byte name at +0x08, a permission-flag byte at +0x34, and a
// start-block short at +0x36. The *_owner_match family authenticates the
// caller against the entry's owner key (with public/shared-flag fallbacks),
// sidev_direntry_name_match compares the file name, sidev_direntry_find_by_name
// scans for a name, and sidev_open_by_index opens an entry by index.
//
// NON-MATCHING: the flat block-offset arithmetic, the SRAM owner-key LCG
// (__sar2i 64-bit arithmetic-shift intrinsic) descramble loop, and the
// additive checksum stamping are preserved verbatim. Per-file externs are
// declared for the two raw DATs this batch newly references; os_read_time_base
// and msl_memcmp resolve through the shared headers.

// ---- batch_1147 per-file externs ------------------------------------------
extern byte   DAT_806114b2;   // per-entry permission-flag mask (0x20 shared / 0x40 public bits)
extern ushort DAT_cc00206e;   // VI/format hardware config short latched into the header
extern undefined2* os_sram_lock(void);          // lock the OS SRAM image (serial/flash-id view)
extern void        os_sram_commit(int offset);  // release + commit the SRAM image
extern u64         os_read_time_base(void);      // read the CPU time-base counter
extern int         msl_memcmp(int a, int b, int n); // MSL byte compare
extern int         DAT_8060e728;   // [+0x28] format write-back phase counter

// sidev_format (0x803b66cc): public "format card" entry. Mounts the card at the
// given channel; on success it tears the channel back down under an interrupt
// lock (reset, cancel the timeout alarm, mark inactive, latch the -3 idle
// result, clear the phase) and returns 0. On a mount failure the mount error
// is returned unchanged.
int sidev_format(int ch)
{
    int rc;
    int* block;

    rc = sidev_mount(ch, (undefined4*)&block);
    if (-1 < rc) {
        os_disable_interrupts();
        if ((&DAT_8060e700)[ch * 0x44] != 0) {
            FUN_803b930c(ch, 0);
            FUN_803b973c(ch);
            os_cancel_alarm((int*)(&DAT_8060e7e0 + ch * 0x110));
            (&DAT_8060e700)[ch * 0x44] = 0;
            (&DAT_8060e704)[ch * 0x44] = 0xfffffffd;
            *(undefined4*)(&DAT_8060e724 + ch * 0x110) = 0;
        }
        os_get_interrupts_enabled();
        rc = 0;
    }
    return rc;
}

// sidev_format_write_next (0x803b6778): the format multi-sector write-back
// state machine, chained as the erase/write completion callback. It walks a
// per-channel phase counter (+0x28): phases 1..4 erase the four leading sectors
// (header/dir/FAT scratch), phases 5..9 write the five 0x2000 blocks of the
// freshly-built image out to the card, and phase 10 rebuilds the two directory
// double-buffer halves (+0x2000/+0x4000) and the two FAT halves
// (+0x6000/+0x8000) by copying the just-written blocks into the scratch mirrors.
// On any sub-op error, or when the phases complete, it clears the stored
// callback, latches the result, and fires the user completion callback (+0xd0).
void sidev_format_write_next(int ch, int rc)
{
    int phase;
    int base;
    void (*cb)(int, int);

    base = ch * 0x110;
    if (-1 < rc) {
        *(int*)(&DAT_8060e728 + base) = *(int*)(&DAT_8060e728 + base) + 1;
        phase = *(int*)(&DAT_8060e728 + base);
        if (phase < 5) {
            rc = sidev_erase_sector(ch, *(int*)(&DAT_8060e70c + base) * phase, (undefined*)sidev_format_write_next);
        } else {
            if (9 < phase) {
                *(int*)(&DAT_8060e784 + base) = (&DAT_8060e780)[ch * 0x44] + 0x2000;
                leaf_31e8(*(uint*)(&DAT_8060e784 + base), (&DAT_8060e780)[ch * 0x44] + 0x4000, 0x2000);
                *(int*)(&DAT_8060e788 + base) = (&DAT_8060e780)[ch * 0x44] + 0x6000;
                leaf_31e8(*(uint*)(&DAT_8060e788 + base), (&DAT_8060e780)[ch * 0x44] + 0x8000, 0x2000);
                goto done;
            }
            rc = sidev_write(ch, *(int*)(&DAT_8060e70c + base) * (phase - 5), 0x2000,
                             (&DAT_8060e780)[ch * 0x44] + (phase - 5) * 0x2000, (undefined4)sidev_format_write_next);
        }
        if (-1 < rc) {
            return;
        }
    }
done:
    cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
    *(undefined4*)(&DAT_8060e7d0 + base) = 0;
    sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    (*cb)(ch, rc);
    return;
}

// sidev_format_card (0x803b68bc): build every on-card bank in the work area and
// kick the format write-back. After mounting it: (1) clears the header block to
// 0xff and stamps the font-encoding param, the SRAM serial/flash-id words, the
// VI config short, and the current time-base timestamp; (2) derives the 12-byte
// owner key by running an LCG stream (seeded from the time base) added to the
// per-channel 6-byte OS-SRAM pad -- the 8-at-a-time unrolled loop then a 4-byte
// tail; (3) checksums the header; (4) clears+seq-stamps+checksums the two
// directory copies and the two FAT copies (FAT copy carries the reserved-head
// index 5 and the free-block count = total-5); (5) records the done callback,
// flushes the 0xA000 image from the data cache, resets the phase, and starts
// the erase/write chain via sidev_format_write_next. Returns 0 (queued) or a
// negative mount/queue error.
int sidev_format_card(int ch, ushort fontEnc, undefined* doneCb)
{
    unsigned long long tb;
    uint rnd;
    int rc;
    undefined2* sram;
    byte* sramEx;
    uint hi15;
    ushort i;
    short* fat;
    char* pad;
    ushort* hdr;
    short entry;
    char* padPtr;
    unsigned long long lcg;
    longlong step;
    unsigned long long stepHi;
    int* block;

    rc = sidev_mount(ch, (undefined4*)&block);
    if (-1 < rc) {
        hdr = (ushort*)block[0x20];
        memset_wrapper((int)hdr, 0xff, 0x2000);
        i = DAT_cc00206e;
        hdr[0x12] = fontEnc;
        sram = os_sram_lock();
        *(undefined4*)(hdr + 10) = *(undefined4*)(sram + 6);
        *(uint*)(hdr + 0xc) = (uint)*(byte*)(sram + 9);
        os_sram_commit(0);
        tb = os_read_time_base();
        sramEx = os_sram_lock_ex();
        padPtr = (char*)(sramEx + ch * 6);
        entry = 0;
        pad = padPtr;
        lcg = tb;
        while (entry <= 3) {
            rnd = (int)lcg * 0x41c64e6d;
            step = __sar2i((int)(lcg >> 0x20) * 0x41c64e6d +
                           (int)((lcg & 0xffffffff) * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)entry) = (char)step + *pad;
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 1)) = (char)step + padPtr[(short)(entry + 1)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 2)) = (char)step + padPtr[(short)(entry + 2)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 3)) = (char)step + padPtr[(short)(entry + 3)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 4)) = (char)step + padPtr[(short)(entry + 4)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 5)) = (char)step + padPtr[(short)(entry + 5)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 6)) = (char)step + padPtr[(short)(entry + 6)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            hi15 = (uint)stepHi & 0x7fff;
            rnd = hi15 * 0x41c64e6d;
            step = __sar2i((int)((unsigned long long)hi15 * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)(short)(entry + 7)) = (char)step + padPtr[(short)(entry + 7)];
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            lcg = (unsigned long long)((uint)stepHi & 0x7fff);
            pad = pad + 8;
            entry = entry + 8;
        }
        padPtr = padPtr + entry;
        while (entry <= 0xb) {
            rnd = (int)lcg * 0x41c64e6d;
            step = __sar2i((int)(lcg >> 0x20) * 0x41c64e6d +
                           (int)((lcg & 0xffffffff) * 0x41c64e6d >> 0x20) + (uint)(0xffffcfc6 < rnd),
                           rnd + 0x3039, 0x10);
            *(char*)((int)hdr + (int)entry) = (char)step + *padPtr;
            step = step * 0x41c64e6d + 0x3039;
            stepHi = __sar2i((int)((unsigned long long)step >> 0x20), (uint)step, 0x10);
            lcg = (unsigned long long)((uint)stepHi & 0x7fff);
            padPtr = padPtr + 1;
            entry = entry + 1;
        }
        os_sram_commit_from(0);
        *(uint*)(hdr + 0xe) = (uint)i;
        *(unsigned long long*)(hdr + 6) = tb;
        hdr[0x10] = 0;
        hdr[0x11] = *(ushort*)(block + 2);
        sidev_block_checksum(hdr, 0x1fc, (short*)(hdr + 0xfe), (short*)(hdr + 0xff));
        for (i = 0; (short)i < 2; i = i + 1) {
            hdr = (ushort*)(block[0x20] + ((short)i + 1) * 0x2000);
            memset_wrapper((int)hdr, 0xff, 0x2000);
            hdr[0xffd] = i;
            sidev_block_checksum(hdr, 0x1ffc, (short*)(hdr + 0xffe), (short*)(hdr + 0xfff));
        }
        for (entry = 0; entry < 2; entry = entry + 1) {
            fat = (short*)(block[0x20] + (entry + 3) * 0x2000);
            memset_wrapper((int)fat, 0, 0x2000);
            fat[2] = entry;
            fat[3] = *(short*)(block + 4) + -5;
            fat[4] = 4;
            sidev_block_checksum((ushort*)(fat + 2), 0x1ffc, fat, fat + 1);
        }
        if (doneCb == (undefined*)0x0) {
            doneCb = (undefined*)&DAT_803b1c68;
        }
        block[0x34] = (int)doneCb;
        os_dc_store_range(block[0x20], 0xa000);
        block[10] = 0;
        rc = sidev_erase_sector(ch, block[3] * block[10], (undefined*)sidev_format_write_next);
        if (rc < 0) {
            sidev_set_result_if_idle(block, rc);
        }
    }
    return rc;
}

// sidev_format_default (0x803b6f14): reformat the card using the system font
// encoding (queried from sidev_get_font_encoding) as the header locale stamp.
void sidev_format_default(int ch, undefined* doneCb)
{
    ushort fontEnc;

    fontEnc = sidev_get_font_encoding();
    sidev_format_card(ch, fontEnc, doneCb);
    return;
}

// sidev_direntry_name_match (0x803b6f5c): compare the 0x20-byte file name stored
// in a directory entry (entryBase +0x08) against a caller name. Returns 1 on an
// exact match (equal up to a NUL, or equal for all 0x20 bytes with the caller
// name also terminating), 0 otherwise.
undefined4 sidev_direntry_name_match(int entryBase, char* name)
{
    char stored;
    char want;
    char* p;
    int n;

    p = (char*)(entryBase + 8);
    n = 0x20;
    do {
        n = n + -1;
        if (n < 0) {
            if (*name == '\0') {
                return 1;
            }
            return 0;
        }
        stored = *p;
        p = p + 1;
        want = *name;
        name = name + 1;
        if (stored != want) {
            return 0;
        }
    } while (want != '\0');
    return 1;
}

// sidev_direntry_owner_match (0x803b6fc4): authenticate the caller against a
// directory entry's 6-byte owner key. The card's owner key is read from the
// mounted block (+0x10c). Returns -4 if the entry is free (name[0]==0xff), 0 if
// the card has no owner (DAT_8060e920 sentinel) or the entry's first 4 + next 2
// owner-key bytes match the card owner key, otherwise -10 (permission denied).
undefined4 sidev_direntry_owner_match(int block, char* entry)
{
    undefined4 rc;
    int cmp;
    undefined* ownerKey;

    ownerKey = *(undefined**)(block + 0x10c);
    if (*entry == -1) {
        rc = 0xfffffffc;
    } else if ((ownerKey == (undefined*)&DAT_8060e920) ||
               ((cmp = msl_memcmp((int)entry, (int)ownerKey, 4), cmp == 0 &&
                 (cmp = msl_memcmp((int)(entry + 4), (int)(ownerKey + 4), 2), cmp == 0)))) {
        rc = 0;
    } else {
        rc = 0xfffffff6;
    }
    return rc;
}

// sidev_direntry_owner_match_ex (0x803b7058): owner match with the shared /
// public permission-flag fallbacks. It runs the base owner match, then on a
// -10 (denied) result consults the entry's permission byte (+0x34, masked by
// DAT_806114b2): if the "shared" bit 0x20 is set and the entry's owner-id
// prefix matches the system shared-owner constant (-0x7f9f16e0 / -0x7f9f16dc)
// it grants access; if the "public" bit 0x40 is set and the maker-code prefix
// matches the shared constant while the game-code half matches the card owner,
// it also grants access. Returns 0 (allowed), -4 (free), or -10 (denied).
int sidev_direntry_owner_match_ex(int block, char* entry)
{
    byte flags;
    int cmp;
    undefined* ownerKey;
    int rc;

    ownerKey = *(undefined**)(block + 0x10c);
    if (*entry == -1) {
        rc = -4;
    } else if ((ownerKey == (undefined*)&DAT_8060e920) ||
               ((rc = msl_memcmp((int)entry, (int)ownerKey, 4), rc == 0 &&
                 (rc = msl_memcmp((int)(entry + 4), (int)(ownerKey + 4), 2), rc == 0)))) {
        rc = 0;
    } else {
        rc = -10;
    }
    if (rc == -10) {
        flags = entry[0x34] & DAT_806114b2;
        if ((((flags & 0x20) == 0) || (cmp = msl_memcmp((int)entry, -0x7f9f16e0, 4), cmp != 0)) ||
            (cmp = msl_memcmp((int)(entry + 4), -0x7f9f16dc, 2), cmp != 0)) {
            if ((((flags & 0x40) != 0) && (cmp = msl_memcmp((int)entry, -0x7f9f16e0, 4), cmp == 0)) &&
                (cmp = msl_memcmp((int)(entry + 4), (int)(ownerKey + 4), 2), cmp == 0)) {
                rc = 0;
            }
        } else {
            rc = 0;
        }
    }
    return rc;
}

// sidev_direntry_access_check (0x803b718c): full access gate for a directory
// entry. Runs the base owner match, applies the shared/public flag fallbacks
// (as in *_owner_match_ex), and finally, if still denied, grants access when the
// entry's "world" permission bit 0x04 is set. Returns 0 (allowed), -4 (free), or
// -10 (denied).
int sidev_direntry_access_check(int block, char* entry)
{
    byte flags;
    int cmp;
    int cmp2;
    int ownerKey;

    ownerKey = *(int*)(block + 0x10c);
    cmp = sidev_direntry_owner_match(block, entry);
    if (cmp == -10) {
        flags = entry[0x34] & DAT_806114b2;
        if ((((flags & 0x20) == 0) || (cmp2 = msl_memcmp((int)entry, -0x7f9f16e0, 4), cmp2 != 0)) ||
            (cmp2 = msl_memcmp((int)(entry + 4), -0x7f9f16dc, 2), cmp2 != 0)) {
            if ((((flags & 0x40) != 0) && (cmp2 = msl_memcmp((int)entry, -0x7f9f16e0, 4), cmp2 == 0)) &&
                (ownerKey = msl_memcmp((int)(entry + 4), ownerKey + 4, 2), ownerKey == 0)) {
                cmp = 0;
            }
        } else {
            cmp = 0;
        }
    }
    if ((cmp == -10) && ((entry[0x34] & 4U) != 0)) {
        cmp = 0;
    }
    return cmp;
}

// sidev_direntry_find_by_name (0x803b7280): scan the mounted directory for an
// entry whose name matches. Requires the card mounted (block[0] active, else
// -3). Walks all 0x7f entries of the directory block (base = geometry field
// +0x84, stride 0x40): for each non-free entry that passes the owner match it
// compares the 0x20-byte name; on a hit it writes the entry index to *outIndex
// and returns 0. Returns -4/-10 per-entry (skipped), or -0xc (-12) if no entry
// matched.
undefined4 sidev_direntry_find_by_name(int* block, char* name, int* outIndex)
{
    char stored;
    char want;
    bool match;
    int rc;
    undefined4 result;
    char* entry;
    char* p;
    char* np;
    undefined* ownerKey;
    int index;

    if (*block == 0) {
        result = 0xfffffffd;
    } else {
        entry = (char*)sidev_geom_field_84((int)block);
        index = 0;
        do {
            ownerKey = (undefined*)block[0x43];
            if (*entry == -1) {
                rc = -4;
            } else if ((ownerKey == (undefined*)&DAT_8060e920) ||
                       ((rc = msl_memcmp((int)entry, (int)ownerKey, 4), rc == 0 &&
                         (rc = msl_memcmp((int)(entry + 4), (int)(ownerKey + 4), 2), rc == 0)))) {
                rc = 0;
            } else {
                rc = -10;
            }
            if (-1 < rc) {
                p = entry + 8;
                rc = 0x20;
                np = name;
                do {
                    rc = rc + -1;
                    if (rc < 0) {
                        if (*np == '\0') {
                            match = true;
                        } else {
                            match = false;
                        }
                        goto decided;
                    }
                    stored = *p;
                    p = p + 1;
                    want = *np;
                    np = np + 1;
                    if (stored != want) {
                        match = false;
                        goto decided;
                    }
                } while (want != '\0');
                match = true;
decided:
                if (match) {
                    *outIndex = index;
                    return 0;
                }
            }
            index = index + 1;
            entry = entry + 0x40;
        } while (index < 0x7f);
        result = 0xfffffffc;
    }
    return result;
}

// sidev_open_by_index (0x803b73d0): open a file by its directory index into the
// caller's file handle (outHandle). Validates the index range [0,0x7e], mounts
// the card, resolves the directory entry (geometry field +0x84 + index*0x40),
// runs the access check (with the world-readable +0x34 bit fallback), and
// validates the entry's start block (+0x36) is within [5, total blocks). On
// success it fills the handle {channel, index, offset=0, startBlock}. Returns 0,
// -0x80 (bad index), -6 (bad entry / out of range), or a mount/access error.
int sidev_open_by_index(int ch, int index, int* outHandle)
{
    int rc;
    char* entry;
    int* block;

    if ((index < 0) || (0x7e < index)) {
        rc = -0x80;
    } else {
        *outHandle = -1;
        rc = sidev_mount(ch, (undefined4*)&block);
        if (-1 < rc) {
            rc = sidev_geom_field_84((int)block);
            entry = (char*)(rc + index * 0x40);
            rc = sidev_direntry_owner_match_ex((int)block, entry);
            if ((rc == -10) && ((entry[0x34] & 4U) != 0)) {
                rc = 0;
            }
            if (-1 < rc) {
                if ((*(ushort*)(entry + 0x36) < 5) ||
                    (*(ushort*)(block + 4) <= *(ushort*)(entry + 0x36))) {
                    rc = -6;
                } else {
                    *outHandle = ch;
                    outHandle[1] = index;
                    outHandle[2] = 0;
                    *(undefined2*)(outHandle + 4) = *(undefined2*)(entry + 0x36);
                }
            }
            rc = sidev_set_result_if_idle(block, rc);
        }
    }
    return rc;
}

// ============================================================================
// batch_1148: SI-device (memory-card) file open/create + read/write I/O engine.
// Appended to src/input/si_device.cpp, continuing the batch_1141..1147 driver
// from 0x803b73d0.
//
// This is the public file API layered on the mount/directory code above:
//   sidev_open_by_name   - resolve a file by name into a handle
//   sidev_close          - release a handle
//   sidev_create         - create a new file (find a free dir slot, allocate a
//                          FAT chain sized to the request, stamp the dir entry)
//   sidev_create_alloc_complete - the FAT-alloc completion cb that fills the new
//                          directory entry (owner key, permission byte, start
//                          block, creation time) and writes the directory back
//   sidev_seek           - position a handle at a byte offset by walking the FAT
//   sidev_read_file / sidev_read_continue - chunked file read, block by block
//   sidev_write_dir_complete / sidev_write_block_next - chunked file write with
//                          per-block erase+write and a final dir-entry time stamp
//
// A file handle is 5 words: {channel, dirIndex, byteOffset, byteLength,
// currentBlock(short)}. The mounted block carries the geometry (block[3] =
// bytes/block), the directory (geom field +0x84), and the FAT (geom field +0x88).
// Each directory entry is 0x40 bytes: owner key +0x00, name +0x08, perm byte
// +0x34, start block +0x36, block count +0x38, creation time +0x28.
//
// NON-MATCHING: the flat DAT_8060e7xx block-offset arithmetic, the __div2i /
// __sar2i 64-bit intrinsics, and the clz-style success tests are preserved
// verbatim. Per-file externs are declared for the raw symbols this batch newly
// references; msl_strlen/__div2i and (from batch_1147) msl_memcmp/os_read_time_base
// resolve through the shared headers / earlier externs.

// ---- batch_1148 per-file externs ------------------------------------------
extern uint msl_strlen(int s);                       // MSL string length
extern int  msl_strncpy(int dst, int src, int n);   // MSL bounded string copy
extern u64  __div2i(u32 hiDividend, u32 loDividend, u32 hiDivisor, u32 loDivisor); // Ghidra 64-bit divide
extern int  DAT_8060e7bc;   // [+0xbc] pending new-file directory-slot index (short)
extern int  DAT_8060e7c0;   // [+0xc0] active file-handle pointer for the current op

// forward decls (defined later in this batch)
void sidev_create_alloc_complete(int ch, int rc);
void sidev_read_continue(int ch, int rc);
void sidev_write_dir_complete(int ch, int rc);
void sidev_write_block_next(int ch, int rc);

// sidev_open_by_name (0x803b74d4): resolve a file by name into the caller handle
// (outHandle). Mounts the card, scans the directory (0x7f entries of 0x40,
// base = geometry field +0x84): for each entry that passes the owner match it
// compares the name; the first hit's index is used to validate the start block
// (+0x36 within [5, total blocks)) and fill the handle {ch, index, offset=0,
// startBlock}. Sets *outHandle = -1 up front. Latches -3 (unmounted), -4 (not
// found), -6 (bad start block) via sidev_set_result_if_idle.
void sidev_open_by_name(int ch, char* name, int* outHandle)
{
    ushort startBlk;
    int* block;
    int rc;
    char* entry;
    int hitIndex;
    int index;

    *outHandle = -1;
    rc = sidev_mount(ch, (undefined4*)&block);
    hitIndex = 0;
    if (-1 < rc) {
        if (*block == 0) {
            rc = -3;
        } else {
            entry = (char*)sidev_geom_field_84((int)block);
            index = 0;
            do {
                rc = sidev_direntry_owner_match((int)block, entry);
                if ((-1 < rc) && (rc = sidev_direntry_name_match((int)entry, name), rc != 0)) {
                    rc = 0;
                    hitIndex = index;
                    goto resolved;
                }
                index = index + 1;
                entry = entry + 0x40;
            } while (index < 0x7f);
            rc = -4;
        }
resolved:
        if (-1 < rc) {
            rc = sidev_geom_field_84((int)block);
            rc = rc + hitIndex * 0x40;
            startBlk = *(ushort*)(rc + 0x36);
            if ((startBlk < 5) || (*(ushort*)(block + 4) <= startBlk)) {
                rc = -6;
            } else {
                *outHandle = ch;
                outHandle[1] = hitIndex;
                outHandle[2] = 0;
                *(undefined2*)(outHandle + 4) = *(undefined2*)(rc + 0x36);
            }
        }
        sidev_set_result_if_idle(block, rc);
    }
    return;
}

// sidev_close (0x803b75f0): release a file handle. Mounts the card (channel is
// handle[0]); on success invalidates the handle (handle[0] = -1) and latches the
// idle result (0).
void sidev_close(int* handle)
{
    int rc;
    int* block;

    rc = sidev_mount(*handle, (undefined4*)&block);
    if (-1 < rc) {
        *handle = -1;
        sidev_set_result_if_idle(block, 0);
    }
    return;
}

// sidev_stub_ok (0x803b7644): trivial stub, always returns 0.
undefined4 sidev_stub_ok(void)
{
    return 0;
}

// sidev_create_alloc_complete (0x803b764c): FAT-allocation completion callback
// for sidev_create. On success it fills the freshly-reserved directory entry
// (geom field +0x84 + pending-slot index +0xbc): copies the caller's owner key
// (+0x00 / +0x04 from the create context +0xc0), sets the permission byte 4,
// the start block (+0x36 = the alloc head +0xbe), zeroes the copy-protect and
// icon/banner fields, stamps the file-length and creation time (time base /
// (busclock>>2)), mirrors the start block into the caller handle, and writes the
// directory back via sidev_dir_writeback. On error latches the result and fires
// the user callback (+0xd0).
void sidev_create_alloc_complete(int ch, int rc)
{
    uint entry;
    int geom;
    int base;
    void (*cb)(int, int);
    u64 tb;
    uint hz;

    base = ch * 0x110;
    cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
    *(undefined4*)(&DAT_8060e7d0 + base) = 0;
    if (-1 < rc) {
        geom = sidev_geom_field_84((int)(&DAT_8060e700 + ch * 0x44));
        entry = geom + (uint)*(ushort*)(&DAT_8060e7bc + base) * 0x40;
        leaf_31e8(entry, (&DAT_8060e80c)[ch * 0x44], 4);
        leaf_31e8(entry + 4, (&DAT_8060e80c)[ch * 0x44] + 4, 2);
        *(undefined1*)(entry + 0x34) = 4;
        *(undefined1*)(entry + 0x35) = 0;
        *(undefined2*)(entry + 0x36) = *(undefined2*)(&DAT_8060e7be + base);
        *(undefined1*)(entry + 7) = 0;
        *(undefined4*)(entry + 0x2c) = 0xffffffff;
        *(undefined2*)(entry + 0x30) = 0;
        *(undefined2*)(entry + 0x32) = 0;
        *(undefined4*)(entry + 0x3c) = 0xffffffff;
        *(ushort*)(entry + 0x32) = *(ushort*)(entry + 0x32) & 0xfffc | 1;
        *(undefined4*)(*(int*)(&DAT_8060e7c0 + base) + 8) = 0;
        *(undefined2*)(*(int*)(&DAT_8060e7c0 + base) + 0x10) = *(undefined2*)(entry + 0x36);
        hz = DAT_800000f8 >> 2;
        tb = os_read_time_base();
        tb = __div2i((uint)((u64)tb >> 0x20), (uint)tb, 0, hz);
        *(int*)(entry + 0x28) = (int)tb;
        rc = sidev_dir_writeback(ch, (undefined4)cb);
        if (-1 < rc) {
            return;
        }
    }
    sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    if (cb != (void(*)(int, int))0x0) {
        (*cb)(ch, rc);
    }
    return;
}

// sidev_create (0x803b777c): create a new file of nbytes. Validates the name
// length (<= 0x20), mounts, requires nbytes a non-zero multiple of the block
// size (block[3]); scans the directory once, rejecting a duplicate name (-7) and
// recording the first free slot; if no free slot (-8), if the free-block count
// (geom field +0x88 word[3]) can't hold the request (-9), else it stamps the new
// slot's name and block-count, records the caller handle {ch, index}, and kicks
// sidev_fat_alloc for the chain with sidev_create_alloc_complete as the
// completion. Returns 0/queued or a negative error (-0xc name too long, -0x80
// bad size).
int sidev_create(int ch, char* name, uint nbytes, int* outHandle, undefined* doneCb)
{
    uint namelen;
    int rc;
    char* dir;
    ushort entryIdx;
    char* entry;
    ushort freeSlot;
    int* block;

    namelen = msl_strlen((int)name);
    if (namelen < 0x21) {
        rc = sidev_mount(ch, (undefined4*)&block);
        if (-1 < rc) {
            if ((nbytes == 0) || (nbytes != (nbytes / (uint)block[3]) * block[3])) {
                rc = -0x80;
            } else {
                freeSlot = 0xffff;
                dir = (char*)sidev_geom_field_84((int)block);
                entry = dir;
                for (entryIdx = 0; entryIdx < 0x7f; entryIdx = entryIdx + 1) {
                    if (*entry == -1) {
                        if (freeSlot == 0xffff) {
                            freeSlot = entryIdx;
                        }
                    } else {
                        rc = msl_memcmp((int)entry, block[0x43], 4);
                        if (((rc == 0) &&
                             (rc = msl_memcmp((int)(entry + 4), block[0x43] + 4, 2), rc == 0)) &&
                            (rc = sidev_direntry_name_match((int)entry, name), rc != 0)) {
                            rc = sidev_set_result_if_idle(block, -7);
                            return rc;
                        }
                    }
                    entry = entry + 0x40;
                }
                if (freeSlot == 0xffff) {
                    rc = sidev_set_result_if_idle(block, -8);
                } else {
                    rc = sidev_geom_field_88((int)block);
                    if (block[3] * (uint)*(ushort*)(rc + 6) < nbytes) {
                        rc = sidev_set_result_if_idle(block, -9);
                    } else {
                        if (doneCb == (undefined*)0x0) {
                            doneCb = (undefined*)&DAT_803b1c68;
                        }
                        block[0x34] = (int)doneCb;
                        *(ushort*)(block + 0x2f) = freeSlot;
                        *(short*)(dir + (uint)freeSlot * 0x40 + 0x38) = (short)(nbytes / (uint)block[3]);
                        msl_strncpy((int)(dir + (uint)freeSlot * 0x40 + 8), (int)name, 0x20);
                        block[0x30] = (int)outHandle;
                        *outHandle = ch;
                        outHandle[1] = (uint)freeSlot;
                        rc = sidev_fat_alloc(ch, nbytes / (uint)block[3], (undefined4)sidev_create_alloc_complete);
                        if (rc < 0) {
                            rc = sidev_set_result_if_idle(block, rc);
                        }
                    }
                }
            }
        }
    } else {
        rc = -0xc;
    }
    return rc;
}

// sidev_seek (0x803b799c): position the handle (handle[0]=ch, [1]=dirIndex,
// [2]=byteOffset, [4]=currentBlock) at byte offset `offset` for a transfer of
// `len` bytes. Mounts, validates the current block and offset ranges (bad ->
// -0x80), then bounds-checks offset+len against the file length (dir entry +0x38
// * block size); if the requested offset is before the cached position it
// restarts from the file's start block (+0x36). It walks the FAT (geom field
// +0x88) block by block until the block-aligned offset is reached, updating the
// handle position, and returns the mounted block ptr via *outBlock. Returns 0,
// or -6 (chain broken) / -0xb (out of range) latched.
int sidev_seek(int* handle, int len, uint offset, undefined4* outBlock)
{
    int rc;
    int fileLen;
    int* block;

    rc = sidev_mount(*handle, (undefined4*)&block);
    if (-1 < rc) {
        if (((*(ushort*)(handle + 4) < 5) ||
             ((uint)*(ushort*)(block + 4) <= (uint)*(ushort*)(handle + 4))) ||
            ((int)((uint)*(ushort*)(block + 4) * block[3]) <= handle[2])) {
            rc = sidev_set_result_if_idle(block, -0x80);
        } else {
            rc = sidev_geom_field_84((int)block);
            rc = rc + handle[1] * 0x40;
            fileLen = (uint)*(ushort*)(rc + 0x38) * block[3];
            if (((int)offset < fileLen) && ((int)(offset + len) <= fileLen)) {
                block[0x30] = (int)handle;
                handle[3] = len;
                if ((int)offset < handle[2]) {
                    handle[2] = 0;
                    *(undefined2*)(handle + 4) = *(undefined2*)(rc + 0x36);
                    if ((*(ushort*)(handle + 4) < 5) ||
                        (*(ushort*)(block + 4) <= *(ushort*)(handle + 4))) {
                        rc = sidev_set_result_if_idle(block, -6);
                        return rc;
                    }
                }
                rc = sidev_geom_field_88((int)block);
                do {
                    if ((offset & ~(block[3] - 1U)) <= (uint)handle[2]) {
                        handle[2] = offset;
                        *outBlock = (undefined4)block;
                        return 0;
                    }
                    handle[2] = handle[2] + block[3];
                    *(undefined2*)(handle + 4) = *(undefined2*)(rc + (uint)*(ushort*)(handle + 4) * 2);
                } while ((4 < *(ushort*)(handle + 4)) &&
                         (*(ushort*)(handle + 4) < *(ushort*)(block + 4)));
                rc = sidev_set_result_if_idle(block, -6);
            } else {
                rc = sidev_set_result_if_idle(block, -0xb);
            }
        }
    }
    return rc;
}

// sidev_read_continue (0x803b7b54): per-block read continuation callback. On a
// successful sub-read it decrements the remaining byte count in the active
// handle (+0xc, base DAT_8060e7c0), and if bytes remain it advances the file
// cursor (+8), follows the FAT (geom field +0x88) to the next block (+0x10),
// validates it (bad -> -6), then reads the next min(blockSize, remaining) bytes
// into the transfer buffer (+0xb4) chaining itself. On completion or error it
// clears the stored callback (+0xd0), latches the result, and fires the user
// callback.
void sidev_read_continue(int ch, int rc)
{
    uint blockSize;
    int geom;
    uint nextBlk;
    int base;
    uint chunk;
    int handle;
    void (*cb)(int, int);
    int step;

    base = ch * 0x110;
    if (-1 < rc) {
        handle = *(int*)(&DAT_8060e7c0 + base);
        if (*(int*)(handle + 0xc) < 0) {
            rc = -0xe;
        } else {
            step = (~(*(int*)(&DAT_8060e70c + base) - 1U) &
                    *(int*)(handle + 8) + *(int*)(&DAT_8060e70c + base)) - *(int*)(handle + 8);
            *(int*)(handle + 0xc) = *(int*)(handle + 0xc) - step;
            if (0 < *(int*)(handle + 0xc)) {
                geom = sidev_geom_field_88((int)(&DAT_8060e700 + ch * 0x44));
                *(int*)(handle + 8) = *(int*)(handle + 8) + step;
                *(undefined2*)(handle + 0x10) = *(undefined2*)(geom + (uint)*(ushort*)(handle + 0x10) * 2);
                nextBlk = (uint)*(ushort*)(handle + 0x10);
                if ((nextBlk < 5) || (*(ushort*)(&DAT_8060e710 + base) <= nextBlk)) {
                    rc = -6;
                } else {
                    blockSize = *(uint*)(&DAT_8060e70c + base);
                    chunk = blockSize;
                    if ((int)*(uint*)(handle + 0xc) < (int)blockSize) {
                        chunk = *(uint*)(handle + 0xc);
                    }
                    rc = sidev_read(ch, blockSize * nextBlk, chunk, *(undefined4*)(&DAT_8060e7b4 + base),
                                    (undefined4)sidev_read_continue);
                    if (-1 < rc) {
                        return;
                    }
                }
            }
        }
    }
    cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
    *(undefined4*)(&DAT_8060e7d0 + base) = 0;
    sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
    (*cb)(ch, rc);
    return;
}

// sidev_read_file (0x803b7c84): public read of `len` bytes from a handle at byte
// `offset` into `buf`. Requires offset and len block-aligned (0x1ff mask, else
// -0x80). Seeks the handle (sidev_seek), access-checks the target directory
// entry, invalidates the destination cache range, records the done callback, and
// kicks the first block read (min(blockSize - intra-block offset, len)) chaining
// sidev_read_continue. Returns 0/queued or a negative error.
int sidev_read_file(int* handle, uint buf, uint offset, uint len, undefined* doneCb)
{
    int rc;
    uint chunk;
    uint intra;
    int* block;

    if (((len & 0x1ff) == 0) && ((offset & 0x1ff) == 0)) {
        rc = sidev_seek(handle, offset, len, (undefined4*)&block);
        if (-1 < rc) {
            rc = sidev_geom_field_84((int)block);
            rc = sidev_direntry_access_check((int)block, (char*)(rc + handle[1] * 0x40));
            if (rc < 0) {
                rc = sidev_set_result_if_idle(block, rc);
            } else {
                os_dc_invalidate_range(buf, offset);
                if (doneCb == (undefined*)0x0) {
                    doneCb = (undefined*)&DAT_803b1c68;
                }
                block[0x34] = (int)doneCb;
                rc = block[3];
                intra = handle[2] & rc - 1U;
                chunk = rc - intra;
                if ((int)offset < (int)chunk) {
                    chunk = offset;
                }
                rc = sidev_read(*handle, intra + rc * (uint)*(ushort*)(handle + 4), chunk, buf,
                                (undefined4)sidev_read_continue);
                if (rc < 0) {
                    sidev_set_result_if_idle(block, rc);
                }
            }
        }
    } else {
        rc = -0x80;
    }
    return rc;
}

// sidev_write_dir_complete (0x803b7dc8): per-block write continuation. On a good
// sub-write it decrements the remaining count (+0xc, base DAT_8060e7c0); when the
// file body is fully written (count <= 0) it stamps the directory entry's
// modification time (geom field +0x84 + entry*0x40 + 0x28 = time base / (bus>>2))
// and writes the directory back via sidev_dir_writeback; otherwise it advances
// the file cursor (+8), follows the FAT to the next block (+0x10), validates it
// (bad -> -6), and erases that sector chaining sidev_write_block_next. On error
// clears the callback, latches, and fires the user callback.
void sidev_write_dir_complete(int ch, int rc)
{
    int geom;
    undefined4 cb2;
    uint hz;
    int base;
    int handle;
    void (*cb)(int, int);
    int* blockPtr;
    u64 tb;

    base = ch * 0x110;
    blockPtr = &DAT_8060e700 + ch * 0x44;
    if (-1 < rc) {
        handle = *(int*)(&DAT_8060e7c0 + base);
        if (*(int*)(handle + 0xc) < 0) {
            rc = -0xe;
        } else {
            *(int*)(handle + 0xc) = *(int*)(handle + 0xc) - *(int*)(&DAT_8060e70c + base);
            if (*(int*)(handle + 0xc) < 1) {
                geom = sidev_geom_field_84((int)blockPtr);
                handle = *(int*)(handle + 4);
                hz = DAT_800000f8 >> 2;
                tb = os_read_time_base();
                tb = __div2i((uint)((u64)tb >> 0x20), (uint)tb, 0, hz);
                *(int*)(geom + handle * 0x40 + 0x28) = (int)tb;
                cb2 = *(undefined4*)(&DAT_8060e7d0 + base);
                *(undefined4*)(&DAT_8060e7d0 + base) = 0;
                rc = sidev_dir_writeback(ch, cb2);
            } else {
                geom = sidev_geom_field_88((int)blockPtr);
                *(int*)(handle + 8) = *(int*)(handle + 8) + *(int*)(&DAT_8060e70c + base);
                *(undefined2*)(handle + 0x10) = *(undefined2*)(geom + (uint)*(ushort*)(handle + 0x10) * 2);
                hz = (uint)*(ushort*)(handle + 0x10);
                if ((hz < 5) || (*(ushort*)(&DAT_8060e710 + base) <= hz)) {
                    rc = -6;
                    goto done;
                }
                rc = sidev_erase_sector(ch, *(int*)(&DAT_8060e70c + base) * hz, (undefined*)sidev_write_block_next);
            }
            if (-1 < rc) {
                return;
            }
        }
    }
done:
    cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
    *(undefined4*)(&DAT_8060e7d0 + base) = 0;
    sidev_set_result_if_idle(blockPtr, rc);
    (*cb)(ch, rc);
    return;
}

// sidev_write_block_next (0x803b7f38): after erasing a data sector, write the
// current transfer buffer (+0xb4) out to the freshly-erased block (device timing
// word * the active handle's current block +0x10), chaining sidev_write_dir_complete.
// On any error it clears the callback (+0xd0), latches the result, and fires the
// user callback.
void sidev_write_block_next(int ch, int rc)
{
    int base;
    void (*cb)(int, int);

    base = ch * 0x110;
    if ((rc < 0) ||
        (rc = sidev_write(ch, *(int*)(&DAT_8060e70c + base) *
                              (uint)*(ushort*)(*(int*)(&DAT_8060e7c0 + base) + 0x10),
                          *(int*)(&DAT_8060e70c + base), *(undefined4*)(&DAT_8060e7b4 + base),
                          (undefined4)sidev_write_dir_complete), rc < 0)) {
        cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
        *(undefined4*)(&DAT_8060e7d0 + base) = 0;
        sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc);
        (*cb)(ch, rc);
    }
    return;
}

// ============================================================================
// batch_1149: SI-device (memory-card) file write + delete + directory-info
// (stat / setinfo) + rename + serial API. Appended to src/input/si_device.cpp,
// continuing the batch_1141..1148 driver from 0x803b7f38.
//
//   sidev_write_file        - public write of len bytes to an open handle
//   sidev_delete_free_complete / sidev_delete_by_index / sidev_delete_by_name
//                           - delete a file: blank the dir entry, write it back,
//                             then free its FAT chain
//   sidev_layout_banner_icon - derive the banner + icon image/animation data
//                             offsets from an entry's banner/icon-format flags
//   sidev_get_dir_entry     - stat: copy a directory entry into a caller info blob
//   sidev_set_dir_entry / sidev_set_dir_entry_sync
//                           - setinfo: apply an info blob back to the dir entry,
//                             restamp the modification time, and write it back
//   sidev_rename            - rename a file (find old + reject a dup new name)
//   sidev_get_serial        - fold the header block into a 64-bit card serial
//
// A directory entry (0x40 bytes) carries: owner key +0x00, banner-format byte
// +0x07, name +0x08, creation time +0x28, copy-protect word +0x2c, banner/icon
// mode shorts +0x30/+0x32, start block +0x36, block count +0x38, comment offset
// +0x3c. The banner/icon layout writes computed data offsets into the caller
// info blob (+0x3c banner, +0x40 icon-0, +0x44.. per-frame icons, +0x64 anim).
//
// NON-MATCHING: the flat entry-offset arithmetic, the __div2i/__sar2i 64-bit
// intrinsics, and the clz-style success tests are preserved verbatim. All raw
// symbols resolve through externs/definitions already present in this file.

// sidev_write_file (0x803b7fe8): public write of len bytes from buf to an open
// handle at byte offset. Seeks the handle, requires offset and len block-aligned
// to the card block size (else -0x80), owner-match-checks the target directory
// entry, flushes the source cache range, records the done callback and source
// pointer (+0x2d), and kicks the first sector erase chaining sidev_write_block_next.
int sidev_write_file(int* handle, uint buf, uint offset, uint len, undefined* doneCb)
{
    int rc;
    int* block;

    rc = sidev_seek(handle, offset, len, (undefined4*)&block);
    if (-1 < rc) {
        if (((len & block[3] - 1U) == 0) && ((offset & block[3] - 1U) == 0)) {
            rc = sidev_geom_field_84((int)block);
            rc = sidev_direntry_owner_match_ex((int)block, (char*)(rc + handle[1] * 0x40));
            if (rc < 0) {
                rc = sidev_set_result_if_idle(block, rc);
            } else {
                os_dc_store_range(buf, offset);
                if (doneCb == (undefined*)0x0) {
                    doneCb = (undefined*)&DAT_803b1c68;
                }
                block[0x34] = (int)doneCb;
                block[0x2d] = buf;
                rc = sidev_erase_sector(*handle, block[3] * (uint)*(ushort*)(handle + 4),
                                        (undefined*)sidev_write_block_next);
                if (rc < 0) {
                    sidev_set_result_if_idle(block, rc);
                }
            }
        } else {
            rc = sidev_set_result_if_idle(block, -0x80);
        }
    }
    return rc;
}

// sidev_delete_free_complete (0x803b80fc): completion callback for the delete
// directory write-back. Clears the stored callback (+0xd0), then frees the file's
// FAT chain starting at the saved head block (+0xbe). On error (or if the free
// fails) it latches the result and fires the user callback.
void sidev_delete_free_complete(int ch, int rc)
{
    int base;
    void (*cb)(int, int);

    base = ch * 0x110;
    cb = *(void(**)(int, int))(&DAT_8060e7d0 + base);
    *(undefined4*)(&DAT_8060e7d0 + base) = 0;
    if (((rc < 0) ||
         (rc = sidev_fat_free(ch, *(ushort*)(&DAT_8060e7be + base), (undefined4)cb), rc < 0)) &&
        (sidev_set_result_if_idle((int*)(&DAT_8060e700 + ch * 0x44), rc), cb != (void(*)(int, int))0x0)) {
        (*cb)(ch, rc);
    }
    return;
}

// sidev_delete_by_index (0x803b81a0): delete a file by directory index. Validates
// the index [0,0x7e], mounts, owner-match-checks the entry, saves the file's start
// block into the FAT-free head (+0xbe), blanks the 0x40-byte entry to 0xff, records
// the done callback, and writes the directory back chaining sidev_delete_free_complete
// which frees the FAT chain. Returns 0/queued or a negative error.
int sidev_delete_by_index(int ch, int index, undefined* doneCb)
{
    int rc;
    char* entry;
    int* block;

    if ((index < 0) || (0x7e < index)) {
        rc = -0x80;
    } else {
        rc = sidev_mount(ch, (undefined4*)&block);
        if (-1 < rc) {
            rc = sidev_geom_field_84((int)block);
            entry = (char*)(rc + index * 0x40);
            rc = sidev_direntry_owner_match_ex((int)block, entry);
            if (rc < 0) {
                rc = sidev_set_result_if_idle(block, rc);
            } else {
                rc = sidev_stub_ok();
                if (rc == 0) {
                    *(undefined2*)((int)block + 0xbe) = *(undefined2*)(entry + 0x36);
                    memset_wrapper((int)entry, 0xff, 0x40);
                    if (doneCb == (undefined*)0x0) {
                        doneCb = (undefined*)&DAT_803b1c68;
                    }
                    block[0x34] = (int)doneCb;
                    rc = sidev_dir_writeback(ch, (undefined4)sidev_delete_free_complete);
                    if (rc < 0) {
                        sidev_set_result_if_idle(block, rc);
                    }
                } else {
                    rc = sidev_set_result_if_idle(block, -1);
                }
            }
        }
    }
    return rc;
}

// sidev_delete_by_name (0x803b82cc): delete a file by name. Mounts, finds the
// directory entry by name, saves its start block into the FAT-free head (+0xbe),
// blanks the entry, records the done callback, and writes the directory back
// chaining sidev_delete_free_complete. Returns 0/queued or a negative error.
int sidev_delete_by_name(int ch, char* name, undefined* doneCb)
{
    int rc;
    int index;
    int* block;

    rc = sidev_mount(ch, (undefined4*)&block);
    if (-1 < rc) {
        rc = sidev_direntry_find_by_name(block, name, &index);
        if (rc < 0) {
            rc = sidev_set_result_if_idle(block, rc);
        } else {
            rc = sidev_stub_ok();
            if (rc == 0) {
                rc = sidev_geom_field_84((int)block);
                rc = rc + index * 0x40;
                *(undefined2*)((int)block + 0xbe) = *(undefined2*)(rc + 0x36);
                memset_wrapper(rc, 0xff, 0x40);
                if (doneCb == (undefined*)0x0) {
                    doneCb = (undefined*)&DAT_803b1c68;
                }
                block[0x34] = (int)doneCb;
                rc = sidev_dir_writeback(ch, (undefined4)sidev_delete_free_complete);
                if (rc < 0) {
                    sidev_set_result_if_idle(block, rc);
                }
            } else {
                rc = sidev_set_result_if_idle(block, -1);
            }
        }
    }
    return rc;
}

// sidev_layout_banner_icon (0x803b83dc): derive the banner and icon data offsets
// for a directory entry, writing them into the caller info blob. Reads the entry's
// banner-format field (+0x2c/-1 sentinel and low 2 bits of +0x07) and the per-icon
// 2-bit mode field (+0x30): mode 2 = a full RGB5A3 banner/icon (0x1800 / 0x800
// bytes), mode 1 = a CI8 paletted banner/icon (0xc00+0xe00 / 0x400 bytes, flags a
// shared palette). It packs eight icon-frame offsets (info +0x44..+0x50 in two
// rows of 0x10), a shared palette offset (+0x64) if any CI icon was seen, and the
// running end offset (+0x68). Unused slots get the 0xffffffff sentinel.
void sidev_layout_banner_icon(int entry, int info)
{
    bool sharedPalette;
    uint mode;
    byte bannerFmt;
    uint bit;
    int off2;
    int cursor;
    int row;
    int frames;

    cursor = *(int*)(entry + 0x2c);
    if (cursor == -1) {
        *(undefined1*)(info + 0x2e) = 0;
        cursor = 0;
        *(undefined2*)(info + 0x34) = 0;
        *(undefined2*)(info + 0x36) = 0;
    }
    sharedPalette = false;
    bannerFmt = *(byte*)(entry + 7) & 3;
    if (bannerFmt == 2) {
        *(int*)(info + 0x3c) = cursor;
        cursor = cursor + 0x1800;
        *(undefined4*)(info + 0x40) = 0xffffffff;
    } else if ((bannerFmt < 2) && ((*(byte*)(entry + 7) & 3) != 0)) {
        *(int*)(info + 0x3c) = cursor;
        off2 = cursor + 0xc00;
        cursor = cursor + 0xe00;
        *(int*)(info + 0x40) = off2;
    } else {
        *(undefined4*)(info + 0x3c) = 0xffffffff;
        *(undefined4*)(info + 0x40) = 0xffffffff;
    }
    frames = 2;
    bit = 0;
    row = info;
    do {
        mode = (int)(uint)*(ushort*)(entry + 0x30) >> (bit & 0x3f) & 3;
        if (mode == 2) {
            *(int*)(row + 0x44) = cursor;
            cursor = cursor + 0x800;
        } else if ((mode < 2) && (mode != 0)) {
            *(int*)(row + 0x44) = cursor;
            sharedPalette = true;
            cursor = cursor + 0x400;
        } else {
            *(undefined4*)(row + 0x44) = 0xffffffff;
        }
        mode = (int)(uint)*(ushort*)(entry + 0x30) >> (bit + 2 & 0x3f) & 3;
        if (mode == 2) {
            *(int*)(row + 0x48) = cursor;
            cursor = cursor + 0x800;
        } else if ((mode < 2) && (mode != 0)) {
            *(int*)(row + 0x48) = cursor;
            sharedPalette = true;
            cursor = cursor + 0x400;
        } else {
            *(undefined4*)(row + 0x48) = 0xffffffff;
        }
        mode = (int)(uint)*(ushort*)(entry + 0x30) >> (bit + 4 & 0x3f) & 3;
        if (mode == 2) {
            *(int*)(row + 0x4c) = cursor;
            cursor = cursor + 0x800;
        } else if ((mode < 2) && (mode != 0)) {
            *(int*)(row + 0x4c) = cursor;
            sharedPalette = true;
            cursor = cursor + 0x400;
        } else {
            *(undefined4*)(row + 0x4c) = 0xffffffff;
        }
        mode = (int)(uint)*(ushort*)(entry + 0x30) >> (bit + 6 & 0x3f) & 3;
        if (mode == 2) {
            *(int*)(row + 0x50) = cursor;
            cursor = cursor + 0x800;
        } else if ((mode < 2) && (mode != 0)) {
            *(int*)(row + 0x50) = cursor;
            sharedPalette = true;
            cursor = cursor + 0x400;
        } else {
            *(undefined4*)(row + 0x50) = 0xffffffff;
        }
        bit = bit + 8;
        row = row + 0x10;
        frames = frames + -1;
    } while (frames != 0);
    if (sharedPalette) {
        *(int*)(info + 100) = cursor;
        cursor = cursor + 0x200;
    } else {
        *(undefined4*)(info + 100) = 0xffffffff;
    }
    *(int*)(info + 0x68) = cursor;
    return;
}

// sidev_get_dir_entry (0x803b85d4): stat a file by directory index into the caller
// info blob. Validates the index [0,0x7e], mounts, access-checks the entry, then
// copies out: owner key (+0x28/+0x2c of the blob), file size (block count +0x38 *
// block size -> +0x20), name (+0x00), creation time (+0x24), banner byte (+0x2e),
// copy-protect word (+0x30), banner/icon mode shorts (+0x34/+0x36), comment offset
// (+0x38), and computes the banner/icon data offsets via sidev_layout_banner_icon.
int sidev_get_dir_entry(int ch, int index, uint info)
{
    int rc;
    char* entry;
    int* block;

    if ((index < 0) || (0x7e < index)) {
        rc = -0x80;
    } else {
        rc = sidev_mount(ch, (undefined4*)&block);
        if (-1 < rc) {
            rc = sidev_geom_field_84((int)block);
            entry = (char*)(rc + index * 0x40);
            rc = sidev_direntry_access_check((int)block, entry);
            if (-1 < rc) {
                leaf_31e8(info + 0x28, (uint)entry, 4);
                leaf_31e8(info + 0x2c, (uint)(entry + 4), 2);
                *(uint*)(info + 0x20) = (uint)*(ushort*)(entry + 0x38) * block[3];
                leaf_31e8(info, (uint)(entry + 8), 0x20);
                *(undefined4*)(info + 0x24) = *(undefined4*)(entry + 0x28);
                *(char*)(info + 0x2e) = entry[7];
                *(undefined4*)(info + 0x30) = *(undefined4*)(entry + 0x2c);
                *(undefined2*)(info + 0x34) = *(undefined2*)(entry + 0x30);
                *(undefined2*)(info + 0x36) = *(undefined2*)(entry + 0x32);
                *(undefined4*)(info + 0x38) = *(undefined4*)(entry + 0x3c);
                sidev_layout_banner_icon((int)entry, info);
            }
            rc = sidev_set_result_if_idle(block, rc);
        }
    }
    return rc;
}

// sidev_set_dir_entry (0x803b86e8): apply an info blob back to a directory entry
// and write the directory back with the given completion callback. Validates the
// index and the copy-protect / comment fields (must be -1 or in range, else
// -0x80), mounts, owner-match-checks the entry, then writes back the banner byte
// (+0x07), copy-protect word (+0x2c), banner/icon mode shorts (+0x30/+0x32),
// comment offset (+0x3c), recomputes the banner/icon layout, forces the copy-mode
// bit when uncopyable, restamps the creation time, and writes the directory back.
int sidev_set_dir_entry(int ch, int index, int info, undefined4 doneCb)
{
    uint hz;
    int rc;
    char* entry;
    u64 tb;
    int* block;

    if ((((index < 0) || (0x7e < index)) ||
         ((*(uint*)(info + 0x30) != 0xffffffff && (0x1ff < *(uint*)(info + 0x30))))) ||
        ((*(uint*)(info + 0x38) != 0xffffffff && (0x1fc0 < (*(uint*)(info + 0x38) & 0x1fff))))) {
        rc = -0x80;
    } else {
        rc = sidev_mount(ch, (undefined4*)&block);
        if (-1 < rc) {
            rc = sidev_geom_field_84((int)block);
            entry = (char*)(rc + index * 0x40);
            rc = sidev_direntry_owner_match_ex((int)block, entry);
            if (rc < 0) {
                rc = sidev_set_result_if_idle(block, rc);
            } else {
                entry[7] = *(char*)(info + 0x2e);
                *(undefined4*)(entry + 0x2c) = *(undefined4*)(info + 0x30);
                *(undefined2*)(entry + 0x30) = *(undefined2*)(info + 0x34);
                *(undefined2*)(entry + 0x32) = *(undefined2*)(info + 0x36);
                *(undefined4*)(entry + 0x3c) = *(undefined4*)(info + 0x38);
                sidev_layout_banner_icon((int)entry, info);
                if (*(int*)(entry + 0x2c) == -1) {
                    *(ushort*)(entry + 0x32) = *(ushort*)(entry + 0x32) & 0xfffc | 1;
                }
                hz = DAT_800000f8 >> 2;
                tb = os_read_time_base();
                tb = __div2i((uint)((u64)tb >> 0x20), (uint)tb, 0, hz);
                *(int*)(entry + 0x28) = (int)tb;
                rc = sidev_dir_writeback(ch, doneCb);
                if (rc < 0) {
                    sidev_set_result_if_idle(block, rc);
                }
            }
        }
    }
    return rc;
}

// sidev_set_dir_entry_sync (0x803b885c): blocking setinfo. Runs sidev_set_dir_entry
// with the OS thread-wakeup completion callback, then waits for the result.
void sidev_set_dir_entry_sync(int ch, int index, int info)
{
    int rc;

    rc = sidev_set_dir_entry(ch, index, info, (undefined4)dsp_task_wakeup_thread);
    if (-1 < rc) {
        sidev_wait_result(ch);
    }
    return;
}

// sidev_rename (0x803b88a4): rename a file from oldName to newName. Rejects empty /
// free-marker names and names longer than 0x20 (-0xc). Mounts, scans the directory
// once recording the index of oldName and of newName; if oldName is absent -> -4,
// if newName already exists -> -7, otherwise owner-match-checks the old entry,
// copies newName into it, restamps the creation time, and writes the directory
// back with the given completion callback.
int sidev_rename(int ch, char* oldName, char* newName, undefined4 doneCb)
{
    int rc;
    uint namelen;
    char* dir;
    int nameMatch;
    int newIndex;
    int index;
    char* entry;
    u64 tb;
    int* block;

    if ((((*oldName == (char)0xff) || (*newName == (char)0xff)) || (*oldName == '\0')) || (*newName == '\0')) {
        rc = -0x80;
    } else {
        namelen = msl_strlen((int)oldName);
        if ((namelen < 0x21) && (namelen = msl_strlen((int)newName), namelen < 0x21)) {
            rc = sidev_mount(ch, (undefined4*)&block);
            if (-1 < rc) {
                rc = -1;
                newIndex = -1;
                dir = (char*)sidev_geom_field_84((int)block);
                index = 0;
                entry = dir;
                do {
                    if (((*entry != (char)0xff) && (nameMatch = msl_memcmp((int)entry, block[0x43], 4), nameMatch == 0))
                        && (nameMatch = msl_memcmp((int)(entry + 4), block[0x43] + 4, 2), nameMatch == 0)) {
                        nameMatch = sidev_direntry_name_match((int)entry, oldName);
                        if (nameMatch != 0) {
                            rc = index;
                        }
                        nameMatch = sidev_direntry_name_match((int)entry, newName);
                        if (nameMatch != 0) {
                            newIndex = index;
                        }
                    }
                    index = index + 1;
                    entry = entry + 0x40;
                } while (index < 0x7f);
                if (rc == -1) {
                    rc = sidev_set_result_if_idle(block, -4);
                } else if (newIndex == -1) {
                    dir = dir + rc * 0x40;
                    rc = sidev_direntry_owner_match_ex((int)block, dir);
                    if (rc < 0) {
                        rc = sidev_set_result_if_idle(block, rc);
                    } else {
                        msl_strncpy((int)(dir + 8), (int)newName, 0x20);
                        {
                            uint hz = DAT_800000f8 >> 2;
                            tb = os_read_time_base();
                            tb = __div2i((uint)((u64)tb >> 0x20), (uint)tb, 0, hz);
                        }
                        *(int*)(dir + 0x28) = (int)tb;
                        rc = sidev_dir_writeback(ch, doneCb);
                        if (rc < 0) {
                            sidev_set_result_if_idle(block, rc);
                        }
                    }
                } else {
                    rc = sidev_set_result_if_idle(block, -7);
                }
            }
        } else {
            rc = -0xc;
        }
    }
    return rc;
}

// sidev_get_serial (0x803b8aa0): read the card's 64-bit serial number by XOR-folding
// the first eight words of the header block (block[0x20]) into two output words:
// out[0] = w0^w2^w4^w6, out[1] = w1^w3^w5^w7. Validates the channel [0,1], mounts,
// and latches the idle result. Returns 0 or a negative error.
int sidev_get_serial(int ch, uint* out)
{
    uint w0;
    uint w2;
    uint w4;
    uint w6;
    int rc;
    uint* hdr;
    int* block;

    if ((ch < 0) || (1 < ch)) {
        rc = -0x80;
    } else {
        rc = sidev_mount(ch, (undefined4*)&block);
        if (-1 < rc) {
            hdr = (uint*)block[0x20];
            w0 = *hdr;
            w2 = hdr[2];
            w4 = hdr[4];
            w6 = hdr[6];
            out[1] = hdr[1] ^ hdr[3] ^ hdr[5] ^ hdr[7];
            *out = w0 ^ w2 ^ w4 ^ w6;
            rc = sidev_set_result_if_idle(block, 0);
        }
    }
    return rc;
}
