// Batch: batch_882 | Module: arcade map-editor init / setup (context, buffers, defaults)
// Address range: 0x80311ec0 - 0x803121dc
// Date: 2026-09-11
//
// The arcade map-editor's one-time setup path, called from session_alloc_all_systems /
// session_preload_by_mode / asel_reset_screen and the big editor-init routine FUN_80312248
// (next batch). These allocate the two editor heaps - the 0x11950-byte context block
// DAT_80612be4 and the 0x8000-byte load/serialize scratch DAT_80612be8 - set the default
// config, preload weapon/sprite assets, and initialise the palette.
//
//   aveditor_alloc_load_buffer               0x80311ec0  alloc + zero the 0x8000 load buffer
//                                                        DAT_80612be8, then FUN_80346b64 init.
//   aveditor_preload_assets                  0x80311f04  ensure buffer alloced, run loader
//                                                        FUN_8033c5dc + sprite_load_all_visible.
//   aveditor_preload_weapon_resources        0x80311f3c  preload each weapon set's entity
//                                                        resources (entity_preload_resources).
//   aveditor_set_default_config              0x80311f98  set default config (3/9/0xb), apply.
//   aveditor_enable_preview_mode             0x80311ff0  enable preview flag + fade @ctx.
//   aveditor_reset_config_fields             0x8031201c  reset config working fields to defaults.
//   aveditor_alloc_context                   0x803120bc  alloc + zero the 0x11950 editor ctx
//                                                        block DAT_80612be4 and seed defaults.
//   aveditor_init_subsystems                 0x8031216c  init editor sub-systems (3 calls).
//   aveditor_load_default_palette_if_missing 0x80312194  load default palette when the saved
//                                                        buffer has no 0xc77672 chunk.
//   aveditor_init_default_palette            0x803121dc  load default palette + init HSV color.
//
// NON-MATCHING: the decompiler renders the mem_alloc / loader calls with the PPC ABI's full
// 8 FPR + 8 GPR spill parameter list; the real mem_alloc is s32(size, tag) and the loaders
// take no meaningful args here, so those phantom pass-through parameters are dropped and the
// calls are written with their true arguments. The int->float value read in the ctx seed is
// kept as a plain float store. Named helpers resolve via generated_decls.h; every raw
// FUN_/DAT_/FLOAT_ symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

// Named cross-module helpers (mem_alloc, memset_wrapper, leaf_31e8, sprite_load_all_visible,
// entity_preload_resources, weapon_set_get_name, color_to_rgb_via_hsv) and this batch's own
// functions resolve through generated_decls.h at C++ linkage and must NOT be re-declared.
extern "C" {

// editor heaps + config globals
extern int          DAT_80612be4;   // editor context block
extern int          DAT_80612be8;   // load/serialize scratch buffer
extern unsigned int DAT_805d59a0;   // config id
extern int          DAT_805d59a4;
extern int          DAT_805d59a8;
extern unsigned int DAT_805d59ac;
extern int          DAT_805d59dc;   // config touched flag

// palette globals
extern unsigned int DAT_805d6630;   // active palette (0x100 bytes)
extern unsigned int DAT_805d6730;   // current color RGB
extern unsigned int DAT_805d674c;   // current color index (byte 0)

// view/fade constants
extern float FLOAT_8061a5f8;
extern float FLOAT_8061a604;

// raw FUN_ helpers (not in generated_decls.h)
void FUN_80346b64(int buffer);                 // init the load buffer
int  FUN_8033c5dc(int buffer);                 // run the asset loader; returns loaded ctx
void FUN_80339aa4(void);                        // post-load fixup
int  FUN_8031d6cc(void);                        // weapon-set count
int  FUN_8031d6d4(int index);                   // weapon-set id at index
unsigned char FUN_8031d654(unsigned int cfgId); // config -> byte
u32  FUN_8031d6e8(void);                        // derived config word
unsigned char FUN_8031d614(unsigned int cfgId); // apply config id, returns flag
void FUN_80314480(void);                        // subsystem init A
void FUN_803144fc(void);                        // subsystem init B
void FUN_80314934(void);                        // subsystem init C
int* FUN_80339b90(int* scratch, int key);       // resolve serialized chunk (null if absent)
void FUN_803257a0(unsigned char* dst, int mode);// palette color select init

} // extern "C"

// ---------------------------------------------------------------------------
// 0x80311ec0  aveditor_alloc_load_buffer
// ---------------------------------------------------------------------------
void aveditor_alloc_load_buffer(void)
{
    DAT_80612be8 = mem_alloc(0x8000, 2);
    int buffer = memset_wrapper(DAT_80612be8, 0, 0x8000);
    FUN_80346b64(buffer);
}

// ---------------------------------------------------------------------------
// 0x80311f04  aveditor_preload_assets
// ---------------------------------------------------------------------------
void aveditor_preload_assets(void)
{
    if (DAT_80612be8 == 0) {
        aveditor_alloc_load_buffer();
    }
    int loaded = FUN_8033c5dc(DAT_80612be8);
    (void)loaded;                 // decompiler passes it to the loader; real proto is void()
    sprite_load_all_visible();
    FUN_80339aa4();
}

// ---------------------------------------------------------------------------
// 0x80311f3c  aveditor_preload_weapon_resources
// ---------------------------------------------------------------------------
void aveditor_preload_weapon_resources(void)
{
    for (int i = 0; i < FUN_8031d6cc(); i = i + 1) {
        int setId = FUN_8031d6d4(i);
        if (0 < setId) {
            int name = weapon_set_get_name(setId);
            if (name != -1) {
                entity_preload_resources(name);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80311f98  aveditor_set_default_config
// ---------------------------------------------------------------------------
void aveditor_set_default_config(void)
{
    DAT_805d59a0 = 3;
    DAT_805d59a8 = 9;
    DAT_805d59a4 = 0xb;
    DAT_805d59ac = (unsigned int)FUN_8031d614(3);
    DAT_805d59dc = 0;
}

// ---------------------------------------------------------------------------
// 0x80311ff0  aveditor_enable_preview_mode
// ---------------------------------------------------------------------------
void aveditor_enable_preview_mode(void)
{
    float fade = FLOAT_8061a5f8;
    *(int*)(DAT_80612be4 + 0x200) = 1;
    *(float*)(DAT_80612be4 + 0x22c) = fade;
    *(int*)(DAT_80612be4 + 0x230) = 1;
    *(int*)(DAT_80612be4 + 0x234) = 1;
}

// ---------------------------------------------------------------------------
// 0x8031201c  aveditor_reset_config_fields
// ---------------------------------------------------------------------------
void aveditor_reset_config_fields(void)
{
    *(int*)(DAT_80612be4 + 0x210) = -1;
    *(int*)(DAT_80612be4 + 0x214) = 1;
    *(int*)(DAT_80612be4 + 0x218) = 1;
    *(int*)(DAT_80612be4 + 0x21c) = 0;
    *(int*)(DAT_80612be4 + 0x220) = -1;
    *(u32*)(DAT_80612be4 + 0x224) = (u32)FUN_8031d654(DAT_805d59a0);
    *(int*)(DAT_80612be4 + 0x248) = 0;
    *(int*)(DAT_80612be4 + 0x204) = DAT_805d59a0;
    *(u32*)(DAT_80612be4 + 0x208) = FUN_8031d6e8();
    *(int*)(DAT_80612be4 + 0x27c) = 0;
}

// ---------------------------------------------------------------------------
// 0x803120bc  aveditor_alloc_context
// ---------------------------------------------------------------------------
void aveditor_alloc_context(void)
{
    DAT_80612be4 = mem_alloc(0x11950, 1);
    memset_wrapper(DAT_80612be4, 0, 0x11948);
    float view = FLOAT_8061a604;
    *(int*)(DAT_80612be4 + 0x25c) = 0;
    *(int*)(DAT_80612be4 + 0x264) = 0;
    *(int*)(DAT_80612be4 + 0x244) = 0;
    *(int*)(DAT_80612be4 + 0x248) = 0;
    *(float*)(DAT_80612be4 + 0x2f84) = view;
    *(float*)(DAT_80612be4 + 0x2f88) = view;
    *(int*)(DAT_80612be4 + 0x1185c) = -1;
    *(int*)(DAT_80612be4 + 0x11858) = 0;
    *(int*)(DAT_80612be4 + 0x23c) = 1;
    *(int*)(DAT_80612be4 + 0x240) = 0;
    *(int*)(DAT_80612be4 + 0x24c) = -1;
}

// ---------------------------------------------------------------------------
// 0x8031216c  aveditor_init_subsystems
// ---------------------------------------------------------------------------
void aveditor_init_subsystems(void)
{
    FUN_80314480();
    FUN_803144fc();
    FUN_80314934();
}

// ---------------------------------------------------------------------------
// 0x80312194  aveditor_load_default_palette_if_missing
// ---------------------------------------------------------------------------
void aveditor_load_default_palette_if_missing(int* scratch)
{
    int* chunk = FUN_80339b90(scratch, 0xc77672);
    if (chunk == nullptr) {
        leaf_31e8((u32)&DAT_805d6630, 0x80480d10, 0x100);
    }
}

// ---------------------------------------------------------------------------
// 0x803121dc  aveditor_init_default_palette
// ---------------------------------------------------------------------------
void aveditor_init_default_palette(void)
{
    leaf_31e8((u32)&DAT_805d6630, 0x80480d10, 0x100);
    FUN_803257a0((unsigned char*)&DAT_805d674c, 1);
    color_to_rgb_via_hsv((&DAT_805d6630)[(unsigned char)DAT_805d674c], (float*)&DAT_805d6730);
}
