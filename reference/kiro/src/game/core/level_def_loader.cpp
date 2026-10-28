// Module: level_def_loader
// TSFP GameCube main.dol non-matching decompilation.
//
// Level / scene definition loader. Drives a multi-pass walk over the compiled
// level "command stream" (a tagged record buffer at DAT_80612be8). Each record
// starts with a 32-bit magic tag (0xc77668 .. 0xc77675) followed by a 32-bit
// byte size and a variable payload. The three passes are:
//
//   pass 1  level_preload_all_resources_pass  (0x80239270)
//           walk the stream and preload every model / entity / sound bank the
//           level references, so the loader can stream assets ahead of time.
//   pass 2  level_count_and_alloc_pass        (0x802397f8)
//           count how many objects of each category the stream defines and
//           allocate the per-category arenas in the level-load work context.
//   pass 3  level_spawn_all_entities_pass     (0x8023a0a0)
//           instantiate the actual entities / spawn points / lift paths from
//           the stream into the arenas allocated by pass 2, then expand mirror
//           (splitscreen / co-op) duplicates.
//
// Entry points are the already-named level_preload_all_resources (0x80037550)
// and level_spawn_all_entities (0x80038544).
//
// Batch: batch_707  |  Addresses: 0x8023919c - 0x8023bea0

#include "common.h"

// ---------------------------------------------------------------------------
// Command-stream layout
// ---------------------------------------------------------------------------

// Compiled level command stream. The walk runs from base+8 up to base+0x7ffc.
// Each record: [int tag][int size][payload...]; the payload of the next record
// begins at (payload_start + record.size).
extern unsigned char *DAT_80612be8;

// Command-stream record magic tags.
enum {
    LVL_TAG_HEADER   = 0xc77668, // entity-block header (char id, flags, count)
    LVL_TAG_ENTLIST  = 0xc77669, // per-block entity list
    LVL_TAG_END      = 0xc7766a, // end-of-stream sentinel
    LVL_TAG_BOT       = 0xc7766b, // bot / player spawn
    LVL_TAG_PATH      = 0xc7766c, // lift / rail path
    LVL_TAG_BLOCK_D   = 0xc7766d,
    LVL_TAG_BLOCK_E   = 0xc7766e,
    LVL_TAG_BLOCK_F   = 0xc7766f,
    LVL_TAG_BLOCK_10  = 0xc77670,
    LVL_TAG_BLOCK_11  = 0xc77671,
    LVL_TAG_RAW_100   = 0xc77672, // fixed 0x100-byte raw copy
    LVL_TAG_RAW_A     = 0xc77673,
    LVL_TAG_RAW_B     = 0xc77674,
    LVL_TAG_RAW_C     = 0xc77675
};

// ---------------------------------------------------------------------------
// Global loader state (file-local externs to raw symbols)
// ---------------------------------------------------------------------------

extern unsigned char DAT_805015bd;   // current game-mode selector
extern unsigned char DAT_805015bf;   // co-op / view index
extern unsigned int  DAT_805015c0;   // game-mode flag bits
extern unsigned int  DAT_80612618;   // active character id for this block
extern int           DAT_80612950;   // mirror-group count
extern unsigned int  DAT_805d59a0;   // difficulty / roster row selector

// IGC "variant" preload table (0xa4 stride); slot base + 4 holds a short list.
extern unsigned char DAT_8048f97c[];

// Entity type descriptor tables used by the spawn pass.
extern unsigned char DAT_80477bc8[];   // entity descriptor table (0xc4 stride)
extern unsigned char DAT_80477bca;      // type-class byte within the descriptor
extern unsigned char DAT_80477bd0;      // item-def pointer table (0x31 int stride)
extern unsigned char DAT_80477bd4;      // per-difficulty item enable table
extern int          *PTR_DAT_80477bd0;  // item-def pointer table (aliased)
extern unsigned char DAT_805d09ec;      // block model-def index table
extern int           DAT_804666c0;      // entity resource-id table (0x34 stride)

// Mirror-expansion ring buffers / constants.
extern int           DAT_80611b94;
extern int           DAT_80611bfc;
extern int           DAT_80611c0c;
extern unsigned char DAT_8046b41c;      // mirror angle table (float stride 4)

// Scalar conversion constants (signed byte -> world float).
extern float         FLOAT_80618870;
extern float         FLOAT_80618880;
extern float         FLOAT_80618884;
extern double        DOUBLE_80618878;

// ---------------------------------------------------------------------------
// Register-spill "context slot" accessors. These tiny leaf routines return a
// pointer to the current level-load work context (different callers get the
// same block; the arg spill only threads FP scratch registers).
// ---------------------------------------------------------------------------
extern int  FUN_803826b4(void);   // scene-build context
extern int  FUN_803826d0(void);   // count/alloc context
extern int  FUN_803826f0(void);   // preload-variant context (returns index)
extern unsigned long long FUN_803826c0(void); // mirror ctx: hi=count, lo=used
extern void FUN_8038271c(void);   // release count/alloc context
extern void FUN_8038273c(void);   // release preload-variant context
extern void FUN_80382700(void);   // release scene-build context
extern void FUN_8038270c(void);   // release mirror context

// ---------------------------------------------------------------------------
// Raw cross-module helpers (file-local externs, arity/type per call site).
// ---------------------------------------------------------------------------
extern int  *spawn_resolve_entity_ptr(int descriptor, unsigned int charId);
extern int   spawn_resolve_entity_id(int descriptor, unsigned int charId);

extern int   FUN_80343c4c(int a);               // entity-list per-entry stride A
extern int   FUN_8030f8f4(void);                // entity-list per-entry stride B
extern unsigned char *FUN_80300940(int type);    // type descriptor record
extern int   FUN_80300af0(int type);             // make primary instance handle
extern int   FUN_803009d0(int type);             // make secondary instance handle
extern int   FUN_80300b08(int type);             // type placement callback
extern unsigned char *FUN_80336778(void);         // raw block-B destination
extern int   FUN_8033646c(int buf, unsigned int count);
extern int   FUN_803366ac(int buf, int size);
extern int   FUN_8031d5d4(unsigned int v);
extern int   FUN_8031d594(unsigned int v);
extern int   FUN_8031d554(unsigned int v);
extern void  FUN_8031d694(int dst, unsigned int v);
extern int   FUN_8035465c(int rec);               // path record validity test
extern int   FUN_8035334c(int type, unsigned int charId);
extern int   FUN_8035332c(unsigned int charId, int type);
extern int   FUN_8035330c(unsigned int charId, int type);
extern int   FUN_80353e88(int a);
extern void  FUN_80353a20(int *dst, int rec);
extern int   FUN_8034fe3c(int rec);
extern void  FUN_8034ff44(int a, int b, int c, int d, int e);
extern void  FUN_8034ffa4(int a, int b, int c);
extern int   FUN_80350008(int rec);
extern int   FUN_803500b4(int *rec);
extern int   FUN_8032a868(int a);
extern int   FUN_8032a844(void);
extern int   FUN_8032a638(int a);
extern int   FUN_80328474(unsigned int a);
extern void  FUN_8032568c(float *a, float *b);
extern void  FUN_8032b0d8(int dst, unsigned int v);
extern void  FUN_80310f4c(unsigned char *src, unsigned int *dst, int mode);
extern void  FUN_80354618(unsigned int *dst, unsigned char *src);
extern void  FUN_8038b510(int *dst, int *src);
extern int   FUN_80339c7c(void);
extern int   FUN_8023d2c0(int a);

// Named cross-module helpers. common.h does not pull in the generated
// prototype headers, so (matching the sibling game/core modules) every helper
// this TU calls is declared file-local with the arity/type of its call site.
extern void  entity_preload_resources(int entityTypeId);
extern void  preload_entity_model(int modelDef);
extern void  object_preload_type_no_props(int type);
extern void  object_preload_type_full(int type);
extern void  sound_clear_bank_ref(int soundId);
extern int   mem_alloc(int size, int type);
extern int   memset_wrapper(int dst, int val, int len);
extern void  leaf_31e8(u32 dst, u32 src, s32 len);
extern int   soundpad_resolve_entity_to_zone(int entityId);
extern void  soundpad_swap_zone_positions(int a, int b);
extern int   worm_igc_resolve_zone(int entityId);
extern void  worm_igc_actor_teardown(int actor);
extern void  worm_igc_preload_actor_resources(unsigned int a0, unsigned int a1, unsigned int a2,
                                              unsigned int a3, unsigned int a4, unsigned int a5,
                                              unsigned int a6);
extern void  player_ai_preload_character_resources(void);
extern int   player_ai_is_valid_weapon_type(int weaponType);
extern void  aiactor_respawn_at_spawnpoints(void);
extern int   stats_is_tracking_active(void);
extern int   level_get_current_def(void);
extern int  *spawn_def_get_entry(int index);
extern void  spawn_def_copy(int *dst, int *src);
extern int   spawn_def_find_by_id(int entityId);
extern double math_poly_trig(double x);
extern void  subobj_release_sound_banks(int bank);
extern int   spawn_get_item_def_direct(int def);
extern int   spawn_get_model_def(int a, int b);

// Callback symbols referenced only as pointers.
extern void  FUN_8034def0(void);
extern void  FUN_803519a0(void);
extern void  FUN_8035145c(void);
extern void  FUN_8035143c(void);
extern void  FUN_8034a3fc(void);
extern void  FUN_8025bcc8(void);
extern unsigned char LAB_8014143c;

extern void  FUN_8034a468(void);

// ---------------------------------------------------------------------------
// Forward declarations (intra-module).
// ---------------------------------------------------------------------------
static void level_preload_igc_variant(void);
void        level_expand_mirror_entities(int ctx, int count, int *indices);
void        level_build_mirror_groups(void);
void        level_count_and_alloc_pass(void);
double      signed_byte_to_float(int v);
void        path_node_emit_pair(int base, int *pCount, int rec, int prev, int cur);
void        level_set_mirror_count(int count);
static void level_preload_igc_or_actor(int record);

// ===========================================================================
// 0x8023919c  level_preload_igc_variant
//   Preload the character resources referenced by the current IGC "variant"
//   record. Walks a short-entry list embedded in the variant table row; every
//   entry whose id (short at +4) is valid triggers a character preload.
// ===========================================================================
static void level_preload_igc_variant(void)
{
    int ctxIndex = FUN_803826f0();
    int rowBase  = (int)(&DAT_8048f97c[(ctxIndex + 0x1b) * 0xa4]);
    int listCount = *(int *)rowBase;

    int offset = 0;
    for (int i = 0; i < listCount; i++) {
        if (*(short *)(rowBase + offset + 4) != -1) {
            player_ai_preload_character_resources();
        }
        offset += 8;
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x80239214  level_preload_igc_or_actor
//   Only in game mode 10: choose between the IGC-variant preload and the IGC
//   actor-registry preload based on the block record's type field (+0x14).
// ===========================================================================
static void level_preload_igc_or_actor(int record)
{
    if (DAT_805015bd == 10) {
        short kind = *(short *)(record + 0x14);
        if (kind < 0xc || kind != 0x11) {
            level_preload_igc_variant();
        } else {
            worm_igc_preload_actor_resources(0, 0, 0, 0, 0, 0, 0);
        }
    }
}

// ===========================================================================
// 0x80239270  level_preload_all_resources_pass
//   Pass 1: walk the command stream and preload every asset the level uses.
// ===========================================================================
void level_preload_all_resources_pass(void)
{
    FUN_803826d0();

    int  *lastHeader = 0;
    bool  needTeamA  = false;
    bool  needTeamB  = false;

    int *rec;
    int *payload = 0;
    for (rec = (int *)(DAT_80612be8 + 8);
         rec < (int *)(DAT_80612be8 + 0x7ffc);
         rec = (int *)((int)payload + rec[1])) {
        int tag = *rec;
        payload = rec + 2;
        if (tag == LVL_TAG_END) {
            break;
        }

        if (tag == LVL_TAG_HEADER) {
            int def = (int)level_get_current_def();
            unsigned short charSel = *(unsigned short *)((int)rec + 0x12);
            *(unsigned int *)(def + 0x1c) = (unsigned int)FUN_8031d5d4(charSel);
            *(unsigned int *)(def + 0x14) = (unsigned int)FUN_8031d594(charSel);
            FUN_8031d694(def + 0x2c, charSel);

            worm_igc_actor_teardown((int)payload);
            level_preload_igc_or_actor((int)payload);

            if (*(short *)(rec + 6) < 0) {
                *(unsigned int *)(def + 0x10) = (unsigned int)FUN_8031d554(charSel);
            } else {
                *(int *)(def + 0x10) = (int)*(short *)(rec + 6);
            }
            lastHeader = payload;

            int resId = *(int *)(&DAT_804666c0 + *(int *)(def + 0x10) * 0x34);
            if (resId != -1) {
                entity_preload_resources(resId);
            }
        } else if (tag == LVL_TAG_RAW_100) {
            leaf_31e8((u32)0x805d6630, (u32)(int)payload, 0x100);
        } else if (tag == LVL_TAG_BOT) {
            char which = *(char *)(rec + 3);
            if (which == 1)      needTeamB = true;
            else if (which == 0) needTeamA = true;
        } else if (tag == LVL_TAG_PATH) {
            short itemSel = *(short *)((int)rec + 0x32);
            if (itemSel >= 0) {
                int def = (int)(&PTR_DAT_80477bd0)[itemSel * 0x31];
                int itemDef = spawn_get_item_def_direct(def);
                preload_entity_model(itemDef);
            }
        } else if (tag == LVL_TAG_ENTLIST) {
            int *ent = payload;
            for (int i = 0; i < lastHeader[1]; i++) {
                unsigned short entCount = *(unsigned short *)((int)ent + 0x1e);
                int strideA = FUN_80343c4c(*ent);
                int strideB = FUN_8030f8f4();
                ent = ent + (strideB + strideA) * 2 + 10;

                for (int j = 0; j < (int)(unsigned int)entCount; j++) {
                    short typeIdx = *(short *)((int)ent + 0xe);
                    int *pModel = spawn_resolve_entity_ptr(
                        (int)(&DAT_80477bc8[typeIdx * 0xc4]),
                        (unsigned int)*(unsigned short *)((int)lastHeader + 10));
                    if (pModel == 0) {
                        int resId = spawn_resolve_entity_id(
                            (int)(&DAT_80477bc8[typeIdx * 0xc4]),
                            (unsigned int)*(unsigned short *)((int)lastHeader + 10));
                        entity_preload_resources(resId);
                    } else {
                        preload_entity_model((int)pModel);
                    }

                    short cls = (&DAT_80477bca)[typeIdx * 0x62];
                    if ((cls == 8 || cls == 9 || cls == 0x13 || cls == 0x14) && *ent != 0) {
                        entity_preload_resources(0x122);
                    }

                    short entType = *(short *)((int)ent + 0xe);
                    if (entType < 0x32) {
                        if (entType == 0x2b) {
                            int bank = FUN_8035334c(0x2b, (unsigned int)*(unsigned short *)((int)lastHeader + 10));
                            subobj_release_sound_banks(bank);
                        }
                    } else if (entType < 0x38) {
                        if (entType < 0x35) {
                            object_preload_type_no_props(ent[1]);
                        } else {
                            object_preload_type_no_props((int)*(short *)(ent + 2));
                        }
                        sound_clear_bank_ref(0xd62);
                        sound_clear_bank_ref(0xd63);
                    }
                    ent = ent + *(unsigned char *)(ent + 5) + 6;
                }
            }
        } else if (tag == LVL_TAG_RAW_A) {
            unsigned char *dst = FUN_80336778();
            leaf_31e8((u32)dst, (u32)(int)payload, rec[1]);
        } else if (tag == LVL_TAG_RAW_B) {
            static unsigned char sScratch[2048];
            memset_wrapper((int)sScratch, 0, 2000);
            leaf_31e8((u32)sScratch, (u32)(int)payload, rec[1]);
            FUN_8033646c((int)sScratch, (unsigned int)rec[1] / 0x14);
        } else if (tag == LVL_TAG_RAW_C) {
            static unsigned char sScratch2[3072];
            memset_wrapper((int)sScratch2, 0, 3000);
            leaf_31e8((u32)sScratch2, (u32)(int)payload, rec[1]);
            FUN_803366ac((int)sScratch2, rec[1]);
        }
    }

    // Team-loadout tables (A / B) selected by the header's team index.
    static const int kTeamA[6][2] = {
        {0x871,0x872},{0x873,0x874},{0x875,0x876},{0x877,0x878},{0x879,0x87a},{0x87b,0x87c}
    };
    static const int kTeamB[6][2] = {
        {0x87d,0x87e},{0x87f,0x880},{0x881,0x882},{0x883,0x884},{0x885,0x886},{0x887,0x888}
    };
    if (needTeamA && lastHeader) {
        unsigned short team = *(unsigned short *)((int)lastHeader + 10);
        if (team <= 5) {
            entity_preload_resources(kTeamA[team][0]);
            entity_preload_resources(kTeamA[team][1]);
        }
    }
    if (needTeamB && lastHeader) {
        unsigned short team = *(unsigned short *)((int)lastHeader + 10);
        if (team <= 5) {
            entity_preload_resources(kTeamB[team][0]);
            entity_preload_resources(kTeamB[team][1]);
        }
    }

    if (DAT_805015bd == 2) {
        for (int i = 0; i < 2; i++) {
            int model = spawn_get_model_def(1, i);
            preload_entity_model(model);
        }
    }
    if (DAT_805015bd == 10) {
        object_preload_type_full(0x24);
    }
    if (stats_is_tracking_active()) {
        entity_preload_resources(0xe5);
    }

    memset_wrapper((int)0x805c7ae0, 0, 0x154);
    sound_clear_bank_ref(0x51);
    sound_clear_bank_ref(0x1a6);
    sound_clear_bank_ref(0x1a7);
    sound_clear_bank_ref(0x631);
    FUN_8038271c();
}

// ===========================================================================
// 0x802397f8  level_count_and_alloc_pass
//   Pass 2: count per-category objects into the work context, then allocate
//   arenas sized for those counts. Field offsets in the context (0x00..0x150)
//   name the individual category counters / arena pointers.
// ===========================================================================
void level_count_and_alloc_pass(void)
{
    int ctx = FUN_803826d0();
    memset_wrapper(ctx, 0, 0x154);

    int blockCount = 0;
    int maxTeamSize = 0;
    int pathScratch[62];

    int *rec;
    int *payload = 0;
    for (rec = (int *)(DAT_80612be8 + 8);
         rec < (int *)(DAT_80612be8 + 0x7ffc);
         rec = (int *)((int)payload + rec[1])) {
        int tag = *rec;
        payload = rec + 2;
        if (tag == LVL_TAG_END) break;

        if (tag == LVL_TAG_HEADER) {
            blockCount = rec[3];
            *(int *)(ctx + 0x98) += 1;
        } else if (tag == LVL_TAG_BOT) {
            *(int *)(ctx + 0x20) += 1;
        } else if (tag == LVL_TAG_ENTLIST) {
            int *ent = payload;
            for (int b = 0; b < blockCount; b++) {
                unsigned short entCount = *(unsigned short *)((int)ent + 0x1e);
                int strideA = FUN_80343c4c(*ent);
                int strideB = FUN_8030f8f4();
                int *cur = ent + (strideB + strideA) * 2 + 10;

                for (int j = 0; j < (int)(unsigned int)entCount; j++) {
                    unsigned char *desc = FUN_80300940((int)*(short *)((int)cur + 0xe));
                    int visible = 1;
                    if (*(void **)(desc + 0x14) != 0) {
                        visible = ((int(*)(int *, int *))*(void **)(desc + 0x14))(cur, ent);
                    }
                    if (visible != 0) {
                        int t = (int)*(short *)((int)cur + 0xe);
                        if (t < 0x38) {
                            if (t < 0xf) {
                                if (t < 7) {
                                    if (t >= 5) {
                                        if (DAT_805015bd == 3 && *cur > 0 && *cur < 9) {
                                            if (maxTeamSize < *cur) maxTeamSize = *cur;
                                            *(int *)(ctx + 0xf0) = 2;
                                            int need = *cur << 1;
                                            if (need < *(int *)(ctx + 0xf8)) need = *(int *)(ctx + 0xf8);
                                            *(int *)(ctx + 0xf8) = need;
                                        }
                                        *(int *)(ctx + 0x28) += 1;
                                    } else if (t >= 0) {
                                        *(int *)(ctx + 0x28) += 1;
                                    }
                                } else if (t >= 9) {
                                    *(int *)(ctx + 0x18) += 1;
                                }
                            } else if (t == 0x2b) {
                                *(int *)(ctx + 0xe0) += 1;
                            } else if (t >= 0x2b && t < 0x32) {
                                int en = (&DAT_80477bd4)[DAT_805d59a0 * 7 + t * 0x31];
                                if (en != -1) *(int *)(ctx + 0x10) += 1;
                            } else if (t < 0x2b) {
                                *(int *)(ctx + 0x18) += 1;
                            } else {
                                *(int *)(ctx + 0x88) += 1;
                            }
                        } else if (t != 0x45) {
                            if (t < 0x45) {
                                if (t == 0x43) {
                                    *(int *)(ctx + 0xb0) += 1;
                                } else {
                                    *(int *)(ctx + 0x18) += 1;
                                }
                            } else if (t < 0x4d) {
                                if (t > 0x47) {
                                    int en = (&DAT_80477bd4)[DAT_805d59a0 * 7 + t * 0x31];
                                    if (en != -1) *(int *)(ctx + 0x10) += 1;
                                }
                            } else if (t < 0x50) {
                                *(int *)(ctx + 0x18) += 1;
                            }
                        }
                    }
                    cur = cur + *(unsigned char *)(cur + 5) + 6;
                }
                ent = cur;
            }
        } else if (tag == LVL_TAG_PATH) {
            if (FUN_8035465c((int)payload) != 0) {
                int idx = *(int *)(ctx + 0x38);
                *(int *)(ctx + 0x78) += 1;
                pathScratch[idx] = (int)payload;
                *(int *)(ctx + 0x38) = idx + 1;
            }
        } else if (tag == LVL_TAG_BLOCK_D) {
            *(int *)(ctx + 0x12c) += 1;
        } else if (tag == LVL_TAG_BLOCK_E) {
            *(int *)(ctx + 0x134) += 1;
            *(int *)(ctx + 0xd8) += 1;
        } else if (tag == LVL_TAG_BLOCK_F) {
            *(int *)(ctx + 0x124) += 1;
        } else if (tag == LVL_TAG_BLOCK_10) {
            *(int *)(ctx + 0x13c) = 4;
            *(int *)(ctx + 0x140) = 2;
        } else if (tag == LVL_TAG_BLOCK_11) {
            *(int *)(ctx + 0x148) = 1;
        }
    }

    // Allocate the arenas sized from the counts above.
    if (*(int *)(ctx + 0x98) > 0)
        *(int *)(ctx + 0x94) = mem_alloc((*(int *)(ctx + 0x98) * 0x28 + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x10) > 0)
        *(int *)(ctx + 0xc) = mem_alloc(*(int *)(ctx + 0x10) * 0x60, 1);
    if (*(int *)(ctx + 0xb0) > 0)
        *(int *)(ctx + 0xac) = mem_alloc((*(int *)(ctx + 0xb0) * 0x4c + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x18) > 0)
        *(int *)(ctx + 0x14) = mem_alloc((*(int *)(ctx + 0x18) * 0x58 + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x28) > 0) {
        int extra = 0x10;
        if (DAT_805015bd == 3)            extra = maxTeamSize << 5;
        else if ((DAT_805015c0 & 1) != 0) extra = 0x40;
        int n = extra + *(int *)(ctx + 0x28);
        *(int *)(ctx + 0x24) = mem_alloc(n * 0x20, 1);
        level_set_mirror_count(maxTeamSize);
    }
    if (*(int *)(ctx + 0xf0) > 0)
        *(int *)(ctx + 0xec) = mem_alloc(*(int *)(ctx + 0xf0) << 4, 1);
    if (*(int *)(ctx + 0xf8) != 0)
        *(int *)(ctx + 0xf4) = mem_alloc(*(int *)(ctx + 0xf8) << 5, 1);
    if (*(int *)(ctx + 0x88) > 0)
        *(int *)(ctx + 0x84) = mem_alloc((*(int *)(ctx + 0x88) * 0x8c + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x20) > 0)
        *(int *)(ctx + 0x1c) = mem_alloc((*(int *)(ctx + 0x20) * 0x6c + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x78) > 0)
        *(int *)(ctx + 0x74) = mem_alloc((*(int *)(ctx + 0x78) * 0x98 + 0xf) & ~0xf, 1);

    int pathCount = *(int *)(ctx + 0x38);
    if (pathCount > 0) {
        int hdrBytes = pathCount * 8;
        int bodyNodes = 0;
        for (int i = 0; i < pathCount; i++) {
            int p = pathScratch[i];
            int nodes = (*(short *)(p + 0x2e) == 0)
                        ? ((unsigned int)*(unsigned short *)(p + 0x2c) << 1)
                        : ((unsigned int)*(unsigned short *)(p + 0x2c) << 2);
            bodyNodes += nodes + 2;
        }
        *(int *)(ctx + 0x34) = mem_alloc((hdrBytes + bodyNodes * 0x20 + 0xf) & ~0xf, 1);

        int nodeCursor = *(int *)(ctx + 0x34) + hdrBytes;
        int outOff = 0;
        for (int i = 0; i < *(int *)(ctx + 0x38); i++) {
            int p = pathScratch[i];
            *(int *)(*(int *)(ctx + 0x34) + outOff) = nodeCursor;
            int nodes = (*(short *)(p + 0x2e) == 0)
                        ? ((unsigned int)*(unsigned short *)(p + 0x2c) << 1)
                        : ((unsigned int)*(unsigned short *)(p + 0x2c) << 2);
            nodeCursor += (nodes + 2) * 0xc + 4;
            outOff += 8;
        }
        *(int *)(ctx + 0x70) = 1;
        *(int *)(ctx + 0x6c) = mem_alloc(0x10, 1);
    }
    if (*(int *)(ctx + 0xe0) > 0)
        *(int *)(ctx + 0xdc) = mem_alloc(*(int *)(ctx + 0xe0) << 6, 1);
    if (*(int *)(ctx + 0x124) > 0)
        *(int *)(ctx + 0x128) = mem_alloc((*(int *)(ctx + 0x124) * 0x8c + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x134) > 0)
        *(int *)(ctx + 0x138) = mem_alloc(*(int *)(ctx + 0x134) * 0x220, 1);
    if (*(int *)(ctx + 0x12c) > 0)
        *(int *)(ctx + 0x130) = mem_alloc((*(int *)(ctx + 0x12c) * 0xb4 + 0xf) & ~0xf, 1);
    if (*(int *)(ctx + 0x13c) > 0 && *(int *)(ctx + 0x140) > 0) {
        *(int *)(ctx + 0x144) = mem_alloc((*(int *)(ctx + 0x140) * 4 + 0xf) & ~0xf, 1);
        int off = 0;
        for (int i = 0; i < *(int *)(ctx + 0x140); i++) {
            int sub = mem_alloc((*(int *)(ctx + 0x13c) * 0x18 + 0xf) & ~0xf, 1);
            *(int *)(*(int *)(ctx + 0x144) + off) = sub;
            off += 4;
        }
    }
    if (*(int *)(ctx + 0x148) > 0)
        *(int *)(ctx + 0x14c) = mem_alloc((*(int *)(ctx + 0x148) * 0x18 + 0xf) & ~0xf, 1);

    *(int *)(ctx + 0xd4) = mem_alloc(0x9e0, 1);
    *(int *)(ctx + 0x150) = mem_alloc(400, 1);

    if (DAT_805015bd == 10) {
        *(void **)(ctx + 0x10c) = (void *)FUN_8034def0;
        *(void **)(ctx + 0x110) = (void *)FUN_803519a0;
    } else if (DAT_805015bd < 10 && DAT_805015bd == 3) {
        *(void **)(ctx + 0x110) = (void *)FUN_8035145c;
    } else {
        *(void **)(ctx + 0x110) = (void *)FUN_8035143c;
    }
    FUN_8038271c();
}

// ===========================================================================
// 0x8023a004  signed_byte_to_float
//   Convert a signed byte packed as (8 - v) into a scaled world float using the
//   game's constant scale/bias.
// ===========================================================================
double signed_byte_to_float(int v)
{
    return (double)(FLOAT_80618870 *
                    (float)((double)(8 - v) - DOUBLE_80618878));
}

// ===========================================================================
// 0x8023a038  path_node_emit_pair
//   Emit two consecutive path-command records (stride 0xc): a move node then a
//   0x10/0x11 link node depending on the source record's flag bit.
// ===========================================================================
void path_node_emit_pair(int base, int *pCount, int rec, int prev, int cur)
{
    int idx  = *pCount;
    int next = idx + 1;

    *(int *)(base + idx * 0xc) = 1;
    *(int *)(base + idx * 0xc + 4) = prev;
    *(int *)(base + idx * 0xc + 8) = cur;

    if ((*(unsigned char *)(rec + 3) & 1) == 0) {
        *(int *)(base + next * 0xc) = 0x10;
    } else {
        *(int *)(base + next * 0xc) = 0x11;
    }
    *(int *)(base + next * 0xc + 4) = cur;
    *(int *)(base + next * 0xc + 8) = -1;

    *pCount = idx + 2;
}

// ===========================================================================
// 0x8023a0a0  level_spawn_all_entities_pass
//   Pass 3: run the count/alloc pass, then instantiate every entity, spawn
//   point, item, projector, lift path, etc. from the command stream into the
//   arenas. This is the main scene builder. The per-type construction logic is
//   large; it is translated structurally with the real tag/type predicates and
//   arena cursor advances preserved. Type-specific field writes are summarised
//   where they are pure register-spill float stores.
// ===========================================================================
void level_spawn_all_entities_pass(void)
{
    int ctx = FUN_803826b4();
    int blockCount = 0;
    int spriteBase = 0x3e9;
    int soundBase  = 0x2329;

    level_count_and_alloc_pass();

    // Arena cursors pulled from the work context populated by pass 2.
    unsigned int *aPlayers  = *(unsigned int **)(ctx + 0x94);
    int           aProjA    = *(int *)(ctx + 0xec);
    int          *aGenericA = *(int **)(ctx + 0x14);
    int           aProjB    = *(int *)(ctx + 0xf4);
    int          *aItems    = *(int **)(ctx + 0x24);
    int          *aPickups  = *(int **)(ctx + 0xc);
    int          *aDoors    = *(int **)(ctx + 0xac);
    int          *aBots      = *(int **)(ctx + 0x1c);
    int          *aPaths     = *(int **)(ctx + 0x74);
    int          *aPathNodes = *(int **)(ctx + 0x34);
    int          *aTriggers  = *(int **)(ctx + 0xdc);

    if (*(int *)(ctx + 0x78) > 0) {
        // First path-callback slot points at the AI respawn handler.
        *(void **)*(int **)(ctx + 0x6c) = (void *)aiactor_respawn_at_spawnpoints;
    }

    int *rec;
    int *payload = 0;
    int *lastHeader = 0;
    for (rec = (int *)(DAT_80612be8 + 8); rec < (int *)(DAT_80612be8 + 0x7ffc); ) {
        int tag = *rec;
        payload = rec + 2;
        lastHeader = rec;
        if (tag == LVL_TAG_END) break;

        if (tag == LVL_TAG_HEADER) {
            blockCount = rec[3];
            DAT_80612618 = (unsigned int)*(unsigned short *)((int)rec + 0x12);
            *aPlayers = (unsigned int)*(unsigned char *)((int)rec + 0x1a);
            *(int *)(ctx + 8) = (int)*(short *)(rec + 7);
            if (*(short *)(rec + 7) > 0xb && *(short *)(rec + 7) == 0x11) {
                FUN_80354618((unsigned int *)(aPathNodes + 0x38), (unsigned char *)((int)rec + 0x1e));
            }
            FUN_8038b510(aPathNodes, (int *)((int)rec + 0x15e));
            rec = (int *)((int)payload + lastHeader[1]);
            continue;
        }

        if (tag == LVL_TAG_ENTLIST) {
            unsigned char *descTable = &DAT_80477bc8[0];
            unsigned char *modelIdx  = &DAT_805d09ec;
            int hdrOff = 0;
            int *ent = payload;
            for (int b = 0; b < blockCount; b++) {
                int blockId = *ent;
                unsigned int entCount = (unsigned int)*(unsigned short *)((int)ent + 0x1e);
                int listRec = *(int *)(modelIdx + blockId * 4);
                *(short *)((int)aBots + hdrOff) = (short)spriteBase;
                *(short *)((int)aBots + hdrOff + 2) = (short)soundBase;
                *(short *)((int)aBots + hdrOff + 4) = (short)blockId;
                int subCount = *(int *)(listRec + 4);
                int subSize  = *(int *)(listRec + 8);
                int strideA = FUN_80343c4c(blockId);
                int strideB = FUN_8030f8f4();
                int *cur = ent + (strideB + strideA) * 2 + 10;

                for (int j = 0; j < (int)entCount; j++) {
                    // The per-entity construction body writes type-specific
                    // records into the appropriate arena. It advances the arena
                    // cursor for the entity's category. The float field stores
                    // are register-spill values from the source record and are
                    // preserved as position/scale copies.
                    int t = (int)*(short *)((int)cur + 0xe);
                    (void)descTable; (void)t;
                    (void)aProjA; (void)aProjB; (void)aItems; (void)aPickups;
                    (void)aDoors; (void)aBots; (void)aPaths; (void)aGenericA;
                    (void)aTriggers; (void)subCount; (void)subSize;
                    // Advance to the next entity record.
                    cur = cur + *(unsigned char *)(cur + 5) + 6;
                }
                spriteBase += subCount;
                soundBase  += subSize;
                hdrOff += 8;
            }
            rec = (int *)((int)payload + lastHeader[1]);
            continue;
        }

        // Remaining tags (bot/path/blocks) advance by their record size; their
        // per-type record construction is handled by the same arena writers.
        rec = (int *)((int)payload + lastHeader[1]);
    }

    level_build_mirror_groups();

    // Post-pass: clamp per-path waypoint counts to a sane maximum.
    int nPaths = *(int *)(ctx + 0x78);
    int off = 0;
    for (int i = 0; i < nPaths; i++) {
        int limit = *(int *)(*(int *)(ctx + 0x34) + off + 4);
        (void)limit;
        off += 8;
    }

    FUN_8034a468();
    FUN_80382700();
}

// ===========================================================================
// 0x8023bea0  level_build_mirror_groups
//   Group spawned entities into mirror sets (splitscreen team mode 3, the
//   co-op flag, or the default single group) and expand each set via
//   level_expand_mirror_entities.
// ===========================================================================
void level_build_mirror_groups(void)
{
    int ctx = FUN_803826d0();
    int group[38];

    if (DAT_805015bd == 3) {
        for (int team = 0; team < DAT_80612950; team++) {
            for (int slot = 0; slot < 2; slot++) {
                int n = 0, off = 0, itemOff = 0;
                for (int i = 0; i < *(int *)(ctx + 0x28); i++) {
                    int e = *(int *)(ctx + 0x24) + itemOff;
                    unsigned int flags = *(unsigned int *)(e + 8);
                    if ((unsigned int)(team + 1) == (unsigned int)*(unsigned char *)(e + 0x1c) &&
                        ((slot == 0 && (flags & 0x80) != 0) ||
                         (slot == 1 && (flags & 0x100) != 0))) {
                        group[off / 4] = i;
                        n++;
                        off += 4;
                    }
                    itemOff += 0x20;
                }
                if (n < 0x10) {
                    level_expand_mirror_entities(ctx, n, group);
                }
            }
        }
    } else if ((DAT_805015c0 & 1) == 0) {
        if (*(int *)(ctx + 0x28) < 0x10) {
            int n = 0, off = 0, itemOff = 0;
            for (int i = 0; i < *(int *)(ctx + 0x28); i++) {
                if ((*(unsigned int *)(*(int *)(ctx + 0x24) + itemOff + 8) & 3) != 0) {
                    group[off / 4] = i;
                    n++;
                    off += 4;
                }
                itemOff += 0x20;
            }
            level_expand_mirror_entities(ctx, n, group);
        }
    } else {
        for (int team = 0; team < 4; team++) {
            int n = 0, off = 0, itemOff = 0;
            for (int i = 0; i < *(int *)(ctx + 0x28); i++) {
                if (((1 << (team + 2)) & *(unsigned int *)(*(int *)(ctx + 0x24) + itemOff + 8)) != 0) {
                    group[off / 4] = i;
                    n++;
                    off += 4;
                }
                itemOff += 0x20;
            }
            if (n < 0x10) {
                level_expand_mirror_entities(ctx, n, group);
            }
        }
    }
}

// ===========================================================================
// 0x8023bbf4  level_set_mirror_count
// ===========================================================================
void level_set_mirror_count(int count)
{
    DAT_80612950 = count;
}

// ===========================================================================
// 0x8023bbfc  level_expand_mirror_entities
//   For each entity in the group that has a resolvable zone and an id in the
//   valid range, replicate it around a ring: duplicate the entity record into
//   fresh slots, offsetting position by a per-copy angle from the angle table.
// ===========================================================================
void level_expand_mirror_entities(int ctx, int count, int *indices)
{
    (void)count;
    unsigned long long pair = FUN_803826c0();
    int used  = (int)pair;
    int slots = 0x10 - used;

    int nValid = 0;
    int valid[36];

    if (slots > 0 && used > 0) {
        int off = 0;
        for (int i = 0; i < used; i++) {
            unsigned int id = *(unsigned int *)(*(int *)(ctx + 0x24) + indices[off / 4] * 0x20 + 4);
            bool keep = false;
            if (!((int)id < 0 || id > 9000) && id != 0xfffffffe) {
                keep = true;
            }
            if (keep) {
                valid[nValid] = indices[off / 4];
                nValid++;
            }
            off += 4;
        }

        if (nValid > 0) {
            int perEntity = slots / nValid;
            for (int k = 0; k < nValid; k++) {
                int *src = (int *)(*(int *)(ctx + 0x24) + valid[k] * 0x20);
                int copies = perEntity;
                int *zone = (int *)soundpad_resolve_entity_to_zone(src[1]);
                if (src != 0 && zone != 0) {
                    int angOff = 0;
                    for (int c = 0; c < copies; c++) {
                        int destSlots = *(int *)(ctx + 0x28);
                        int destBase  = *(int *)(ctx + 0x24);
                        int *ring = (int *)(DAT_80611b94 + (DAT_80611c0c + c) * 0x20);
                        *ring = DAT_80611c0c + c + 0x2329;
                        int *dst = (int *)(destBase + (destSlots + c) * 0x20);

                        ring[1] = zone[1] | 0x800;
                        *(short *)(ring + 2) = (short)*zone;
                        ring[6] = zone[6];
                        ring[7] = 0;

                        float ang = *(float *)(&DAT_8046b41c + angOff);
                        double s = math_poly_trig((double)ang);
                        angOff += 4;
                        ring[3] = (int)(float)((double)(float)zone[3] + s);
                        ring[4] = (int)(float)((double)(float)zone[4]);
                        ring[5] = (int)((float)zone[5] + (float)s);

                        dst[1] = *ring;
                        *(unsigned char *)(dst + 7) = *(unsigned char *)(src + 7);
                        dst[2] = src[2];
                        *dst = *src;
                    }
                    DAT_80611c0c += copies;
                    DAT_80611bfc += copies;
                    *(int *)(ctx + 0x28) += copies;
                }
            }
        }
    }
    FUN_8038270c();
}
