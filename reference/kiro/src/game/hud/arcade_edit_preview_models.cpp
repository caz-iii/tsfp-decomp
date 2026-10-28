// Batch: batch_965 | Module: arcade/challenge editor 3D entity-preview subsystem
// Address range: 0x8033c7e8 - 0x8033cff4
// Date: 2026-09-12
//
// The arcade/challenge editor's in-viewport 3D entity preview (continues the preload work in
// batch_964). Loads the palette-entry preview models, spawns/destroys the preview entity instances,
// renders them each frame (multipass), positions and spins each preview entity, and drives a
// camera-fit interpolation. The preview context is DAT_80612d68 (the 0x1e5b0 working buffer from
// aveditor_preload_map_assets); the per-cell preview-entity handle array lives at
// DAT_80612be4+0x4158 (0x20 entries, 4-byte stride). Cell records are the DAT_80477bxx block
// (0xc4 stride, 0x50 entries). Callers: aveditor_build_screen, aveditor_update_preview_panel,
// aveditor_update_entity_preview, aveditor_screen_tick.
//
//   aveditor_preview_load_models        0x8033c7e8  load each placed cell's preview model.
//   aveditor_preview_teardown           0x8033c958  destroy spawned preview entities, reset viewport.
//   aveditor_preview_render             0x8033ca2c  draw the visible preview entities (multipass).
//   aveditor_preview_place_entity       0x8033cac4  position + orient + spin one preview entity.
//   aveditor_preview_entity_type_for    0x8033cc98  map a cell kind byte to a preview entity type id.
//   aveditor_preview_spawn_entities     0x8033ccdc  spawn a preview entity per placed cell.
//   aveditor_preview_entry_scale        0x8033ce4c  per-entry preview model scale factor.
//   aveditor_preview_set_fit_target     0x8033ced0  set the camera-fit source/target rect (no tag).
//   aveditor_preview_set_fit_target_ex  0x8033cf64  set the camera-fit source/target rect (+ tag).
//   aveditor_preview_fit_step           0x8033cff4  advance the camera-fit interpolation one step.
//
// NON-MATCHING: the model-load/render/spawn functions are emitted with the _savegpr_/_restgpr_
// runtime helpers (FUN_803826e4/FUN_80382730/__save_gpr_28/__save_gpr_25) - pure register spill,
// expressed as plain locals / omitted. render_entity_load_model / render_instance_draw_multipass /
// entity_spawn_by_type / mtx4_* are declared file-locally with call-site-accurate types (their
// shared decls in generated_decls.h differ; this TU includes only common.h). The 8 register
// pass-through slots forwarded to the model/spawn helpers are opaque forwarded registers.

#include "common.h"

extern "C" {

extern int    DAT_80612be4;   // editor context base (preview-entity array at +0x4158)
extern int    DAT_80612d68;   // preview/camera-fit context (0x1e5b0 working buffer)
extern int    DAT_805d59a0;   // active weapon-set / roster index
extern int    DAT_806125e8;   // model-instance table base (0x30 stride)

// placed-cell record block (0xc4 stride, 0x50 entries)
extern int    DAT_80477bca;   // cell kind field (short)
extern int    DAT_80477bd4;   // cell entity-id array base (0x1c stride)
extern int    DAT_80477bec;   // cell preview-scale float (0xc4 stride; +0x1c per roster row)
extern int    DAT_80477c80;   // cell flags field (int; 0xc4 byte stride == 0x31 int stride)

extern short  DAT_805c9858;   // palette-value -> model-slot index table (2-byte stride)
extern int    DAT_805d6038;   // preview-panel screen-descriptor table (4-byte stride)
extern int    DAT_803e0aa8;   // preview spawn transform block (pos + rot defaults)
extern int    DAT_803e0aac;
extern int    DAT_803e0ab0;
extern float  DAT_803e0ab4;
extern int    DAT_803e0ab8;
extern int    DAT_803e0abc;

extern int    DAT_806109c4;   // preview aspect numerator
extern int    DAT_806109c8;   // preview aspect denominator

extern float  FLOAT_8061ab28; // preview grid spacing X/Z
extern float  FLOAT_8061ab2c; // preview kind-offset scale
extern float  FLOAT_8061ab30; // preview grid spacing Y
extern float  FLOAT_8061ab34; // per-kind spin rate (kind 1)
extern float  FLOAT_8061ab38; // per-kind spin rate (kind 2)
extern float  FLOAT_8061ab3c; // per-kind spin rate (kind 3)
extern float  FLOAT_8061ab40; // per-kind spin rate (kind 4-6)
extern float  FLOAT_8061ab44; // spin wrap period (2*pi)
extern float  FLOAT_8061ab50; // preview model uniform scale
extern float  FLOAT_8061ab54; // loadout-model scale numerator
extern float  FLOAT_8061ab58; // preview scale divisor
extern double DOUBLE_8061ab48;// int->double conversion bias

// named cross-module helpers (call-site-accurate types; this TU includes only common.h)
extern void memset_wrapper(int dst, int value, int len);
extern int  aveditor_get_palette_entry_value(int type);
extern int* aveditor_get_loadout_palette_record(int index);
extern unsigned long long aveditor_apply_weapon_set_reload(unsigned long long a1, double a2,
                                                           double a3, double a4, unsigned long long a5,
                                                           unsigned long long a6, unsigned long long a7,
                                                           unsigned long long a8);
extern unsigned render_entity_load_model(unsigned long long f1, double d2, double d3, double d4,
                                         unsigned long long a5, unsigned long long a6,
                                         unsigned long long a7, unsigned long long a8);
extern unsigned long long render_instance_draw_multipass(unsigned long long f1, double d2,
                                                         int instance);
extern unsigned long long entity_destroy_with_event(unsigned* entity);
extern unsigned entity_spawn_by_type(unsigned long long f1, double d2, double d3, unsigned long long f4,
                                     unsigned long long f5, unsigned long long f6, unsigned long long f7,
                                     unsigned long long f8, unsigned typeId, void* transform,
                                     void* scaleBlock, int a12, int a13);
extern void mtx4_set_identity(int mtx);
extern unsigned long long mtx4_set_scale(double sx, double sy, double sz);
extern void mtx4_transform_point(float* mtx, float* point);
extern void hud_input_reset_repeat_timers(void);
extern void hud_viewport_enable(void);

// _savegpr_/_restgpr_ runtime helpers threading the render context
extern unsigned long long FUN_803826e4(void);
extern void FUN_80382730(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

// raw FUN_ helpers not yet named
extern void FUN_80345c2c(int stage);
extern void FUN_80345da8(int stage);
extern void FUN_80340e78(void);
extern unsigned FUN_8033cc98(int cell);   // aveditor_preview_entity_type_for (fwd)

} // extern "C"

// -----------------------------------------------------------------------------

// Load each placed cell's preview model into the model-instance table. Clears the two roster
// counters and three model handles, then for every placed cell (flags bit 8 clear, kind != 8) loads
// the model for its palette value (flags bit 4 clear) or for its roster entity id (bit 4 set),
// storing the loaded handle at DAT_806125e8[slot*0x30+4] where slot comes from DAT_805c9858. Finally
// clears the 0x20-entry preview-entity handle array.
void aveditor_preview_load_models(unsigned long long r3, double r4, double r5, double r6,
                                  unsigned long long r7, unsigned long long r8,
                                  unsigned long long r9, unsigned long long r10)
{
    unsigned long long ctx = FUN_803826e4();
    int roster = DAT_805d59a0;
    ctx = aveditor_apply_weapon_set_reload(ctx, r4, r5, r6, r7, r8, r9, r10);
    *(int*)(DAT_80612be4 + 0x4150) = 0;
    *(int*)(DAT_80612be4 + 0x4154) = 0;
    *(int*)(DAT_80612d68 + 0x88) = -1;
    *(int*)(DAT_80612d68 + 0x90) = -1;
    *(int*)(DAT_80612d68 + 0x94) = -1;

    int cellOff = 0;
    for (int i = 0; i < 0x50; i++, cellOff += 0xc4) {
        unsigned flags = *(unsigned*)((int)&DAT_80477c80 + cellOff);
        if ((flags & 0x100) != 0 || *(short*)((int)&DAT_80477bca + cellOff) == 8) {
            continue;
        }
        if ((flags & 4) == 0) {
            int val = aveditor_get_palette_entry_value(i);
            if (val != -1) {
                unsigned h = render_entity_load_model(ctx, r4, r5, r6, r7, r8, r9, r10);
                short slot = *(short*)((int)&DAT_805c9858 + val * 2);
                *(unsigned*)(DAT_806125e8 + slot * 0x30 + 4) = h;
            }
        } else {
            int idx2 = *(int*)((int)&DAT_80477bd4 + roster * 0x1c + cellOff) * 2;
            short slot = *(short*)((int)&DAT_805c9858 + idx2);
            if (*(int*)(DAT_806125e8 + slot * 0x30 + 4) == 0) {
                unsigned h = render_entity_load_model(ctx, r4, r5, r6, r7, r8, r9, r10);
                *(unsigned*)(DAT_806125e8 + slot * 0x30 + 4) = h;
            }
        }
    }

    // clear the 0x20-entry preview-entity handle array
    for (int j = 0, off = 0x4158; j < 0x20; j++, off += 4) {
        *(int*)(DAT_80612be4 + off) = 0;
    }
    FUN_80382730();
}

// Tear down the preview: destroy every spawned preview entity (0x14-stride slot table at
// DAT_80612d68+0xa0, count at +0x5a0), clear the table, run the render-stage teardown, clear the
// editor's "preview active" flag bit, and re-enable the normal viewport/input.
void aveditor_preview_teardown(void)
{
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612d68 + 0x5a0); i++, off += 0x14) {
        unsigned* ent = *(unsigned**)(DAT_80612d68 + off + 0xa4);
        if (ent != (unsigned*)0) {
            entity_destroy_with_event(ent);
        }
    }
    *(int*)(DAT_80612d68 + 0x5a0) = 0;
    memset_wrapper(DAT_80612d68 + 0xa0, 0, 0x500);
    if (*(int*)(DAT_80612be4 + 0x200) == 0) {
        FUN_80345c2c(4);
    }
    FUN_80345c2c(2);
    FUN_80345c2c(3);
    FUN_80345da8(6);
    FUN_80340e78();
    *(unsigned*)(DAT_80612be4 + 0x25c) &= 0xfdffffff;
    hud_input_reset_repeat_timers();
    hud_viewport_enable();
}

// Draw the visible preview entities. Walks the panel's cell list (count at screen+0x44, 6-byte
// records at +0x48): each valid cell (kind < 5, sub != 2) whose preview entity is visible
// (handle+0x28 bit 0) is drawn via the multipass renderer.
void aveditor_preview_render(unsigned long long r3, double r4)
{
    int screen = __save_gpr_28();
    int handleIdx = 0;
    int recOff = 0;
    unsigned long long acc = r3;
    for (int i = 0; i < *(int*)(screen + 0x44); i++, recOff += 6) {
        unsigned char* rec = (unsigned char*)(screen + recOff + 0x48);
        if (*rec < 5 && rec[5] != 2) {
            int handle = *(int*)(DAT_80612be4 + handleIdx + 0x4158);
            handleIdx += 4;
            if ((*(unsigned*)(handle + 0x28) & 1) != 0) {
                acc = render_instance_draw_multipass(acc, r4, *(int*)(handle + 4));
            }
        }
    }
    __restore_gpr_28();
}

// Position, orient and spin one preview entity for a cell record. Computes the grid position from
// the cell's byte coordinates (indices 1..4), applies a per-kind axis offset and picks the per-kind
// spin rate, transforms the point through the panel matrix, advances the spin by the frame delta
// (wrapping at 2*pi), and writes the final position into the entity (+0x64/+0x68/+0x6c) and rot(+0x70).
void aveditor_preview_place_entity(unsigned char* rec, int handleIndex, float* origin, float* mtx)
{
    int ent = *(int*)(DAT_80612be4 + handleIndex * 4 + 0x4158);

    // byte coords -> float via the Ghidra int->double magic-constant trick
    float px = FLOAT_8061ab28 * (float)((double)CONCAT44(0x43300000, (unsigned)rec[1]) - DOUBLE_8061ab48);
    float py = FLOAT_8061ab30 * (float)((double)CONCAT44(0x43300000, (unsigned)rec[3]) - DOUBLE_8061ab48);
    float pz = FLOAT_8061ab28 * (float)((double)CONCAT44(0x43300000, (unsigned)rec[2]) - DOUBLE_8061ab48);
    float kindOff = FLOAT_8061ab28 * (float)((double)CONCAT44(0x43300000, (unsigned)rec[4]) - DOUBLE_8061ab48) * FLOAT_8061ab2c;

    unsigned char kind = *rec;
    if (kind == 3) {
        *(float*)(ent + 0x70) = FLOAT_8061ab3c;
        px += kindOff;
    } else if (kind < 3) {
        if (kind == 1) {
            *(float*)(ent + 0x70) = FLOAT_8061ab34;
            px += kindOff;
        } else if (kind != 0) {
            *(float*)(ent + 0x70) = FLOAT_8061ab38;
            pz += kindOff;
        }
    } else if (kind < 7 && kind < 5) {
        *(float*)(ent + 0x70) = FLOAT_8061ab40;
        pz += kindOff;
    }

    float pos[3];
    pos[0] = px + origin[0];
    pos[1] = py + origin[1];
    pos[2] = pz + origin[2];
    mtx4_transform_point(mtx, pos);

    float wrap = FLOAT_8061ab44;
    *(float*)(ent + 0x70) += *(float*)(DAT_80612d68 + 0x3c);
    while (wrap <= *(float*)(ent + 0x70)) {
        *(float*)(ent + 0x70) -= wrap;
    }
    *(float*)(ent + 0x64) = pos[0];
    *(float*)(ent + 0x68) = pos[1];
    *(float*)(ent + 0x6c) = pos[2];
}

// Map a cell kind byte (record+4) to the preview entity type id: kind 0 -> none (-1), 1 -> 0x88c,
// 2 -> 0x88d, 3 -> 0x88e, else none.
unsigned aveditor_preview_entity_type_for(int rec)
{
    unsigned char kind = *(unsigned char*)(rec + 4);
    if (kind == 2) {
        return 0x88d;
    }
    if (kind < 2) {
        if (kind == 0) {
            return 0xffffffff;
        }
        return 0x88c;
    }
    if (3 < kind) {
        return 0xffffffff;
    }
    return 0x88e;
}

// Spawn one preview entity per placed cell for the active panel (descriptor from DAT_805d6038 keyed
// by the r25 spill). For each valid cell (kind < 5, sub != 2): set an identity+scale transform,
// destroy any existing handle, spawn the entity of the mapped type, store the new handle at
// DAT_80612be4+0x4158, and force its render/active/visible flag bits on.
void aveditor_preview_spawn_entities(void)
{
    int screen = *(int*)((int)&DAT_805d6038 + __save_gpr_25() * 4);
    int handleOff = 0;
    int recOff = 0;

    for (int i = 0; i < *(int*)(screen + 0x44); i++, recOff += 6) {
        unsigned char* rec = (unsigned char*)(screen + recOff + 0x48);
        int spawnBlock[6];
        spawnBlock[0] = DAT_803e0aa8;
        spawnBlock[1] = DAT_803e0aac;
        spawnBlock[2] = DAT_803e0ab0;
        *(float*)&spawnBlock[3] = DAT_803e0ab4;
        spawnBlock[4] = DAT_803e0ab8;
        spawnBlock[5] = DAT_803e0abc;

        if (*rec < 5 && rec[5] != 2) {
            unsigned char m[96];
            mtx4_set_identity((int)(intptr_t)m);
            double s = (double)FLOAT_8061ab50;
            unsigned long long mm = mtx4_set_scale(s, s, s);
            unsigned* existing = *(unsigned**)(DAT_80612be4 + handleOff + 0x4158);
            if (existing != (unsigned*)0) {
                mm = entity_destroy_with_event(existing);
                *(int*)(DAT_80612be4 + handleOff + 0x4158) = 0;
            }
            unsigned type = FUN_8033cc98((int)(intptr_t)rec);
            unsigned h = entity_spawn_by_type(mm, s, s, 0, 0, 0, 0, 0, type,
                                              &spawnBlock[3], spawnBlock, 0, 0);
            int slot = handleOff + 0x4158;
            handleOff += 4;
            *(unsigned*)(DAT_80612be4 + slot) = h;
            int inner = *(int*)(*(int*)(DAT_80612be4 + slot) + 4);
            *(unsigned*)(inner + 0x30) |= 1;
            *(unsigned*)(*(int*)(DAT_80612be4 + slot) + 0x28) |= 1;
            *(unsigned*)(*(int*)(DAT_80612be4 + slot) + 0x10) |= 1;
        }
    }
    __restore_gpr_25();
}

// Per-entry preview model scale factor. Uses the cell's preview-scale float (roster-offset when the
// flags bit 4 is set); loadout entries (0x11..0x15, 0x16) instead derive it from their palette
// record's field [4]. Returned divided by the preview scale divisor.
double aveditor_preview_entry_scale(int index)
{
    float f;
    if ((*(unsigned char*)((int)&DAT_80477c80 + index * 0xc4) & 4) == 0) {
        f = *(float*)((int)&DAT_80477bec + index * 0xc4);
    } else {
        f = *(float*)((int)&DAT_80477bec + index * 0xc4 + DAT_805d59a0 * 0x1c);
    }
    if ((unsigned)(index - 0x11) < 5 || index == 0x16) {
        int* rec = aveditor_get_loadout_palette_record(index + -0x11);
        f = FLOAT_8061ab54 / (float)rec[4];
    }
    return (double)(f / FLOAT_8061ab58);
}

// Set the camera-fit source rect + target size (letterbox-fit to the preview aspect). No animation
// tag. Fields at DAT_80612d68: +0x14/+0x18 pos, +0x1c/+0x20 size, +4/+8 current pos, +0xc/+0x10
// fitted size, +0x34 tag = 0.
void aveditor_preview_set_fit_target(int posX, int posY, int w, int h)
{
    *(int*)(DAT_80612d68 + 0x14) = posX;
    *(int*)(DAT_80612d68 + 0x18) = posY;
    *(int*)(DAT_80612d68 + 0x1c) = w;
    *(int*)(DAT_80612d68 + 0x20) = h;
    *(int*)(DAT_80612d68 + 4) = posX;
    *(int*)(DAT_80612d68 + 8) = posY;
    if (h < (w * DAT_806109c8) / DAT_806109c4) {
        *(int*)(DAT_80612d68 + 0xc) = w;
        *(int*)(DAT_80612d68 + 0x10) = (w * DAT_806109c8) / DAT_806109c4;
    } else {
        *(int*)(DAT_80612d68 + 0xc) = (h * DAT_806109c4) / DAT_806109c8;
        *(int*)(DAT_80612d68 + 0x10) = h;
    }
    *(int*)(DAT_80612d68 + 0x34) = 0;
}

// As aveditor_preview_set_fit_target, but seeds the "target" pos at +0x24/+0x28 and the fitted size
// at +0x2c/+0x30 (the animation destination), and stores an animation tag at +0x34.
void aveditor_preview_set_fit_target_ex(int posX, int posY, int w, int h, int tag)
{
    *(int*)(DAT_80612d68 + 0x14) = posX;
    *(int*)(DAT_80612d68 + 0x18) = posY;
    *(int*)(DAT_80612d68 + 0x1c) = w;
    *(int*)(DAT_80612d68 + 0x20) = h;
    *(int*)(DAT_80612d68 + 0x24) = posX;
    *(int*)(DAT_80612d68 + 0x28) = posY;
    if (h < (w * DAT_806109c8) / DAT_806109c4) {
        *(int*)(DAT_80612d68 + 0x2c) = w;
        *(int*)(DAT_80612d68 + 0x30) = (w * DAT_806109c8) / DAT_806109c4;
    } else {
        *(int*)(DAT_80612d68 + 0x2c) = (h * DAT_806109c4) / DAT_806109c8;
        *(int*)(DAT_80612d68 + 0x30) = h;
    }
    *(int*)(DAT_80612d68 + 0x34) = tag;
}

// Advance the camera-fit interpolation one step: while the step counter (+0xd) is positive, lerp the
// current pos/size (words 1..4) a fraction (1/counter) toward the targets (words 9..0xc), then
// decrement the counter. No-op when the context is null or the counter has run out.
void aveditor_preview_fit_step(void)
{
    int* p = (int*)(intptr_t)DAT_80612d68;
    if (p[0] == 0) {
        return;
    }
    if (p[0xd] < 1) {
        return;
    }
    p[1] = p[1] + (p[9]   - p[1]) / p[0xd];
    p[2] = p[2] + (p[10]  - p[2]) / p[0xd];
    p[3] = p[3] + (p[0xb] - p[3]) / p[0xd];
    p[4] = p[4] + (p[0xc] - p[4]) / p[0xd];
    p[0xd] = p[0xd] - 1;
}
