// Module: prop_object_util
// TSFP GameCube main.dol non-matching decompilation.
//
// Small shared utilities for level props: pivot/root anchor readers, the
// world-space pivot resolver used across the prop tick/fire/geometry code,
// behaviour-state predicates and setters, and the prop object destructor.
// These are the leaf helpers the larger prop modules (prop_object_behavior,
// prop_object_render) build on.
//
// Object layout (obj = entity; rec = obj+0x7c model record):
//   obj+0x04 : frame/skeleton context ; obj+0x64/0x68/0x6c : world position xyz
//   obj+0x70 : object base yaw (degrees)
//   rec+0x08 : model flags (bit1 collision, bit2 static, bit4 damageable,
//              bit0x40 weapon, bit0x80 animated)
//   rec+0x0c : prop type id ; rec+0x10 : behaviour state
//
// Batch: batch_712  |  Addresses: 0x80240238 - 0x802407f4
// NON-MATCHING decompilation.

#include "common.h"

// Ghidra float-sqrt intrinsic (common.h owns no math intrinsics).
static inline float tsfp_sqrtf(float x) {
    float r = x;
    if (x > 0.0f) {
        r = x;
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
    }
    return r;
}
#ifndef SQRT
#define SQRT(x) tsfp_sqrtf((float)(x))
#endif

// ---------------------------------------------------------------------------
// Shared constants (file-local externs to raw symbols).
// ---------------------------------------------------------------------------
extern float FLOAT_80618908;   // deg->rad numerator (PI)
extern float FLOAT_8061890c;   // deg->rad denominator (180)
extern float FLOAT_80618910;   // 0.0f
extern float FLOAT_80618944;   // angle-wrap low bound (-180)
extern float FLOAT_80618948;   // 360 (angle wrap span)
extern int   DAT_80611cb8;     // live-prop count

// ---------------------------------------------------------------------------
// Cross-module helpers (file-local; common.h owns no raw symbols).
// ---------------------------------------------------------------------------
extern int  prop_def_by_index(int idx);          // prop_table_loader.cpp

extern void mtx4_set_rotation_y(double angle, float *out);
extern void mtx4_set_translate_rotz(double x, double y, double z, double angle, float *out);
extern void mtx4_set_translation(double x, double y, double z);
extern void mtx4_multiply(float *a, float *b, float *out);
extern void mtx4_transform_vector_alias(float *mtx, float *v);
extern void mtx4_transform_point(float *mtx, float *v);
extern void mtx4_transform_point_alias(float *mtx, float *v);
extern void entity_build_local_matrix(float *out, int obj, int a3, int a4);
extern double math_atan2(double y, double x);
extern double FUN_80390324(double x);             // roundf

extern void weapon_get_muzzle_position(void);
extern unsigned int prop_is_interactive_type(int obj);  // prop_object_lifecycle.cpp (0x8024181c)
extern void aiactor_active_swapremove_owner(int entity);
extern void item_pool_a_free(void *rec);
extern void item_pool_b_free(void *buf);

extern unsigned long long FUN_803826f0(void);     // geometry-helper work context
extern void FUN_8038273c(void);                   // release geometry-helper context

// Forward declarations (definition order differs from call order).
void prop_read_root_anchor(int obj, float *out);
void prop_read_root_anchor_ctx(void);

// 0x80240238 | prop_yaw_to_point
// Compute the yaw (degrees, wrapped to (-180,180]) from the prop toward the
// world point 'target', measured relative to the object's base orientation.
double prop_yaw_to_point(int obj, float *target)
{
    float base[3];   // local_68/64/60
    float m[17];     // afStack_5c

    // FUN_80240440 fills the local anchor via the work-context output pointer,
    // which in this frame aliases base[]; the explicit-out reader is equivalent.
    prop_read_root_anchor(obj, base);

    mtx4_set_rotation_y((double)((FLOAT_80618908 * *(float *)(obj + 0x70)) / FLOAT_8061890c), m);
    mtx4_transform_vector_alias(m, base);
    base[0] = base[0] + *(float *)(obj + 100);
    base[1] = base[1] + *(float *)(obj + 0x68);
    base[2] = base[2] + *(float *)(obj + 0x6c);

    double yaw = math_atan2((double)(base[0] - target[0]), (double)(base[2] - target[2]));
    double deg = (double)((FLOAT_8061890c * (float)yaw) / FLOAT_80618908);
    double rounded = FUN_80390324((double)((float)(deg - (double)FLOAT_80618944) / FLOAT_80618948));
    return -(double)(float)((double)FLOAT_80618948 * (double)(float)rounded - deg);
}

// 0x80240324 | prop_read_root_anchor
// Read the prop root/pivot bone anchor (prop-def bone def+0x1c) into 'out',
// defaulting to (0,0,0) when there is no root bone. Weapon-driven objects defer
// to weapon_get_muzzle_position.
void prop_read_root_anchor(int obj, float *out)
{
    int rec = *(int *)(obj + 0x7c);
    out[0] = FLOAT_80618910;
    out[1] = FLOAT_80618910;
    out[2] = FLOAT_80618910;
    if ((*(unsigned int *)(rec + 8) & 0x40) == 0) {
        int def = prop_def_by_index(*(int *)(rec + 0xc));
        unsigned int packed = *(unsigned int *)(def + 0x1c);
        if (-1 < (int)packed) {
            int *node = *(int **)(*(int *)(obj + 4) + 4);
            out[0] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18);
            node = *(int **)(*(int *)(obj + 4) + 4);
            packed = *(unsigned int *)(def + 0x1c);
            out[1] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18 + 4);
            node = *(int **)(*(int *)(obj + 4) + 4);
            packed = *(unsigned int *)(def + 0x1c);
            out[2] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18 + 8);
        }
    } else {
        weapon_get_muzzle_position();
    }
}

// 0x80240440 | prop_read_root_anchor_ctx
// Work-context variant of prop_read_root_anchor: reads the prop root anchor into
// the caller's work-context output slot. For prop type 9 it additionally rotates
// the anchor about Z by the object yaw and transforms the pivot through it.
void prop_read_root_anchor_ctx(void)
{
    unsigned long long ctx = FUN_803826f0();
    int self = (int)(ctx >> 0x20);
    float *out = (float *)ctx;
    int rec = *(int *)(self + 0x7c);

    out[0] = FLOAT_80618910;
    out[1] = FLOAT_80618910;
    out[2] = FLOAT_80618910;
    if ((*(unsigned int *)(rec + 8) & 0x40) == 0) {
        int def = prop_def_by_index(*(int *)(rec + 0xc));
        unsigned int packed = *(unsigned int *)(def + 0x1c);
        if (-1 < (int)packed) {
            int *node = *(int **)(*(int *)(self + 4) + 4);
            out[0] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18);
            node = *(int **)(*(int *)(self + 4) + 4);
            packed = *(unsigned int *)(def + 0x1c);
            out[1] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18 + 4);
            node = *(int **)(*(int *)(self + 4) + 4);
            packed = *(unsigned int *)(def + 0x1c);
            out[2] = *(float *)(node[*node * -0x30 + ((int)packed >> 0x10) * 0x30 + 0x1b] +
                                (packed & 0xffff) * 0x18 + 8);
        }
        if (*(int *)(rec + 0xc) == 9) {
            int *node = *(int **)(*(int *)(self + 4) + 4);
            float *b = (float *)node[*node * -0x30 + 0x1b];
            float mA[16];  // afStack_98
            float mB[22];  // afStack_58
            mtx4_set_translate_rotz((double)b[0], (double)b[1], (double)b[2],
                                    (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                                    mB);
            mtx4_set_translation(-(double)b[0], -(double)b[1], -(double)b[2]);
            mtx4_multiply(mA, mB, mB);
            mtx4_transform_point(mB, out);
        }
    } else {
        weapon_get_muzzle_position();
    }
    FUN_8038273c();
}

// 0x802405e8 | prop_get_world_pivot
// Resolve the prop's world-space pivot: read the root anchor (via the work
// context) then transform it through the object's full local matrix.
void prop_get_world_pivot(int obj, float *out)
{
    float p[3];      // local_58/54/50
    float m[17];     // afStack_4c
    prop_read_root_anchor_ctx();
    entity_build_local_matrix(m, obj, 1, 0);
    mtx4_transform_point_alias(m, p);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

// 0x8024065c | prop_is_state_idle
// Return 1 when the prop behaviour state (rec+0x10) equals 1 (idle), else 0.
unsigned int prop_is_state_idle(int obj)
{
    return countLeadingZeros(1 - *(int *)(*(int *)(obj + 0x7c) + 0x10)) >> 5;
}

// 0x80240674 | prop_set_idle_if_valid
// If the entity (rec[0]) is valid and its state (rec[4]) is not destroyed(9) or
// 8, force it to the idle state (1).
void prop_set_idle_if_valid(int *rec)
{
    if ((prop_is_interactive_type(rec[0]) != 0) && (rec[4] != 9) && (rec[4] != 8)) {
        rec[4] = 1;
    }
}

// 0x802406c8 | prop_init_state_from_flags
// Choose the prop's initial behaviour state from its model flags (rec+8):
//   weapon (bit0x40) -> 5 ; animated (bit0x80) & (damageable|collision) -> 1 ;
//   otherwise -> 4 (return-to-rest / static).
void prop_init_state_from_flags(int rec)
{
    unsigned int flags = *(unsigned int *)(rec + 8);
    if ((flags & 0x40) != 0) {
        *(int *)(rec + 0x10) = 5;
        return;
    }
    if ((flags & 0x80) != 0) {
        if (((flags & 4) != 0) || ((flags & 2) != 0)) {
            *(int *)(rec + 0x10) = 1;
            return;
        }
    }
    *(int *)(rec + 0x10) = 4;
}

// 0x80240710 | prop_reset_active_state
// If the entity (rec[0]) is valid and not destroyed(9)/8, reset its state to 5
// (weapon, flag 0x40) or 4 (return-to-rest) depending on the model flag bit.
void prop_reset_active_state(int *rec)
{
    if ((prop_is_interactive_type(rec[0]) != 0) && (rec[4] != 9)) {
        if (rec[4] != 8) {
            if ((rec[2] & 0x40) == 0) {
                rec[4] = 4;
            } else {
                rec[4] = 5;
            }
        }
    }
}

// 0x8024077c | prop_is_destroyed
// Return 1 when the prop is destroyed/hidden: the hidden visibility flag
// (def+0x90) is set in the frame flags, or the behaviour state is 9 or 0xc.
unsigned int prop_is_destroyed(int obj)
{
    int rec = *(int *)(obj + 0x7c);
    int def = prop_def_by_index(*(int *)(rec + 0xc));
    if ((*(unsigned int *)(*(int *)(obj + 4) + 0x30) & *(unsigned int *)(def + 0x90)) == 0) {
        int state = *(int *)(rec + 0x10);
        if ((state == 9) || (state == 0xc)) {
            return 1;
        }
        return 0;
    }
    return 1;
}

// 0x802407f4 | prop_object_destroy
// Prop object destructor: unlink from the active list, free the model record's
// bone buffer (rec+0x6c) and the record itself, and decrement the live-prop
// count.
void prop_object_destroy(int obj)
{
    int *rec = *(int **)(obj + 0x7c);
    aiactor_active_swapremove_owner(obj);
    item_pool_b_free((void *)rec[0x1b]);
    item_pool_a_free(rec);
    DAT_80611cb8 = DAT_80611cb8 - 1;
}
