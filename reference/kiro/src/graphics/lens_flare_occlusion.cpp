// Address range: 0x8017c72c - 0x8017ce28 | batch_464 | Date: 2026-09-06
// Subsystem: graphics (lens-flare occlusion test + light-source registry + animated
// sprite-texture streaming). tag gx-lensflare.
//
// The lens-flare system tracks up to 8 light sources.  Each source owns a record
// table (DAT_80411380[src], 0x2640 bytes) of light entries (stride 0x1c, up to 0x44)
// and an aux/history buffer (DAT_804113a0[src], 0x770 bytes, stride 0x90).  Per-frame,
// visible flares are projected to screen and drawn as occlusion-test billboards
// (gx_draw_sprite_quad) whose covered area is accumulated into DAT_80611584; the final
// composited flare sprites are then emitted by lens_flare_render_all.  The animated
// sprite-texture ids (DAT_8058fca0, 8 x 0x10) are resolved once and streamed via the
// secondary DVD queue.
//
//   sprite_anim_queue_pair       0x8017c72c  queue anim streams for a sprite id pair
//   lens_flare_state_reset       0x8017c7d8  clear the 8 per-source occlusion counters
//   lens_flare_alloc_buffers     0x8017c838  alloc the 8 source record/aux buffers
//   lens_flare_nop               0x8017c8dc  empty
//   sprite_anim_resolve_ids      0x8017c8e0  resolve sprite texture ids into the slot table
//   lens_flare_buffers_clear     0x8017c958  null the 8 source record pointers
//   lens_flare_occlusion_render  0x8017c98c  draw occlusion billboards + accumulate area
//   lens_flare_add_light         0x8017cd30  add a light source (alloc + set pos + advance)
//   lens_flare_light_alloc       0x8017cd98  allocate a light entry in the current source
//   lens_flare_light_advance     0x8017ce28  commit the pending light entry

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

// ---- runtime helpers not in generated_decls --------------------------------
extern unsigned long long FUN_803826dc(void);   // frame push (returns frame handle)
extern void FUN_80382728(void);   // frame pop (of 803826dc)
extern unsigned long long FUN_803826e8(void);   // frame push (returns frame handle)
extern void FUN_80382734(void);   // frame pop (of 803826e8)
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern unsigned long long __save_fpr_29(void);
extern void __restore_fpr_29(void);
extern void memset_wrapper(int dst, int val, int len);
// FUN_8017ce5c (lens_flare_light_set_world_pos) is now defined below (batch_465).

// ---- lens-flare source tables ----------------------------------------------
extern int   DAT_80411380;   // [8] per-source light record table base (0x2640 each)
extern int   DAT_804113a0;   // [8] per-source aux/history buffer base (0x770 each)
extern int   DAT_804113c0;   // [8] per-source sprite index
extern int   DAT_8058fca0;   // resolved animated-texture id slot table (8 x 0x10)
extern int   DAT_8058fc40;   // [8] per-source current light count (0x44 cap)
extern int   DAT_8058fc60;   // [8] per-source aux word A
extern int   DAT_8058fc80;   // [8] per-source aux word B (-1 init)
extern int   DAT_80611568;   // buffers-allocated gate

// ---- lens-flare occlusion record table (DAT_8058fd20, stride 0x24) ----------
extern int   DAT_8058fd20;   // +0x00 owning render context id
extern int   DAT_8058fd28;   // +0x08 render-target slot id
extern int   DAT_8058fd30;   // +0x10 screen x  (float)
extern int   DAT_8058fd34;   // +0x14 screen y  (float)
extern int   DAT_8058fd38;   // +0x18 half-size (int)
extern int   DAT_8058fd3c;   // +0x1c half-size b (int)
extern int   DAT_8058fd40;   // +0x20 packed colour

// ---- occlusion / flare state counters --------------------------------------
extern int   DAT_80612330;   // active occlusion record count
extern int   DAT_8061232c;   // pending-light flag (light_alloc/advance)
extern int   DAT_80612334, DAT_80612338, DAT_8061233c, DAT_80612340;  // source cursors
extern int   DAT_80611f70;   // frame flare counter
extern int   DAT_80610a68, DAT_80610a6c;  // queued anim sprite index pair
extern int   DAT_80612614;   // game-mode id (0x67 => no flares/anim)

// ---- render context + viewport / draw-stat globals -------------------------
extern int*  DAT_80610a20;   // render-target pool ([0]=base)
extern int   DAT_80611d54;   // render context (+0x214 = current context id)
extern int*  DAT_80611748;   // clip-rect bounds
extern int   DAT_806109c4, DAT_806109c8;   // screen width / height
extern int   DAT_8060f950, DAT_8060f954;   // reference (design) width / height
extern int   DAT_80611580;   // flare draw-count stat
extern unsigned int DAT_80611584;  // accumulated occlusion area stat (float bits)

extern double DOUBLE_80616660;   // int->double magic bias (0x43300000<<32)
extern double DOUBLE_80616668;   // int->double magic bias (design dims)
extern float  FLOAT_80616658;    // secondary-quad size scale

// helper: PPC int->float via magic-bias (value negated by ^0x80000000 idiom)
static inline float i2f_signed(int v)
{
    return (float)((double)((unsigned long long)0x4330000000000000ULL |
                            (unsigned long long)(unsigned int)(v ^ 0x80000000)) - DOUBLE_80616660);
}
static inline float i2f_design(int v)
{
    return (float)((double)((unsigned long long)0x4330000000000000ULL |
                            (unsigned long long)(unsigned int)v) - DOUBLE_80616668);
}

// sprite_anim_queue_pair (0x8017c72c)
// Record the animated-sprite index pair (DAT_80610a68 / DAT_80610a6c) and, unless in
// game-mode 0x67, queue each valid sprite's animation texture for streaming through the
// secondary DVD queue (name via sprite_get_texture_name, dest slot &DAT_8058fca0[idx*4]).
// Caller scene_load_level_model.
void sprite_anim_queue_pair(int idxA, int idxB)
{
    DAT_80610a68 = idxA;
    DAT_80610a6c = idxB;
    if (DAT_80612614 != 0x67) {
        if (idxA != -1) {
            char* name = sprite_get_texture_name((&DAT_804113c0)[idxA]);
            stream_secondary_queue_anim(name, (int)(&DAT_8058fca0 + idxA * 4),
                                        (int)(&DAT_8058fca0 + idxA * 4));
        }
        if (DAT_80610a6c != -1) {
            int off = DAT_80610a6c * 4;
            char* name = sprite_get_texture_name((&DAT_804113c0)[DAT_80610a6c]);
            stream_secondary_queue_anim(name, (int)(&DAT_8058fca0 + off),
                                        (int)(&DAT_8058fca0 + off));
        }
    }
}

// lens_flare_state_reset (0x8017c7d8)
// Clear the 8 per-source occlusion counters (DAT_8058fc40 count=0, DAT_8058fc80 id=-1,
// DAT_8058fc60 aux=0) and the frame flare cursors/counters.
void lens_flare_state_reset(void)
{
    int off = 0;
    DAT_8061233c = 0;
    DAT_80612338 = 0;
    DAT_80612334 = 0;
    for (int i = 8; i != 0; i--) {
        *(int*)((int)&DAT_8058fc40 + off) = 0;
        *(int*)((int)&DAT_8058fc80 + off) = 0xffffffff;
        *(int*)((int)&DAT_8058fc60 + off) = 0;
        off += 4;
    }
    DAT_80612340 = 0xffffffff;
    DAT_80612330 = 0;
    DAT_80611f70 = 0;
}

// lens_flare_alloc_buffers (0x8017c838)
// Allocate the 8 per-source light-record tables (0x2640) and aux buffers (0x770),
// zeroing each, then reset the occlusion state.  Skipped entirely in game-mode 0x67.
// Caller effect init FUN_801c17a8.
void lens_flare_alloc_buffers(void)
{
    if (DAT_80612614 != 0x67) {
        int off = 0;
        for (int i = 0; i < 8; i++) {
            *(int*)((int)&DAT_80411380 + off) = mem_alloc(0x2640, 1);
            memset_wrapper(*(int*)((int)&DAT_80411380 + off), 0, 0x2640);
            *(int*)((int)&DAT_804113a0 + off) = mem_alloc(0x770, 1);
            memset_wrapper(*(int*)((int)&DAT_804113a0 + off), 0, 0x770);
            off += 4;
        }
    }
    lens_flare_state_reset();
}

// lens_flare_nop (0x8017c8dc)
// Empty lens-flare callback stub.
void lens_flare_nop(void) {}

// sprite_anim_resolve_ids (0x8017c8e0)
// Resolve each of the 8 animated-sprite texture ids into the slot table DAT_8058fca0
// (stride 0x10) when still unset (-1), from the source sprite indices DAT_804113c0.
// Frame push/pop via FUN_803826e8.  Caller FUN_801c1cfc.
void sprite_anim_resolve_ids(void)
{
    FUN_803826e8();
    int slotOff = 0, idxOff = 0;
    for (int i = 0; i < 8; i++) {
        if (*(int*)((int)&DAT_8058fca0 + slotOff) == -1) {
            *(int*)((int)&DAT_8058fca0 + slotOff) =
                (int)sprite_get_texture_id(*(int*)((int)&DAT_804113c0 + idxOff));
        }
        slotOff += 0x10;
        idxOff += 4;
    }
    lens_flare_nop();
    FUN_80382734();
}

// lens_flare_buffers_clear (0x8017c958)
// Null the 8 per-source record-table pointers (DAT_80411380) unless the buffers were
// already allocated (gate DAT_80611568).  Caller FUN_80356f0c.
void lens_flare_buffers_clear(void)
{
    if (DAT_80611568 != 0) {
        return;
    }
    int off = 0;
    for (int i = 8; i != 0; i--) {
        *(int*)((int)&DAT_80411380 + off) = 0;
        off += 4;
    }
}

// lens_flare_occlusion_render (0x8017c98c)
// Per-frame lens-flare occlusion pass for the active render context: walk the occlusion
// record table (DAT_8058fd20, stride 0x24) and, for records belonging to the current
// context (+0x214), project the flare to screen with viewport-aspect correction, draw
// the primary occlusion-test billboard (gx_draw_sprite_quad) and an optional coloured
// secondary quad, accumulate the covered (clipped-to-DAT_80611748) area into the stat
// DAT_80611584.  Finally emits the composited flare sprites via lens_flare_render_all.
// Frame push/pop FUN_803826dc.  Caller scene render FUN_801c2584.
void lens_flare_occlusion_render(void)
{
    FUN_803826dc();
    int savedBlend = gx_set_blend_mode(0);
    gx_set_sub_material_id(2);

    int off = 0;
    for (int i = 0; i < DAT_80612330; i++) {
        if (*(int*)((int)&DAT_8058fd20 + off) == *(int*)(DAT_80611d54 + 0x214)) {
            int slot = *DAT_80610a20 + *(int*)((int)&DAT_8058fd28 + off) * 0x58;
            unsigned int packedColor = *(unsigned int*)((int)&DAT_8058fd40 + off);
            unsigned short u1 = *(unsigned short*)(slot + 0x4e);
            unsigned short v1 = *(unsigned short*)(slot + 0x52);

            // aspect-corrected half-height of the billboard
            float aspect = (i2f_signed(DAT_806109c8) / i2f_design(DAT_8060f954)) /
                           (i2f_signed(DAT_806109c4) / i2f_design(DAT_8060f950));
            float halfY = i2f_signed(*(int*)((int)&DAT_8058fd3c + off)) * aspect;
            float halfX = i2f_signed(*(int*)((int)&DAT_8058fd38 + off));

            float cx = *(float*)((int)&DAT_8058fd30 + off);
            float cy = *(float*)((int)&DAT_8058fd34 + off);
            double x0 = (double)(cx - halfX);
            double x1 = (double)(cx + halfX);
            double y0 = (double)(float)((double)cy - (double)halfY);
            double y1 = (double)(float)((double)cy + (double)halfY);

            gx_set_active_material(*(int*)((int)&DAT_8058fd28 + off));
            unsigned int uu = (unsigned int)u1 << 4;
            unsigned int vv = (unsigned int)v1 << 4;
            gx_draw_sprite_quad(x0, y0, x1, y1, 0, 0, uu, vv);

            // optional coloured secondary quad when colour differs from the neutral key
            if ((int)packedColor - (int)(packedColor & 0xff) != 0x7f7f7f00) {
                float s1 = (float)((double)FLOAT_80616658 * (double)halfY);
                float s0 = (float)((double)FLOAT_80616658 *
                                   (double)i2f_signed(*(int*)((int)&DAT_8058fd38 + off)));
                gx_draw_sprite_quad((double)(cx - s0), (double)(cy - s1),
                                    (double)(cx + s0), (double)(cy + s1), 0, 0, uu, vv);
            }
            DAT_80611580 = DAT_80611580 + 1;

            // clip the billboard to the clip-rect and accumulate covered area
            double clipX0 = (double)i2f_signed(DAT_80611748[0]);
            double clipX1 = (double)i2f_signed(DAT_80611748[1]);
            double clipY0 = (double)i2f_signed(DAT_80611748[5]);
            double clipY1 = (double)i2f_signed(DAT_80611748[6]);
            if (x0 < clipX0) x0 = clipX0;
            if (clipX1 < x1) x1 = clipX1;
            if (y0 < clipY0) y0 = clipY0;
            if (clipY1 < y1) y1 = clipY1;
            DAT_80611584 = (unsigned int)((float)(x1 - x0) * (float)(y1 - y0) +
                                          i2f_signed((int)DAT_80611584));
        }
        off += 0x24;
    }

    lens_flare_render_all();
    gx_set_blend_mode(savedBlend);
    FUN_80382728();
}

// lens_flare_light_alloc (0x8017cd98)
// Allocate a light entry (stride 0x1c) in the current source's record table
// (DAT_80411380[cursor]) when below the 0x44 cap: store the 3 position words, mark
// slot -1, zero the rest, and clear the 9 aux history channels (stride 0x10, +0xc) in
// DAT_804113a0.  Returns 1 on success, 0 when full.  Clears the pending flag
// DAT_8061232c.
int lens_flare_light_alloc(int posA, int posB, int posC)
{
    int count = (&DAT_8058fc40)[DAT_8061233c];
    DAT_8061232c = 0;
    if (count < 0x44) {
        int aux = (&DAT_80411380)[DAT_8061233c];
        int* entry = (int*)((&DAT_804113a0)[DAT_8061233c] + count * 0x1c);
        entry[0] = posA;
        entry[1] = posB;
        aux = aux + count * 0x90;
        entry[2] = posC;
        entry[3] = 0xffffffff;
        entry[4] = 0;
        entry[5] = 0;
        entry[6] = 0;
        for (int j = 9; j != 0; j--) {
            *(int*)(aux + 0xc) = 0;
            aux = aux + 0x10;
        }
        return 1;
    }
    return 0;
}

// lens_flare_light_advance (0x8017ce28)
// Commit the pending light entry: if the current source has room (< 0x44) and a light
// was staged (DAT_8061232c > 0), bump the source's light count.
void lens_flare_light_advance(void)
{
    if (0x43 < (int)(&DAT_8058fc40)[DAT_8061233c]) {
        return;
    }
    if (DAT_8061232c < 1) {
        return;
    }
    (&DAT_8058fc40)[DAT_8061233c] = (&DAT_8058fc40)[DAT_8061233c] + 1;
}

// lens_flare_add_light (0x8017cd30)
// Add a light source to the current flare group: allocate a light entry
// (lens_flare_light_alloc, position from __save_fpr_29 frame) and, on success, set its
// world position (FUN_8017ce5c) and advance the source's light count
// (lens_flare_light_advance).
void lens_flare_add_light(unsigned int posC)
{
    unsigned long long frame = __save_fpr_29();
    int ok = lens_flare_light_alloc((int)(frame >> 0x20), (int)frame, (int)posC);
    if (ok != 0) {
        lens_flare_light_set_world_pos(frame, 0.0, 0.0, 0, 0, 0, 0, 0, 0);
        lens_flare_light_advance();
    }
    __restore_fpr_29();
}

// ============================================================================
// Address range: 0x8017ce5c - 0x8017d45c | batch_465 | Date: 2026-09-06
// Subsystem: graphics (lens-flare occlusion-query pipeline). tag gx-lensflare.
//
// Continuation of the lens-flare system.  These functions manage the GPU
// occlusion-query pipeline that drives flare visibility: light-source world
// positions are recorded (lens_flare_light_set_world_pos), occlusion billboard
// records are appended (lens_flare_occ_record_add), the per-source ring is reset
// and advanced (lens_flare_source_reset), and the low-level query lifecycle
// (begin/end/store/read) wraps the GX occlusion counter.  Each frame,
// lens_flare_occlusion_test_render draws the occlusion-test billboards into the
// GX FIFO and issues one query per light source, while lens_flare_resolve_visibility
// scans back for a source whose query results are ready, reads them back, and
// re-tests the visibility grid.
//
//   lens_flare_light_set_world_pos    0x8017ce5c  record a light's world pos + query links
//   lens_flare_occ_record_add         0x8017cfc8  append an occlusion billboard record
//   lens_flare_source_reset           0x8017d0d8  reset + advance the per-source ring
//   lens_flare_nop_b                  0x8017d130  empty
//   lens_flare_query_store_result     0x8017d134  store a resolved query result
//   lens_flare_query_begin            0x8017d190  begin an occlusion query
//   lens_flare_query_end              0x8017d1c8  end an occlusion query
//   lens_flare_query_read             0x8017d208  read a query result (bounds-checked)
//   lens_flare_occlusion_test_render  0x8017d260  per-frame occlusion-test render pass
//   lens_flare_resolve_visibility     0x8017d45c  scan/latch a ready source + grid re-test

// ---- occlusion-query pipeline globals (batch_465) ---------------------------
extern int   DAT_80590590;   // [0x800] query-result array (u32 per query id)
extern int   DAT_80612328;   // occlusion-query ring cursor (mod 0x800)
extern int   DAT_80612028;   // seed source-state value copied on ring reset
extern int   DAT_80612254;   // active GX material pointer
extern int   DAT_8058fd28;   // occ record +0x08 sprite/material slot id (also below)
extern int   DAT_8058fd2c;   // occ record +0x0c sprite index
extern int   DAT_8058fd30;   // occ record +0x10 screen x  (float)
extern int   DAT_8058fd34;   // occ record +0x14 screen y  (float)
extern int   DAT_8058fd38;   // occ record +0x18 half-size (int)
extern int   DAT_8058fd3c;   // occ record +0x1c half-size b (int)
extern int   DAT_8058fd40;   // occ record +0x20 packed colour
extern int   DAT_8056b7c8;   // visibility grid cell base (stride 0x54, row 0x39c)
extern int   DAT_80611de0;   // grid cell height (screen-H divisor)
extern int   DAT_80611de4;   // grid cell width  (screen-W divisor)

extern volatile unsigned int DAT_cc008000;   // GX command / vertex FIFO port

// ---- raw runtime helpers (batch_465) ----------------------------------------
extern void FUN_803cb4b0(void* out);                // sample query counter -> f1 (extraout)
extern void FUN_803cbefc(int* a, int* b, int* c, int* d, int* e, unsigned int* f);  // read query
extern void FUN_803cc034(void);                     // GX begin occlusion query
extern void FUN_803c6f40(unsigned int id);          // GX flush/resolve query id
// FUN_8017dc0c (occ_query_end_bookkeeping) is now defined below (batch_467).
void occ_query_end_bookkeeping(int id);   // fwd decl
extern int  FUN_803c75e4(int a, int b);             // GX set draw parameters
extern void FUN_8017fab4(void);                     // occlusion render prologue
extern void FUN_803826d4(void);                     // frame push (occlusion-test render)
extern void FUN_80382720(void);                     // frame pop  (of 803826d4)
extern unsigned int FUN_803826d8(void);             // frame push (resolve-visibility)
extern void FUN_80382724(void);                     // frame pop  (of 803826d8)
// FUN_8017d6a8 (lens_flare_occ_result_lookup) is now defined below (batch_466).
int lens_flare_occ_result_lookup(int key0, int key1, int key2);   // fwd decl

extern float  FLOAT_80616670;   // world-pos Z upper bound + bias
extern float  FLOAT_80616674;   // world-pos Z lower bound
extern float  FLOAT_80616678;   // world-pos Z scale
extern float  FLOAT_8061667c;   // occ record half-size scale
extern float  FLOAT_80616680;   // occ record half-size scale (source 5)

// lens_flare_light_set_world_pos (0x8017ce5c)
// Record a light source's world position + occlusion-query links into the current
// source's history buffer.  The three incoming FP registers (f29/f30/f31, and their
// paired-single upper halves) carry the interpolation endpoints; they are expressed
// here as explicit parameters.  Gated on: source has room (<0x44), pending slot free
// (<9), and the biased Z (param_z + FLOAT_80616670) lies within the near/far window.
// On success: samples the query counter (FUN_803cb4b0), accumulates query timing into
// the record (+0x14 /0x14, +0x18), and writes the position triple into the source's
// light-record vertex table (DAT_80411380[src]).  Uses __save/__restore_gpr_28.
void lens_flare_light_set_world_pos(unsigned long long param_pos, double param_y, double param_z,
                                    float lo0, float hi0, float lo1, float hi1,
                                    float lo2, float hi2)
{
    float afStack_58[4];
    // interpolation endpoints spilled from f29/f30/f31 (+ ps upper halves)
    float local_48 = lo2, local_44 = hi2;
    (void)lo0; (void)hi0; (void)lo1; (void)hi1;

    int savedGpr = __save_gpr_28();
    int pending  = DAT_8061232c;
    int count    = (&DAT_8058fc40)[DAT_8061233c];
    double zBiased = (double)(float)(param_z + (double)FLOAT_80616670);

    if (count < 0x44 && DAT_8061232c < 9 &&
        (double)FLOAT_80616674 < zBiased && zBiased < (double)FLOAT_80616670) {
        int rec = (&DAT_804113a0)[DAT_8061233c] + count * 0x1c;
        double px = 0.0;                       // extraout_f1 from FUN_803cb4b0
        FUN_803cb4b0(afStack_58);
        double zScale = (double)FLOAT_80616678;

        *(int*)(rec + 0x14) = *(int*)(rec + 0x14) + savedGpr / 0x14;
        *(int*)(rec + 0x18) = *(int*)(rec + 0x18) + savedGpr;

        int vtx = (pending + count * 9) * 0x10;
        int vbase = (&DAT_80411380)[DAT_8061233c];
        *(float*)(vbase + vtx)       = (float)px;
        *(float*)(vbase + vtx + 4)   = (float)param_y;
        *(float*)(vbase + vtx + 8)   = (float)(zBiased + zScale) * (local_44 - local_48) + local_48;
        *(int*)  (vbase + vtx + 0xc) = savedGpr / 0x14;
        DAT_8061232c = DAT_8061232c + 1;
    }
    __restore_gpr_28();
}

// lens_flare_occ_record_add (0x8017cfc8)
// Append an occlusion billboard record into the occlusion record table
// (DAT_8058fd20, stride 0x24, 9 words), when below the 0x3c cap.  Fields:
// +0x00 render-context id (param_ctx), +0x08 sprite/material slot (resolved from the
// slot table DAT_8058fca0 when param_slot==-1), +0x0c sprite index (param_sprite),
// +0x10/0x14 screen x/y, +0x18/0x1c half-sizes (int->float magic-bias scaled by
// FLOAT_8061667c; source 5 rescales +0x1c by FLOAT_80616680), +0x20 packed colour.
void lens_flare_occ_record_add(double param_x, double param_y, int param_ctx,
                               int param_slot, int param_sprite,
                               unsigned int param_w, unsigned int param_h, int param_color)
{
    int idx = DAT_80612330;
    if (DAT_80612330 < 0x3c) {
        int recOff = DAT_80612330 * 0x24;
        DAT_80612330 = DAT_80612330 + 1;
        (&DAT_8058fd20)[idx * 9] = param_ctx;

        if (param_slot == -1) {
            if ((&DAT_8058fca0)[param_sprite * 4] < 0) {
                (&DAT_8058fd28)[idx * 9] = DAT_8058fca0;
            } else {
                (&DAT_8058fd28)[idx * 9] = (&DAT_8058fca0)[param_sprite * 4];
            }
        } else {
            (&DAT_8058fd28)[idx * 9] = param_slot;
        }

        float scale = FLOAT_8061667c;
        double bias  = DOUBLE_80616660;
        double hLocal = (double)((unsigned long long)0x4330000000000000ULL |
                                 (unsigned long long)(param_h ^ 0x80000000));
        float fw = FLOAT_8061667c * (float)((double)((unsigned long long)0x4330000000000000ULL |
                                 (unsigned long long)(param_w ^ 0x80000000)) - DOUBLE_80616660);

        *(int*)((int)&DAT_8058fd2c + recOff) = param_sprite;
        (&DAT_8058fd30)[idx * 9] = (int)(float)param_x;
        (&DAT_8058fd34)[idx * 9] = (int)(float)param_y;
        (&DAT_8058fd38)[idx * 9] = (int)fw;
        (&DAT_8058fd3c)[idx * 9] = (int)(scale * (float)(hLocal - bias));

        if (param_sprite == 5) {
            double h2 = (double)((unsigned long long)((unsigned long long)0x4330000000000000ULL |
                        (unsigned long long)(unsigned int)(&DAT_8058fd3c)[idx * 9]) ^ 0x8000000000000000ULL);
            (&DAT_8058fd3c)[idx * 9] = (int)((float)(h2 - bias) * FLOAT_80616680);
        }
        (&DAT_8058fd40)[idx * 9] = param_color;
    }
}

// lens_flare_source_reset (0x8017d0d8)
// Reset the current occlusion frame and advance the per-source ring cursor
// (DAT_8061233c, mod 9 via the PPC signed-modulo idiom), clearing the newly-selected
// source's count/state (DAT_8058fc40/60) and seeding its saved word (DAT_8058fc80)
// from DAT_80612028.
void lens_flare_source_reset(void)
{
    int seed = DAT_80612028;
    DAT_80612330 = 0;
    int hi = DAT_8061233c + 1 >> 0x1f;
    DAT_80611f70 = 0;
    int idx = (hi * 8 | (unsigned int)((DAT_8061233c + 1) * 0x20000000 + hi) >> 0x1d) - hi;
    DAT_8061233c = idx;
    (&DAT_8058fc40)[idx] = 0;
    (&DAT_8058fc80)[idx] = seed;
    (&DAT_8058fc60)[idx] = 0;
}

// lens_flare_nop_b (0x8017d130)
// Empty stub.
void lens_flare_nop_b(void) {}

// lens_flare_query_store_result (0x8017d134)
// Read the resolved occlusion query (FUN_803cbefc returns visible-pixel count as its
// 4th out-param) and store it into the query-result array DAT_80590590 at the
// dynamic-id-relative slot (param_id - 0x2f10).
void lens_flare_query_store_result(int param_id)
{
    unsigned int uStack_28;
    int iStack_24;
    int local_20;
    int iStack_1c;
    int iStack_18;
    int aiStack_14[4];

    FUN_803cbefc(aiStack_14, &iStack_18, &iStack_1c, &local_20, &iStack_24, &uStack_28);
    *(int*)((int)&DAT_80590590 + (param_id - 0x2f10) * 4) = local_20;
}

// lens_flare_query_begin (0x8017d190)
// Begin an occlusion query for query-id param_id: mark its result slot pending
// (0xfffffffd) and issue the GX begin-query (FUN_803cc034).  Returns 1.
int lens_flare_query_begin(int param_id)
{
    *(int*)((int)&DAT_80590590 + param_id * 4) = (int)0xfffffffd;
    FUN_803cc034();
    return 1;
}

// lens_flare_query_end (0x8017d1c8)
// End an occlusion query: flush/resolve the GX query (FUN_803c6f40 with the
// dynamic-id form (param_id+0x2f10)&0xffff) and run end bookkeeping (FUN_8017dc0c).
// Returns 1.
int lens_flare_query_end(int param_id)
{
    FUN_803c6f40((unsigned int)(param_id + 0x2f10) & 0xffff);
    occ_query_end_bookkeeping(param_id + 0x2f10);
    return 1;
}

// lens_flare_query_read (0x8017d208)
// Read a query result for query-id param_id (must be in [0,0x800)) into *out when the
// stored result is non-negative (ready); returns 1.  Otherwise stores 0 and returns 0.
int lens_flare_query_read(int param_id, int* out)
{
    if (out == (int*)0) {
        return 0;
    }
    if (-1 < param_id && param_id < 0x800 &&
        -1 < *(int*)((int)&DAT_80590590 + param_id * 4)) {
        *out = *(int*)((int)&DAT_80590590 + param_id * 4);
        return 1;
    }
    *out = 0;
    return 0;
}

// lens_flare_occlusion_test_render (0x8017d260)
// Per-frame occlusion-test render pass for the current source.  Saves the GX blend
// mode/sub-material-id/colour-update state (drawing colour-write disabled), binds the
// occlusion sprite material (sprite_get_texture_id(0) validated against the material
// pool bounds DAT_80610a20[2]/[3], else gx_bind_material_fallback), then for each of
// the source's light records issues one occlusion query (lens_flare_query_begin),
// streams the record's occlusion-test triangle-fan vertices (position triple + magenta
// colour 0xff00ffff) directly into the GX FIFO (DAT_cc008000), and ends the query
// (lens_flare_query_end), advancing the query ring (DAT_80612328, mod 0x800).  Query
// vertex table stride +0x90 (DAT_80411380), query record stride +0x1c (DAT_804113a0).
void lens_flare_occlusion_test_render(void)
{
    FUN_803826d4();
    int count = (&DAT_8058fc40)[DAT_8061233c];
    int savedBlend = gx_set_blend_mode(2);
    int savedSub   = gx_set_sub_material_id(0);
    gx_set_color_update_mode(0);   // disable colour writes for the occlusion pass
    FUN_8017fab4();
    gx_begin_2d_draw();
    FUN_803c75e4(6, 0);

    int tex = (int)sprite_get_texture_id(0);
    if (tex < 0 || DAT_80610a20[3] <= tex || tex <= DAT_80610a20[2]) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = *DAT_80610a20 + tex * 0x58;
    }
    gx_set_cull_mode(0);

    int recOff = 0;
    int vtxOff = 0;
    for (int i = 0; i < count; i++) {
        if (lens_flare_query_begin(DAT_80612328) != 0) {
            int recBase = (&DAT_804113a0)[DAT_8061233c];
            int prims   = *(int*)(recBase + recOff + 0x14);
            int* v      = (int*)((&DAT_80411380)[DAT_8061233c] + vtxOff);

            gx_begin_primitive(0xb8, 0, (short)prims);
            while (prims != 0) {
                int inner = v[3];             // vertex count for this fan
                for (int k = 0; k < inner; k++) {
                    DAT_cc008000 = (unsigned int)v[0];
                    DAT_cc008000 = (unsigned int)v[1];
                    DAT_cc008000 = (unsigned int)v[2];
                    DAT_cc008000 = 0xff00ffff;
                }
                prims = prims - inner;
                v = v + 4;
            }

            if (lens_flare_query_end(DAT_80612328) != 0) {
                *(int*)(recBase + recOff + 0xc) = DAT_80612328;
                int hi = DAT_80612328 + 1 >> 0x1f;
                DAT_80612328 = (hi * 0x800 |
                                (unsigned int)((DAT_80612328 + 1) * 0x200000 + hi) >> 0x15) - hi;
            }
        }
        recOff += 0x1c;
        vtxOff += 0x90;
    }

    gx_end_2d_draw();
    gx_set_blend_mode(savedBlend);
    gx_set_sub_material_id(savedSub);
    gx_set_color_update_mode(1);   // restore colour writes
    FUN_80382720();
}

// lens_flare_resolve_visibility (0x8017d45c)
// Per-frame visibility resolve.  Scans the 8 sources backward from the current ring
// cursor (DAT_8061233c) for a "ready" source: one whose state flag (DAT_8058fc60) is
// set, or whose last light record's query id has a ready result (lens_flare_query_read).
// The first ready source is latched (DAT_80612338/34/40) and marked active
// (DAT_8058fc60=1); each of its records' query results is read back and scaled *5 into
// the record (+0x10).  Finally the screen visibility grid (DAT_8056b7c8, cells stride
// 0x54, rows stride 0x39c, dims screen/DAT_80611de0/de4) is re-tested via FUN_8017d6a8,
// setting each active cell's +0x18 visibility flag.
void lens_flare_resolve_visibility(void)
{
    int auStack_34[13];
    int local_38;
    unsigned int frame = FUN_803826d8();
    bool found = false;

    for (int probe = 1; probe <= 7; probe++) {
        int raw = (DAT_8061233c + 8) - probe;
        int hi  = raw >> 0x1f;
        int src = (hi * 8 | (unsigned int)(raw * 0x20000000 + hi) >> 0x1d) - hi;
        bool ready = false;

        if ((&DAT_8058fc60)[src] == 0) {
            if ((&DAT_8058fc40)[src] == 0) {
                ready = true;
            } else {
                int qid = *(int*)((&DAT_804113a0)[src] + ((&DAT_8058fc40)[src] - 1) * 0x1c + 0xc);
                if (-1 < qid && lens_flare_query_read(qid, auStack_34) != 0) {
                    ready = true;
                }
            }
        } else {
            ready = true;
        }

        if (ready) {
            found = true;
            DAT_80612338 = src;
            break;
        }
    }

    if (found) {
        int count = (&DAT_8058fc40)[DAT_80612338];
        int recOff = 0;
        DAT_80612334 = DAT_80612338;
        DAT_80612340 = (int)frame;
        (&DAT_8058fc60)[DAT_80612338] = 1;

        for (int i = 0; i < count; i++) {
            int recBase = (&DAT_804113a0)[DAT_80612338];
            int qid = *(int*)(recBase + recOff + 0xc);
            if (-1 < qid) {
                if (lens_flare_query_read(qid, &local_38) == 0) {
                    (&DAT_8058fc60)[DAT_80612338] = 0;
                } else {
                    *(int*)(recBase + recOff + 0x10) = local_38 * 5;
                }
            }
            recOff += 0x1c;
        }

        int rowOff = 0;
        for (unsigned int gy = 0; gy < DAT_806109c8 / DAT_80611de0 + 1; gy++) {
            int colOff = 0;
            for (unsigned int gx = 0; gx < DAT_806109c4 / DAT_80611de4 + 1; gx++) {
                int cell = (int)&DAT_8056b7c8 + colOff + rowOff;
                if (*(int*)(cell + 0x1c) != 0) {
                    if (lens_flare_occ_result_lookup(0, cell, 0) == 0) {
                        *(int*)(cell + 0x18) = 0;
                    } else {
                        *(int*)(cell + 0x18) = 1;
                    }
                }
                colOff += 0x54;
            }
            rowOff += 0x39c;
        }
    }
    FUN_80382724();
}

// ============================================================================
// Address range: 0x8017d6a8 - 0x8017da30 | batch_466 | Date: 2026-09-06
// Subsystem: graphics (lens-flare occlusion-query submission ring). tag gx-lensflare.
//
// The GPU-side of the occlusion pipeline.  lens_flare_occ_result_lookup reads a
// cached query result out of the current source's query table (the counterpart to
// batch_465's lens_flare_query_read, keyed by the three record ids).  The remaining
// functions implement a small fixed 16-entry occlusion-query submission ring
// (DAT_80592590, stride 0x10): occ_ring_reset clears it, occ_ring_find_free /
// occ_ring_is_active scan it, occ_scratch_* manage a bump allocator (base from
// FUN_803aa370) for the per-query DMA payload, and occ_query_submit issues the GPU
// occlusion query via the DMA path (FUN_803abe54 / FUN_80399500) wrapped in a frame
// push/pop.
//
//   lens_flare_occ_result_lookup  0x8017d6a8  cached query-result lookup (3-key match)
//   occ_ring_nop                  0x8017d730  empty
//   occ_ring_find_free            0x8017d78c  first free ring slot (or -1)
//   occ_ring_reset                0x8017d7c4  clear all 16 ring slots
//   occ_ring_is_active            0x8017d7ec  is a query id active in the ring?
//   occ_query_submit              0x8017d848  claim a ring slot + submit a DMA query
//   occ_query_run                 0x8017d95c  submit then drain until the query is idle
//   occ_scratch_begin             0x8017d9b0  init the scratch bump allocator + ring
//   occ_scratch_reset             0x8017da24  rewind scratch to the frame base
//   occ_scratch_alloc             0x8017da30  bump-allocate (32-byte aligned) scratch

// ---- occlusion-query submission ring (batch_466) ----------------------------
extern int   DAT_80592590;   // [0x10] ring slot +0x00 active flag
extern int   DAT_80592594;   // [0x10] ring slot +0x04 query id
extern int   DAT_80592598;   // [0x10] ring slot +0x08 frame handle
extern int   DAT_8059259c;   // [0x10] ring slot +0x0c user tag
extern int   DAT_80610a70;   // rolling query id counter
extern int   DAT_80612348;   // active-query count
extern int   DAT_8061234c;   // scratch allocator base
extern int   DAT_80612350;   // scratch frame base (post-header)
extern int   DAT_80612354;   // scratch bump cursor
extern int   DAT_80612358;   // scratch size limit
extern int   DAT_8061235c;   // scratch header base

// ---- raw runtime helpers (batch_466) ----------------------------------------
extern int  FUN_803aa370(void);                     // alloc scratch region base
extern void FUN_8039cca0(void);                     // DMA guard enter
extern void FUN_8039ccc8(void);                     // DMA guard leave
extern void FUN_80399500(unsigned int a, int size); // program DMA length
extern void FUN_803abe54(unsigned int* dst, int a, int b, unsigned int flag,
                         unsigned int hi, int handle, int size, int tag);  // issue DMA query

// lens_flare_occ_result_lookup (0x8017d6a8)
// Look up a cached occlusion-query result in the active source's query table
// (DAT_804113a0[DAT_80612338], stride 0x1c) whose three key words (+0x00/+0x04/+0x08)
// match (param0, param1, param2).  Returns the smaller of the two stored result
// counts (+0x10 vs +0x18), or 0 if no matching record.  Shared by
// lens_flare_resolve_visibility, particle soft-overlay and shadow projection.
int lens_flare_occ_result_lookup(int key0, int key1, int key2)
{
    int count = (&DAT_8058fc40)[DAT_80612334];
    int off = 0;
    if (0 < count) {
        do {
            int* rec = (int*)((&DAT_804113a0)[DAT_80612338] + off);
            if (rec[1] == key1 && rec[2] == key2 && rec[0] == key0) {
                if (rec[6] <= rec[4]) {
                    return rec[6];
                }
                return rec[4];
            }
            off += 0x1c;
            count--;
        } while (count != 0);
    }
    return 0;
}

// occ_ring_nop (0x8017d730)
// Empty stub.
void occ_ring_nop(void) {}

// occ_ring_find_free (0x8017d78c)
// Return the index of the first inactive ring slot (DAT_80592590[i]==0), scanning up
// to 16 slots; returns -1 when the ring is full.
int occ_ring_find_free(void)
{
    int idx = 0;
    int off = 0;
    for (int n = 0x10; n != 0; n--) {
        if (*(int*)((int)&DAT_80592590 + off) == 0) {
            return idx;
        }
        idx++;
        off += 0x10;
    }
    return -1;
}

// occ_ring_reset (0x8017d7c4)
// Clear the active flag on all 16 ring slots.
void occ_ring_reset(void)
{
    int off = 0;
    for (int n = 0x10; n != 0; n--) {
        *(int*)((int)&DAT_80592590 + off) = 0;
        off += 0x10;
    }
}

// occ_ring_is_active (0x8017d7ec)
// Return 1 if the ring holds an active slot (flag==1) whose query id (+0x04) equals
// param_id, else 0.  Fast-out when no queries are active (DAT_80612348==0).
int occ_ring_is_active(int param_id)
{
    if (DAT_80612348 == 0) {
        return 0;
    }
    int off = 0;
    for (int n = 0x10; n != 0; n--) {
        if (*(int*)((int)&DAT_80592590 + off) == 1 &&
            *(int*)((int)&DAT_80592594 + off) == param_id) {
            return 1;
        }
        off += 0x10;
    }
    return 0;
}

// occ_query_submit (0x8017d848)
// Claim a free ring slot (waiting for one when param_wait!=0), record the query
// (assign a rolling id DAT_80610a70, frame handle, user tag), align the payload size
// to 32 bytes, and issue the GPU occlusion query through the DMA path
// (FUN_803abe54 / FUN_80399500) guarded by FUN_8039cca0/ccc8.  Returns the assigned
// query id (0 when no slot was obtained).  Wrapped in a frame push/pop (FUN_803826dc).
int occ_query_submit(unsigned int param_ptr, unsigned int param_arg, int param_size,
                     int param_wait, int param_mode, int param_tag)
{
    unsigned long long frame = FUN_803826dc();
    unsigned int hi = (unsigned int)(frame >> 0x20);
    int slot = occ_ring_find_free();
    if (slot == -1 && param_wait != 0) {
        do {
            slot = occ_ring_find_free();
        } while (slot == -1);
    }

    int queryId = 0;
    if (slot != -1) {
        FUN_8039cca0();
        queryId = DAT_80610a70;
        (&DAT_80592594)[slot * 4] = DAT_80610a70;
        (&DAT_80592590)[slot * 4] = 1;
        *(int*)((int)&DAT_80592598 + slot * 0x10) = (int)frame;
        DAT_80610a70 = DAT_80610a70 + 1;
        *(int*)((int)&DAT_8059259c + slot * 0x10) = param_tag;
        FUN_80399500(hi, (param_size + 0x1f) & (int)0xffffffe0);
        DAT_80612348 = DAT_80612348 + 1;
        FUN_803abe54((unsigned int*)(slot * 0x20 + -0x7fa6d970), 0, 0,
                     (unsigned int)(param_mode != 0), hi, (int)frame, param_size, (int)0x8017d734);
        FUN_8039ccc8();
    }
    (void)param_ptr; (void)param_arg;
    FUN_80382728();
    return queryId;
}

// occ_query_run (0x8017d95c)
// Submit an occlusion query (occ_query_submit) and, on success, drain the ring by
// polling occ_ring_is_active until the query id is no longer active.  Returns 1 on a
// successful submission, 0 when no slot was available.
int occ_query_run(unsigned int param_ptr, unsigned int param_arg, int param_size, int param_wait)
{
    int queryId = occ_query_submit(param_ptr, param_arg, param_size, param_wait, 1, 0);
    if (queryId == 0) {
        return 0;
    }
    while (occ_ring_is_active(queryId) != 0) {
        // spin until the GPU retires this query
    }
    return 1;
}

// occ_scratch_begin (0x8017d9b0)
// Initialise the occlusion scratch bump allocator: allocate the base region
// (FUN_803aa370), reserve a 0x100-byte header, set the frame/cursor pointers and the
// size limit (param_end - base), clear the ring (occ_ring_reset), and kick off the
// first query pass (occ_query_run) over the header region.
void occ_scratch_begin(int param_end)
{
    unsigned char header[8];
    unsigned char body[284];

    DAT_8061234c = FUN_803aa370();
    DAT_80612350 = DAT_8061234c + 0x100;
    DAT_80612358 = param_end - DAT_8061234c;
    DAT_80612354 = DAT_80612350;
    DAT_8061235c = DAT_8061234c;
    memset_wrapper((int)header, 0, 0x120);
    occ_ring_reset();
    occ_query_run((unsigned int)(unsigned long long)(size_t)body,
                  (unsigned int)DAT_8061235c, 0x100, 1);
}

// occ_scratch_reset (0x8017da24)
// Rewind the scratch bump cursor back to the frame base.
void occ_scratch_reset(void)
{
    DAT_80612354 = DAT_80612350;
}

// occ_scratch_alloc (0x8017da30)
// Bump-allocate param_size bytes (rounded up to 32) from the scratch region; when
// param_commit==1, latch the new cursor as the frame base.
void occ_scratch_alloc(int param_size, int param_commit)
{
    DAT_80612354 = DAT_80612354 + ((param_size + 0x1f) & (int)0xffffffe0);
    if (param_commit != 1) {
        return;
    }
    DAT_80612350 = DAT_80612354;
}

// ============================================================================
// Address range: 0x8017da54 - 0x8017dc84 | batch_467 | Date: 2026-09-06
// Subsystem: graphics (occlusion-query scratch pool + GPU result ring). tag gx-lensflare.
//
// The plumbing behind the occlusion-query pipeline: a dedicated memory pool for query
// payloads (occ_pool_* : create the pool with alloc/free callbacks, query remaining
// scratch), and the GPU->CPU result ring (DAT_805968a0 / DAT_805978a0, 0x400 entries)
// that carries finished occlusion counters back to the flare code.
// occ_query_end_bookkeeping pushes a finished token onto the ring (the function
// referenced by batch_465's lens_flare_query_end); occ_result_ring_pop pops the next
// pending result and hands it to the GPU read-back (FUN_803c5ab0);
// occ_query_result_dispatch is the magic-tagged (0xbeef / id-range) entry point that
// validates the query id, stores the resolved result (lens_flare_query_store_result)
// and pops the ring.
//
//   occ_pool_scratch_remaining    0x8017da54  bytes left in the scratch region
//   occ_pool_base                 0x8017da64  scratch allocator base pointer
//   occ_pool_alloc_cb             0x8017da6c  pool alloc callback -> mem_alloc
//   occ_pool_free_cb              0x8017da90  pool free  callback -> mem_free_top
//   occ_pool_create               0x8017dab0  create the occlusion-query scratch pool
//   occ_result_grid_store         0x8017db34  store a packed result into the 4x4 grid
//   occ_result_ring_pop           0x8017db90  pop the next pending GPU result
//   occ_read_mode_set             0x8017db80  set/swap the occlusion read-back mode
//   occ_query_end_bookkeeping     0x8017dc0c  push a finished query token onto the ring
//   occ_query_result_dispatch     0x8017dc84  validate + store a resolved query result

// ---- occlusion-query pool / result-ring globals (batch_467) -----------------
extern int   DAT_8061236c;   // occlusion-query scratch pool handle
extern int   DAT_805968a0;   // [0x400] GPU result ring: query token
extern int   DAT_805978a0;   // [0x400] GPU result ring: user id
extern int   DAT_80612384;   // ring: last-popped index
extern int   DAT_80612388;   // ring: write cursor (mod 0x400)
extern int   DAT_8061238c;   // ring: read cursor  (mod 0x400)
extern int   DAT_80612390;   // pending query id being resolved
extern int   DAT_80612362;   // magic-tag latch (cleared when tag != 0xbeef)
extern short DAT_805928a0;   // [] result grid low  half (4x4-block hashed)
extern short DAT_805928c0;   // [] result grid high half
extern unsigned char DAT_80610a78;   // occlusion read-back mode
extern unsigned char DAT_80610a7a;   // dispatch re-entry guard

// ---- raw runtime helpers (batch_467) ----------------------------------------
extern void mem_free_top(int ptr);
extern int  FUN_8018bc84(int a, int b);                 // compute pool payload size
extern int  FUN_801849f0(int size, int a, int b);       // alloc pool descriptor
extern void FUN_8018bcb4(int pool, int a, int b, float magic, float base,
                         void* allocCb, void* freeCb);  // configure pool callbacks
extern void FUN_80184a5c(int pool, int size, int a);    // finalize pool
extern void FUN_803c57c8(void* a, void* b, void* c, void* d, void* e);  // query context read
extern int  FUN_803c5de0(void);                         // get query context A
extern int  FUN_803c5de8(void);                         // get query context B
extern void FUN_803c59c4(int ctx, void* a, void* b);    // sample query context
extern void FUN_803c5ab0(int token);                    // issue GPU read-back for token
extern void FUN_803c5818(int* a, void* b, void* c, void* d, void* e, void* f, void* g);  // read query fields
extern void FUN_803c6b7c(void);                         // end-query flush
extern void FUN_803c5b64(void);                         // ring-empty stall handler
// halt_stub_assert resolves through generated_externs.h (do not re-declare)

// occ_pool_scratch_remaining (0x8017da54)
// Return the number of bytes left in the occlusion scratch region
// (limit DAT_80612358 minus cursor DAT_80612354).
int occ_pool_scratch_remaining(void)
{
    return DAT_80612358 - DAT_80612354;
}

// occ_pool_base (0x8017da64)
// Return the scratch allocator base pointer.
int occ_pool_base(void)
{
    return DAT_8061234c;
}

// occ_pool_alloc_cb (0x8017da6c)
// Pool alloc callback: forwards to the 2-arg mem_alloc (allocation type forced to 0,
// i.e. bottom-of-heap).  Registered with the pool by occ_pool_create.
int occ_pool_alloc_cb(int size)
{
    return mem_alloc(size, 0);
}

// occ_pool_free_cb (0x8017da90)
// Pool free callback: forwards to mem_free_top.
void occ_pool_free_cb(int ptr)
{
    mem_free_top(ptr);
}

// occ_pool_create (0x8017dab0)
// Create the occlusion-query scratch pool sized for 0x7d1 entries of 0x1000 bytes:
// compute the payload size (FUN_8018bc84), allocate the descriptor (FUN_801849f0),
// wire up the alloc/free callbacks (FUN_8018bcb4), and finalize (FUN_80184a5c).  The
// float argument is a reinterpreted magic word (bit pattern of -5.983387e-39).
void occ_pool_create(void)
{
    int payload = FUN_8018bc84(0x7d1, 0x1000);
    DAT_8061236c = FUN_801849f0(payload + 0x1c, 2, 1);
    FUN_8018bcb4(DAT_8061236c, 0x7d1, 0x1000, -5.983387e-39f, (float)(DAT_8061236c + 0x1c),
                 (void*)&occ_pool_alloc_cb, (void*)&occ_pool_free_cb);
    FUN_80184a5c(DAT_8061236c, payload + 0x1c, 1);
}

// occ_result_grid_store (0x8017db34)
// Store a packed 32-bit occlusion result into the coarse 4x4-block result grid
// (DAT_805928a0 low half / DAT_805928c0 high half).  The index is derived from the
// (x,y) block coordinates: each coordinate splits into a /4 block index (arithmetic
// shift, C-truncated toward zero) and a 0..3 sub-cell, packed into a 32-entry row.
void occ_result_grid_store(unsigned int x, unsigned int y, int packed)
{
    int xBlk = ((int)x >> 2) + (int)((int)x < 0 && (x & 3) != 0);
    int yBlk = ((int)y >> 2) + (int)((int)y < 0 && (y & 3) != 0);
    int idx = ((xBlk + yBlk * 0x10) * 0x20 + (int)(y & 3) * 4 + (int)(x & 3)) * 2;
    *(short*)((int)&DAT_805928a0 + idx) = (short)packed;
    *(short*)((int)&DAT_805928c0 + idx) = (short)((unsigned int)packed >> 0x10);
}

// occ_read_mode_set (0x8017db80)
// Set the occlusion read-back mode (DAT_80610a78), returning the previous value.
unsigned char occ_read_mode_set(unsigned char mode)
{
    unsigned char prev = DAT_80610a78;
    DAT_80610a78 = mode;
    return prev;
}

// occ_result_ring_pop (0x8017db90)
// Pop the next pending GPU result off the ring: when the read cursor has caught up to
// the write cursor (DAT_80612388==DAT_8061238c) invoke the empty-stall handler
// (FUN_803c5b64); otherwise latch the read index (DAT_80612384), advance the read
// cursor (mod 0x400), and issue the GPU read-back for that token (FUN_803c5ab0),
// guarded by FUN_8039cca0/ccc8.
void occ_result_ring_pop(void)
{
    if (DAT_80612388 == DAT_8061238c) {
        FUN_803c5b64();
    } else {
        FUN_8039cca0();
        int idx = DAT_8061238c * 4;
        DAT_80612384 = DAT_8061238c;
        DAT_8061238c = DAT_8061238c + 1 & 0x3ff;
        FUN_803c5ab0(*(int*)((int)&DAT_805968a0 + idx));
        FUN_8039ccc8();
    }
}

// occ_query_end_bookkeeping (0x8017dc0c)
// Push a finished occlusion-query token onto the GPU result ring: flush the query
// (FUN_803c6b7c), sample the query context (FUN_803c5de0 / FUN_803c59c4), and under the
// DMA guard store the token (DAT_805968a0) + user id (param_id, DAT_805978a0) at the
// write cursor, advancing it (mod 0x400).  Referenced by lens_flare_query_end.
void occ_query_end_bookkeeping(int param_id)
{
    int local_18;
    int auStack_14[4];

    FUN_803c6b7c();
    int ctx = FUN_803c5de0();
    FUN_803c59c4(ctx, auStack_14, &local_18);
    FUN_8039cca0();
    int idx = DAT_80612388 * 4;
    DAT_80612388 = DAT_80612388 + 1 & 0x3ff;
    *(int*)((int)&DAT_805968a0 + idx) = local_18;
    *(int*)((int)&DAT_805978a0 + idx) = param_id;
    FUN_8039ccc8();
}

// occ_query_result_dispatch (0x8017dc84)
// Magic-tagged entry point that resolves a finished occlusion query.  The tag word
// (param & 0xffff) must be 0xbeef-derived and lie in the valid query-id range
// [10000, 0x3711); a re-entry guard (DAT_80610a7a) serializes dispatch.  If the id
// doesn't match the pending query (DAT_80612390) it re-reads the hardware query
// context (FUN_803c57c8 / FUN_803c5de0 / ...) and asserts (halt_stub_assert) on
// mismatch; otherwise it stores the resolved result
// (lens_flare_query_store_result) and pops the ring (occ_result_ring_pop).
void occ_query_result_dispatch(unsigned int param_1)
{
    unsigned char bStack_28, bStack_25, bStack_24, bStack_23, bStack_22, bStack_21;
    unsigned char uStack_27, uStack_26;
    int iStack_20;
    int uStack_1c;
    int auStack_18[5];

    if ((param_1 & 0xffff) != 0xbeef) {
        DAT_80612362 = 0;
    }
    unsigned int id = param_1 & 0xffff;
    if (9999 < id && id < 0x3711 && DAT_80610a7a == 0) {
        DAT_80610a7a = 1;
        if (DAT_80612390 != (int)id) {
            if (DAT_80612390 < 10000 || 0x3710 < DAT_80612390) {
                DAT_80610a7a = 0;
                return;
            }
            param_1 = (unsigned int)DAT_80612390 & 0xffff;
        }
        if (DAT_80612390 != (int)(param_1 & 0xffff)) {
            FUN_803c57c8(&bStack_21, &bStack_22, &bStack_23, &bStack_24, &bStack_25);
            int ctx = FUN_803c5de0();
            FUN_803c59c4(ctx, auStack_18, &uStack_1c);
            ctx = FUN_803c5de8();
            FUN_803c59c4(ctx, auStack_18, &uStack_1c);
            FUN_803c5ab0(*(int*)((int)&DAT_805968a0 + DAT_80612384 * 4));
            int* q = (int*)FUN_803c5de8();
            FUN_803c5818(q, &bStack_21, &bStack_22, &iStack_20, &uStack_26, &uStack_27, &bStack_28);
            halt_stub_assert();
        }
        lens_flare_query_store_result((int)(param_1 & 0xffff));
        occ_result_ring_pop();
    }
}

// ============================================================================
// Address range: 0x8017ddb8 - 0x8017e034 | batch_468 (part 1/2) | Date: 2026-09-06
// Subsystem: graphics (occlusion-query DMA upload + poll/reset/init). tag gx-lensflare.
//
// The upload + polling side of the occlusion pipeline.  occ_dma_upload streams a
// buffer to the GPU in 0x4000-byte chunks through the DMA engine; occ_scratch_setup
// carves out the aligned scratch/vertex regions for a query batch; occ_query_poll and
// occ_query_flush drive the result ring from the retire side; occ_init_a/occ_init_b
// are the (trivial) subsystem init entry points (occ_init_b builds the query pool).
//
//   occ_dma_upload       0x8017ddb8  DMA a buffer to the GPU in 0x4000 chunks
//   occ_query_poll       0x8017de5c  poll the retire ring for a finished query
//   occ_query_flush      0x8017df00  flush a stuck query (0xdead sentinel)
//   occ_scratch_setup    0x8017df48  carve aligned scratch/vertex regions
//   occ_init_a           0x8017e02c  init entry (no-op, returns 0)
//   occ_init_b           0x8017e034  init entry: build the query pool

// ---- occlusion DMA / poll globals (batch_468) -------------------------------
extern int*  DAT_80612398;   // scratch region A pointer
extern int   DAT_806123c0;   // aligned scratch base
extern int   DAT_806123c4;   // scratch cursor copy
extern unsigned char DAT_80612360;  // flush-pending flag
extern int   DAT_80612361;   // flush counter

// ---- raw runtime helpers (batch_468 part 1) ---------------------------------
// mem_page_alloc / mem_page_free resolve through generated headers (do not re-declare)
extern void FUN_80399780(int page, int src, int len);   // DMA program src/len
extern void FUN_803998d8(int flag);                     // DMA kick / wait
extern void FUN_8039982c(unsigned int hi, int page, int len);  // DMA program dst
extern int  FUN_803991b8(void);                         // scratch top query A
extern int  FUN_803991b0(void);                         // scratch top query B
extern int  FUN_8039def4(void);                         // region validity check
extern void FUN_8039da88(int* a, int* b);               // clear region range
extern void FUN_803991c8(int addr);                     // set scratch cursor
extern int  FUN_803990d4(unsigned int a, unsigned int b, int c);  // compute region size
extern int  FUN_80399144(int a, unsigned int b);        // reserve region
extern void FUN_803990c4(int addr);                     // commit region
extern unsigned short FUN_803c6ff4(void);               // read current query id

// occ_dma_upload (0x8017ddb8)
// Stream a source buffer (frame handle from FUN_803826e8) to the GPU in 0x4000-byte
// chunks: for each chunk program the DMA source/length (FUN_80399780), kick
// (FUN_803998d8), and program the destination (FUN_8039982c), advancing both halves of
// the 64-bit address.  Wrapped in a frame push/pop and a DMA page (mem_page_alloc/free).
void occ_dma_upload(unsigned int param_dstHi, unsigned int param_srcLo, int param_len)
{
    unsigned long long frame = FUN_803826e8();
    int page = mem_page_alloc();
    (void)param_dstHi; (void)param_srcLo;
    do {
        unsigned int hi = (unsigned int)(frame >> 0x20);
        int chunk = 0x4000;
        if (param_len < 0x4000) {
            chunk = param_len;
        }
        FUN_80399780(page, (int)frame, chunk);
        FUN_803998d8(0);
        FUN_8039982c(hi, page, chunk);
        param_len = param_len - 0x4000;
        frame = ((unsigned long long)(hi + 0x4000) << 0x20) | (unsigned int)((int)frame + 0x4000);
    } while (0 < param_len);
    FUN_803998d8(0);
    mem_page_free(page);
    FUN_80382734();
}

// occ_query_poll (0x8017de5c)
// Poll the retire side of the result ring for the query at the last-popped slot
// (DAT_805978a0[DAT_80612384]).  When the id is in the valid range [10000, 0x3711),
// poll the hardware query id (FUN_803c6ff4, up to 10 tries); on a match store the
// result (lens_flare_query_store_result) and pop the ring (occ_result_ring_pop).
void occ_query_poll(void)
{
    unsigned int id = *(unsigned int*)((int)&DAT_805978a0 + DAT_80612384 * 4);
    DAT_80612390 = (int)id;
    if (9999 < (int)id && (int)id < 0x3711) {
        DAT_80610a7a = 0;
        int tries = 0;
        while (id != FUN_803c6ff4() && tries < 10) {
            tries++;
        }
        if (id == FUN_803c6ff4()) {
            DAT_80610a7a = 1;
            lens_flare_query_store_result((int)id);
            occ_result_ring_pop();
        }
    }
}

// occ_query_flush (0x8017df00)
// Flush a stuck occlusion query when the flush-pending flag (DAT_80612360) is set: pop
// the ring, issue the 0xdead sentinel query (FUN_803c6f40) and end-flush (FUN_803c6b7c).
void occ_query_flush(void)
{
    if (DAT_80612360 != 0) {
        DAT_80612360 = 0;
        DAT_80612361 = 0;
        occ_result_ring_pop();
        FUN_803c6f40(0xdead);
        FUN_803c6b7c();
    }
}

// occ_scratch_setup (0x8017df48)
// Carve the aligned scratch/vertex regions for a query batch: query the scratch top
// (FUN_803991b8), align the base to 32 bytes (DAT_806123c0), reserve param_1 bytes for
// region A (DAT_80612398), validate (FUN_8039def4) and clear it, then reserve/commit
// the vertex region via the FUN_80399xxx region allocator.
void occ_scratch_setup(int param_1)
{
    int top = FUN_803991b8();
    FUN_803991b0();
    DAT_806123c0 = top + 0x1f & (int)0xffffffe0;
    DAT_80612398 = (int*)(DAT_806123c0 + param_1 + 0x1f & (int)0xffffffe0);
    DAT_806123c4 = DAT_806123c0;
    unsigned int ok = (unsigned int)FUN_8039def4();
    if (-1 < (int)ok) {
        *DAT_80612398 = 0;
    }
    FUN_8039da88(DAT_80612398, DAT_80612398 + 2);
    FUN_803991c8((int)DAT_80612398 + 0x21 & (int)0xffffffe0);
    unsigned int a = (unsigned int)FUN_803991b8();
    unsigned int b = (unsigned int)FUN_803991b0();
    int region = FUN_803990d4(a, b, 1);
    FUN_803991c8(region);
    region = FUN_80399144(region + 0x1f & (int)0xffffffe0, b & 0xffffffe0);
    FUN_803990c4(region);
    FUN_803991c8((int)(b & 0xffffffe0));
}

// occ_init_a (0x8017e02c)
// Occlusion subsystem init entry (no-op).  Returns 0.
int occ_init_a(void)
{
    return 0;
}

// occ_init_b (0x8017e034)
// Occlusion subsystem init entry: build the query scratch pool.  Returns 0.
int occ_init_b(void)
{
    occ_pool_create();
    return 0;
}
