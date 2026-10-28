// Batch: batch_964 | Module: arcade/challenge editor map asset preloader
// Address range: 0x8033c558 - 0x8033c5dc
// Date: 2026-09-12
//
// The arcade/challenge editor's asset-preload pass (driven by aveditor_preload_assets @0x80311f20).
// Before entering the editor it allocates the working buffer, then queues every asset the current
// map needs for streaming: the node-roster entities, the six-per-pad tile textures ("pad_data_tile_
// %d_%d.war"), the cutscene entity set (when in cutscene mode), and every placed palette entry's
// entity/animation resources. Tile counts come from the pad-descriptor table at DAT_8047c158
// (0x140 stride), placed-cell records from the DAT_80477bxx block (0xc4 stride, 0x50 entries).
//
//   aveditor_get_pad_tile_count   0x8033c558  tile count for a pad index (descriptor byte - 1).
//   aveditor_preload_node_entities 0x8033c574  queue every node-roster entity for streaming.
//   aveditor_preload_map_assets   0x8033c5dc  full map asset preload (buffer + tiles + palette).
//
// NON-MATCHING: all three are emitted with the _savegpr_/_restgpr_ runtime helpers
// (FUN_803826dc/FUN_803826f0/FUN_80382728/FUN_8038273c) - pure register spill, omitted; the streamer
// context they thread is expressed as a plain local. The 8 register pass-through slots forwarded to
// entity_preload_resources / mem_alloc / the message builder are opaque forwarded registers
// (Compile-Clean rule 3). This TU includes only common.h.

#include "common.h"

extern "C" {

extern int  DAT_80612d68;   // editor working buffer (0x1e5b0 bytes)

// pad-descriptor table (0x140 stride): +0 tile-count byte at DAT_8047c158, +1 alt count DAT_8047c159
extern char DAT_8047c158;
extern char DAT_8047c159;

// placed-node entity table (0xe0 stride, 0x2b entries): id at +0, type-record at +4
extern int  DAT_8042e2c4;   // node type-record base (+4 of entry 0)
extern int  DAT_8042e2c8;   // node id base (+8 of entry 0)

// placed-cell record block (0xc4 stride, 0x50 entries)
extern int  DAT_80477bca;   // cell kind field (short)
extern int  DAT_80477bd4;   // cell entity-id array base (6 x 0x1c)
extern int  DAT_80477c80;   // cell flags field

extern int  DAT_805d0aa0;   // per-tile stream-request scratch (0x40 stride, 0xb40 pad stride)

extern int  s_pad_data_tile_d__d_war_80481a90; // "pad_data_tile_%d_%d.war"

// named cross-module helpers (call-site-accurate types; resolve via shared headers)
extern int  mem_alloc(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                      unsigned long long r6, unsigned long long r7, unsigned long long r8,
                      unsigned long long r9, unsigned long long r10, int size, int align,
                      unsigned a11, unsigned a12, unsigned a13, unsigned a14, unsigned a15,
                      unsigned a16);
extern void memset_wrapper(int dst, int value, int len);
extern int  game_is_in_cutscene_mode(void);
extern int  player_ai_resolve_entity_callback(int record);
extern unsigned long long entity_preload_resources(unsigned long long f1, unsigned long long r4,
                                                   unsigned long long r5, unsigned long long r6,
                                                   unsigned long long r7, unsigned long long r8,
                                                   unsigned long long r9, unsigned long long r10,
                                                   int entityTypeId);
extern void stream_queue_raw_asset(void);
extern int  aveditor_get_palette_entry_value(int index);

// _savegpr_/_restgpr_ runtime helpers returning the threaded streamer context
extern unsigned long long FUN_803826dc(void);
extern unsigned long long FUN_803826f0(void);
extern void FUN_80382728(void);
extern void FUN_8038273c(void);

// raw FUN_ helpers not yet named
extern void FUN_80387fb8(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                         unsigned long long r6, unsigned long long r7, unsigned long long r8,
                         unsigned long long r9, unsigned long long r10, int scratch, int fmt,
                         int argA, int argB, unsigned a13, unsigned a14, unsigned a15, unsigned a16);
extern unsigned long long FUN_80348f88(unsigned long long f1, unsigned long long r4,
                                       unsigned long long r5, unsigned long long r6,
                                       unsigned long long r7, unsigned long long r8,
                                       unsigned long long r9, unsigned long long r10);

} // extern "C"

// -----------------------------------------------------------------------------

// Tile count for a pad index: the descriptor's leading byte (0x140 stride) minus one.
int aveditor_get_pad_tile_count(int padIndex)
{
    return (unsigned char)(&DAT_8047c158)[padIndex * 0x140] - 1;
}

// Queue every node-roster entity for streaming. Walks the 0x2b-entry node table (0xe0 stride):
// entries with a valid id (+0xe0 != -1) resolve their entity type and prime the streamer.
void aveditor_preload_node_entities(unsigned long long r3, unsigned long long r4,
                                    unsigned long long r5, unsigned long long r6,
                                    unsigned long long r7, unsigned long long r8,
                                    unsigned long long r9, unsigned long long r10)
{
    unsigned long long ctx = FUN_803826f0();
    int off = 0xe0;
    for (int i = 1; i < 0x2c; i++, off += 0xe0) {
        if (*(int*)(&DAT_8042e2c8 + off) != -1) {
            int typeId = player_ai_resolve_entity_callback((int)(intptr_t)(&DAT_8042e2c4 + off));
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, typeId);
        }
    }
    FUN_8038273c();
}

// Full editor map asset preload. Allocates the 0x1e5b0-byte working buffer and clears its resource
// table, then queues: (cutscene mode only) the 0x1d-entry cutscene entity set; the six-per-pad tile
// textures via the "pad_data_tile_%d_%d.war" name; three fixed editor entities (0x88c-0x88e); every
// placed palette entry's entity/animation resources; and finally the node-roster entities.
void aveditor_preload_map_assets(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                                 unsigned long long r6, unsigned long long r7, unsigned long long r8,
                                 unsigned long long r9, unsigned long long r10, unsigned a9,
                                 unsigned a10, unsigned a11, unsigned a12, unsigned a13, unsigned a14,
                                 unsigned a15, unsigned a16)
{
    unsigned long long ctx = FUN_803826dc();
    DAT_80612d68 = mem_alloc(ctx, r4, r5, r6, r7, r8, r9, r10, 0x1e5b0, 1, a11, a12, a13, a14, a15,
                             a16);
    *(int*)(DAT_80612d68 + 0x5a0) = 0;
    memset_wrapper(DAT_80612d68 + 0xa0, 0, 0x500);

    if (game_is_in_cutscene_mode() != 0) {
        for (int i = 0; i < 0x1d; i++) {
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, i + 0x7b3);
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, i + 2000);
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, i + 0x7ed);
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, i + 0x80a);
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, i + 0x827);
            ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, i + 0x844);
        }
    }

    // widest tile-row count across all pads (alt-count byte, 0x140 stride)
    unsigned maxRows = 0;
    for (int i = 0, off = 0; i < 0x2d; i++, off += 0x140) {
        unsigned c = (unsigned char)(&DAT_8047c159)[off];
        if (maxRows < c) {
            maxRows = c;
        }
    }

    int rowBase = 0;
    for (int row = 0; row < (int)maxRows; row++, rowBase += 0x40) {
        int col = 0, padOff = 0;
        do {
            FUN_80387fb8(ctx, r4, r5, r6, r7, r8, r9, r10,
                         (int)(intptr_t)(&DAT_805d0aa0 + rowBase + padOff),
                         (int)(intptr_t)&s_pad_data_tile_d__d_war_80481a90, col + 1, row + 1,
                         a13, a14, a15, a16);
            stream_queue_raw_asset();
            col++;
            padOff += 0xb40;
        } while (col < 6);
    }

    ctx = FUN_80348f88(ctx, r4, r5, r6, r7, r8, r9, r10);
    ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, 0x88c);
    ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, 0x88d);
    ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, 0x88e);

    // placed-cell records (0xc4 stride, 0x50 entries)
    int cellOff = 0;
    for (int i = 0; i < 0x50; i++, cellOff += 0xc4) {
        unsigned flags = *(unsigned*)((int)&DAT_80477c80 + cellOff);
        short kind = *(short*)((int)&DAT_80477bca + cellOff);
        if ((flags & 0x100) != 0 || kind == 8 || kind == 9) {
            continue;
        }
        if ((flags & 4) == 0) {
            int val = aveditor_get_palette_entry_value(i);
            if (-1 < val) {
                val = aveditor_get_palette_entry_value(i);
                ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, val);
            }
        } else {
            int entOff = 0;
            for (int j = 0; j < 6; j++, entOff += 0x1c) {
                int typeId = *(int*)((int)&DAT_80477bd4 + cellOff + entOff);
                if (-1 < typeId) {
                    ctx = entity_preload_resources(ctx, r4, r5, r6, r7, r8, r9, r10, typeId);
                }
            }
        }
    }

    aveditor_preload_node_entities(ctx, r4, r5, r6, r7, r8, r9, r10);
    FUN_80382728();
}
