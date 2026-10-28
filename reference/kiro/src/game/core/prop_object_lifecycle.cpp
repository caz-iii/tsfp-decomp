// Module: prop_object_lifecycle
// TSFP GameCube main.dol non-matching decompilation.
//
// Level-prop construction / spawn placement, a couple of small type/sound
// predicates, and the prop-update-subsystem state block. This is the front end
// that turns a level spawn descriptor into a live prop entity (allocating its
// model record + bone buffer, seeding orientation/limits, registering the
// per-type update/render callbacks) plus the small globals that gate the
// subsystem's periodic scan.
//
// Batch: batch_713  |  Addresses: 0x8024083c - 0x802418b4 (subset)
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// Shared constants / prop-subsystem state (file-local externs to raw symbols).
// ---------------------------------------------------------------------------
extern float FLOAT_806108ec;   // frame delta scale
extern float FLOAT_80618908;   // deg->rad numerator (PI)
extern float FLOAT_8061890c;   // deg->rad denominator (180)
extern float FLOAT_80618910;   // 0.0f
extern float FLOAT_80618928;   // midpoint blend (0.5f)
extern float FLOAT_8061893c;   // (unused placeholder value fed to floor query)
extern float FLOAT_80618944;   // angle-wrap low bound
extern float FLOAT_80618960;   // spin-rate scale
extern double DOUBLE_80618900; // 0x43300000_00000000 bias

extern int  DAT_80612614;      // current level id
extern int  DAT_806109c0;      // rng/frame counter
extern int  DAT_80610ba0;      // slot-record table base (0x29c stride)
extern int  DAT_80611cb8;      // live-prop count
extern int  DAT_80611d78;      // co-op partner / owner table base
extern char DAT_805015bd;      // mode discriminator byte ('\x03' splitscreen)
extern char DAT_80501572;      // splitscreen player-count gate
extern int  DAT_805015bf;      // active-variant column index

// prop-update subsystem state block (0x805cacd8 .. 0x805cad28)
extern unsigned char DAT_805cacd8[];  // 3x 0x10 sub-blocks
extern int  DAT_805cad08;
extern int  DAT_805cad0c;      // one-time-init latch
extern int  DAT_805cad10;
extern int  DAT_805cad14;
extern int  DAT_805cad18;
extern int  DAT_805cad1c;
extern int  DAT_805cad20;      // pending target/id
extern int  DAT_805cad24;      // random re-scan countdown
extern int  DAT_805cad28;      // pending flag

// ---------------------------------------------------------------------------
// Cross-module helpers (file-local; common.h owns no raw symbols).
// ---------------------------------------------------------------------------
extern int  prop_def_by_index(int idx);                 // prop_table_loader.cpp
extern void prop_init_state_from_flags(int rec);        // prop_object_util.cpp
extern void prop_read_aim_anchor(int obj, float *out);  // prop_object_behavior.cpp
extern void prop_compute_muzzle_origin(int obj, float *out); // prop_object_behavior.cpp
extern void prop_death_burst_tick(void);                // prop_object_behavior.cpp (0x8023f64c)
extern void prop_explode_tick(void);                    // prop_object_behavior.cpp (0x8023f8f8)

extern int  item_pool_a_alloc(void);
extern int  item_pool_b_alloc(void);
extern void memset_wrapper(int dst, int val, int len);
extern void render_instance_set_material(double d1, int *frame, int a3);
extern void weapon_on_spawn(int entity, int desc);
extern int  floor_height_from_position(double d1, double d2);
extern void entity_special_list_add(int entity);
extern void entity_set_render_type(int entity, int cb);
extern void entity_register_for_update(int entity, int phase, int cb);
extern void mtx4_set_rotation_zyx(double rx, double ry, double rz, float *out);
extern void mtx4_transform_vector(float *mtx, float *v);
extern unsigned int rng_counter_tick(void);
extern void FUN_80242298(unsigned int *subBlock);       // clear one subsystem sub-block
extern double FUN_80390324(double x);                   // roundf (unused here, kept for parity)

extern unsigned long long FUN_803826f0(void);           // constructor work context
extern int  __save_gpr_28(void);                        // Ghidra spill marker
extern void FUN_8038273c(void);                          // release constructor context

// Forward declaration (prop_object_init calls prop_spawn_place, defined later).
void prop_spawn_place(void);

// 0x8024181c | prop_is_interactive_type
// Return 0 for the "static / effect / destroyed" prop types (7, 8, 9) and 1 for
// every other type. Used to gate idle/active state resets to real props.
unsigned int prop_is_interactive_type(int obj)
{
    int type = *(int *)(*(int *)(obj + 0x7c) + 0xc);
    if (type != 8) {
        if (type < 8) {
            if (6 < type) {   // type == 7
                return 0;
            }
        } else if (type < 10) {   // type == 9
            return 0;
        }
    }
    return 1;
}

// 0x80241854 | prop_get_ambient_sound_id
// Return the prop ambient sound id (0xbfa), or -1 (no sound) on level 4.
int prop_get_ambient_sound_id(void)
{
    if (DAT_80612614 != 4) {
        return 0xbfa;
    }
    return -1;
}

// 0x80241874 | prop_pending_is_empty
// Return 1 when the subsystem pending id (DAT_805cad20) is zero, else 0.
unsigned int prop_pending_is_empty(void)
{
    return countLeadingZeros(DAT_805cad20) >> 5;
}

// 0x8024188c | prop_request_enable
// Record a pending id and flag the subsystem active; on first use latch the
// one-time-init flag DAT_805cad0c.
void prop_request_enable(int pendingId)
{
    DAT_805cad20 = pendingId;
    DAT_805cad28 = 1;
    if (DAT_805cad0c != 0) {
        return;
    }
    DAT_805cad0c = 1;
}

// 0x802418b4 | prop_subsystem_reset
// Reset the prop-update subsystem: clear the three 0x10-byte sub-blocks, zero
// the state globals, mark the object frame flag, and seed a random re-scan
// countdown (rng % 10 + 5).
void prop_subsystem_reset(void)
{
    int self = (int)(((unsigned long long)__save_gpr_28()) >> 0x20);
    int off = 0;
    for (int i = 0; i < 3; i = i + 1) {
        FUN_80242298((unsigned int *)((int)DAT_805cacd8 + off));
        off = off + 0x10;
    }
    DAT_805cad08 = 0;
    DAT_805cad0c = 0;
    DAT_805cad10 = 0;
    DAT_805cad1c = 0;
    *(int *)(*(int *)(self + 4) + 0x30) = 7;
    DAT_805cad14 = 0;
    DAT_805cad18 = 0;
    *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) | 0x400;
    DAT_805cad20 = 0;
    DAT_805cad24 = (int)(rng_counter_tick() % 10) + 5;
}

// 0x8024083c | prop_object_init
// Prop object constructor. Allocate the model record (item_pool_a) and its bone
// buffer (item_pool_b), bump the live-prop count, set up the render material and
// object flags, copy the fields from the spawn descriptor, derive the behaviour
// state and clamp limits, then dispatch to the weapon or non-weapon spawn init.
// Finally applies any per-column stat/offset adjustments from the slot record.
void prop_object_init(void)
{
    unsigned long long ctx = FUN_803826f0();
    int self = (int)(ctx >> 0x20);
    int *desc = (int *)ctx;

    int *rec = (int *)item_pool_a_alloc();
    DAT_80611cb8 = DAT_80611cb8 + 1;
    memset_wrapper((int)rec, 0, 0x1dc);
    rec[0] = self;
    *(int **)(self + 0x7c) = rec;
    rec[0x1b] = item_pool_b_alloc();
    memset_wrapper(rec[0x1b], 0, 0x130);

    *(unsigned char *)(self + 0x32) = 2;
    *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) | 0x10;
    *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) & 0xffff3fff;
    *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) | 0x4000;
    render_instance_set_material((double)FLOAT_80618910, *(int **)(self + 4), 1);

    rec[1] = desc[0];
    rec[2] = desc[4];
    rec[5] = desc[0x15];
    rec[3] = desc[7];
    rec[0x70] = desc[9];
    prop_init_state_from_flags((int)rec);

    rec[0x30] = desc[0x1b];
    rec[6] = desc[0x1a];
    rec[0x16] = (int)FLOAT_80618910;
    rec[0x17] = (int)((float)desc[0x12] *
                      (FLOAT_80618960 /
                       (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900)));
    rec[0x18] = (int)FLOAT_80618910;
    rec[0xd] = desc[0xc];
    rec[0xe] = desc[0xd];
    rec[0xf] = desc[0xe];
    rec[0x10] = desc[0xf];
    rec[0x11] = (int)FLOAT_80618944;
    rec[0x12] = (int)FLOAT_8061890c;
    // Order min/max pairs.
    {
        float a = (float)rec[0xd];
        if ((float)rec[0xe] < a) { rec[0xd] = rec[0xe]; rec[0xe] = (int)a; }
        a = (float)rec[0xf];
        if ((float)rec[0x10] < a) { rec[0xf] = rec[0x10]; rec[0x10] = (int)a; }
    }
    rec[0x33] = desc[0x13];
    rec[0x32] = desc[0x14];
    rec[0x2d] = desc[0x1c];
    rec[0x34] = 0;
    rec[0x36] = 0;
    rec[0x1a] = 0;
    rec[0x71] = (int)FLOAT_80618910;
    rec[0x72] = (int)FLOAT_80618910;
    *(int *)(self + 0x1a4) = 0;
    *(int *)(rec[0x1b] + 0xe8) = 0;
    *(int *)(rec[0x1b] + 0xf0) = 1000;
    *(int *)(rec[0x1b] + 0xf4) = 1000;
    rec[0x1c] = 0;
    memset_wrapper((int)(rec + 0x66), 0, 0x18);
    rec[0x2e] = (int)FLOAT_80618910;
    rec[0x6c] = (int)FLOAT_80618910;
    rec[0x65] = (int)FLOAT_80618910;
    rec[0x2f] = desc[8];
    rec[0x35] = 0;
    rec[0x6e] = desc[0x17];
    rec[0x6f] = desc[0x18];
    rec[7] = (int)(FLOAT_80618928 * ((float)rec[0xe] - (float)rec[0xd]) + (float)rec[0xd]);
    rec[9] = (int)(FLOAT_80618928 * ((float)rec[0x10] - (float)rec[0xf]) + (float)rec[0xf]);

    if ((rec[2] & 0x40) == 0) {
        prop_spawn_place();
    } else {
        weapon_on_spawn(self, (int)desc);
    }

    rec[0x1f] = rec[7];
    rec[0x20] = rec[9];
    rec[0x21] = rec[0xb];
    rec[0x22] = rec[0x16];
    rec[0x23] = rec[0x17];
    rec[0x24] = rec[0x18];
    rec[0x25] = rec[0xd];
    rec[0x26] = rec[0xf];
    rec[0x27] = rec[0x11];
    rec[0x28] = rec[0xe];
    rec[0x29] = rec[0x10];
    rec[0x2a] = rec[0x12];

    int col = (int)*(short *)((int)desc + 0x84);   // desc+0x21 short
    if (((col != -2) && (rec[0x1b] != 0)) &&
        (*(int *)(rec[0x1b] + 0x30) >= 0) && (*(int *)(rec[0x1b] + 0x30) < 0x42)) {
        short *pAdj = (short *)0;
        int slot = DAT_80610ba0 + *(int *)(rec[0x1b] + 0x30) * 0x29c;
        int idxA = *(int *)(slot + 0x14);
        int idxB = *(int *)(slot + 0x1c);
        if (col == -1) {
            pAdj = (short *)(DAT_80610ba0 + *(int *)(rec[0x1b] + 0x30) * 0x29c + 0x24);
        } else if (col < 8) {
            pAdj = (short *)(col * 0xc + 0x803f3e20 + (unsigned int)(unsigned char)DAT_805015bf * 4);
        }
        if ((0 < idxA) && (idxA < 0x2c) && (pAdj != (short *)0)) {
            rec[idxA + 0x39] = rec[idxA + 0x39] + (int)pAdj[0];
        }
        if ((idxB != idxA) && (0 < idxB) && (idxB < 0x2c) && (pAdj != (short *)0)) {
            rec[idxB + 0x39] = rec[idxB + 0x39] + (int)pAdj[1];
        }
    }
    FUN_8038273c();
}

// 0x80240bf4 | prop_spawn_place
// Non-weapon prop spawn placement. Positions the prop at its root anchor,
// derives ground height, seeds orientation from the clamp limits, installs the
// per-type update/render callback (prop_explode_tick / prop_death_burst_tick /
// type-9 dual registration), and runs the prop's on-spawn callback (def+0x2c).
void prop_spawn_place(void)
{
    unsigned long long ctx = __save_gpr_28();
    int self = (int)(ctx >> 0x20);
    int desc = (int)ctx;
    int rec = *(int *)(self + 0x7c);
    int *def = (int *)prop_def_by_index(*(int *)(rec + 0xc));

    float pos[3];       // afStack_78
    float muzzle[3];    // local_6c/68/64
    float anchor[3];    // local_60/5c/58
    float m[21];        // afStack_54

    *(int *)(rec + 0x1d8) = desc;
    *(int *)(rec + 0x68) = *(int *)(DAT_80611d78 + 0x14);
    *(int *)(rec + 0xc4) = *(int *)(desc + 0x28);
    *(int *)(rec + 100) = *(int *)(desc + 0x2c);
    if ((DAT_805015bd == '\x03') && (DAT_80501572 < '\x01')) {
        *(int *)(rec + 0x1b4) = 0;
    } else {
        *(int *)(rec + 0x1b4) = *(int *)(desc + 0x58);
    }

    if (-1 < def[6]) {
        mtx4_set_rotation_zyx((double)((FLOAT_80618908 * *(float *)(self + 0xc0)) / FLOAT_8061890c),
                              (double)((FLOAT_80618908 * *(float *)(self + 0x70)) / FLOAT_8061890c),
                              (double)((FLOAT_80618908 * *(float *)(self + 200)) / FLOAT_8061890c), m);
        int *node = *(int **)(*(int *)(self + 4) + 4);
        int hi = def[6] >> 0x10;
        int lo = (def[6] & 0xffff) * 0x18;
        int base0 = *node;
        anchor[0] = *(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo);
        anchor[1] = *(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo + 4);
        anchor[2] = *(float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo + 8);
        mtx4_transform_vector(m, anchor);
        *(float *)(self + 100) = *(float *)(self + 100) - anchor[0];
        *(float *)(self + 0x68) = *(float *)(self + 0x68) - anchor[1];
        *(float *)(self + 0x6c) = *(float *)(self + 0x6c) - anchor[2];
    }

    if ((*(unsigned int *)(rec + 8) & 4) == 0) {
        prop_read_aim_anchor(self, pos);
        *(int *)(rec + 0xb0) = floor_height_from_position((double)FLOAT_8061893c, (double)FLOAT_8061893c);
    } else {
        prop_compute_muzzle_origin(self, muzzle);
        *(int *)(rec + 0xb0) = floor_height_from_position((double)FLOAT_8061893c, (double)FLOAT_8061893c);
        if (*(int *)(rec + 0xb0) == 0) {
            muzzle[0] = *(float *)(self + 100) + *(float *)(self + 0x1a8);
            muzzle[1] = *(float *)(self + 0x68) + *(float *)(self + 0x1ac);
            muzzle[2] = *(float *)(self + 0x6c) + *(float *)(self + 0x1b0);
            *(int *)(rec + 0xb0) = floor_height_from_position((double)FLOAT_8061893c, (double)FLOAT_8061893c);
        }
        int owner = *(int *)(rec + 0x6c);
        if (owner != 0) {
            if (*(int *)(desc + 0x14) == -1) {
                *(int *)(owner + 0x30) = def[0];
            } else {
                *(int *)(owner + 0x30) = *(int *)(desc + 0x14);
            }
        }
    }

    float ry = *(float *)(desc + 0x40);
    if ((*(float *)(desc + 0x30) <= ry) && (ry <= *(float *)(desc + 0x34))) {
        *(float *)(rec + 0x1c) = ry;
    }
    float rp = *(float *)(desc + 0x44);
    if ((*(float *)(desc + 0x38) <= rp) && (rp <= *(float *)(desc + 0x3c))) {
        *(float *)(rec + 0x24) = rp;
    }
    *(int *)(rec + 0x20) = *(int *)(rec + 0x1c);
    *(int *)(rec + 0x28) = *(int *)(rec + 0x24);
    *(int *)(*(int *)(self + 4) + 0x30) = 0;
    *(unsigned int *)(*(int *)(self + 4) + 0x30) =
        *(unsigned int *)(*(int *)(self + 4) + 0x30) | def[0x23] | def[5] | def[0x21];

    if (*(float *)(desc + 0x88) < FLOAT_80618910) {
        *(int *)(self + 0x1d8) = def[DAT_805015bf + 0x25];
    } else {
        *(float *)(self + 0x1d8) = *(float *)(desc + 0x88);
    }
    *(int *)(self + 0x1cc) = *(int *)(self + 0x1d8);
    *(int *)(self + 0x1dc) = def[0x28];

    switch (*(int *)(rec + 0xc)) {
    case 0: case 1: case 3: case 4: case 5:
    case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x10:
    case 0x12: case 0x14: case 0x15: case 0x16:
    case 0x18: case 0x19: case 0x1b: case 0x1c:
        entity_special_list_add(self);
        entity_set_render_type(self, (int)&prop_explode_tick);
        break;
    case 6: case 7: case 10: case 0x11: case 0x13: case 0x17: case 0x1a: case 0x1d:
        entity_special_list_add(self);
        entity_set_render_type(self, (int)&prop_death_burst_tick);
        break;
    case 8:
        entity_set_render_type(self, (int)&prop_death_burst_tick);
        break;
    case 9:
        entity_set_render_type(self, 0);
        entity_register_for_update(self, 10, 0x800c69e4);
        entity_register_for_update(self, 3, 0x801f81e8);
        break;
    default:
        break;
    }

    if ((void *)def[0x2c] != 0) {
        ((void (*)(int))def[0x2c])(self);
    }
}
