// memcard_save.cpp
// Memory-card (CARD) save-file I/O layer. Builds/reads the game's save-file
// records via the CARD async command primitives (gx_cmd_add_type* / the
// gx_cmd_list_execute pump - shared low-level command-list helpers), translates
// raw negative CARD status codes to the game's result enum, and assembles the
// save-file banner/comment header ("TimeSplitters: Future Perfect" / "MapMaker",
// plus a CRC32 checksum). The active card/player handle comes from
// notif_get_player_data; the in-RAM record table is DAT_80612d58.
//
// Address range: 0x8019160c - 0x80191aa0
// Batch: batch_500
#include "common.h"

extern "C" {
// --- CARD command-list primitives (shared low-level helpers) ---
extern int  notif_get_player_data(void);
extern void notif_set_player_slot(int slot);
extern void gx_cmd_add_type1(int h, int a2, int *rec, int buf, int len);
extern void gx_cmd_add_type2(int h, int a2, int *rec, int buf, int len, int a6, int *extra);
extern void gx_cmd_add_type4(int h, int a2, int a3, int buf, unsigned int len, int a6, int *rec);
extern void gx_cmd_add_type5(int h, int a2, void *cb, int a4);
extern void gx_cmd_add_type6(int h, int a2, int *rec);
extern void gx_cmd_add_type7(int h, int a2, int a3, int *rec);
extern int  gx_cmd_list_execute(int a1, int *outCode, int *outTag, void *outData);

// --- record / banner build helpers ---
extern int  FUN_803b5d40(int handle, unsigned int *out1, int *out2);   // CARD probe/status
extern int  FUN_8038b5c8(int rec);                                     // record length query
extern int  FUN_8038b378(unsigned int *a, unsigned int *b);            // tag compare
extern int  FUN_80387b68(int field, int expect, int len);             // field validate
extern void FUN_8038b510(int *dst, int *src);                          // string copy
extern void FUN_8038b4a0(int dst, int src);                            // string append
extern unsigned int crc32_buffer(void *buf, int len);
extern int  leaf_31e8(int dst, int src, int len);
extern unsigned long long FUN_803826e4(void);
extern void FUN_80382730(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

// --- data ---
extern int  DAT_806124e0;    // pending record ptr
extern int  DAT_806124e4;    // pending op type
extern int  DAT_806124e8;    // pending record frame
extern int  DAT_806124f0;    // scan result: found record slot (part A)
extern int  DAT_806124f4;    // scan result: found record slot (part B)
extern int  DAT_80612370;    // expected save-file id blob
extern int  DAT_80612d58;    // save record table base
extern unsigned char *DAT_8060f940;   // banner/work buffer
extern int  DAT_8060f948;    // build variant id (region/mode)
extern int  DAT_80610ad8;    // expected save-file magic tag
extern int  DAT_80610ae0;    // "Carte : " (localized map label)
extern int  s_TimeSplitters_Futuro_Perfecto_8041f208;    // ES title
extern int  s__TimeSplitters_Future_Perfect_8041f1e7;    // EN title (offset +1)
extern int  s_Carte___80610ae0;
extern int  s_MapMaker___8041f228;
extern int  DAT_8041e9e8;    // default banner icon/palette blob (0x800 bytes)
} // extern "C"

// ---------------------------------------------------------------------------
// card_translate_result (FUN_8019160c)
// Map a raw CARD status code (negative errno-style) to the game's result enum.
// A dense binary-search over the known CARD_RESULT_* values; unknown codes fall
// through to 0x12 (generic fatal).
// ---------------------------------------------------------------------------
int card_translate_result(int code) {
    if (code == -8) return 2;
    if (code < -8) {
        if (code == -0x80) return 0x12;
        if (code < -0x80) {
            if (code != -0x1a7b) {
                if (code < -0x1a7b) {
                    if (-0x1a7d < code) return 0x13;
                } else if (code < -0x1a79) {
                    return 0x14;
                }
            }
        } else {
            if (code == -0xd) return 0x11;
            if (-0xe < code && -10 < code) return 2;
        }
    } else {
        if (code == -3) return 4;
        if (code < -3) {
            if (code == -5) return 0xf;
            if (code < -5) {
                if (-7 < code) return 0xe;
                return 9;
            }
        } else {
            if (code == 0xe) return 1;
            if (code < 0xe && code < -1) return 0xd;
        }
    }
    return 0x12;
}

// ---------------------------------------------------------------------------
// card_write_record (FUN_80191704)
// Issue an async CARD write (gx_cmd_add_type4) of the current record's payload
// (rec+0x20 buffer, rec+0x28 length, capped at 0x10000). Records the pending op
// (type 1) + record frame for completion tracking. Spill frame FUN_... via
// __save_gpr_28 supplies the record pointer.
// ---------------------------------------------------------------------------
void card_write_record(void) {
    unsigned long long frame = (unsigned long long)(unsigned int)__save_gpr_28();
    int rec = (int)(frame >> 0x20);
    int handle = notif_get_player_data();
    unsigned int len = *(unsigned short *)(rec + 0x32);
    notif_set_player_slot(1);
    if (0x10000 < len) len = 0x10000;
    gx_cmd_add_type4(handle, 0, *(int *)(rec + 0x28), (int)DAT_8060f940, len, 0,
                     (int *)(rec + 0x20));
    DAT_806124e4 = 1;
    DAT_806124e0 = rec;
    DAT_806124e8 = (int)frame;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// card_delete_record (FUN_80191790)
// Issue an async CARD delete (gx_cmd_add_type7) for the record at param_2.
// ---------------------------------------------------------------------------
int card_delete_record(int param_1, int rec) {
    (void)param_1;
    int handle = notif_get_player_data();
    notif_set_player_slot(3);
    DAT_806124e4 = 3;
    gx_cmd_add_type7(handle, 0, *(int *)(rec + 0x28), (int *)(rec + 0x20));
    return 1;
}

// ---------------------------------------------------------------------------
// card_read_dir_entry (FUN_801917f4)
// Kick a CARD directory/get-status command (gx_cmd_add_type6) and pump the
// command list to completion; on the terminal tag 0xe returns success, else
// translates the failing code.
// ---------------------------------------------------------------------------
int card_read_dir_entry(void) {
    int outTag = 0;
    int outCode = 0;
    int outData[2];
    int handle = notif_get_player_data();
    bool done = false;
    notif_set_player_slot(4);
    gx_cmd_add_type6(handle, 0, (int *)&DAT_806124f0);
    while (!done) {
        int rc = gx_cmd_list_execute(0, &outCode, &outTag, outData);
        if (rc != 0) break;
        if (outTag == 0xe) done = true;
    }
    if (done) return 1;
    return card_translate_result(outCode);
}

// ---------------------------------------------------------------------------
// card_probe_status (FUN_8019188c)
// Query CARD probe status (FUN_803b5d40); 0 = OK (return 1), else translate.
// ---------------------------------------------------------------------------
int card_probe_status(void) {
    unsigned int a = 0;
    int b = 0;
    int handle = notif_get_player_data();
    int rc = FUN_803b5d40(handle, &a, &b);
    if (rc == 0) return 1;
    return card_translate_result(rc);
}

// ---------------------------------------------------------------------------
// card_scan_entry_cb (FUN_801918cc)
// Per-entry callback for the directory scan (card_scan_dir). Validates the file
// magic tag (DAT_80610ad8) and the game id fields (DAT_80612370), and when they
// match copies the entry into the next free record-table slot
// (DAT_80612d58 + count*0x38), stashing its size and command context.
// ---------------------------------------------------------------------------
void card_scan_entry_cb(int param_1, int param_2, int param_3, int param_4) {
    (void)param_1; (void)param_2;
    unsigned long long frame = FUN_803826e4();
    int *entry = (int *)(unsigned int)(frame >> 0x20);
    int recLen = FUN_8038b5c8((int)entry);

    if (FUN_8038b378((unsigned int *)((int)entry + recLen + -4), (unsigned int *)&DAT_80610ad8) == 0
        && FUN_80387b68((int)(entry + 10), DAT_80612370, 4) == 0
        && FUN_80387b68((int)(entry + 0xb), DAT_80612370 + 4, 2) == 0) {
        int table = DAT_80612d58;
        int count = *(int *)(table + 8);
        *(int *)(table + 8) = count + 1;
        int *slot = (int *)(table + count * 0x38 + 0x10);
        FUN_8038b510(slot, entry);
        *(unsigned char *)((int)slot + recLen + -4) = 0;
        *(short *)((int)slot + 0x32) = (short)entry[8];
        slot[0xb] = entry[9];
        slot[10] = (int)frame;
        slot[9] = param_4;
        slot[8] = param_3;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// card_scan_dir (FUN_801919cc)
// Enumerate all CARD files (gx_cmd_add_type5 with card_scan_entry_cb), building
// the matching-record table (reset count, cap 2). Pumps the command list to the
// terminal tag 0xe. Returns success or the translated failure code.
// ---------------------------------------------------------------------------
int card_scan_dir(void) {
    int outTag = 0;
    int outCode = 0;
    int outDataA = 0;
    int outDataB = 0;
    int handle = notif_get_player_data();
    bool done = false;
    notif_set_player_slot(0);
    *(int *)(DAT_80612d58 + 4) = 2;
    *(int *)(DAT_80612d58 + 8) = 0;
    gx_cmd_add_type5(handle, 0, (void *)card_scan_entry_cb, 0);
    while (!done) {
        int rc = gx_cmd_list_execute(0, &outCode, &outTag, &outDataA);
        if (rc != 0) break;
        if (outTag == 0xe) done = true;
    }
    *(int *)(DAT_80612d58 + 8) = 0;
    DAT_806124f0 = outDataA;
    DAT_806124f4 = outDataB;
    return card_translate_result(outCode);
}

// ---------------------------------------------------------------------------
// card_create_record (FUN_80191aa0)
// Build the save-file banner/comment header in the work buffer DAT_8060f940:
// title string (region-specific TS:FP / TS:FP-ES), mode string (MapMaker / Carte),
// the default icon+palette blob (0x800), the payload size, and a CRC32 checksum;
// then issue the async CARD create/write (gx_cmd_add_type1 create, type2 write).
// ---------------------------------------------------------------------------
void card_create_record(int param_1, int param_2, int mode) {
    (void)param_1; (void)param_2;
    int banner[26];

    unsigned long long frame = (unsigned long long)(unsigned int)__save_gpr_25();
    int *rec = (int *)(unsigned int)(frame >> 0x20);
    int handle = notif_get_player_data();
    unsigned char *buf = DAT_8060f940;
    unsigned char *payload = DAT_8060f940 + 0x848;
    notif_set_player_slot(2);

    FUN_8038b510(banner, rec);
    FUN_8038b4a0((int)banner, -0x7f9ef528);
    leaf_31e8((int)(buf + 0x40), (int)&DAT_8041e9e8, 0x800);

    if (DAT_8060f948 == 4) {
        FUN_8038b510((int *)buf, (int *)&s_TimeSplitters_Futuro_Perfecto_8041f208);
    } else {
        FUN_8038b510((int *)buf, (int *)((int)&s__TimeSplitters_Future_Perfect_8041f1e7 + 1));
    }
    if (DAT_8060f948 == 1) {
        FUN_8038b510((int *)(buf + 0x20), (int *)&s_Carte___80610ae0);
    } else {
        FUN_8038b510((int *)(buf + 0x20), (int *)&s_MapMaker___8041f228);
    }
    FUN_8038b4a0((int)(buf + 0x20), (int)rec);
    FUN_8038b4a0((int)(buf + 0x20), -0x7f9ef518);

    *(unsigned int *)(buf + 0x840) = (unsigned int)*(unsigned short *)((int)rec + 0x32);
    unsigned int crc = crc32_buffer(buf, 0x844);
    *(unsigned int *)(buf + 0x844) = crc;
    leaf_31e8((int)payload, (int)frame, (int)*(unsigned short *)((int)rec + 0x32));

    if (mode == 0) {
        gx_cmd_add_type1(handle, 0, banner, (int)DAT_8060f940, 0xa000);
    } else {
        gx_cmd_add_type2(handle, 0, banner, (int)DAT_8060f940, 0xa000, 0, rec + 8);
    }
    DAT_806124e4 = 2;
    __restore_gpr_25();
}

// ===========================================================================
// batch_505 additions: CARD save-op completion poll, mount-state helpers, and
// the save-slot label builder. Address range 0x80191c10 - 0x80191e30.
// ===========================================================================

extern "C" {
extern int  FUN_801a6dec(void);                  // CARD mount probe (0 == mounted)
extern void gx_cmd_add_type12(int h, int a2);    // CARD format/mount sync command
extern int  DAT_80611a88;                         // card-mounted flag
extern unsigned long long FUN_803826e8(void);    // spill save/base
extern void FUN_80382734(void);                  // spill restore
extern int  get_string_by_id(int id);
extern int  FUN_80388098(unsigned long long acc, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, int cap,
                         int *fmt, int arg0, int arg1, int r8, int r9, int r10);
extern int  DAT_80527b48;   // slot-name buffer A (2 x 0x40)
extern int  DAT_80527bc8;   // slot-name buffer B (2 x 0x20)
extern int  DAT_80610aec;   // "%s %c" style slot-label format
} // extern "C"

// ---------------------------------------------------------------------------
// card_poll_pending (FUN_80191c10)
// Pump the outstanding async CARD command to completion. When the pending op is
// a write (DAT_806124e4 == 1) and the terminal tag 0xe arrives, clamp/normalize
// the recorded payload size, copy the payload into the record frame, verify the
// stored CRC32 against a freshly-computed one (marking the record "ERSE" on
// mismatch), and clear the pending state. Errors are translated + latched.
// ---------------------------------------------------------------------------
void card_poll_pending(void) {
    int outTag = 0;
    int outCode = 0;
    int outData = 0;
    int outExtra = 0;

    __save_gpr_28();
    int rec = DAT_806124e0;
    unsigned char *buf = DAT_8060f940;
    if (DAT_806124e4 != 0) {
        unsigned char *payload = DAT_8060f940 + 0x848;
        int rc = gx_cmd_list_execute(0, &outCode, &outTag, &outData);
        if (rc == 0) {
            if (DAT_806124e4 == 1 && outTag == 0xe) {
                if (0x8000 < *(unsigned int *)(buf + 0x840)) {
                    buf[0x840] = 0;
                    buf[0x841] = 0;
                    buf[0x842] = 0x80;
                    buf[0x843] = 0;
                }
                *(short *)(rec + 0x32) = (short)*(unsigned int *)(buf + 0x840);
                leaf_31e8((int)DAT_806124e8, (int)payload, *(int *)(buf + 0x840));
                DAT_806124e4 = 0;
                unsigned int crc = crc32_buffer(buf, 0x844);
                if (*(unsigned int *)(buf + 0x844) != crc) {
                    *(int *)DAT_806124e8 = 0x41525345; // 'ARSE' erase marker
                }
            }
        } else {
            DAT_806124e4 = 0;
            DAT_806124f4 = outExtra;
            DAT_806124f0 = outData;
            card_translate_result(outCode);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// card_mount_reset (FUN_80191d44)
// Clear the mounted flag and release the player card slot.
// ---------------------------------------------------------------------------
void card_mount_reset(void) {
    DAT_80611a88 = 0;
    notif_set_player_slot(0);
}

// ---------------------------------------------------------------------------
// card_mount_poll (FUN_80191d70)
// Probe whether the card is mounted (FUN_801a6dec == 0); latch the mounted flag
// and return it.
// ---------------------------------------------------------------------------
int card_mount_poll(void) {
    if (FUN_801a6dec() == 0) {
        DAT_80611a88 = 1;
    }
    return DAT_80611a88;
}

// ---------------------------------------------------------------------------
// card_format_sync (FUN_80191dac)
// Issue the CARD format/mount-sync command (gx_cmd_add_type12) and pump the
// command list until the terminal tag 0xe. Returns 1 on success; on a failing
// pump translates the code into *out and returns 0.
// ---------------------------------------------------------------------------
int card_format_sync(int handle, int *out) {
    int outData[4];
    for (;;) {
        gx_cmd_add_type12(handle, handle);
        int rc = gx_cmd_list_execute(handle, out, outData, (void *)0);
        if (rc != 0) {
            *out = card_translate_result(*out);
            return 0;
        }
        if (outData[0] == 0xe) break;
    }
    *out = 1;
    return 1;
}

// ---------------------------------------------------------------------------
// card_build_slot_labels (FUN_80191e30)
// Build the two save-slot display labels ("<fmt> A" / "<fmt> B") into the slot
// name buffers using the localized format string id 0xe28. Fills both the 0x40-
// stride primary buffer (DAT_80527b48) and the 0x20-stride secondary buffer
// (DAT_80527bc8) for slots 0 and 1.
// ---------------------------------------------------------------------------
void card_build_slot_labels(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                            unsigned long long a4, unsigned long long a5, unsigned long long a6,
                            unsigned long long a7, unsigned long long a8) {
    (void)a1;
    unsigned long long acc = FUN_803826e8();
    int slot = 0;
    int offB = 0;
    int offA = 0;
    do {
        int fmt = get_string_by_id(0xe28);
        acc = FUN_80388098(acc, a2, a3, a4, a5, a6, a7, a8,
                           (int)(&DAT_80527b48 + offA), 0x40, &DAT_80610aec, fmt, slot + 0x41,
                           0, 0, 0);
        fmt = get_string_by_id(0xe28);
        acc = FUN_80388098(acc, a2, a3, a4, a5, a6, a7, a8,
                           (int)(&DAT_80527bc8 + offB), 0x40, &DAT_80610aec, fmt, slot + 0x41,
                           0, 0, 0);
        slot++;
        offA += 0x40;
        offB += 0x20;
    } while (slot < 2);
    FUN_80382734();
}
