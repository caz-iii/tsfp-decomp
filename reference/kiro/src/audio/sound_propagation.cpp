// sound_propagation.cpp
// Sound-zone propagation / pickup-visibility helpers for the audio system.
// This cluster resolves which render/audio zone a sound source occupies, reads
// the game's zone lookup tables, and (per frame) sweeps active sound-pad slots
// to spread a source's contribution into neighbouring zones with a distance
// falloff. It sits alongside sound_occlusion.cpp: occlusion decides whether a
// source is blocked, this file decides which zones it lights up and how it
// decays across the zone graph.
//
// Key data:
//   - the (row x col) zone lookup table at base -0x7fb88428 (0x80477bd8),
//     addressed as [col*0xc4 + row*0x1c] dwords (sound_zone_table_lookup);
//   - the short class table at DAT_80477bd4 (stride 0x31 dwords / 7 shorts) that
//     maps a (row,col) pair to a zone-kind constant (sound_classify_zone_kind);
//   - the sound-pad slot buffer (soundpad_get_buffer / soundpad_get_active_count)
//     whose 0x14-byte entries hold two zone ids at +4/+8 and a flag word at +0.
//
// Address range: 0x80353250 - 0x80353adc  (batch_1007)
//
// Self-contained translation unit: raw FUN_/DAT_/FLOAT_ symbols are declared
// file-locally with concrete types matching each call site; named cross-module
// helpers (soundpad_*, math_poly_trig, objective_*) resolve at link time.

typedef unsigned int       undefined4;
typedef unsigned int       uint;
typedef unsigned short     ushort;
typedef unsigned long long undefined8;
typedef unsigned long long ulonglong;
typedef long long          longlong;

static inline unsigned int countLeadingZeros(unsigned int x) {
    unsigned int n = 0;
    if (x == 0) return 32;
    while ((x & 0x80000000u) == 0) { n++; x <<= 1; }
    return n;
}

extern "C" {

// ---- named cross-module helpers (concrete signatures) ----
int    soundpad_get_buffer(void);
int    soundpad_get_active_count(void);
int    soundpad_resolve_entity_to_zone(int entityId);
double math_poly_trig(double x);
void   objective_update_prop_zone_mask(void);

// ---- raw same-cluster callees ----
undefined8 FUN_803826e8(void);            // enter sound-source propagation ctx -> (hi=record, lo=out vec)
void       FUN_80382734(void);            // leave sound-source propagation ctx
int        FUN_8035332c(int row, int col);// zone table lookup (dup of local one)

// ---- raw globals ----
extern int   DAT_80612d68;   // active objective/pickup record ptr
extern int   DAT_80612614;   // current game/objective mode (0x66 = pickup mode)
extern int   DAT_805d59a0;   // current player/zone row index
extern int   DAT_80611b8c;   // zone descriptor array base (stride 0x20)
extern int   DAT_80611b94;   // sound-pad zone slot array base (stride 0x20)
extern int   DAT_80611c0c;   // sound-pad zone slot count
extern int   DAT_80611b04;   // zone-table pointer array base (stride 4)
extern int   DAT_80611d74;   // zone-context base (+0xdec = current zone index)
extern char  DAT_805015bd;   // co-op / game mode selector
extern char  DAT_80477bca;   // per-class anim-state flags table (stride 0x62)
extern int   DAT_80477bd4;   // (row,col) zone-kind short table base

extern float FLOAT_8061ad00;
extern float FLOAT_8061ad24;
extern float FLOAT_8061ad50, FLOAT_8061ad54, FLOAT_8061ad58, FLOAT_8061ad5c;
extern float FLOAT_8061ad60, FLOAT_8061ad64, FLOAT_8061ad68, FLOAT_8061ad6c;
extern float FLOAT_8061ad70, FLOAT_8061ad74, FLOAT_8061ad78;

// ============================================================================
// 0x8035330c  sound_zone_table_lookup
// Read a dword from the (row,col) zone lookup table (base 0x80477bd8, addressed
// as col*0xc4 + row*0x1c dwords). FUN_8035332c is a byte-identical duplicate.
// ============================================================================
undefined4 sound_zone_table_lookup(int row, int col)
{
    return *(undefined4 *)(col * 0xc4 + row * 0x1c + -0x7fb88428);
}

// 0x8035332c  duplicate of sound_zone_table_lookup (kept as its own symbol so
// the FUN_8035332c call site in sound_apply_pickup_zone_mask resolves).
undefined4 sound_zone_table_lookup_dup(int row, int col)
{
    return *(undefined4 *)(col * 0xc4 + row * 0x1c + -0x7fb88428);
}

// ============================================================================
// 0x8035334c  sound_classify_zone_kind
// Map a (row,col) pair through the short zone-kind table at DAT_80477bd4 to a
// small kind constant. All out-of-band values collapse to 5; only exactly 0x889
// yields 3. (The decompiled branches all return 5 except that one case.)
// ============================================================================
undefined4 sound_classify_zone_kind(int row, int col)
{
    int kind = (&DAT_80477bd4)[col * 7 + row * 0x31];
    if (kind == 0x889) {
        return 3;
    }
    return 5;
}

// ============================================================================
// 0x80353250  sound_apply_pickup_zone_mask
// When in pickup mode (0x66) with an active pickup record, resolve the pickup's
// zone kind and OR the matching zone bit into the render node's zone mask
// (node +0x30). Otherwise defer to the generic prop zone-mask update.
// ============================================================================
void sound_apply_pickup_zone_mask(int param_1)
{
    int kind;
    int node;
    int zoneSel;

    if ((DAT_80612d68 == 0) || (DAT_80612614 != 0x66)) {
        objective_update_prop_zone_mask();
    } else {
        zoneSel = -1;
        kind = FUN_8035332c(DAT_805d59a0, *(int *)(DAT_80612d68 + 0x88));
        node = *(int *)(param_1 + 4);
        if (kind == 0x1c) {
            zoneSel = 7;
        } else if ((kind < 0x1c) && (0x1a < kind)) {
            zoneSel = 7;
        }
        *(uint *)(node + 0x30) =
            *(uint *)(node + 0x30) |
            (*(int **)(node + 4))[**(int **)(node + 4) * -0x30 + zoneSel * 0x30 + 0x14];
    }
}

// ============================================================================
// 0x803534f0  sound_project_zone_delta
// Project a direction vector (param_1) onto the delta between the two zone
// positions referenced by a sound-pad slot (param_3 +4/+8 zone ids into the
// DAT_80611b8c descriptor array). If the two ids match, both ends collapse to
// the second zone. The squared distance is smoothed with a Newton reciprocal-
// sqrt refinement before the projection. Returns the (double-promoted) dot.
// ============================================================================
double sound_project_zone_delta(float *param_1, int *param_2, int param_3)
{
    float  dx, dy, dz;
    int   *zoneA, *zoneB;
    float  d2, inv;
    int    srcId, zoneId;
    int   *zoneRef;
    int   *zoneEnd;

    zoneEnd = (int *)(DAT_80611b8c + *(int *)(param_3 + 4) * 0x20);
    srcId   = *param_2;
    zoneId  = *zoneEnd;
    zoneRef = (int *)(DAT_80611b8c + *(int *)(param_3 + 8) * 0x20);
    zoneA   = zoneEnd;
    if (srcId == zoneId) { zoneA = zoneRef; }
    zoneB = zoneEnd;
    if (srcId == zoneId) { zoneB = zoneRef; }
    if (srcId == zoneId) { zoneEnd = zoneRef; }

    dy = (float)param_2[4] - (float)zoneB[4];
    dx = (float)param_2[3] - (float)zoneA[3];
    dz = (float)param_2[5] - (float)zoneEnd[5];
    d2 = dz * dz + dx * dx + dy * dy;
    if (FLOAT_8061ad54 < d2) {
        inv = 1.0f / (float)__builtin_sqrtf(d2);
        inv = inv * -(FLOAT_8061ad58 * d2 * inv * inv - FLOAT_8061ad50);
        d2  = FLOAT_8061ad24 * inv * -(FLOAT_8061ad58 * d2 * inv * inv - FLOAT_8061ad50);
        dx  = dx * d2;
        dy  = dy * d2;
        dz  = dz * d2;
    }
    return (double)(param_1[2] * dz + *param_1 * dx + param_1[1] * dy);
}

// ============================================================================
// 0x803535e4  sound_propagate_source_soft
// Per-frame "soft" propagation pass for one sound source. Enters the source
// context (FUN_803826e8 -> record hi / out-vec lo), advances the source
// position along a poly-trig-derived direction, records it into the matching
// sound-pad zone slot, then sweeps active pads and clears the +0x10000 bit on
// any pad whose projected distance falls below FLOAT_8061ad6c.
// ============================================================================
void sound_propagate_source_soft(void)
{
    float  step;
    int   *rec;
    int    padBuf;
    int    slot;
    int   *outVec;
    uint  *pad;
    int    hit;
    int    remaining;
    double speed;
    double angle;
    double dist;
    undefined8 ctx;
    float  dir_x, dir_y, dir_z;

    ctx    = FUN_803826e8();
    rec    = (int *)((ulonglong)ctx >> 0x20);
    outVec = (int *)ctx;
    hit    = 0;
    speed  = 0.0;                 // f1 result of enter-ctx (source speed)
    padBuf = soundpad_get_buffer();

    dist   = (double)((float)((double)FLOAT_8061ad5c * speed) / FLOAT_8061ad60);
    angle  = math_poly_trig((double)(float)(dist - (double)FLOAT_8061ad64));
    dir_x  = (float)angle;
    dir_y  = FLOAT_8061ad00;
    angle  = math_poly_trig(dist);
    dir_z  = (float)angle;

    slot      = 0;
    remaining = DAT_80611c0c;
    if (0 < DAT_80611c0c) {
        do {
            if (*rec == (int)*(short *)(DAT_80611b94 + slot + 8)) {
                hit = DAT_80611b94 + slot;
                break;
            }
            slot      = slot + 0x20;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    if (hit != 0) {
        *(int *)(hit + 0xc)  = rec[3];
        *(int *)(hit + 0x10) = rec[4];
        *(int *)(hit + 0x14) = rec[5];
        *(undefined4 *)(hit + 0x1c) = 1;
        *(float *)(hit + 0x18) = (float)speed;
        rec[1] = rec[1] | 0x10000;
    }
    rec[6]    = (int)(float)speed;
    step      = FLOAT_8061ad68;
    outVec[0] = rec[3];
    outVec[1] = rec[4];
    outVec[2] = rec[5];
    rec[3]    = (int)(step * dir_x + (float)rec[3]);
    rec[4]    = (int)(step * dir_y + (float)rec[4]);
    rec[5]    = (int)(step * dir_z + (float)rec[5]);

    slot = 0;
    for (int i = 0; i < soundpad_get_active_count(); i = i + 1) {
        pad = (uint *)(padBuf + slot);
        if ((((pad[1] != 0xffffffff) && (pad[2] != 0xffffffff)) &&
             ((*rec == *(int *)(DAT_80611b8c + pad[1] * 0x20) ||
               (*rec == *(int *)(DAT_80611b8c + pad[2] * 0x20))))) &&
            (speed = sound_project_zone_delta(&dir_x, rec, (int)pad),
             speed < (double)FLOAT_8061ad6c)) {
            *pad = *pad & 0xfffeffff;
        }
        slot = slot + 0x14;
    }
    FUN_80382734();
}

// ============================================================================
// 0x803537cc  sound_propagate_source_hard
// Per-frame "hard" propagation pass. Like the soft pass but sets the +0x80000
// bit on the record, snaps the out position every 0x5a frames, uses the harder
// step/threshold constants (FLOAT_8061ad70/74/78), and on a below-threshold pad
// swaps the two zone ids so the source's own zone is first, then toggles the
// pad's bit 1 / clears the +0x10000 bit.
// ============================================================================
void sound_propagate_source_hard(void)
{
    float  step;
    int   *rec;
    int    padBuf;
    int    slot;
    float *outVec;
    uint   otherId;
    int    frame;
    uint  *pad;
    int    i;
    double dist;
    double poly;
    double angle;
    double speed;
    undefined8 ctx;
    float  dir_x, dir_y, dir_z;

    ctx    = FUN_803826e8();
    rec    = (int *)((ulonglong)ctx >> 0x20);
    outVec = (float *)ctx;
    speed  = 0.0;                 // f1 result of enter-ctx (source speed)
    padBuf = soundpad_get_buffer();

    rec[1] = rec[1] | 0x80000;
    dist   = (double)((float)((double)FLOAT_8061ad5c * speed) / FLOAT_8061ad60);
    poly   = math_poly_trig((double)(float)(dist - (double)FLOAT_8061ad64));
    dir_x  = (float)poly;
    dir_y  = FLOAT_8061ad00;
    poly   = math_poly_trig(dist);
    frame  = (int)speed;
    dir_z  = (float)poly;

    outVec[0] = (float)rec[3];
    outVec[1] = (float)rec[4];
    outVec[2] = (float)rec[5];
    step = FLOAT_8061ad70;
    if (frame == (frame / 0x5a) * 0x5a) {
        outVec[0] = FLOAT_8061ad70 * dir_x + outVec[0];
        outVec[1] = step * dir_y + outVec[1];
        outVec[2] = step * dir_z + outVec[2];
    }
    rec[6] = (int)(float)speed;
    step   = FLOAT_8061ad74;
    rec[3] = (int)(FLOAT_8061ad74 * dir_x + (float)rec[3]);
    rec[4] = (int)(step * dir_y + (float)rec[4]);
    rec[5] = (int)(step * dir_z + (float)rec[5]);

    slot = 0;
    for (i = 0; i < soundpad_get_active_count(); i = i + 1) {
        pad = (uint *)(padBuf + slot);
        if ((((pad[1] != 0xffffffff) && (pad[2] != 0xffffffff)) &&
             ((*rec == *(int *)(DAT_80611b8c + pad[1] * 0x20) ||
               (*rec == *(int *)(DAT_80611b8c + pad[2] * 0x20))))) &&
            (speed = sound_project_zone_delta(&dir_x, rec, (int)pad),
             speed < (double)FLOAT_8061ad78)) {
            otherId = pad[1];
            if (*(int *)(DAT_80611b8c + otherId * 0x20) != *rec) {
                pad[1] = pad[2];
                pad[2] = otherId;
            }
            otherId = *pad;
            if ((otherId & 2) == 0) {
                *pad = otherId | 2;
            } else {
                *pad = otherId & 0xfffeffff;
            }
        }
        slot = slot + 0x14;
    }
    FUN_80382734();
}

// ============================================================================
// 0x803539d8  sound_get_zone_position
// Resolve the sound entity id at (param_1 +8) to its zone descriptor and copy
// the zone's world position (+0xc/+0x10/+0x14) into param_2[0..2].
// ============================================================================
void sound_get_zone_position(int param_1, undefined4 *param_2)
{
    int zone;

    zone = soundpad_resolve_entity_to_zone((int)*(short *)(param_1 + 8));
    param_2[0] = *(undefined4 *)(zone + 0xc);
    param_2[1] = *(undefined4 *)(zone + 0x10);
    param_2[2] = *(undefined4 *)(zone + 0x14);
}

// ============================================================================
// 0x80353a20  sound_update_pickup_flags
// Update a pickup/sound record's activation flags from the current co-op mode
// (DAT_805015bd) and the entity's anim-state class flags. In mode 3 with an
// armed entity it arms the record (fields +7 / +0x11 / OR 0x100 into +0xd);
// otherwise it clears field 0 when the entity's class flag byte equals 0x1f.
// ============================================================================
void sound_update_pickup_flags(undefined4 *param_1, int param_2)
{
    float armVal;
    int   cls;

    armVal = FLOAT_8061ad00;
    cls    = (int)*(short *)(param_2 + 0xe);
    if (*(short *)(param_2 + 0x12) < 0) {
        if ((&DAT_80477bca)[cls * 0x62] != 0x1f) { return; }
        param_1[0] = 0;
        return;
    }
    if (DAT_805015bd == 10) {
        if (*(short *)(param_2 + 10) == 0) { return; }
        if ((&DAT_80477bca)[cls * 0x62] != 0x1f) { return; }
        param_1[0] = 0;
        return;
    }
    if (9 < DAT_805015bd) { return; }
    if (DAT_805015bd != 3) { return; }
    if (*(short *)(param_2 + 10) != 0) {
        param_1[7]    = (undefined4)(*(short *)(param_2 + 0x12) + 1);
        param_1[0x11] = *(undefined4 *)&armVal;
        param_1[0xd]  = param_1[0xd] | 0x100;
        return;
    }
    if ((&DAT_80477bca)[cls * 0x62] != 0x1f) { return; }
    param_1[0] = 0;
}

// ============================================================================
// 0x80353adc  sound_is_zone_slot_full
// True (1) iff the current zone's slot-fill field (+0x8c of the zone-table entry
// selected by DAT_80611d74 +0xdec) equals 0x16, computed via CLZ>>5.
// ============================================================================
uint sound_is_zone_slot_full(void)
{
    uint clz;

    clz = countLeadingZeros(0x16 - *(int *)(*(int *)(DAT_80611b04 +
                                    *(int *)(DAT_80611d74 + 0xdec) * 4) + 0x8c));
    return clz >> 5;
}

} // extern "C"
