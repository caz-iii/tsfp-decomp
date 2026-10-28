// ============================================================================
// Address range: 0x803694e4 - 0x803699b3 | batch_1032 | 2026-09-12
//
// Banner texture streaming loader - a 2-slot streaming loader for the arcade
// "banner"/scoreboard textures that sit right after the ekun grid minigame
// board module. Each of the two slots is a 0x24-byte record in the table at
// DAT_805eca70 tagged 'BANK' (0x42414e4b); a slot holds the scratch buffer
// offset, the render-target/texture id, an async DVD handle, the source name
// index and a load state (0 idle / 1 queued / 2 reading). The loader can pull a
// texture synchronously (asset_read_to_buffer + render_target_load_from_data)
// or asynchronously via the DVD2 async path, and a per-frame poll promotes the
// finished async reads into their render targets.
//
//   banner_name_ensure_ext   0x803694e4  ensure a name ends in the ".gct" tag
//   banner_slot_load         0x8036954c  load one banner texture (sync or async)
//   banner_poll_async        0x803696a4  per-frame: finish/kick async slot loads
//   banner_stream_reset      0x8036976c  measure sizes, reset both slots
//   banner_stream_init       0x8036985c  assign scratch buffers, kick slot 0
//   banner_slot_release_tex  0x80369934  release a slot's render target
//   banner_stream_shutdown   0x80369994  close handles + release both slots
//
// NOTE: non-matching decomp. The asset/DVD/sprite engine entry points take a
// wide 8-float-register + int-tail ABI in this build; the generated prototypes
// summarise them differently, so the calls here go through typed function-
// pointer casts to the raw entry addresses (matching the batch_1030/1031
// approach). The 2-slot table layout (stride 0x24 at DAT_805eca70) and the
// 'BANK' tag are reproduced from the original field offsets.

#include "common.h"
#include "generated_decls.h"

// ---- 2-slot banner table (stride 0x24 at 0x805eca70) -----------------------
extern int  DAT_805eca70;   // +0x00 slot render-target id (current)
extern int  DAT_805eca74;   // +0x04 tag 'BANK'
extern int  DAT_805eca78;   // +0x08
extern int  DAT_805eca7c;   // +0x0c
extern int  DAT_805eca80;   // +0x10 scratch buffer offset
extern int  DAT_805eca84;   // +0x14 texture/render-target id (loaded)
extern int  DAT_805eca88;   // +0x18 async DVD handle (-1 = none)
extern int  DAT_805eca8c;   // +0x1c source name index
extern int  DAT_805eca90;   // +0x20 load state (0 idle / 1 queued / 2 reading)
extern int  DAT_805ecaac;   // slot-1 async handle mirror (guard)

extern int  DAT_805ec9f8;   // source name table (per-slot names, stride 4)
extern int  DAT_80612eb8;   // max banner asset size (rounded)
extern int  DAT_806112ec;   // the ".gct" comparison tag bytes ('g','c','t')
extern int  DAT_806126c8;   // active pack/context handle for async load

// name-string helpers (strlen / 3-byte tag compare).
extern int  FUN_8038b5c8(int str);              // strlen
extern int  FUN_8038b378(unsigned int* a, unsigned int* b);  // 3-byte tag compare

// scratch-scope enter/leave for this module.
extern unsigned long long FUN_803826e4(void);   // scratch enter (self ctx)
extern int  FUN_803826d4(void);                  // scratch enter (count)
extern void FUN_80382720(void);                  // scratch leave (reset)
extern void FUN_80382730(void);                  // scratch leave (poll/init)

// get_resource_slot_size / get_resource_data_base resolve via generated_externs.h.

// raw engine entry ABIs (typed casts; see file NOTE).
typedef unsigned long long (*SpriteLoadFn)(double, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long,
    int, int, int, int, unsigned int, int, int, int);
typedef int (*AssetSizeFn)(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned char*);
typedef unsigned long long (*AssetReadFn)(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned char*,
    int, unsigned int, int, unsigned int, int, int, int);
typedef unsigned long long (*RtLoadFn)(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long,
    int, int*, int*, int, unsigned int, int, int, int);
typedef int (*AsyncFromPackFn)(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned char*,
    int, int, int, unsigned int, int, int, int);
typedef void (*DvdReadAsyncFn)(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long,
    int, int, int, unsigned int, int, int, int, int);

// forward decls (raw addresses -> named).
unsigned long long banner_slot_load(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long,
    int srcIdx, int slot, int async, int, unsigned int, int, int, int);   // 0x8036954c
void banner_slot_release_tex(int slot);                                    // 0x80369934

// -------------------------------------------------------------------------
// 0x803694e4  banner_name_ensure_ext - ensure a name ends in the ".gct" tag.
//   If the last 3 chars of the name string are not the "gct" tag, overwrite
//   them with 'g','c','t'. (str points at a NUL-terminated path; strlen via
//   FUN_8038b5c8, 3-byte compare via FUN_8038b378.)
// -------------------------------------------------------------------------
int banner_name_ensure_ext(int str)
{
    int len = FUN_8038b5c8(str);
    if (FUN_8038b378((unsigned int*)(str + len - 3), (unsigned int*)&DAT_806112ec) == 0) {
        len = FUN_8038b5c8(str);
        char* end = (char*)(str + len);
        end[-3] = 'g';
        end[-2] = 'c';
        end[-1] = 't';
    }
    return str;
}

// -------------------------------------------------------------------------
// 0x8036954c  banner_slot_load - load one banner texture (sync or async).
//   Opens the slot's source texture, measures its (page-rounded) size, then in
//   sync mode reads it straight into the scratch buffer and builds the render
//   target; in async mode kicks a DVD2 async read (or a pack async load) and
//   marks the slot state queued/reading.
// -------------------------------------------------------------------------
unsigned long long banner_slot_load(unsigned long long f1, double a2, double a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, int srcIdx, int slot, int async, int p12, unsigned int p13,
    int p14, int p15, int p16)
{
    (void)f1;
    unsigned char hdr[168];
    unsigned long long tex = ((SpriteLoadFn)&sprite_load_single_texture)(
        (double)0, a2, a3, a4, a5, a6, a7, a8,
        (int)hdr, (&DAT_805ec9f8)[srcIdx], async, p12, p13, p14, p15, p16);

    int size = ((AssetSizeFn)&asset_get_size)(tex, a2, a3, a4, a5, a6, a7, a8, hdr);
    unsigned int rounded = (unsigned int)(size + 0x7ff) & 0xfffff800;
    if (rounded == 0) return tex;

    if (async == 0) {
        tex = ((AssetReadFn)&asset_read_to_buffer)(tex, a2, a3, a4, a5, a6, a7, a8, hdr,
            (&DAT_805eca80)[slot * 9], rounded, p12, p13, p14, p15, p16);
        ((RtLoadFn)&render_target_load_from_data)(tex, a2, a3, a4, a5, a6, a7, a8,
            (&DAT_805eca80)[slot * 9], &DAT_805eca70 + slot * 9, &DAT_805eca84 + slot * 9,
            p12, p13, p14, p15, p16);
        (&DAT_805eca70)[slot * 9] = (&DAT_805eca84)[slot * 9];
    } else {
        if ((DAT_805eca88 == -1) && (DAT_805ecaac == -1)) {
            int h = ((AsyncFromPackFn)&asset_load_async_from_pack)(tex, a2, a3, a4, a5, a6, a7, a8,
                hdr, 0, DAT_806126c8, p12, p13, p14, p15, p16);
            (&DAT_805eca88)[slot * 9] = h;
        }
        if ((&DAT_805eca88)[slot * 9] == -1) {
            *(int*)(&DAT_805eca8c + slot * 0x24) = srcIdx;
            (&DAT_805eca90)[slot * 9] = 1;
        } else {
            ((DvdReadAsyncFn)&dvd2_handle_read_async)(tex, a2, a3, a4, a5, a6, a7, a8,
                (&DAT_805eca88)[slot * 9], (&DAT_805eca80)[slot * 9], 0, rounded, 0, p14, p15, p16);
            (&DAT_805eca90)[slot * 9] = 2;
        }
    }
    return tex;
}

// -------------------------------------------------------------------------
// 0x803696a4  banner_poll_async - per-frame: finish/kick async slot loads.
//   For each slot: if it is reading (state 2) and the DVD handle has gone idle,
//   close it and promote the buffer into the render target; if it is queued
//   (state 1) retry the load.
// -------------------------------------------------------------------------
void banner_poll_async(unsigned long long f1, double a2, double a3, unsigned long long a4,
    unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8)
{
    (void)f1;
    unsigned long long ctx = FUN_803826e4();
    int slot = 0;
    int off = 0;
    do {
        int state = *(int*)((int)&DAT_805eca90 + off);
        if (state == 2) {
            if (dvd2_handle_is_busy(*(int*)((int)&DAT_805eca88 + off)) != 0) {
                dvd2_handle_close(*(int*)((int)&DAT_805eca88 + off));
                *(int*)((int)&DAT_805eca88 + off) = -1;
                ctx = ((RtLoadFn)&render_target_load_from_data)(ctx, a2, a3, a4, a5, a6, a7, a8,
                    *(int*)((int)&DAT_805eca80 + off), (int*)((int)&DAT_805eca70 + off),
                    (int*)((int)&DAT_805eca84 + off), 0, 0, 0, 0, 0);
                *(int*)((int)&DAT_805eca70 + off) = *(int*)((int)&DAT_805eca84 + off);
                *(int*)((int)&DAT_805eca90 + off) = 0;
            }
        } else if ((state < 2) && (0 < state)) {
            ctx = banner_slot_load(ctx, a2, a3, a4, a5, a6, a7, a8,
                *(int*)(&DAT_805eca8c + off), slot, 1, 0, 0, 0, 0, 0);
        }
        slot++;
        off += 0x24;
    } while (slot < 2);
    FUN_80382730();
}

// -------------------------------------------------------------------------
// 0x8036976c  banner_stream_reset - measure sizes, reset both slots.
//   Opens each source texture to find the largest (page-rounded) size, records
//   it in DAT_80612eb8, then resets both slot records to idle with a blank
//   texture id and the 'BANK' tag.
// -------------------------------------------------------------------------
void banner_stream_reset(unsigned long long f1, double a2, double a3, unsigned long long a4,
    unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8,
    int p9, int p10, int p11, int p12, unsigned int p13, int p14, int p15, int p16)
{
    (void)f1; (void)p9; (void)p10;
    unsigned char hdr[104];
    int count = FUN_803826d4();
    int nameOff = 0;
    for (int i = 0; i < count; i++) {
        unsigned long long name = banner_name_ensure_ext(*(int*)((int)&DAT_805ec9f8 + nameOff));
        unsigned long long tex = ((SpriteLoadFn)&sprite_load_single_texture)(
            (double)0, a2, a3, a4, a5, a6, a7, a8, (int)hdr,
            *(int*)((int)&DAT_805ec9f8 + nameOff), p11, p12, p13, p14, p15, p16);
        (void)name;
        int size = ((AssetSizeFn)&asset_get_size)(tex, a2, a3, a4, a5, a6, a7, a8, hdr);
        unsigned int rounded = (unsigned int)(size + 0x7ff) & 0xfffff800;
        if ((unsigned int)DAT_80612eb8 < rounded) DAT_80612eb8 = rounded;
        nameOff += 4;
    }

    int slot = 0;
    int off = 0;
    do {
        *(int*)((int)&DAT_805eca80 + off) = 0;
        int blank = sprite_get_texture_id(0);
        *(int*)((int)&DAT_805eca70 + off) = blank;
        *(int*)((int)&DAT_805eca84 + off) = blank;
        *(int*)((int)&DAT_805eca74 + off) = 0x42414e4b;   // 'BANK'
        *(int*)((int)&DAT_805eca78 + off) = 0;
        *(int*)((int)&DAT_805eca7c + off) = 0;
        *(int*)((int)&DAT_805eca88 + off) = -1;
        *(int*)((int)&DAT_805eca90 + off) = 0;
        slot++;
        off += 0x24;
    } while (slot < 2);
    FUN_80382720();
}

// -------------------------------------------------------------------------
// 0x8036985c  banner_stream_init - assign scratch buffers, kick slot 0.
//   Computes the end of the resource data region, lays the two banner buffers
//   out just past it (each DAT_80612eb8 rounded), initialises both slot records,
//   and starts an async load of slot 0.
// -------------------------------------------------------------------------
void banner_stream_init(unsigned long long f1, double a2, double a3, unsigned long long a4,
    unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8)
{
    (void)f1;
    unsigned long long ctx = FUN_803826e4();
    int rounded2 = (DAT_80612eb8 + 0x7ff) * 2 & (int)0xfffff000;
    int slotSize = get_resource_slot_size();
    int dataBase = get_resource_data_base();
    unsigned int cursor = ((unsigned int)((dataBase + slotSize) - rounded2) + 0x1f) & 0xffffffe0;

    int slot = 0;
    int off = 0;
    do {
        *(unsigned int*)((int)&DAT_805eca80 + off) = cursor;
        cursor += (unsigned int)(DAT_80612eb8 + 0x7ff) & 0xfffff800;
        int blank = sprite_get_texture_id(0);
        *(int*)((int)&DAT_805eca70 + off) = blank;
        *(int*)((int)&DAT_805eca84 + off) = blank;
        *(int*)((int)&DAT_805eca74 + off) = 0x42414e4b;   // 'BANK'
        *(int*)((int)&DAT_805eca78 + off) = 0;
        *(int*)((int)&DAT_805eca7c + off) = 0;
        *(int*)((int)&DAT_805eca88 + off) = -1;
        *(int*)((int)&DAT_805eca90 + off) = 0;
        if (slot == 0) {
            ctx = banner_slot_load(ctx, a2, a3, a4, a5, a6, a7, a8, 0, 0, 1, 0, 0, 0, 0, 0);
        }
        slot++;
        off += 0x24;
    } while (slot < 2);
    FUN_80382730();
}

// -------------------------------------------------------------------------
// 0x80369934  banner_slot_release_tex - release a slot's render target.
//   If the slot's loaded texture id differs from the blank id, free its render
//   target, then reset the slot to the blank id.
// -------------------------------------------------------------------------
void banner_slot_release_tex(int slot)
{
    int blank = sprite_get_texture_id(0);
    int cur = (&DAT_805eca84)[slot * 9];
    if (cur != blank) render_target_clear_slot(cur);
    (&DAT_805eca84)[slot * 9] = sprite_get_texture_id(0);
}

// -------------------------------------------------------------------------
// 0x80369994  banner_stream_shutdown - close handles + release both slots.
// -------------------------------------------------------------------------
void banner_stream_shutdown(void)
{
    int slot = 0;
    int off = 0;
    do {
        if (*(int*)((int)&DAT_805eca90 + off) != 0) {
            dvd2_handle_close(*(int*)((int)&DAT_805eca88 + off));
            *(int*)((int)&DAT_805eca88 + off) = -1;
            *(int*)((int)&DAT_805eca90 + off) = 0;
        }
        banner_slot_release_tex(slot);
        slot++;
        off += 0x24;
    } while (slot < 2);
}

// ============================================================================
// Address range: 0x80369a18 - 0x8036a30b | batch_1033 | 2026-09-12
//
// Banner entity + compositor - the higher half of the banner subsystem. The
// banner is a spinning arcade scoreboard/advert object: an entity is spawned
// with three update callbacks (physics, collision, render) plus a render-type
// hook, it flips between the two streamed textures on a timer, and its render
// callback composites the current/next slot textures into a 256x256 scratch
// render target (with a vertical crossfade wipe) that the entity then displays.
//
//   banner_alloc_composite_rt 0x80369a18  allocate the 256x256 composite RT
//   banner_entity_spawn       0x80369a44  spawn the banner entity + hooks
//   banner_collision_cb       0x80369b20  collision response: pick a spin flag
//   banner_entity_update      0x80369be0  per-frame flip timer + reel + spin
//   banner_render_cb          0x80369e0c  render cb: composite slots into the RT
//   banner_composite_draw     0x80369f9c  draw the two-texture crossfade wipe
//   banner_actor_spawn        0x8036a2e8  level actor spawn wrapper -> stream_init
//
// NOTE: non-matching decomp (see the file NOTE above). Entity/collision/GX
// engine helpers resolve through generated_decls.h; the wide-ABI ones use the
// typed casts declared at the top of this file. The banner entity control block
// (offsets +0x98 timer, +0xa0 flags, +0xbc slot index, +0xe4 slot count) hangs
// off the entity's +0x7c pointer, reproduced from the original offsets.

extern int  DAT_806112e8;                    // 256x256 composite render target
extern int  DAT_80610a20;                    // material table base ptr
extern int  DAT_803e0c78, DAT_803e0c7c, DAT_803e0c80;  // spin pivot vector
extern float FLOAT_8061b340, FLOAT_8061b344, FLOAT_8061b348, FLOAT_8061b34c;
extern float FLOAT_8061b350, FLOAT_8061b354, FLOAT_8061b358, FLOAT_8061b35c;
extern double DOUBLE_8061b360;
extern float FLOAT_8061b36c, FLOAT_8061b370, FLOAT_8061b374, FLOAT_8061b378, FLOAT_8061b37c;
extern int  DAT_806109c0;                    // frame counter
extern int  DAT_806108e4;                    // start-frame counter
extern double DOUBLE_8061b288;

// collision_result_restore, collision_alloc_strip_table, entity_register_for_update,
// gx_set_scissor_rect resolve via generated_decls.h. The three with a raw ABI that
// differs from the canonical prototype are invoked through typed casts below.
typedef unsigned long long (*RenderTypeFn)(int, void*);
typedef void (*RotateAboutPtFn)(int, void*, float*);
typedef unsigned long long (*LevelSpawnFn)(unsigned long long, double, double,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long);

extern int  FUN_803826f0(void);              // scratch enter (render/update: entity)
extern int  FUN_803826e8(void);              // scratch enter (render cb: entity)
extern void FUN_8038273c(void);              // scratch leave
extern void FUN_80382734(void);              // scratch leave

// banner internal callbacks (raw addresses, resolved to these names).
void banner_entity_update(unsigned long long, double, double, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long);  // 0x80369be0
void banner_collision_cb(int* entity);                                                 // 0x80369b20
void banner_composite_draw(int texA, int texB, int matDst, int mode);                  // 0x80369f9c
void banner_render_cb(void);                                                            // 0x80369e0c

// -------------------------------------------------------------------------
// 0x80369a18  banner_alloc_composite_rt - allocate the 256x256 composite RT.
// -------------------------------------------------------------------------
void banner_alloc_composite_rt(void)
{
    render_target_alloc_scratch(0x100, 0x100, &DAT_806112e8);
}

// -------------------------------------------------------------------------
// 0x80369a44  banner_entity_spawn - spawn the banner entity + hooks.
//   Fills the source name index table from the caller's name-block + count,
//   initialises the banner control block (slot count, spin timer, flags),
//   registers the physics/collision/dispose update callbacks and the render
//   type, then kicks the streaming reset. Caller: ekun_object_init_dispatch.
// -------------------------------------------------------------------------
void banner_entity_spawn(unsigned long long f1, double a2, double a3, unsigned long long a4,
    unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8,
    int entity, int nameBlock, int count, int p12, int p13, int p14, int p15, int p16)
{
    (void)f1;
    int off = 0;
    int i = 0;
    int ctrl = *(int*)(entity + 0x7c);
    for (int n = count; n > 0; n--) {
        *(int*)((int)&DAT_805ec9f8 + off) = nameBlock + i;
        i += 0x2d;
        off += 4;
    }
    *(int*)(ctrl + 0xe4) = count;
    *(int*)(ctrl + 0xbc) = 0;
    *(float*)(ctrl + 0x98) = FLOAT_8061b340;
    *(int*)(ctrl + 0xa0) = 1;

    entity_register_for_update(entity, 8, (int)0x80369be0);    // banner_entity_update
    entity_register_for_update(entity, 3, (int)0x80369b20);    // banner_collision_cb
    entity_register_for_update(entity, 0xc, (int)0x80369994);  // banner_stream_shutdown
    unsigned long long r = ((RenderTypeFn)&entity_set_render_type)(entity, (void*)(int)0x80369e0c); // banner_render_cb
    *(float*)(entity + 0x1cc) = FLOAT_8061b344;
    banner_stream_reset(r, a2, a3, a4, a5, a6, a7, a8, count, 0, 0, p12, (unsigned int)p13, p14, p15, p16);
}

// -------------------------------------------------------------------------
// 0x80369b20  banner_collision_cb - collision response: pick a spin flag.
//   On a floor/side hit (result 0/2/3) decays the spin momentum; if there is
//   still momentum and it was not a bottom hit, set the "keep spinning" flag
//   (bit 2), otherwise set the "settle" flag (bit 3).
// -------------------------------------------------------------------------
void banner_collision_cb(int* entity)
{
    int ctrl = *(int*)(*entity + 0x7c);
    unsigned char res[8];
    collision_result_restore((int)res);
    int result = *(int*)(res + 4);
    if ((result == 0) || (result == 2) || (result == 3)) {
        *(float*)(*entity + 0x1cc) = *(float*)(*entity + 0x1cc) - (float)entity[8];
    }
    if ((result != 3) && (FLOAT_8061b340 < *(float*)(*entity + 0x1cc))) {
        if ((result != 0) && (result != 2)) return;
        *(unsigned int*)(ctrl + 0xa0) |= 4;
        return;
    }
    *(unsigned int*)(ctrl + 0xa0) |= 8;
}

// -------------------------------------------------------------------------
// 0x80369be0  banner_entity_update - per-frame flip timer + reel + spin.
//   Polls the async streaming, then (when visible) advances the flip timer,
//   swaps to the "flipping" material flags, and at the flip midpoint releases
//   the outgoing slot texture and kicks the load of the next source; at the end
//   of the flip it advances the current slot index and re-arms the timer. Then
//   spins the banner mesh about its pivot.
// -------------------------------------------------------------------------
void banner_entity_update(unsigned long long f1, double a2, double a3, unsigned long long a4,
    unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8)
{
    (void)f1;
    int self = FUN_803826f0();
    banner_poll_async(0, a2, a3, a4, a5, a6, a7, a8);

    if ((*(unsigned int*)(self + 0x10) & 0x80000000) != 0) {
        int ctrl = *(int*)(self + 0x7c);
        int* rigPtr = *(int**)(*(int*)(self + 4) + 4);
        float* pivot = (float*)rigPtr[*rigPtr * -0x30 + 0x1b];
        float pivotVec[3];
        *(int*)&pivotVec[0] = DAT_803e0c78;
        *(int*)&pivotVec[1] = DAT_803e0c7c;
        *(int*)&pivotVec[2] = DAT_803e0c80;

        unsigned int flags = *(unsigned int*)(ctrl + 0xa0);
        if ((flags & 8) == 0) {
            if (flags & 1) {
                double timer = (double)*(float*)(ctrl + 0x98);
                if ((flags & 4) == 0) {
                    double denom = (double)(int)*(int*)&DAT_806109c0;
                    double num   = (double)(int)DAT_806108e4;
                    *(float*)(ctrl + 0x98) = (float)((double)(FLOAT_8061b344 /
                        (float)(-denom - DOUBLE_8061b360)) *
                        (double)(float)(-num - DOUBLE_8061b360) + timer);
                }
                *(unsigned int*)(*(int*)(self + 4) + 0x30) |= 5;
                if ((*(unsigned int*)(ctrl + 0xa0) & 2) == 0) {
                    if (FLOAT_8061b35c < *(float*)(ctrl + 0x98)) {
                        *(unsigned int*)(ctrl + 0xa0) |= 2;
                        *(float*)(ctrl + 0x98) = FLOAT_8061b340;
                    }
                } else {
                    if ((FLOAT_8061b348 < *(float*)(ctrl + 0x98)) &&
                        (*(float*)(ctrl + 0x98) < FLOAT_8061b34c)) {
                        *(unsigned int*)(*(int*)(self + 4) + 0x30) &= 0xfffffffb;
                    }
                    if (((double)FLOAT_8061b350 <= (double)*(float*)(ctrl + 0x98)) &&
                        (timer < (double)FLOAT_8061b350)) {
                        unsigned int idx = *(unsigned int*)(ctrl + 0xbc);
                        int sgn = (int)idx >> 0x1f;
                        banner_slot_release_tex((idx & 1 ^ -sgn) + sgn);
                        unsigned int cnt = *(unsigned int*)(ctrl + 0xe4);
                        int nextSrc = idx + 2;
                        banner_slot_load(0, a2, timer, a4, a5, a6, a7, a8,
                            nextSrc - (nextSrc / (int)cnt) * cnt,
                            (idx & 1 ^ -((int)idx >> 0x1f)) + ((int)idx >> 0x1f), 1,
                            nextSrc / (int)cnt, cnt, nextSrc, 0, 0);
                    }
                    if (FLOAT_8061b354 < *(float*)(ctrl + 0x98)) {
                        *(float*)(ctrl + 0x10) += FLOAT_8061b358;
                        *(unsigned int*)(ctrl + 0xa0) &= 0xfffffffd;
                        *(float*)(ctrl + 0x98) = FLOAT_8061b340;
                        int ni = *(int*)(ctrl + 0xbc) + 1;
                        *(int*)(ctrl + 0xbc) = ni - (ni / *(int*)(ctrl + 0xe4)) * *(int*)(ctrl + 0xe4);
                    }
                }
            }
        } else {
            *(unsigned int*)(*(int*)(self + 4) + 0x30) = 2;
        }
        ((RotateAboutPtFn)&mtx4_rotate_about_point)(*(int*)(*(int*)(self + 4) + 0x10) + 0x80, pivotVec, pivot);
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x80369e0c  banner_render_cb - render cb: composite slots into the RT.
//   If the banner is settled (flag bit2 set and not bit8) it just displays the
//   current slot texture; otherwise it renders the two-texture crossfade into
//   the 256x256 composite RT and displays that. Writes the chosen texture id
//   into the mesh's strip-table material slot (unless disposing, flag bit8).
// -------------------------------------------------------------------------
void banner_render_cb(void)
{
    int self = FUN_803826e8();
    int strip = collision_alloc_strip_table(*(int**)(self + 4));
    int ctrl = *(int*)(self + 0x7c);
    int texId;

    if (((*(unsigned int*)(ctrl + 0xa0) & 2) == 0) || ((*(unsigned int*)(ctrl + 0xa0) & 8) != 0)) {
        unsigned int idx = *(unsigned int*)(ctrl + 0xbc);
        int sgn = (int)idx >> 0x1f;
        texId = (&DAT_805eca84)[((idx & 1 ^ -sgn) + sgn) * 9];
    } else {
        gx_set_scissor_rect(0, 0, 0x100, 0x100);
        int savedMat = gx_set_sub_material_id(1);
        gx_set_color_update_mode(0);
        int savedBlend = gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, 0, 0x100, 0x100, 0x7f);
        gx_set_color_update_mode(0);
        unsigned int idx = *(unsigned int*)(ctrl + 0xbc);
        int sgn = (int)idx >> 0x1f;
        int sgn1 = (int)(idx + 1) >> 0x1f;
        banner_composite_draw(
            (&DAT_805eca84)[((idx & 1 ^ -((int)idx >> 0x1f)) + ((int)idx >> 0x1f)) * 9],
            (&DAT_805eca84)[((idx + 1 & 1 ^ -sgn1) + sgn1) * 9],
            *(int*)(strip + 0x50), 1);
        (void)sgn;
        render_target_bind_default(DAT_806112e8);
        gx_fill_rect_flags(0xf, 0, 0, 0x100, 0x100, 0);
        gx_set_color_update_mode(0);
        gx_set_blend_mode(savedBlend);
        gx_set_sub_material_id(savedMat);
        texId = DAT_806112e8;
    }
    if ((*(unsigned int*)(ctrl + 0xa0) & 8) == 0) {
        *(int*)(strip + 0x50) = texId;
    }
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x80369f9c  banner_composite_draw - draw the two-texture crossfade wipe.
//   mode 0: draw both textures full-frame (no wipe). mode 1: draw the incoming
//   texture full-frame, then overdraw the outgoing texture down to a moving
//   horizontal split (the "flip" wipe), with a blank fill below it. The split
//   position is a piecewise function of the current flip timer (in f1).
// -------------------------------------------------------------------------
void banner_composite_draw(int texA, int texB, int matDst, int mode)
{
    (void)matDst;
    unsigned long long packed = (unsigned long long)FUN_803826f0();
    int hi = (int)((unsigned long long)packed >> 0x20);
    int lo = (int)packed;
    // The flip timer arrives in f1; the decompiler surfaced it as extraout_f1.
    // Model it as read from the banner control timer via the packed hi entity.
    double t = (double)*(float*)(*(int*)(hi + 0x7c) + 0x98);

    if (mode == 0) {
        gx_set_active_material(hi);
        int m = *(int*)&DAT_80610a20 + hi * 0x58;
        gx_draw_sprite_rect(0, 0, 0x100, 0x100, 0, 0,
            (unsigned int)*(unsigned short*)(m + 0x4e) << 4,
            (unsigned int)*(unsigned short*)(m + 0x52) << 4);
        gx_set_active_material(lo);
        m = *(int*)&DAT_80610a20 + lo * 0x58;
        gx_draw_sprite_rect(0, 0, 0x100, 0x100, 0, 0,
            (unsigned int)*(unsigned short*)(m + 0x4e) << 4,
            (unsigned int)*(unsigned short*)(m + 0x52) << 4);
    } else if (mode == 1) {
        gx_set_active_material(texA);
        int m = *(int*)&DAT_80610a20 + texA * 0x58;
        gx_draw_sprite_rect(0, 0, 0x100, 0x100, 0, 0,
            (unsigned int)*(unsigned short*)(m + 0x4e) << 4,
            (unsigned int)*(unsigned short*)(m + 0x52) << 4);
        // Wipe split: ramps up for t in [0, b374], ramps down for t >= b36c;
        // the gap (b374 < t < b36c) draws no wipe.
        bool drawWipe = true;
        double split = 0.0;
        if (t < (double)FLOAT_8061b36c) {
            if (t <= (double)FLOAT_8061b374)
                split = (double)(FLOAT_8061b370 * (float)(t / (double)FLOAT_8061b374));
            else
                drawWipe = false;
        } else {
            split = (double)(FLOAT_8061b370 *
                (FLOAT_8061b344 - (float)(t - (double)FLOAT_8061b36c) / FLOAT_8061b374));
        }
        if (drawWipe) {
            double rem = (double)(float)((double)FLOAT_8061b378 - split);
            unsigned int y = (unsigned int)((double)FLOAT_8061b378 - split);
            gx_set_active_material(hi);
            m = *(int*)&DAT_80610a20 + hi * 0x58;
            gx_draw_sprite_rect(0, (int)-split, 0x100, y, 0, 0,
                (unsigned int)*(unsigned short*)(m + 0x4e) << 4,
                (unsigned int)*(unsigned short*)(m + 0x52) << 4);
            int blank = sprite_get_texture_id(0);
            gx_set_active_material(blank);
            m = *(int*)&DAT_80610a20 + hi * 0x58;
            gx_draw_sprite_rect(0, y, 0x100, (int)((double)FLOAT_8061b37c + rem), 0, 0,
                (unsigned int)*(unsigned short*)(m + 0x4e) << 4,
                (unsigned int)*(unsigned short*)(m + 0x52) << 4);
        }
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x8036a2e8  banner_actor_spawn - level actor spawn wrapper -> stream_init.
//   Spawns the banner's placed actor entities then initialises the texture
//   streaming for it.
// -------------------------------------------------------------------------
void banner_actor_spawn(unsigned long long f1, double a2, double a3, unsigned long long a4,
    unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8)
{
    unsigned long long r = ((LevelSpawnFn)&level_spawn_actor_entities)(f1, a2, a3, a4, a5, a6, a7, a8);
    banner_stream_init(r, a2, a3, a4, a5, a6, a7, a8);
}
