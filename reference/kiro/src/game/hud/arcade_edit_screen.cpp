// Batch: batch_878 | Module: arcade map-editor per-frame screen tick + model load + grid utils
// Address range: 0x8030b640 - 0x8030d27c
// Date: 2026-09-11
//
// The arcade map-editor's main per-frame screen tick and its supporting model-load
// and grid/record helpers. aveditor_screen_tick (0x8030b8bc) is the editor screen's
// per-frame update: it runs the save/load FSM (batch_877), streams the preview model,
// steps the camera pan/zoom, resolves the cursor pick against the grid, drives the
// sub-mode navigation (DAT_80612be4[0x95] = active sub-screen), routes input through
// aveditor_screen_input_gate (batch_877), and finally submits the render. All state
// lives in the active screen context DAT_80612be4 (accessed here as int base +
// word offset; the decompiler's float**** casts are index*4 word accesses).
//
//   aveditor_create_model_pool          0x8030b640  resource_pool_create(...,1,6,0x28000)
//                                                   for the 6 preview-model slots.
//   aveditor_poll_model_load            0x8030b670  wait for the model resource set; when
//                                                   ready, unload the stale texture slot and
//                                                   begin loading the new model.
//   aveditor_ensure_models_loaded       0x8030b750  poll the load; on failure raise dirty bit
//                                                   0x2000 and show the load-error dialog.
//   aveditor_find_member_at_cursor      0x8030b7dc  return the member nearest the cursor grid
//                                                   position (min squared distance).
//   aveditor_screen_tick                0x8030b8bc  the per-frame editor screen update (save
//                                                   FSM, model stream, camera, pick, sub-mode
//                                                   nav, input dispatch, render submit).
//   aveditor_write_record_pair          0x8030d0dc  store an 8-byte record pair at base+off,
//                                                   return off+8.
//   aveditor_find_matching_member       0x8030d0f0  scan the member list for one matching the
//                                                   pick's x/y and (grid-snapped) z.
//   aveditor_check_member_placeable     0x8030d1dc  recurse the member chain until one is
//                                                   placeable (FUN_8030d430 flag 0x2000000).
//   aveditor_record_has_value           0x8030d230  search a member record's 6-byte-strided
//                                                   byte list for a value; 1 if present.
//   aveditor_update_member_placeable_flag 0x8030d27c clear the member's select bit, then set
//                                                   the placeable bit (0x20) per the checks.
//
// NON-MATCHING: aveditor_screen_tick carries the full PPC frame/paired-single register file
// as params/locals purely to feed the camera-math and the vararg-style render/gate calls; the
// paired-single doubles (in_f29..in_f31, in_psNN_1) are folded into plain floats. The huge
// DAT_80612be4[0xNN] float**** accesses are modeled as *(u32*)(base + 0xNN*4). void helpers
// whose leftover-register return the decompiler reads (aveditor_save_load_tick / _screen_input_gate,
// hud_screen_destroy_current, avanim_* etc.) are called as statements; hud_screen_destroy_current
// is recovered via a fn-ptr cast where its int result is branched on. Named helpers resolve via
// generated_decls.h; every raw FUN_/DAT_/FLOAT_/__gpr symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

extern "C" {

// prologue/epilogue intrinsics
int  __save_gpr_28(void);
void __restore_gpr_28(void);
int  __save_gpr_25(void);
void __restore_gpr_25(void);

// raw FUN_ helpers
int  FUN_80313258(int index);                     // model id for slot (-1 = none)
void FUN_80340b98(int arg);                        // begin async save
void FUN_8033cff4(void);                           // save step B
void FUN_8033d72c(void);                           // end-of-frame flush
void FUN_8034757c(u64 a1, double a2, double a3, double a4, double a5, double a6, u64 a7,
                  u64 a8, int scratch);            // submit editor render
void FUN_80313cf4(float* pMember);                 // free a member chain
void FUN_80340e78(void);                           // leave sub-view cleanup
void FUN_80337e00(void);                           // open budget overlay
void FUN_8031d730(void);                           // budget overlay step
void FUN_80340cc8(int member);                     // focus the member under cursor
int  FUN_80313af4(double x, double y, double z);    // create a cell at world coords
void FUN_80342478(short* out);                     // snap cursor to grid (out pair)
void FUN_80342298(short* out);                     // read cursor grid coords
u32  FUN_80342874(short* key);                      // grid key occupied?
int  FUN_80332a84(int layoutId);                    // layout accepts type-2 placement?
void FUN_803151e4(double dx, double dy);            // pan the view
void FUN_8033ced0(int a, int b, int c, int d);      // set preview transform
u64  FUN_8033d15c(double angle, int modelId);       // set preview model + angle
u64  FUN_8033ccdc(void);                            // refresh preview
int  FUN_8032204c(int member);                      // count placeable entries for a member
int  FUN_80345de8(double x, double y);              // door/zone hit-test A (-1 = miss)
int  FUN_80345e80(double x, double y);              // door/zone hit-test B (0 = miss)

// active screen context + globals
extern int   DAT_80612be4;   // active screen context base
extern int   DAT_80612be8;   // editor scratch base
extern int   DAT_80612bf4;   // dialog owner / root handle
extern int   DAT_80612c14;   // head of the current member linked list
extern int   DAT_80612d68;   // preview render context ptr
extern int   DAT_806119ec;   // active player/screen index
extern int   DAT_806108e8;   // frame delta numerator
extern int   DAT_806109c0;   // frame delta denominator (int->float)
extern int   DAT_806109c4;   // preview scale denom
extern int   DAT_806109c8;   // preview scale num
extern int   DAT_80612c7c;   // pan bound X
extern int   DAT_80612c80;   // pan bound Y
extern int   DAT_80612c84;   // cursor->grid X bias
extern int   DAT_80612c88;   // cursor->grid Y bias
extern int   DAT_80612c8c;   // last pan direction
extern unsigned char DAT_8051a814;   // per-screen state table (cursor block)
extern unsigned char DAT_8051a820;   // per-screen state (screen root ptr slot)
extern unsigned char DAT_8051a834;   // per-screen cursor X slot
extern unsigned char DAT_8051a838;   // per-screen cursor Y slot
extern unsigned char DAT_8047b908;   // per-model preview angle table
extern void* PTR_PTR_8047f988;       // sub-screen id table

extern float FLOAT_80612c54, FLOAT_80612c58, FLOAT_80612c5c, FLOAT_80612c60;
extern float FLOAT_80612c64, FLOAT_80612c68, FLOAT_80612c6c, FLOAT_80612c70;
extern float FLOAT_80612c74, FLOAT_80612c78;
extern float FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a614, FLOAT_8061a644;
extern float FLOAT_8061a664, FLOAT_8061a668, FLOAT_8061a66c, FLOAT_8061a670;
extern float FLOAT_8061a674, FLOAT_8061a678, FLOAT_8061a67c, FLOAT_8061a680;
extern float FLOAT_8061a684, FLOAT_8061a688, FLOAT_8061a68c, FLOAT_8061a690;
extern float FLOAT_8061a694, FLOAT_8061a698, FLOAT_8061a69c, FLOAT_8061a5f8;
extern float FLOAT_80611a64, FLOAT_80611a68, FLOAT_80611a6c, FLOAT_80611a70;
extern double DOUBLE_8061a5e0;

} // extern "C"

// forward decls
int  aveditor_poll_model_load(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                              u64 param_5, u64 param_6, u64 param_7, u64 param_8);
float* aveditor_find_member_at_cursor(void);
int  aveditor_find_matching_member(void);
u32  aveditor_check_member_placeable(void);
int  aveditor_record_has_value(int param_1, u32 param_2);

// ---------------------------------------------------------------------------
// 0x8030b640  aveditor_create_model_pool
// ---------------------------------------------------------------------------
void aveditor_create_model_pool(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    resource_pool_create(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         1, 6, 0x28000, 0, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x8030b670  aveditor_poll_model_load
// ---------------------------------------------------------------------------
int aveditor_poll_model_load(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                             u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int ready;
    int slot;
    int modelId;
    int have;
    u64 tex = 0;

    ready = __save_gpr_28();      // "commit" flag from incoming register
    (void)param_1;
    if (wait_resources_loaded(1, 0, 6) == 0) {
        slot = 0;
        do {
            modelId = FUN_80313258(slot);
            if ((modelId != -1) &&
                (have = wait_resource_by_id(1, 0, slot + 1, modelId), have == -1)) {
                if (ready != 0) {
                    have = wait_resource_by_id(1, 0, 6, modelId);
                    if (-1 < have) {
                        tex = (u64)texture_slot_unload();
                    }
                    model_begin_load(tex, param_2, param_3, param_4, param_5, param_6, param_7,
                                     param_8, 1, slot, modelId, modelId, 0, 0, 0, 0);
                }
                break;
            }
            slot = slot + 1;
        } while (slot < 6);
    }
    __restore_gpr_28();
    return ready;
}

// ---------------------------------------------------------------------------
// 0x8030b750  aveditor_ensure_models_loaded
// ---------------------------------------------------------------------------
void aveditor_ensure_models_loaded(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                   u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int done = aveditor_poll_model_load(param_1, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8);
    if (done == 0) {
        int base = DAT_80612be4;
        *(u32 *)(base + 0x238) = *(u32 *)(base + 0x238) | 0x2000;
        int msg   = get_string_by_id(0x9ce);
        int owner = get_string_by_id(0x95a);
        int root  = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        hud_create_simple_dialog(root, owner, msg);
    }
}

// ---------------------------------------------------------------------------
// 0x8030b7dc  aveditor_find_member_at_cursor
// ---------------------------------------------------------------------------
float* aveditor_find_member_at_cursor(void)
{
    float* best = (float *)DAT_80612c14;
    for (float* m = (float *)DAT_80612c14; m != (float *)0; m = (float *)*(int *)(m + 0x10)) {
        float dx = (FLOAT_80612c58 *
                    (*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0) -
                     (float)((double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a5e0)) +
                    FLOAT_80612c78) - m[0];
        float dy = (FLOAT_80612c54 *
                    (*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                     (float)((double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a5e0)) +
                    FLOAT_80612c74) - m[1];
        if ((int)(dx * dx + dy * dy) < 0x7fffffff) {
            best = m;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// 0x8030d0dc  aveditor_write_record_pair
// ---------------------------------------------------------------------------
int aveditor_write_record_pair(int base, int off, u32 a, u32 b)
{
    *(u32 *)(base + off) = a;
    ((u32 *)(base + off))[1] = b;
    return off + 8;
}

// ---------------------------------------------------------------------------
// 0x8030d0f0  aveditor_find_matching_member
// ---------------------------------------------------------------------------
int aveditor_find_matching_member(void)
{
    float* pick;
    float* m;
    float  snapZ;
    int    found = 0;

    pick  = (float *)__save_gpr_28();      // scratch pick record
    m     = (float *)DAT_80612c14;
    snapZ = FLOAT_8061a614 + pick[2];
    if (FUN_8032204c((int)pick) != 0) {
        for (; m != (float *)0; m = (float *)*(int *)(m + 0x10)) {
            bool coordMatch = false;
            u32 leadZ = countLeadingZeros((u32)((int)m[4] - (int)pick[4]));
            if ((m[0] == pick[0]) && (m[1] == pick[1])) {
                double bias = (double)CONCAT44(0x43300000, (int)snapZ ^ 0x80000000);
                if (m[2] == (float)(bias - DOUBLE_8061a5e0)) {
                    coordMatch = true;
                }
            }
            if (((leadZ >> 5) != 0) && coordMatch) {
                found = 1;
                break;
            }
        }
    }
    __restore_gpr_28();
    return found;
}

// ---------------------------------------------------------------------------
// 0x8030d1dc  aveditor_check_member_placeable
// ---------------------------------------------------------------------------
u32 aveditor_check_member_placeable(void)
{
    if (aveditor_find_matching_member() == 0) {
        return 0;
    }
    if ((aveditor_place_companion_segments() & 0x2000000) == 0) {
        return aveditor_check_member_placeable();
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x8030d230  aveditor_record_has_value
// ---------------------------------------------------------------------------
int aveditor_record_has_value(int param_1, u32 param_2)
{
    int rec = *(int *)(param_1 + *(int *)(param_1 + 0xc) * 4 + 0x2c);
    int n   = *(int *)(rec + 0x44);
    int off = 0;
    if (0 < n) {
        do {
            if (*(unsigned char *)(rec + off + 0x48) == param_2) {
                return 1;
            }
            off = off + 6;
            n   = n + -1;
        } while (n != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8030d27c  aveditor_update_member_placeable_flag
// ---------------------------------------------------------------------------
void aveditor_update_member_placeable_flag(int param_1)
{
    u32 flags = aveditor_place_companion_segments();
    *(unsigned char *)(param_1 + 0x14) = *(unsigned char *)(param_1 + 0x14) & 0xdf;
    if ((aveditor_record_has_value(param_1, 5) != 0) && ((flags & 0x2000000) == 0)) {
        int placeable = (int)aveditor_check_member_placeable();
        *(unsigned char *)(param_1 + 0x14) =
            (unsigned char)(*(unsigned char *)(param_1 + 0x14) | (~-(placeable != 0) & 0x20U));
    }
}

// ---------------------------------------------------------------------------
// 0x8030b8bc  aveditor_screen_tick
// The per-frame editor screen update. base = DAT_80612be4; context words are
// accessed as *(u32*)(base + index*4).
// ---------------------------------------------------------------------------
void aveditor_screen_tick(u64 param_1, double param_2, double param_3, double param_4,
                          u64 param_5, u64 param_6, double param_7, double param_8,
                          u32 param_9, u32 param_10, u32* param_11, float* param_12,
                          u32* param_13, u32 param_14, u32 param_15, u32 param_16)
{
    int  base = DAT_80612be4;
    int  root;
    u32  pressed;
    u32  held;
    u32  w97;
    u64  frame;

    __save_gpr_25();
    frame = 0;
    hud_screen_clear_all_handlers(DAT_80612bf4);
    pressed = input_get_buttons_pressed(0);
    held    = input_get_buttons_held(0);

    *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xffffbfff;
    *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xfffffbff;

    if (aveditor_save_pending() != 0) {
        aveditor_save_load_tick(frame, param_2, param_3, param_4, param_5, param_6, param_7,
                                param_8, param_9, param_10, (u32)(uintptr_t)param_11,
                                (u32)(uintptr_t)param_12, (u32)(uintptr_t)param_13, param_14,
                                param_15, param_16);
        __restore_gpr_25();
        return;
    }

    if ((*(u32 *)(base + 0x238) & 0x2000) != 0) {
        if (aveditor_poll_model_load(frame, param_2, param_3, param_4, param_5, param_6, param_7,
                                     param_8) != 0) {
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            hud_screen_pop(root);
            *(u32 *)(base + 0x238) = *(u32 *)(base + 0x238) & 0xffffdfff;
            pack_set_force_slot(0xffffffff);
        }
        __restore_gpr_25();
        return;
    }

    if ((*(u32 *)(base + 0x25c) & 0x2000000) != 0) {
        *(int *)DAT_80612d68 = 1;
        FUN_80340b98(1);
        FUN_8033cff4();
        FUN_8033d72c();
        __restore_gpr_25();
        return;
    }

    // camera zoom lerp toward FLOAT_8061a604 target
    {
        float cur = *(float *)(base + 0x98 * 4);
        float step = FLOAT_8061a668 * (float)((double)DAT_806108e8);
        float dt = FLOAT_8061a614 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a5e0);
        if (FLOAT_8061a604 <= cur) {
            if (FLOAT_8061a604 < cur) {
                *(float *)(base + 0x98 * 4) = -(float)(step * dt - cur);
                if (*(float *)(base + 0x98 * 4) < FLOAT_8061a670) {
                    *(float *)(base + 0x98 * 4) = FLOAT_8061a604;
                }
            }
        }
        else {
            *(float *)(base + 0x98 * 4) = (float)(step * dt + cur);
            if (FLOAT_8061a66c < *(float *)(base + 0x98 * 4)) {
                *(float *)(base + 0x98 * 4) = FLOAT_8061a604;
            }
        }
    }

    avanim_tick();

    if (((*(u32 *)(base + 0x25c) & 0x44088) != 0) && ((*(u32 *)(base + 0x25c) & 0x20000000) == 0)) {
        *(u32 *)(base + 0xbc6 * 4) = 0;
        *(u32 *)(base + 0xbc7 * 4) = 0;
        *(u32 *)(base + 0xbdc * 4) = 0;
    }

    aveditor_flush_if_dirty();

    if (((*(u32 *)(base + 0x25c) & 0x16004008) != 0) ||
        ((root = ((int (*)(void))&hud_screen_destroy_current)(), root != 0 &&
          (root = ((int (*)(void))&hud_screen_destroy_current)(),
           (*(u32 *)(root + 0x214) & 4) != 0))) ||
        (((*(u32 *)(base + 0x25c) & 0x80) == 0) &&
         (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
          root != DAT_80612bf4))) {
        FUN_8034757c(frame, param_2, param_3, param_4, param_2, param_3, param_7, param_8,
                     DAT_80612be8);
        FUN_8033d72c();
        __restore_gpr_25();
        return;
    }

    *(u32 *)(base + 0x96 * 4) = 0;

    if (aveditor_can_exit() != 0) {
        int delta = 0;
        int count = 3;
        int target = *(int *)(base + 0x95 * 4);
        if (*(int *)(base + 0x80 * 4) != 0) {
            count = 5;
        }
        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        if (((*(u32 *)(root + 0xc0) & 0x100000) == 0) || ((*(u32 *)(base + 0x25c) & 0x20) != 0)) {
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc4) & 0x10) == 0) {
                if ((held & 2) == 0) {
                    if ((held & 1) != 0) { delta = -1; }
                }
                else { delta = 1; }
            }
        }
        else {
            int hit = FUN_80345de8((double)*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0),
                                   (double)*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0));
            if (hit < 0) {
                delta = FUN_80345e80((double)*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0),
                                     (double)*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0));
            }
        }
        if (delta != 0) {
            target = *(int *)(base + 0x95 * 4) + delta;
            if (target < 0) { target = count + -1; }
            else if (count <= target) { target = 0; }
        }
        if (*(int *)(base + 0x95 * 4) != target) {
            FUN_80313cf4(*(float **)(base + 0xbdf * 4));
            *(u32 *)(base + 0xbdf * 4) = 0;
            *(u32 *)(base + 0xbcd * 4) = 0xffffffff;
            *(short *)(base + 0xbc4 * 4) = (short)0xffff;
            *(u32 *)(base + 0x94 * 4) = *(u32 *)(base + 0x95 * 4);
            *(u32 *)(base + 0x95 * 4) = (u32)target;
            avanim_play_move_sound(0x157e);
            if ((*(u32 *)(base + 0x25c) & 0x80) != 0) {
                FUN_80340e78();
                *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xdfffffff;
                hud_screen_navigate_to(DAT_80612bf4,
                                       *(int *)((int)&PTR_PTR_8047f988 + *(int *)(base + 0x95 * 4) * 4),
                                       -1);
            }
        }
    }

    if (((((int)*(u32 *)(base + 0x95 * 4) < 5) && ((*(u32 *)(base + 0x25c) & 0x4044088) == 0)) &&
         (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
          root == DAT_80612bf4)) &&
        ((((held = input_get_buttons_released(0)), (held & 0x80) != 0) ||
          ((pressed = input_get_buttons_pressed(0)), (pressed & 0x80) == 0)))) {
        if ((*(u32 *)(base + 0x25c) & 0x2000) != 0) {
            *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xffffdfff;
        }
    }

    if (((*(u32 *)(base + 0x25c) & 0x4044088) == 0) &&
        (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
         root == DAT_80612bf4)) {
        if (((pressed & 4) != 0) || ((pressed & 8) != 0)) {
            hud_input_reset_repeat_timers();
        }
        if (((*(u32 *)(base + 0xbcd * 4) == 0xffffffff) &&
             (*(short *)(base + 0xbc4 * 4) == -1)) && (held != 0) &&
            (*(u32 *)(base + 0x4617 * 4) == 0xffffffff)) {
            if ((held & 0x100) != 0) {
                FUN_80337e00();
                FUN_8031d730();
                FUN_8033d72c();
                __restore_gpr_25();
                return;
            }
            if ((held & 0x800) != 0) {
                float* m = aveditor_find_member_at_cursor();
                FUN_80340cc8((int)m);
                FUN_8033d72c();
                __restore_gpr_25();
                return;
            }
        }
        map_update_budget();
        paste_buffer_reserve();
        *(u32 *)(base + 0xbc6 * 4) = 0;
        *(u32 *)(base + 0xbc7 * 4) = 0;
        *(u32 *)(base + 0xbdc * 4) = 0;

        // resolve the cursor pick into a cell handle
        if (((*(u32 *)(base + 0x25c) & 0x20) == 0) && (*(u32 *)(base + 0xbdf * 4) == 0)) {
            int screenIdx = DAT_806119ec * 0x1e0;
            double biasX = (double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000);
            double biasY = (double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000);
            double negZoom = (double)CONCAT44(0x43300000, -(float)*(u32 *)(base + 0x99 * 4));
            double cx = (double)(*(float *)(&DAT_8051a838 + screenIdx) -
                                 (float)(biasY - DOUBLE_8061a5e0));
            double cy = (double)(float)(negZoom - DOUBLE_8061a5e0);
            double cz = (double)(*(float *)(&DAT_8051a834 + screenIdx) -
                                 (float)(biasX - DOUBLE_8061a5e0));
            int cell = FUN_80313af4((double)(float)((double)FLOAT_80612c58 * cz + (double)FLOAT_80612c78),
                                    (double)(float)((double)FLOAT_80612c54 * cx + (double)FLOAT_80612c74),
                                    cy);
            *(u32 *)(base + 0xbc6 * 4) = (u32)cell;

            int sub = *(int *)(base + 0x95 * 4);
            if ((sub == 1) ||
                ((sub == 4) &&
                 (((*(int *)(base + 0x4617 * 4) == 3) ||
                   ((u32)(*(int *)(base + 0x4617 * 4) - 1) < 2)) ||
                  (*(int *)(base + 0x4617 * 4) == -1)))) {
                int outY = 0;
                int outRec = 0;
                if ((avanim_layout_is_0x22(*(int *)(base + 0xbcd * 4)) != 0) &&
                    (*(u32 *)(base + 0xbc6 * 4) != 0)) {
                    aselgrid_pick_cursor_cell(*(int *)(base + 0xbc6 * 4), (int)&outRec, &outY);
                    *(u32 *)(base + 0xbdd * 4) = (u32)outY;
                    if (-1 < outY) {
                        int idx = 0;
                        void* rec = 0;
                        aselgrid_pick_at_point(*(int *)(base + 0xbc6 * 4), *(int *)(base + 0xbdd * 4),
                                               (u32)outRec, (u32)outY, &rec, &idx);
                        if (-1 < idx) {
                            *(u32 *)(base + 0xbdc * 4) = *(u32 *)(base + 0xbc6 * 4);
                            *(u32 *)(base + 0xbc6 * 4) = 0;
                        }
                        goto after_pick;
                    }
                }
                if (((int)*(u32 *)(base + 0xbcd * 4) < 0) && (*(u32 *)(base + 0xbc6 * 4) != 0)) {
                    *(u32 *)(base + 0xbde * 4) = (u32)aselgrid_hit_test_index();
                }
                else {
                    *(u32 *)(base + 0xbde * 4) = 0xffffffff;
                }
            }
        }
    after_pick:
        // sub-mode 0/2: cell edit; else: zone hit
        {
            int sub = *(int *)(base + 0x95 * 4);
            int hitFlag = 0;
            if ((sub == 0) || (sub == 2)) {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(u32 *)(root + 0xc0) & 0x10) != 0) {
                    *(u32 *)(base + 0x238) = *(u32 *)(base + 0x238) & 0xffffbfff;
                }
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(u32 *)(root + 0xc4) & 0x100000) != 0) {
                    if ((*(u32 *)(base + 0x25c) & 0x20) == 0) {
                        if (((*(u32 *)(base + 0x238) & 0x4000) == 0) &&
                            (aveditor_cursor_hits_zone() == 0)) {
                            hitFlag = 1;
                        }
                    }
                    else {
                        aselgrid_flush_visibility_flags();
                        *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xffffffdf;
                    }
                }
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if (((*(u32 *)(root + 0xc0) & 0x40) != 0) && (*(u32 *)(base + 0xbc6 * 4) == 0) &&
                    (*(u32 *)(base + 0xbe7 * 4) != 0)) {
                    aselgrid_clear_change_flags();
                }
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(u32 *)(root + 0xc4) & 0x10) != 0 && (*(u32 *)(base + 0xbdf * 4) == 0) &&
                    ((*(u32 *)(base + 0x238) & 0x4000) == 0)) {
                    aselgrid_recompute_visibility();
                }
            }
            else {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if (((*(u32 *)(root + 0xc4) & 0x100000) != 0) &&
                    (aveditor_cursor_hits_zone() == 0)) {
                    hitFlag = 1;
                }
            }

            // zoom step via shoulder buttons
            if (((held & 0x400) == 0) || (*(int *)(base + 0x80 * 4) == 0)) {
                if (((held & 0x200) != 0) && (*(int *)(base + 0x80 * 4) != 0)) {
                    if ((int)*(u32 *)(base + 0x99 * 4) < -1) {
                        avanim_play_move_sound(0x1576);
                    }
                    else {
                        int nz = -2;
                        *(u32 *)(base + 0x99 * 4) = *(u32 *)(base + 0x99 * 4) - 1;
                        if (-2 < (int)*(u32 *)(base + 0x99 * 4)) {
                            nz = (int)*(u32 *)(base + 0x99 * 4) - 1;
                        }
                        *(u32 *)(base + 0x9a * 4) = (u32)nz;
                        avanim_play_move_sound(0x157d);
                    }
                }
            }
            else if ((int)*(u32 *)(base + 0x99 * 4) < 2) {
                *(u32 *)(base + 0x99 * 4) = *(u32 *)(base + 0x99 * 4) + 1;
                *(u32 *)(base + 0x9a * 4) = *(u32 *)(base + 0x99 * 4) - 1;
                avanim_play_move_sound(0x157d);
            }
            else {
                avanim_play_move_sound(0x1576);
            }

            *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xfffffffb;
            if ((*(int *)(base + 0x95 * 4) == 0) && (*(u32 *)(base + 0xbdf * 4) != 0) &&
                (((int (*)(void))&aselgrid_scan_hit_grid)() != 0)) {
                *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
            }
            FUN_80342478((short *)(base + 0x2f2e));

            // placement/confirm checks for the active sub-mode
            if ((*(int *)(base + 0x95 * 4) == 1) || (*(int *)(base + 0x95 * 4) == 4)) {
                *(u32 *)(base + 0xbc7 * 4) = 0;
                FUN_80342298((short *)(base + 0xbca * 4));
                if (*(int *)(base + 0xbc6 * 4) != 0) {
                    *(u32 *)(base + 0xbc7 * 4) = (u32)(uintptr_t)roster_resolve_picked();
                }
                if ((*(int *)(base + 0x95 * 4) == 4) &&
                    (((*(int *)(base + 0x4617 * 4) == -1) || (*(int *)(base + 0x4617 * 4) == 2) ||
                      (*(int *)(base + 0x4617 * 4) == 5)) &&
                     ((*(int *)(base + 0x4617 * 4) == -1) ||
                      ((*(int *)(base + 0xbc7 * 4) != 0 &&
                        (FUN_80332a84(*(int *)*(int **)(base + 0xbc7 * 4)) != 0)) ||
                       (-1 < (int)*(u32 *)(base + 0xbde * 4)))))) {
                    *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
                }
                else if ((*(int *)(base + 0x95 * 4) == 4) &&
                         ((*(int *)(base + 0x4617 * 4) == 7) || (*(int *)(base + 0x4617 * 4) == 6))) {
                    if (*(int *)(base + 0xbc6 * 4) != 0) {
                        *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
                    }
                }
                else if (*(short *)(base + 0xbc4 * 4) < 0) {
                    *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
                }
            }

            if (*(int *)(base + 0x95 * 4) == 3) {
                *(u32 *)(base + 0xbc8 * 4) = 0xffffffff;
                FUN_80342298((short *)(base + 0xbca * 4));
                if (*(int *)(base + 0xbc6 * 4) != 0) {
                    short key[2];
                    key[0] = *(short *)(base + 0xbca * 4);
                    key[1] = *(short *)(base + 0xbcb * 4);
                    *(u32 *)(base + 0xbc8 * 4) =
                        aselgrid_find_record(*(int *)(base + 0xbc6 * 4), key);
                }
                if ((*(int *)(base + 0xbc6 * 4) == 0) && (*(short *)(base + 0xbc4 * 4) < 0)) {
                    *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
                }
                else {
                    short key[2];
                    key[0] = *(short *)(base + 0xbca * 4);
                    key[1] = *(short *)(base + 0xbcb * 4);
                    if ((*(short *)(base + 0xbc4 * 4) < 0) && ((FUN_80342874(key) & 0xff) != 0)) {
                        *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
                    }
                    else if ((-1 < *(short *)(base + 0xbc4 * 4)) &&
                             ((FUN_80342874(key) & 0xff) != 0) &&
                             ((int)*(u32 *)(base + 0xbc8 * 4) < 0)) {
                        *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 4;
                    }
                }
            }

            asel_reset_if_substate0();
            aveditor_screen_input_gate(frame, param_2, param_3, param_4, param_2, param_3, param_7,
                                       param_8, hitFlag, 0, param_11, (int)(intptr_t)param_12,
                                       (int)(intptr_t)param_13, 0, param_15, param_16);
        }
    }

    if (((*(u32 *)(base + 0x25c) & 0x4044088) == 0) &&
        (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
         root == DAT_80612bf4)) {
        *(u32 *)(base + 0xbe1 * 4) = *(u32 *)(&DAT_8051a834 + DAT_806119ec * 0x1e0);
        *(u32 *)(base + 0xbe2 * 4) = *(u32 *)(&DAT_8051a838 + DAT_806119ec * 0x1e0);
    }
    *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) & 0xfffffffc;

    // camera pan from analog sticks
    w97 = *(u32 *)(base + 0x25c);
    if (((w97 & 0x4044088) == 0) &&
        (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
         root == DAT_80612bf4) && ((w97 & 0x8000000) == 0)) {
        double vx, vy;
        if ((*(u32 *)(&DAT_8051a814 + DAT_806119ec * 0x1e0) & 2) == 0) {
            input_set_analog_value((double)FLOAT_8061a5f8, 0);
            vx = (double)(float)((double)FLOAT_8061a664 * input_get_analog_value(0, 3));
            vy = (double)(float)((double)FLOAT_8061a664 * input_get_analog_value(0, 4));
            input_set_analog_value((double)FLOAT_8061a674, 0);
        }
        else {
            vx = (double)(float)((double)FLOAT_8061a678 * input_get_analog_value(0, 1));
            vy = (double)(float)((double)FLOAT_8061a678 * input_get_analog_value(0, 2));
        }
        vx = (double)(float)((double)FLOAT_8061a60c * vx);
        vy = (double)(float)((double)FLOAT_8061a60c * vy);
        if ((vx != (double)FLOAT_8061a604) || (vy != (double)FLOAT_8061a604)) {
            FUN_803151e4(vx, -vy);
            *(u32 *)(base + 0x25c) = *(u32 *)(base + 0x25c) | 2;
            if (FLOAT_8061a604 <= vx) {
                if (vx <= (double)FLOAT_8061a604) {
                    if ((double)FLOAT_8061a604 <= vy) {
                        if ((double)FLOAT_8061a604 < vy) { DAT_80612c8c = 4; }
                    }
                    else { DAT_80612c8c = 3; }
                }
                else { DAT_80612c8c = 1; }
            }
            else { DAT_80612c8c = 2; }
        }
    }

    FUN_8034757c(frame, param_2, param_3, param_4, param_2, param_3, param_7, param_8, DAT_80612be8);
    FUN_8033d72c();
    __restore_gpr_25();
    (void)param_1; (void)param_10; (void)param_14;
}
