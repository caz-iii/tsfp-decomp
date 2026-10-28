// match_setup.cpp
// Match/game-start configuration. Called from game_start_init to assemble the
// active match descriptor (the 0x98-byte config block at DAT_804a0040): pick
// the level/game-mode id, fill in the participating players (binding default
// "Player N" bots into any empty slots), and populate the per-slot award/rank
// tables from the mode's award layout or the level's default score table.
// Also registers the top-level game event handlers and exposes a small shared
// accessor used across the weapon/subobject code.
//
// Address range: 0x801a6e90 - 0x801a72f8
// Batch: batch_531
#include "common.h"

extern "C" {
// --- match config block globals (0x98-byte descriptor at DAT_804a0040) ---
extern unsigned char DAT_804a0040;   // player count
extern unsigned char DAT_804a0041;   // player count (mirror)
extern unsigned char DAT_804a0054;   // per-slot award/rank table (byte stride, via &+i)
extern unsigned char DAT_804a008c;   // selected level/mode id
extern unsigned char DAT_804a008d;   // mode class (0xa == league/challenge)
extern unsigned char DAT_804a008f;   // scoring-enabled flag
extern unsigned int  DAT_804a0090;   // mode flags
extern unsigned char DAT_804a00d3;   // challenge substate
extern int  PTR_caseD_15_804a0000;   // default-name arg table
extern int  DAT_804a0000;            // config block base

extern unsigned char DAT_80497890;   // ascii char-class table
extern short DAT_8048f980;           // level default score/rank table (0xa4 stride)
extern int  s_Player__d_8042e138;    // "Player %d"

// --- helpers (extern until defined) ---
extern void memset_wrapper(int dst, int val, int len);
extern unsigned int rng_seed_from_value(unsigned char *s);
extern unsigned int FUN_8038c67c(unsigned char *s);       // parse decimal digit -> value
extern int  level_find_def_by_id(int id);
extern int  level_get_info_by_id(int id);
extern bool player_slot_is_active(int slot);
extern int  player_slot_bind_entity(int slot, unsigned long long name, int arg);
extern unsigned long long debug_log_formatted(unsigned long long acc, unsigned long long f2,
                                              unsigned long long f3, unsigned long long f4,
                                              unsigned long long f5, unsigned long long f6,
                                              unsigned long long f7, unsigned long long f8,
                                              int fmt, int arg0, void **arg1, int r6, int r7,
                                              int r8, int r9, int r10);
extern void stats_merge_all_players(void);
extern int  heap_arena_lookup(void);
extern short award_get_name(int slot, int modeId);
extern int  table_lookup_804b00e0(int idx);
extern int  FUN_801c2dcc(int modeId);            // mode-has-intro predicate
extern unsigned long long halt_stub_assert(void);
extern unsigned long long FUN_803826e4(void);    // spill: fetch entry frame
extern void FUN_80382730(void);                  // spill: restore entry frame

// --- event system ---
extern void FUN_80311ea0(void);
extern void event_register_type_handler(int type, void *handler);
extern void event_set_handler(int mask, int a2, void *handler);
extern void event_set_active_context(void *ctx);
extern char FUN_8023d13c;   // event type-0 handler
extern char FUN_8023d0ac;   // event type-1 handler
extern char LAB_8023d184;   // event type-2 handler
extern char FUN_8023d1ac;   // event 0x2000 handler
extern char LAB_8023d1a0;   // active event context
} // extern "C"

// forward decl (defined below) so match_setup_begin can call it
void match_config_init(unsigned char *cfg);

// ---------------------------------------------------------------------------
// match_register_event_handlers (FUN_801a6e90)
// Install the top-level game event handlers (types 0/1/2 and the 0x2000
// message) and set the active event context. Called from game_tick_update.
// ---------------------------------------------------------------------------
void match_register_event_handlers(void) {
    FUN_80311ea0();
    event_register_type_handler(0, &FUN_8023d13c);
    event_register_type_handler(1, &FUN_8023d0ac);
    event_register_type_handler(2, &LAB_8023d184);
    event_set_handler(0x2000, 0, &FUN_8023d1ac);
    event_set_active_context(&LAB_8023d1a0);
}

// ---------------------------------------------------------------------------
// match_config_init (FUN_801a6f00)
// Zero-init and default-populate the 0x98-byte match config block: mode class
// 'e' (0x65), player counts 1/1/0, default flag word 0xd840a, and the reserved
// sentinel fields (0x93 = 0xff, +0x88 = -1).
// ---------------------------------------------------------------------------
void match_config_init(unsigned char *cfg) {
    memset_wrapper((int)cfg, 0, 0x98);
    cfg[0x4c] = 0x65;
    cfg[0] = 1;
    cfg[1] = 1;
    cfg[2] = 0;
    *(unsigned int *)(cfg + 0x50) = 0xd840a;
    cfg[0x4d] = 9;
    *(unsigned int *)(cfg + 0x58) = 0;
    cfg[0x4f] = 1;
    *(unsigned int *)(cfg + 0x5c) = 0;
    cfg[0x93] = 0xff;
    *(unsigned int *)(cfg + 0x88) = 0xffffffff;
}

// ---------------------------------------------------------------------------
// match_slot_field_ptr (FUN_801a72f8)
// Shared accessor: return a pointer to slot `idx`'s 0x7c-stride field at +0x68,
// unless that field's high bit is set (sentinel "use slot 0") in which case the
// base slot-0 field is returned. Used across weapon/subobject code.
// ---------------------------------------------------------------------------
int match_slot_field_ptr(int base, int idx) {
    if ((*(unsigned int *)(base + idx * 0x7c + 0x68) & 0x80000000) != 0) {
        return base + 0x68;
    }
    return base + idx * 0x7c + 0x68;
}

// ---------------------------------------------------------------------------
// match_setup_begin (FUN_801a6f7c)
// Assemble the active match from the incoming argument frame. Resolves the
// level/mode id (arg 1) and player count (arg 2) from the caller, binds default
// "Player N" bots into empty slots, merges stats, then fills the per-slot award
// table two ways: for a league/challenge mode ('\n' class, award_get_name), or
// otherwise from the level's default rank table (DAT_8048f980, 0xa4 stride).
// Called from game_start_init.
// ---------------------------------------------------------------------------
void match_setup_begin(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                       unsigned long long f4, unsigned long long f5, unsigned long long f6,
                       unsigned long long f7, unsigned long long f8) {
    (void)f1;
    long long frame = (long long)FUN_803826e4();
    void **nameArg = (void **)&PTR_caseD_15_804a0000;
    match_config_init(&DAT_804a0040);
    unsigned long long acc = 0;
    DAT_804a008c = 'g';

    if (0x1ffffffffLL < frame) {
        unsigned char *p = *(unsigned char **)((int)frame + 4);
        if (((&DAT_80497890)[*p] & 0x10) != 0) {
            DAT_804a008c = (unsigned char)rng_seed_from_value(p);
        }
    }
    if ((DAT_804a0090 & 0x40) == 0) {
        int *def = (int *)level_find_def_by_id((int)DAT_804a008c);
        if (*def == 10) {
            DAT_804a0090 = 0;
            DAT_804a008d = '\n';
        }
    }
    if (0x2ffffffffLL < frame) {
        unsigned char *p = *(unsigned char **)((int)frame + 8);
        if (((&DAT_80497890)[*p] & 0x10) != 0) {
            DAT_804a0040 = (unsigned char)FUN_8038c67c(p);
            DAT_804a0041 = DAT_804a0040;
        }
    }
    if ((unsigned int)((int)DAT_804a0040 - 1) > 3) {
        halt_stub_assert();
    }

    int count = (3 < DAT_804a0040) ? (int)DAT_804a0040 : 4;
    for (int i = 0; i < count; i++) {
        if (!player_slot_is_active(i)) {
            unsigned long long name = debug_log_formatted(acc, f2, f3, f4, f5, f6, f7, f8,
                                                          (int)&s_Player__d_8042e138, i + 1,
                                                          nameArg, 0, 0, 0, 0, 0);
            nameArg = (void **)0;
            player_slot_bind_entity(i, name, 0);
        }
    }
    stats_merge_all_players();

    if (DAT_804a008d == '\n' && (DAT_804a0090 & 0x40) == 0) {
        bool special = false;
        if (heap_arena_lookup() != 0 && DAT_804a008c != 'd') {
            special = true;
        }
        int n = special ? 2 : (int)DAT_804a0040;
        for (int i = 0; i < n; i++) {
            (&DAT_804a0054)[i] = (unsigned char)award_get_name(i, (int)DAT_804a008c);
        }
        DAT_804a0090 = DAT_804a0090 & 0xfffe7bfd;
        DAT_804a008d = '\n';
        if (table_lookup_804b00e0(4) == 0) {
            DAT_804a008f = 1;
        }
        if (FUN_801c2dcc((int)DAT_804a008c) != 0) {
            DAT_804a0090 = DAT_804a0090 | 0x20000000;
            DAT_804a00d3 = 0;
        }
    } else if ((DAT_804a0090 & 0x40) == 0) {
        int info = level_get_info_by_id((int)DAT_804a008c);
        int rank = (info == 0) ? 0x1d : *(int *)(info + 8);
        if (table_lookup_804b00e0(5) == 0) {
            int off = 0;
            for (int i = 0; i < DAT_804a0040; i++) {
                (&DAT_804a0054)[i] =
                    (unsigned char)*(short *)(&DAT_8048f980 + rank * 0xa4 + off);
                off += 8;
            }
        }
        if (table_lookup_804b00e0(6) == 0) {
            int off = 0;
            for (int i = 0; i < 10 - DAT_804a0040; i++) {
                (&DAT_804a0054)[i + DAT_804a0040] =
                    (unsigned char)*(short *)(&DAT_8048f980 + rank * 0xa4 + off);
                off += 8;
            }
        }
    }
    FUN_80382730();
}
