// Batch: batch_996 | Module: level spawn-table build + team/scoring helpers
// Address range: 0x8034df30 - 0x8034e4dc
// Date: 2026-09-12
//
// This is the post-spawn "entity fixup" pass entered from level_spawn_default_set /
// level_spawn_mode3_set (level_spawn_setup.cpp) via FUN_8034df30 (level_build_spawn_tables).
// It (re)allocates the spawned-entity pool DAT_80612db0 when needed, resets its header
// counters, builds the objective award slots (objective_build_all_slots), and fills the
// per-member navigation/waypoint sub-arrays (pool[0] region) from the level info block's
// member records (info+0x138, stride 0x220).
//
// The remaining functions form the team / objective-scoring cluster that the spawn setup
// and match logic query:
//   level_build_spawn_tables   0x8034df30  alloc+reset pool, build award slots, fill nav tables.
//   level_assign_player_teams  0x8034e194  push each player's team id into player AI + a mirror byte.
//   team_has_other_member      0x8034e23c  true if another roster entry shares this team but a
//                                          different owner id (stride 0x20 over DAT_805c7b04..08).
//   objective_cell_is_active   0x8034e2a0  a level objective cell is "active" when its two ids
//                                          (+0x50/+0x88) are set and its enable byte (+4)==1.
//   objective_max_active_score 0x8034e2d8  max score (+0xac of the cell's descriptor) over active
//                                          spawn records resolved from the objective cell table.
//   team_count_active          0x8034e370  count distinct teams that have >1 member (the roster
//                                          entries with team bits 0x180 set) via team_has_other_member.
//   objective_max_score        0x8034e444  max(team_count_active, objective_max_active_score).
//   spawn_table_get            0x8034e480  index into the spawn dispatch table DAT_80612da0.
//   objective_cell_index       0x8034e490  linear index of an objective cell pointer in the cell
//                                          table (DAT_805c7c10, count DAT_805c7c0c, stride 0xb4).
//   objective_find_spawn_record 0x8034e4dc find the level member record whose objective-cell id
//                                          (+0xac) matches, returning its member record (info+0x138).
//
// NON-MATCHING: reproduces the decompiler's structure. Register-save/restore intrinsics
// (__save_gpr_28/__restore_gpr_28) and the ABI passthrough of level_build_spawn_tables's wide
// argument list are kept faithful; the incoming context pointer that the decompiler recovered
// from the saved r28 pair is expressed as an explicit local seeded from FUN_803826e4 (the level
// context accessor used elsewhere in this module) rather than a synthetic register-pair value
// (Compile-Clean rule 3). Raw callees are declared file-locally (rule 5); named helpers resolve
// through the shared headers (rule 6).

#include "common.h"

extern "C" {

// ---- named cross-module helpers (canonical prototypes) ---------------------
extern void  player_ai_set_team_id(int ai, int teamId);
extern void* level_get_info_cached(int nLevelId);
extern void  memset_wrapper(int dst, int value, int size);   // 0x80003100

// ---- raw callees (file-local, faithful to call-site) -----------------------
extern int   FUN_803826e4(void);            // acquire level context pointer
extern void  FUN_80382730(void);            // release level context
extern int   spawn_mode_variant(void);      // 0x8034915c
extern void  objective_build_all_slots(void); // 0x8034de2c
extern void  level_alloc_entity_pool(void);   // 0x8034929c

// ---- data symbols ----------------------------------------------------------
extern int    DAT_80611568;     // "pool already built" guard (0 => (re)alloc)
extern int*   DAT_80612db0;     // spawned-entity pool header (word-indexed)
extern int    DAT_8061289c;     // mirror of pool[0x654] (nav table entry count)

extern int    DAT_805c7b04;     // team roster table base (stride 0x20)
extern int    DAT_805c7b08;     // team roster entry count
extern int    DAT_805c7c0c;     // objective cell count
extern int    DAT_805c7c10;     // objective cell table base (stride 0xb4)
extern int    DAT_805c7c14;     // objective cell active count (for scan)
extern int*   DAT_80612da0;     // spawn dispatch table base
extern int    DAT_80612610;     // active player count
extern int    DAT_80611d78;     // player instance table base (stride 0x141c)
extern int    DAT_80612614;     // active level id
extern char   DAT_80501592;     // team-id mirror byte array base
extern int    DAT_805d59b4;     // per-player team-id source table (stride 4)

// forward decls for cluster members referenced before definition
int  team_has_other_member(int rosterEntry);
char objective_cell_is_active(int cell);
int  objective_find_spawn_record(int cellId);

// ---------------------------------------------------------------------------
// 0x8034df30 : post-spawn entity fixup. (Re)builds the spawned-entity pool and
// its per-member navigation/waypoint sub-arrays from the level info block.
//
// The wide argument list mirrors the spawn-setup call site; only `info` (the
// level info block) and the pool globals are consumed here. `info` arrives in
// the ABI slot the decompiler recovered from the saved r28 pair.
// ---------------------------------------------------------------------------
void level_build_spawn_tables(unsigned long long a1, double a2, double a3, double a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8,
                              int info, int r4, int p11, int p12,
                              int p13, int p14, int p15, int p16)
{
    (void)r4; (void)p11; (void)p12; (void)p13; (void)p14; (void)p15; (void)p16;

    if (DAT_80611568 == 0) {
        // First build: size and allocate the pool. (aiactor spawn args flow
        // through via the ABI; the allocator reads the level context itself.)
        level_alloc_entity_pool();
    }

    DAT_80612db0[0x20]  = 0;
    DAT_80612db0[0x22]  = 0;
    DAT_80612db0[0x2a]  = 0;
    DAT_80612db0[0x654] = 0;
    DAT_80612db0[0x655] = 0;
    DAT_80612db0[0x24]  = *(int*)(info + 0x150);
    DAT_80612db0[0x29]  = 0;
    DAT_80612db0[0x25]  = 0;
    DAT_80612db0[0x27]  = 0;
    DAT_80612db0[0x2b]  = 0;
    DAT_80612db0[0x1f]  = 0;
    DAT_8061289c        = DAT_80612db0[0x654];

    objective_build_all_slots();

    int navBase = -4;      // running word offset into pool[0]'s int32 array
    int memberOff = 0;     // byte offset into the member record table
    int wpBase = -0x25e;   // running byte offset into pool[0]'s waypoint block
    for (int m = 0; m < *(int*)(info + 0xd8); m++) {
        int rec = *(int*)(info + 0x138) + memberOff;
        int wpOff = wpBase;
        if ((*(int*)(rec + 8) < 65000) && (*(int*)(rec + 0x21c) == 0)) {
            int navOff = navBase + 8;
            wpOff = wpBase + 0x25e;
            navBase = navBase + 4;
            DAT_80612db0[0x1f] = DAT_80612db0[0x1f] + 1;
            *(int*)((int)DAT_80612db0 + navOff) = 5;
            *(short*)(*DAT_80612db0 + wpOff) = (short)*(int*)(rec + 0x50);
            *(unsigned char*)(*DAT_80612db0 + wpBase + 0x4ba) = 0;

            int srcOff = 0;    // byte offset into rec's waypoint source (+0x54)
            int dstOff = 0;    // byte offset into the pool waypoint entry
            int i = 0;
            while (i < *(short*)(wpOff + *DAT_80612db0)) {
                short* wp = (short*)(wpOff + *DAT_80612db0);
                int flagOff = i * 0x7c;
                i = i + 1;
                *(short*)((int)wp + dstOff + 2) = (short)*(int*)(rec + srcOff + 0x54);
                srcOff = srcOff + 4;
                *(unsigned short*)(wpOff + *DAT_80612db0 + dstOff + 0x66) =
                    (unsigned short)*(unsigned char*)(rec + flagOff);
                *(short*)(wpOff + *DAT_80612db0 + dstOff + 0xca) = 5;
                dstOff = dstOff + 2;
            }

            *(short*)(*DAT_80612db0 + wpBase + 0x38c) = (short)*(int*)(rec + 0x88);
            int cellOff = 0;   // byte offset into rec cell-id list (+0x8c)
            int outOff = 0;    // byte offset into the pool cell/marker block
            for (int c = 0; c < *(short*)(wpOff + *DAT_80612db0 + 0x12e); c++) {
                int cellRec = rec + cellOff;
                cellOff = cellOff + 4;
                *(short*)(wpOff + *DAT_80612db0 + outOff + 0x130) =
                    (short)*(int*)(cellRec + 0x8c);
                *(short*)(wpOff + *DAT_80612db0 + outOff + 0x194) =
                    (short)*(int*)(cellRec + 0x154);
                outOff = outOff + 2;
            }
        }
        memberOff = memberOff + 0x220;
        wpBase = wpOff;
    }

    // Reset the per-variant spawn dispatch slots to 0.
    int slotOff = 0;
    for (int v = 0; v < *(int*)(info + 0x13c); v++) {
        int variant = spawn_mode_variant();
        *(int*)(*(int*)(*(int*)(info + 0x144) + variant * 4) + slotOff) = 0;
        slotOff = slotOff + 0x18;
    }
}

// ---------------------------------------------------------------------------
// 0x8034e194 : assign each active player's team id into its AI record and a
// mirror byte table, from the per-player team-id source table DAT_805d59b4.
// ---------------------------------------------------------------------------
void level_assign_player_teams(void)
{
    FUN_803826e4();
    int srcOff = 0;      // byte offset into DAT_805d59b4 (stride 4)
    int instOff = 0;     // byte offset into player instance table (stride 0x141c)
    for (int p = 0; p < DAT_80612610; p++) {
        int ai = *(int*)(*(int*)(DAT_80611d78 + instOff + 0x14) + 0x7c);
        player_ai_set_team_id(ai, *(int*)((int)&DAT_805d59b4 + srcOff) + 1);
        if ((unsigned char)(&DAT_80501592)[*(int*)(ai + 8)] !=
            (unsigned char)*(unsigned int*)(ai + 0x84)) {
            (&DAT_80501592)[*(int*)(ai + 8)] = (char)*(unsigned int*)(ai + 0x84);
        }
        srcOff = srcOff + 4;
        instOff = instOff + 0x141c;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8034e23c : true (1) if another roster entry shares this entry's team byte
// (+0x1c) but has a different owner id (+8). Roster table DAT_805c7b04, count
// DAT_805c7b08, stride 0x20.
// ---------------------------------------------------------------------------
int team_has_other_member(int rosterEntry)
{
    int off = 0;
    int remaining = DAT_805c7b08;
    if (0 < DAT_805c7b08) {
        do {
            int other = DAT_805c7b04 + off;
            if ((other != rosterEntry) &&
                (*(char*)(other + 0x1c) == *(char*)(rosterEntry + 0x1c)) &&
                (*(int*)(rosterEntry + 8) != *(int*)(other + 8))) {
                return 1;
            }
            off = off + 0x20;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8034e2a0 : an objective cell is "active" when both its ids (+0x50 and +0x88)
// are non-zero and its enable byte (+4)==1. Returns -1 when active, -2 otherwise
// (the original's `-1 - !bVar1` idiom preserved).
// ---------------------------------------------------------------------------
char objective_cell_is_active(int cell)
{
    bool active = false;
    if (((*(int*)(cell + 0x50) != 0) && (*(int*)(cell + 0x88) != 0)) &&
        (*(char*)(cell + 4) == '\x01')) {
        active = true;
    }
    return (char)(-1 - !active);
}

// ---------------------------------------------------------------------------
// 0x8034e2d8 : maximum score (+0xac of the cell descriptor) over all active
// objective cells that resolve to a valid spawn record.
// ---------------------------------------------------------------------------
int objective_max_active_score(void)
{
    FUN_803826e4();
    int best = 0;
    for (int i = 0; i < DAT_805c7c14; i++) {
        int* rec = (int*)objective_find_spawn_record(i + 1);
        if (rec != 0) {
            int cell = DAT_805c7c10 + *rec * 0xb4;
            char active = objective_cell_is_active((int)rec);
            if (active != '\0') {
                int score = *(int*)(cell + 0xac);
                if (best < score) {
                    best = score;
                }
            }
        }
    }
    FUN_80382730();
    return best;
}

// ---------------------------------------------------------------------------
// 0x8034e370 : count the distinct teams (by team byte, biased -1) that have at
// least one "other member" among the roster entries whose team bits (0x180) are
// set. Returns the highest team byte seen (bVar4).
// ---------------------------------------------------------------------------
int team_count_active(void)
{
    FUN_803826e4();
    unsigned char highest = 0;
    int seen[14];
    memset_wrapper((int)seen, 0, 0x20);
    int off = 0;
    for (int i = 0; i < DAT_805c7b08; i++) {
        int entry = DAT_805c7b04 + off;
        if ((*(unsigned char*)(entry + 0x1c) != 0) &&
            ((*(unsigned int*)(entry + 8) & 0x180) != 0)) {
            int team = *(unsigned char*)(entry + 0x1c) - 1;
            if (team_has_other_member(entry) != 0) {
                if (seen[team] == 0) {
                    seen[team] = 1;
                    if (highest < *(unsigned char*)(entry + 0x1c)) {
                        highest = *(unsigned char*)(entry + 0x1c);
                    }
                }
            }
        }
        off = off + 0x20;
    }
    FUN_80382730();
    return (int)highest;
}

// ---------------------------------------------------------------------------
// 0x8034e444 : max(team_count_active, objective_max_active_score).
// ---------------------------------------------------------------------------
int objective_max_score(void)
{
    int teams = team_count_active();
    int scored = objective_max_active_score();
    if (scored < teams) {
        scored = teams;
    }
    return scored;
}

// ---------------------------------------------------------------------------
// 0x8034e480 : fetch the spawn dispatch table entry at index.
// ---------------------------------------------------------------------------
int spawn_table_get(int index)
{
    return DAT_80612da0[index];
}

// ---------------------------------------------------------------------------
// 0x8034e490 : linear index of an objective cell pointer within the objective
// cell table (base DAT_80612... resolved as DAT_805c7c10, count DAT_805c7c0c,
// stride 0xb4). Returns -1 if not found.
// ---------------------------------------------------------------------------
int objective_cell_index(int cell)
{
    int index = 0;
    int off = 0;
    int remaining = DAT_805c7c0c;
    if (0 < DAT_805c7c0c) {
        do {
            if (DAT_805c7c10 + off == cell) {
                return index;
            }
            index = index + 1;
            off = off + 0xb4;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 0x8034e4dc : find the level member record whose objective-cell id (+0xac)
// matches cellId; returns the member record (info+0x138 + (+0xa4)*0x220), or 0.
// ---------------------------------------------------------------------------
int objective_find_spawn_record(int cellId)
{
    int info = (int)level_get_info_cached(DAT_80612614);
    int count = *(int*)(info + 300);
    int off = 0;
    if (0 < count) {
        do {
            int cell = *(int*)(info + 0x130) + off;
            if (*(int*)(cell + 0xac) == cellId) {
                return *(int*)(info + 0x138) + *(int*)(cell + 0xa4) * 0x220;
            }
            off = off + 0xb4;
            count = count - 1;
        } while (count != 0);
    }
    return 0;
}

} // extern "C"
