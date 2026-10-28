// worm_ghz_arc.cpp
// GHZ reactor 'A11'/'GA11' four-barrel electric-arc weapon rig (address range
// 0x8021269c - 0x802163ff, batch_658). subsystem:game, worm-effect module.
//
// This is the arc/lightning launcher prop that sits on the GHZ reactor level. It
// owns a parent entity plus four spawned sub-emitter entities (class 0x558) that
// are held at the parent's four muzzle bones until "released" as guided
// arc-projectiles. The rig's shared state lives in the parent's 0x7c control
// block: a 4-slot sub-entity handle table (+0..+0xc), an active-bitmask (+0xca0),
// beam start/end direction vectors (+0xca4/+0xcb0), and a 0x54-byte per-frame
// animation scratch (+0xcb4..). A global concurrency budget DAT_8061283c caps how
// many arcs can be live at once (< 4).
//
// Functions:
//   ghz_arc_muzzle_point     (0x8021269c) resolve a muzzle bone (index by state
//                            0..3 -> bone 0xa..0xd) to a world position + dir.
//   ghz_arc_rig_init         (0x802127bc) spawn the 4 class-0x558 sub-emitters at
//                            the muzzles, wire them into the control block, and
//                            seed the 0x54-byte anim scratch with tuning consts.
//   ghz_arc_beam_render      (0x80212948) GX render: a jagged sine-perturbed
//                            lightning beam (11 segments via
//                            ekun_debris_trail_quad_render) + a textured billboard
//                            strip (sprite 0x325).
//   ghz_arc_render_parent    (0x802134a8) render wrapper for the parent block.
//   ghz_arc_rig_teardown     (0x802134d4) validate + destroy the 4 sub-emitters
//                            (particle handle + weather slot + entity), decrement
//                            the live-arc budget.
//   ghz_arc_rig_tick         (0x802135d8) per-frame master: sine-LUT position/aim
//                            wobble, beam dir update, and (budget-gated, for the
//                            'A11' variant) release one sub-emitter as an arc.
//   ghz_arc_ballistic_time   (0x80213b60) quadratic solve for projectile
//                            time-to-target under gravity.
//   ghz_arc_projectile_tick  (0x80213ccc) the released sub-emitter's per-frame
//                            state machine (states 0..6): launch, seek target
//                            table (DAT_8045a0e4/f0), steer, fire nested
//                            projectiles (class 0x1e/0x557), and detonate (0x26).
//   ghz_arc_beam_aim_tick    (0x802161b4) rotate the beam start/end direction
//                            vectors via the sine LUT into local space.
//   ghz_arc_render_child     (0x802163d8) render wrapper for a released sub-emitter.
//
// Non-matching: 4CC ids ('A11'/'GA11' 0x...313141); the 0x43300000 int<->double
// bias math and the (uint^0x80000000)/2^32 signed-rng->float pattern are written
// as plain float math; the sine LUT (DAT_80583228, &0x7ff quarter-phase, +0x200
// cos offset) is indexed directly; PPC FP-spill frame open/close helpers are
// opaque. Wide FP-ABI ROM helpers route through file-local typed aliases. The
// huge FP-register spill locals Ghidra emits are pure callee-save spills and are
// dropped.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local) ---
extern int   DAT_806109c0;      // per-frame time unit
extern float DAT_806108ec;      // frame-time scale
extern int   DAT_806108e4;      // gameplay-active gate
extern int   DAT_80611c24;      // player/entity pool base (0x2e8 stride, handle>>0x16)
extern int   DAT_80611d54;      // primary player/view block base
extern int   DAT_80611d74;      // local/view actor block base
extern int   DAT_80611d78;      // active character block base
extern int   DAT_80611928;      // per-frame secondary time unit (int)
extern int   DAT_80612250;      // render override flag A
extern int   DAT_80612258;      // render override flag B
extern int   DAT_80612830;      // active target-owner block (turret variant)
extern int   DAT_8061283c;      // live-arc concurrency budget counter (< 4)
extern int   DAT_80612838;      // arc-release enable gate
extern int   DAT_80612834;      // arc-release latch flag
extern int   DAT_8058e3cc;      // identity/transpose scratch matrix source
extern volatile float DAT_cc008000; // GX CP FIFO write port
extern int   DAT_80583228;      // 0x800-entry sine LUT (&0x7ff quarter-phase)

// arc target/aim tables (0x8045a0e4.. : 0x18 stride rows {pos.x,y,z, dir.x,y,z})
extern int   DAT_8045a0e4;      // target row: pos X
extern int   DAT_8045a0e8;      // target row: pos Z (note ROM layout y/z swap)
extern int   DAT_8045a0ec;      // target row: pos (3rd)
extern int   DAT_8045a0f0;      // target row: dir X
extern int   DAT_8045a0f4;      // target row: dir Y
extern int   DAT_8045a0f8;      // target row: dir Z
extern int   DAT_8045a144;      // target-index remap table (8-byte stride)

// seed vectors (0x803dff04..0x803dff48)
extern int   DAT_803dff04; extern int DAT_803dff08; extern int DAT_803dff0c;
extern int   DAT_803dff10; extern int DAT_803dff14; extern int DAT_803dff18;
extern int   DAT_803dff1c; extern int DAT_803dff20; extern int DAT_803dff24;
extern int   DAT_803dff28; extern int DAT_803dff2c; extern int DAT_803dff30;
extern int   DAT_803dff34; extern int DAT_803dff38; extern int DAT_803dff3c;
extern int   DAT_803dff40; extern int DAT_803dff44; extern int DAT_803dff48;

// bias doubles + tuning float pool 0x806180d0..0x806181b8 (shared 0x8061xxxx pool)
extern double DOUBLE_806180d0;  // 2^52 unsigned int->double bias
extern double DOUBLE_806180d8;  // 2^52 unsigned int->double bias (rng path)
extern double DOUBLE_80618128;  // wrap/fmod constant
extern float FLOAT_80618090; extern float FLOAT_80618094; extern float FLOAT_80618098;
extern float FLOAT_8061809c; extern float FLOAT_806180a0; extern float FLOAT_806180a4;
extern float FLOAT_806180ac; extern float FLOAT_806180b0; extern float FLOAT_806180b4;
extern float FLOAT_806180b8; extern float FLOAT_806180bc; extern float FLOAT_806180c0;
extern float FLOAT_806180c8; extern float FLOAT_806180e4; extern float FLOAT_806180e8;
extern float FLOAT_806180ec; extern float FLOAT_806180f0; extern float FLOAT_806180f4;
extern float FLOAT_806180f8; extern float FLOAT_806180fc; extern float FLOAT_80618100;
extern float FLOAT_80618104; extern float FLOAT_80618108; extern float FLOAT_8061810c;
extern float FLOAT_80618110; extern float FLOAT_80618114; extern float FLOAT_80618118;
extern float FLOAT_8061811c; extern float FLOAT_80618120; extern float FLOAT_80618128f;
extern float FLOAT_80618130; extern float FLOAT_80618134; extern float FLOAT_80618138;
extern float FLOAT_8061813c; extern float FLOAT_80618140; extern float FLOAT_80618144;
extern float FLOAT_80618148; extern float FLOAT_8061814c; extern float FLOAT_80618150;
extern float FLOAT_80618154; extern float FLOAT_80618158; extern float FLOAT_8061815c;
extern float FLOAT_80618160; extern float FLOAT_80618164; extern float FLOAT_80618168;
extern float FLOAT_8061816c; extern float FLOAT_80618170; extern float FLOAT_80618174;
extern float FLOAT_80618178; extern float FLOAT_8061817c; extern float FLOAT_80618180;
extern float FLOAT_80618184; extern float FLOAT_80618188; extern float FLOAT_8061818c;
extern float FLOAT_80618190; extern float FLOAT_80618194; extern float FLOAT_80618198;
extern float FLOAT_8061819c; extern float FLOAT_806181a0; extern float FLOAT_806181a4;
extern float FLOAT_806181a8; extern float FLOAT_806181ac; extern float FLOAT_806181b0;
extern float FLOAT_806181b4; extern float FLOAT_806181b8;

// --- helpers reached by raw address (file-local externs; not in generated_decls) ---
extern int  FUN_803826e8(void);        // FP spill frame open (view/entity getter)
extern int  FUN_803826e4(void);        // FP spill frame open (returns pair)
extern void FUN_80382730(void);        // FP spill frame close
extern void FUN_80382734(void);        // FP spill frame close
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern double math_atan2(double y, double x);      // not in generated headers
extern double FUN_80390ae4(double a, double b);    // fmod/wrap-into-range helper
extern void FUN_8023c990(void);                    // post-spawn fixup (void)

// rng_counter_tick is declared void in generated_decls but the ROM returns the
// 32-bit LCG counter in r3.
typedef unsigned int (*GhzRngFn)(void);
static const GhzRngFn __ghz_rng = (GhzRngFn)&rng_counter_tick;

// gx_set_color_update_mode is declared (int) but this call site takes no arg.
typedef void (*GhzGxColorUpdFn)(void);
static const GhzGxColorUpdFn __ghz_gx_color_update = (GhzGxColorUpdFn)&gx_set_color_update_mode;

// entity_spawn_by_type is declared 4-arg/void in generated_decls; the ROM shape
// is a wide FP-ABI call returning the spawned entity in r3.
typedef int (*GhzSpawnTypeFn)(unsigned long long f1, double f2, double f3, unsigned long long f4,
                              unsigned long long f5, unsigned long long f6, unsigned long long f7,
                              unsigned long long f8, int classId, float* pos, float* fields,
                              int a, int b);
static const GhzSpawnTypeFn __ghz_spawn_by_type = (GhzSpawnTypeFn)&entity_spawn_by_type;

// player_ai_projectile_spawn_simple: ROM returns the spawned entity in r3.
typedef int (*GhzProjSimpleFn)(double accuracy, double f2, int projType, float* pos, int* dir,
                               int owner, int* soundDef, int weaponMode, int a, int ownerId,
                               int b, int c);
static const GhzProjSimpleFn __ghz_proj_simple = (GhzProjSimpleFn)&player_ai_projectile_spawn_simple;

// forward decls (registered by address / mutually referenced).
void ghz_arc_muzzle_point(int a, int state, float* posOut, float* dirOut);
void ghz_arc_beam_render(int a, int startBlock, float* dirBlock);
double ghz_arc_ballistic_time(int self, int target);
static void ghz_arc_projectile_seek(int* self, unsigned int* ctrl, float* mtxScratch);

// ---------------------------------------------------------------------------
// 0x8021269c  ghz_arc_muzzle_point
// Resolve a muzzle bone on the parent model (bone index chosen from the state
// param: 0->0xa, 1->0xb, 2->0xc, 3->0xd) into a world position (posOut) and a
// direction (dirOut). The entity ptr + state arrive via the FP-spill frame.
// ---------------------------------------------------------------------------
void ghz_arc_muzzle_point(int a, int state, float* posOut, float* dirOut)
{
    int entity;
    int boneIdx;
    int* nodes;
    float* bone;
    unsigned long long framePair;
    float mtx[22];
    (void)a; (void)state;

    framePair = (unsigned long long)FUN_803826e8();
    entity = (int)(framePair >> 0x20);
    state  = (int)framePair;
    boneIdx = 0;
    entity_build_local_matrix(mtx, (void*)entity, 1, 0);
    if (state == 2)          boneIdx = 0xc;
    else if (state < 2) { if (state == 0) boneIdx = 10; else if (-1 < state) boneIdx = 0xb; }
    else if (state < 4)      boneIdx = 0xd;

    nodes = *(int**)(*(int*)(entity + 4) + 4);
    bone = (float*)(nodes[*nodes * -0x30 + 0x1b] + boneIdx * 0x18);
    posOut[0] = bone[0];
    posOut[1] = bone[1];
    posOut[2] = bone[2];
    mtx4_transform_point(mtx, posOut);
    dirOut[0] = *(float*)(entity + 0xc0) + bone[3];
    dirOut[1] = *(float*)(entity + 0x70) + bone[4];
    dirOut[2] = *(float*)(entity + 200) + bone[5];
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802127bc  ghz_arc_rig_init
// Spawn the 4 sub-emitter entities (class 0x558) at the 4 muzzle points, wire
// them into the parent control block (0x324-stride sub-records + 4-slot handle
// table), and seed the 0x54-byte per-frame anim scratch (+0xcc4..) with tuning
// constants. The 0x41-tagged variant marks a per-record flag.
// ---------------------------------------------------------------------------
void ghz_arc_rig_init(unsigned long long param_1, double param_2, double param_3,
                      unsigned long long param_4, unsigned long long param_5,
                      unsigned long long param_6, unsigned long long param_7,
                      unsigned long long param_8)
{
    int entity;
    int ctrl;
    int sub;
    int i;
    float* scratch;
    unsigned long long framePair;
    float pos[3];
    float dir[3];
    (void)param_1;

    framePair = (unsigned long long)FUN_803826e8();
    entity = (int)(framePair >> 0x20);
    ctrl = *(int*)(entity + 0x7c);
    scratch = (float*)(ctrl + 0xcc4);
    *(unsigned short*)(ctrl + 0xca0) = 0xf;   // all 4 sub-emitters active
    *(unsigned short*)(ctrl + 0xca2) = 0;

    for (i = 0; i < 4; i = i + 1) {
        unsigned long long m = (unsigned long long)((unsigned int)(uintptr_t)(void*)0) | ((unsigned long long)(unsigned int)entity << 0x20) | (unsigned int)i;
        (void)m;
        ghz_arc_muzzle_point(entity, i, pos, dir);
        sub = __ghz_spawn_by_type((unsigned long long)(unsigned int)entity, param_2, param_3,
                                  param_4, param_5, param_6, param_7, param_8, 0x558, pos, dir, 0, 0);
        *(int*)(sub + 0x7c) = ctrl + i * 0x324 + 0x10;
        *(int*)(sub + 0x1a0) = 0;
        if (((unsigned int)framePair & 0xff) == 0x41) {
            *(unsigned char*)(ctrl + i * 0x324 + 0x16) = 6;
            *(unsigned short*)(ctrl + i * 0x324 + 0x1a) = 1;
        }
        *(int*)(ctrl + i * 4) = *(int*)(sub + 0x2c);
        *(int*)(ctrl + i * 0x324 + 0x10) = *(int*)(entity + 0x2c);
    }

    memset_wrapper((int)scratch, 0, 0x54);
    scratch[0] = FLOAT_806180e4;
    *(float*)(ctrl + 0xcc8) = FLOAT_806180e4;
    *(float*)(ctrl + 0xccc) = FLOAT_806180e4;
    *(float*)(ctrl + 0xcd0) = FLOAT_806180e4;
    *(float*)(ctrl + 0xcd4) = FLOAT_806180e4;
    *(float*)(ctrl + 0xcd8) = FLOAT_806180e4;
    *(float*)(ctrl + 0xcdc) = FLOAT_806180e4;
    *(float*)(ctrl + 0xce0) = FLOAT_806180e4;
    *(float*)(ctrl + 0xce4) = FLOAT_806180e4;
    *(float*)(ctrl + 0xce8) = FLOAT_80618098;
    *(float*)(ctrl + 0xcec) = FLOAT_806180e8;
    *(float*)(ctrl + 0xcf0) = FLOAT_806180b0;
    *(float*)(ctrl + 0xd14) = FLOAT_80618090;
    *(float*)(ctrl + 0xd08) = FLOAT_806180ec;
    *(float*)(ctrl + 0xcfc) = FLOAT_806180f0;
    *(float**)(entity + 0x254) = scratch;
    *(unsigned short*)(*(int*)(entity + 4) + 0x1ee) |= 0x10;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80212948  ghz_arc_beam_render
// GX render for the electric arc beam (subsystem:graphics). Builds a jagged,
// sine-LUT-perturbed lightning path between the beam endpoints as 11 additive
// textured quad segments (ekun_debris_trail_quad_render), then draws a 3-vertex
// textured billboard strip (sprite 0x325) for the beam core. dirBlock provides
// the per-axis jitter scale. Wraps a wide FP-spill frame (FUN_803826e4/80382730).
// ---------------------------------------------------------------------------
void ghz_arc_beam_render(int a, int startBlock, float* dirBlock)
{
    int entity;
    int texId;
    int i;
    int* nodes;
    float baseX, baseY, baseZ;
    float endX, endY, endZ;
    float mtx[17];
    double amp;
    double glow;
    unsigned long long framePair;
    unsigned int colBase;
    float* startPt;
    (void)a; (void)startBlock;

    framePair = (unsigned long long)FUN_803826e4();
    entity = (int)(framePair >> 0x20);
    startPt = (float*)(unsigned int)framePair;
    amp = (double)FLOAT_80618098;
    texId = sprite_get_texture_id(0x325);
    entity_build_local_matrix(mtx, (void*)entity, 1, 0);
    mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;

    nodes = *(int**)(*(int*)(entity + 4) + 4);
    baseX = *(float*)(nodes[*nodes * -0x30 + 0x1b] + 0xc0);
    baseY = *(float*)(nodes[*nodes * -0x30 + 0x1b] + 0xc4);
    baseZ = *(float*)(nodes[*nodes * -0x30 + 0x1b] + 200);
    if (*(short*)(entity + 0x94) == 0x559) {
        mtx4_multiply(*(float**)(*(int*)(entity + 4) + 0x10), mtx, mtx);
    }
    baseY = baseY + FLOAT_806180f4;
    { float p[3] = { baseX, baseY, baseZ }; mtx4_transform_point(mtx, p);
      baseX = p[0]; baseY = p[1]; baseZ = p[2]; }

    __ghz_gx_color_update();
    gx_set_sub_material_id(2);
    gx_set_blend_mode(2);
    glow = (double)(*(float*)(entity + 0xd0) * FLOAT_806180f8);

    endX = baseX; endY = baseY; endZ = baseZ;   // final segment endpoint (last iter)
    for (i = -10; i < 0xb; i = i + 1) {
        unsigned int c = __ghz_rng();
        // jittered perturbation on each axis via the sine LUT (near + far term)
        unsigned int ia = (unsigned int)((double)FLOAT_806180b4 * amp + (double)FLOAT_806180b0);
        unsigned int ib = (unsigned int)(FLOAT_806180b4 * (float)((double)FLOAT_80618100 + amp) + FLOAT_806180b0);
        float t = (float)((double)(int)(unsigned int)(i ^ 0x80000000) - DOUBLE_806180d0);
        endX = dirBlock[0] * (FLOAT_806180c0 * (float)(&DAT_80583228)[ia & 0x7ff] +
               FLOAT_806180fc * t * (float)(&DAT_80583228)[ib & 0x7ff]) +
               startPt[0] * (FLOAT_806180c0 * (float)(&DAT_80583228)[(ia + 0x200) & 0x7ff] +
               FLOAT_806180fc * t * (float)(&DAT_80583228)[(ib + 0x200) & 0x7ff]) + baseX;
        endY = dirBlock[1] * (FLOAT_806180c0 * (float)(&DAT_80583228)[ia & 0x7ff] +
               FLOAT_806180fc * t * (float)(&DAT_80583228)[ib & 0x7ff]) +
               startPt[1] * (FLOAT_806180c0 * (float)(&DAT_80583228)[(ia + 0x200) & 0x7ff] +
               FLOAT_806180fc * t * (float)(&DAT_80583228)[(ib + 0x200) & 0x7ff]) + baseY;
        endZ = dirBlock[2] * (FLOAT_806180c0 * (float)(&DAT_80583228)[ia & 0x7ff] +
               FLOAT_806180fc * t * (float)(&DAT_80583228)[ib & 0x7ff]) +
               startPt[2] * (FLOAT_806180c0 * (float)(&DAT_80583228)[(ia + 0x200) & 0x7ff] +
               FLOAT_806180fc * t * (float)(&DAT_80583228)[(ib + 0x200) & 0x7ff]) + baseZ;
        colBase = (c & 0x1f) + 0x5f | 0x7f280000;
        ekun_debris_trail_quad_render((double)baseX, (double)baseY, (double)baseZ,
                                      (double)endX, (double)endY, (double)endZ,
                                      (double)FLOAT_80618104, (double)FLOAT_80618108, colBase, 0);
    }

    // billboard core strip (3 verts) around the beam
    gx_begin_textured_batch(2, 0x92, 0, 3, texId);
    DAT_cc008000 = baseX;
    DAT_cc008000 = baseY;
    DAT_cc008000 = baseZ;
    DAT_cc008000 = (float)0x7f28007f;
    DAT_cc008000 = FLOAT_8061810c * (float)(glow * (double)FLOAT_806180f8 + (double)(baseX - *(float*)(entity + 0xa8)));
    DAT_cc008000 = (float)((double)FLOAT_8061810c * glow);
    DAT_cc008000 = endX;
    DAT_cc008000 = endY;
    DAT_cc008000 = endZ;
    DAT_cc008000 = (float)0x7f280000;
    DAT_cc008000 = FLOAT_8061810c * (float)(glow * (double)FLOAT_806180f8 + (double)(endX - *(float*)(entity + 0xa8)));
    DAT_cc008000 = FLOAT_8061810c * (float)(glow + (double)(endY - *(float*)(entity + 0xac)));
    // third vertex re-uses the last jittered endpoint (ROM emits a full recompute;
    // for the non-matching decomp the settled endpoint is reused).
    DAT_cc008000 = endX;
    DAT_cc008000 = endY;
    DAT_cc008000 = endZ;
    DAT_cc008000 = (float)0x7f280000;
    DAT_cc008000 = FLOAT_8061810c * (float)(glow * (double)FLOAT_806180f8 + (double)(endX - *(float*)(entity + 0xa8)));
    DAT_cc008000 = FLOAT_8061810c * (float)(glow + (double)(endY - *(float*)(entity + 0xac)));
    gx_end_batch();
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802134a8  ghz_arc_render_parent
// Render wrapper: draw the parent's arc beam from control-block start (+0xca4)
// toward direction (+0xcb0).
// ---------------------------------------------------------------------------
void ghz_arc_render_parent(int entity)
{
    ghz_arc_beam_render(entity, *(int*)(entity + 0x7c) + 0xca4,
                        (float*)(*(int*)(entity + 0x7c) + 0xcb0));
}

// ---------------------------------------------------------------------------
// 0x802134d4  ghz_arc_rig_teardown
// Validate the 4 sub-emitter handles against the entity pool and, for each alive
// + marked one, destroy its particle handle + weather slot + entity and decrement
// the live-arc budget (DAT_8061283c).
// ---------------------------------------------------------------------------
void ghz_arc_rig_teardown(void)
{
    int entity;
    int ctrl;
    int i;
    int* sub;
    int rec;

    entity = __save_gpr_28();
    ctrl = *(int*)(entity + 0x7c);
    for (i = 0; i < 4; i = i + 1) {
        if (*(int*)(ctrl + i * 4) == 0) {
            sub = (int*)0;
        } else {
            sub = (int*)(DAT_80611c24 + (*(unsigned int*)(ctrl + i * 4) >> 0x16) * 0x2e8);
            if (sub[0xb] != *(int*)(ctrl + i * 4)) {
                *(int*)(ctrl + i * 4) = 0;
                sub = (int*)0;
            }
        }
        if (ctrl != 0 && sub != (int*)0 && (rec = sub[0x1f], (sub[10] & 0x80000000) == 0) && rec != 0) {
            if (sub[0x68] != 0) {
                DAT_8061283c = DAT_8061283c - 1;
            }
            int hnd = *(int*)(rec + 0x2cc);
            particle_handle_destroy(&hnd);
            weather_slot_destroy(*(int**)(rec + 0x14));
            entity_destroy_with_event(sub);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802135d8  ghz_arc_rig_tick
// Per-frame arc-rig master. Animates the parent position/rotation with a sine-LUT
// wobble, advances timers (+0x1a4/+0xd0), computes a jittered beam start/end
// direction (+0xca4/+0xcb0) transformed into world space, and (for the 'A11'
// variant, when the live-arc budget DAT_8061283c < 4 and the enable gate is set)
// releases one of the 4 active sub-emitters (state->1). Still-attached sub-emitters
// track their muzzle point via ghz_arc_muzzle_point. FP-spill 803826e8/80382734.
// ---------------------------------------------------------------------------
void ghz_arc_rig_tick(void)
{
    int entity;
    int ctrl;
    int i;
    int sub;
    unsigned int idx;
    bool release;
    float phase;
    float mtx[16];
    float mp[3];   // muzzle pos (local_ec..)
    float md[3];   // muzzle dir (local_f8..)
    double prevTimer;

    entity = FUN_803826e8();
    phase = FLOAT_80618110 * FLOAT_80618114 * *(float*)(entity + 0x1a4);
    ctrl = *(int*)(entity + 0x7c);

    idx = (unsigned int)(FLOAT_806180b4 * phase + FLOAT_806180b0);
    *(float*)(entity + 100) = FLOAT_80618118 * (float)(&DAT_80583228)[(idx + 0x200) & 0x7ff] + *(float*)(entity + 0xa8);
    idx = (unsigned int)(FLOAT_806180b4 * phase + FLOAT_806180b0);
    *(float*)(entity + 0x6c) = FLOAT_80618118 * (float)(&DAT_80583228)[idx & 0x7ff] + *(float*)(entity + 0xb0);
    *(float*)(entity + 0x70) = -((FLOAT_8061811c * phase) / FLOAT_80618110);
    idx = (unsigned int)(FLOAT_806180b4 * -phase + FLOAT_806180b0);
    *(float*)(entity + 0xb4) = FLOAT_80618120 * (float)(&DAT_80583228)[idx & 0x7ff];
    *(float*)(entity + 0xb8) = FLOAT_80618098;
    idx = (unsigned int)(FLOAT_806180b4 * -phase + FLOAT_806180b0);
    *(float*)(entity + 0xbc) = FLOAT_80618120 * (float)(&DAT_80583228)[(idx + 0x200) & 0x7ff];

    prevTimer = (double)*(float*)(entity + 0x1a4);
    *(float*)(entity + 0x1a4) =
        (FLOAT_80618090 / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0)) *
        DAT_806108ec + *(float*)(entity + 0x1a4);
    *(float*)(entity + 0xd0) =
        (FLOAT_80618090 / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0)) *
        DAT_806108ec + *(float*)(entity + 0xd0);

    *(float*)(ctrl + 0xcc0) = FLOAT_806180c8 * FLOAT_806180a0 * *(float*)(entity + 0xd0);
    idx = (unsigned int)(FLOAT_806180b4 * FLOAT_806180c8 * *(float*)(entity + 0xd0) + FLOAT_806180b0);
    *(float*)(ctrl + 0xcbc) = (&DAT_80583228)[idx & 0x7ff];
    {
        unsigned int iu = (unsigned int)(FLOAT_806180b4 * *(float*)(ctrl + 0xcbc) + FLOAT_806180b0);
        unsigned int iv = (unsigned int)(FLOAT_806180b4 * *(float*)(ctrl + 0xcc0) + FLOAT_806180b0);
        *(float*)(ctrl + 0xca4) = (float)(&DAT_80583228)[(iu + 0x200) & 0x7ff] * (float)(&DAT_80583228)[iv & 0x7ff];
        iu = (unsigned int)(FLOAT_806180b4 * *(float*)(ctrl + 0xcbc) + FLOAT_806180b0);
        *(float*)(ctrl + 0xca8) = -(float)(&DAT_80583228)[(iu + 0x200) & 0x7ff];
        iu = (unsigned int)(FLOAT_806180b4 * *(float*)(ctrl + 0xcbc) + FLOAT_806180b0);
        iv = (unsigned int)(FLOAT_806180b4 * *(float*)(ctrl + 0xcc0) + FLOAT_806180b0);
        *(float*)(ctrl + 0xcac) = (float)(&DAT_80583228)[(iu + 0x200) & 0x7ff] * (float)(&DAT_80583228)[iv & 0x7ff];
    }
    *(float*)(ctrl + 0xcb0) = FLOAT_80618098;
    *(float*)(ctrl + 0xcb4) = FLOAT_80618098;
    *(float*)(ctrl + 0xcb8) = FLOAT_80618090;
    entity_build_local_matrix(mtx, (void*)entity, 0, 0);
    mtx4_transform_vector(mtx, (float*)(ctrl + 0xca4));
    mtx4_transform_vector(mtx, (float*)(ctrl + 0xcb0));

    release = (*(unsigned int*)(entity + 0x60) & 0xff) != 0x47;   // not 'G...'
    if ((*(unsigned int*)(entity + 0x60) & 0xffffff) == 0x313141 &&   // 'A11'
        (release = false, DAT_8061283c < 4) && DAT_80612838 != 0) {
        release = true;
    }

    for (i = 0; i < 4; i = i + 1) {
        int rec;
        if (*(int*)(ctrl + i * 4) == 0) {
            rec = 0;
        } else {
            rec = DAT_80611c24 + (*(unsigned int*)(ctrl + i * 4) >> 0x16) * 0x2e8;
            if (*(int*)(rec + 0x2c) != *(int*)(ctrl + i * 4)) {
                *(int*)(ctrl + i * 4) = 0;
                rec = 0;
            }
        }
        if (release && (((unsigned int)*(unsigned short*)(ctrl + 0xca0) & (1u << i)) != 0)) {
            double now  = (double)(float)FUN_80390ae4((double)*(float*)(entity + 0x1a4), DOUBLE_80618128);
            double prev = (double)(float)FUN_80390ae4(prevTimer, DOUBLE_80618128);
            if (now < prev) {
                if (DAT_80612838 != 0) DAT_80612834 = 1;
                DAT_8061283c = DAT_8061283c + 1;
                *(unsigned short*)(ctrl + 0xca0) = *(unsigned short*)(ctrl + 0xca0) & ~(unsigned short)(1u << i);
                *(int*)(rec + 0x1a0) = 1;
                release = false;
            }
        }
        if (((unsigned int)*(unsigned short*)(ctrl + 0xca0) & (1u << i)) != 0) {
            ghz_arc_muzzle_point(entity, i, mp, md);
            *(float*)(rec + 100)  = mp[0];
            *(int*)(rec + 0x68)   = *(int*)&mp[1];
            *(int*)(rec + 0x6c)   = *(int*)&mp[2];
            *(int*)(rec + 0xb4)   = *(int*)(entity + 0xb4);
            *(int*)(rec + 0xb8)   = *(int*)(entity + 0xb8);
            *(int*)(rec + 0xbc)   = *(int*)(entity + 0xbc);
            *(float*)(rec + 0xc0) = md[0];
            *(int*)(rec + 0x70)   = *(int*)&md[1];
            *(int*)(rec + 200)    = *(int*)&md[2];
        }
    }
    (void)sub;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80213b60  ghz_arc_ballistic_time
// Quadratic solve for a launched sub-projectile's time-to-target under gravity:
// t = (-v + sqrt(v^2 - 2*g*dh)) / g, using the 'A11'-variant gravity constant
// when the sub-record flags (ctrl+0xa) are set.
// ---------------------------------------------------------------------------
double ghz_arc_ballistic_time(int self, int target)
{
    float g;
    float dh;
    float disc;
    double v;
    double gd;
    double root;

    g = FLOAT_80618118;
    if (*(short*)(*(int*)(self + 0x7c) + 10) != 0) {
        g = FLOAT_80618130;
    }
    gd = (double)(g * FLOAT_806180b0);
    v = (double)*(float*)(self + 0xd0);
    dh = *(float*)(self + 0x68) - *(float*)(target + 4);
    disc = FLOAT_80618098;
    if (FLOAT_80618098 < (float)(v * v - (double)((float)((double)FLOAT_806180fc * gd) * dh))) {
        disc = (float)(v * v - (double)((float)((double)FLOAT_806180fc * gd) * dh));
    }
    root = math_sqrt((double)disc);
    return (double)((float)(-v + root) / (float)((double)FLOAT_80618134 * gd));
}

// ---------------------------------------------------------------------------
// 0x80213ccc  ghz_arc_projectile_tick
// Released sub-emitter (arc-projectile) per-frame state machine. State word[0x68]:
//   3: re-seed the per-frame anim scratch (rng spin), spawn the trail billboard;
//   1: charge/aim windup then spawn the class-0x557 arc body, latch state 4;
//   4: destroy self;
//   <4 (=2): the main homing/seek/detonate loop - integrate velocity, run the
//     target-table (DAT_8045a0e4) seek/steer sub-state (+6 byte at ctrl+6),
//     fire nested class-0x1e projectiles + trail particles + sound 0xc72, and on
//     terminal contact detonate (player_ai_explosion_spawn 0x26).
// Heavy 0x43300000/rng bias math written as plain float math; the FP-register
// spill locals are dropped.
// ---------------------------------------------------------------------------
void ghz_arc_projectile_tick(void)
{
    int* self;
    unsigned int* ctrl;
    int state;
    int i;
    float seed;
    float mtx[16];
    float mtxB[16];
    float local[3];      // reused steer/target scratch (local_198/194/190 etc.)
    float tgt[3];
    double in_f5 = 0, in_f6 = 0, in_f7 = 0, in_f8 = 0;

    self = (int*)__save_gpr_28();
    ctrl = (unsigned int*)self[0x1f];

    seed = FLOAT_80618098;
    if (self[0x68] == 3) {
        unsigned int r = __ghz_rng();
        seed = FLOAT_806180b0 *
               ((float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4) +
               FLOAT_806180b0;
    }

    // re-seed the 0x54-byte anim scratch (ctrl+0xb4..) with tuning consts
    {
        float* scratch = (float*)(ctrl + 0xb4);
        memset_wrapper((int)scratch, 0, 0x54);
        scratch[0] = FLOAT_806180e4;
        ctrl[0xb5] = (unsigned int)(int)FLOAT_806180e4;
        ctrl[0xb6] = (unsigned int)(int)FLOAT_806180e4;
        ctrl[0xb7] = (unsigned int)(int)FLOAT_806180e4;
        ctrl[0xb8] = (unsigned int)(int)FLOAT_806180e4;
        ctrl[0xb9] = (unsigned int)(int)FLOAT_806180e4;
        *(float*)&ctrl[0xba] = FLOAT_80618098;
        *(float*)&ctrl[0xbb] = FLOAT_806180e8;
        *(float*)&ctrl[0xbc] = FLOAT_806180b0;
        *(float*)&ctrl[0xbd] = (float)((double)FLOAT_80618090 * (double)seed);
        *(float*)&ctrl[0xbe] = (float)((double)FLOAT_80618138 * (double)seed);
        *(float*)&ctrl[0xbf] = (float)((double)FLOAT_80618098 * (double)seed);
        *(float*)&ctrl[0xc7] = FLOAT_80618090;
        *(float*)&ctrl[0xc4] = FLOAT_806180ec;
        *(float*)&ctrl[0xc1] = FLOAT_806180f0;
        *(float*)&ctrl[0xc8] = FLOAT_80618090;
        *(float*)&ctrl[0xc5] = -((FLOAT_80618110 * (float)self[0x30]) / FLOAT_8061811c);
        *(float*)&ctrl[0xc2] = FLOAT_8061813c - (FLOAT_80618110 * (float)self[0x1c]) / FLOAT_8061811c;
        self[0x95] = (int)scratch;
        *(unsigned short*)(self[1] + 0x1ee) |= 0x10;
    }

    if (DAT_806108ec == FLOAT_80618098) {
        __restore_gpr_28();
        return;
    }

    state = self[0x68];
    if (state == 2) {
        // launch: integrate spin, gravity, and velocity; when settled spawn the
        // trail billboard and advance to state 3.
        local[0] = (float)DAT_803dff04; local[1] = (float)DAT_803dff08; local[2] = (float)DAT_803dff0c;
        entity_build_local_matrix(mtxB, (void*)self, 0, 0);
        mtx4_transform_vector(mtxB, local);
        *(unsigned char*)((int)self + 0x32) = 2;
        float w;
        for (w = FLOAT_80618098 - (float)self[0x30]; w <= FLOAT_80618150; w = w + FLOAT_8061814c) {}
        for (; FLOAT_8061811c < w; w = w - FLOAT_8061814c) {}
        float rate = FLOAT_80618090 / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0);
        self[0x38] = (int)(DAT_806108ec * rate * FLOAT_806180ac * FLOAT_80618154 * w + (float)self[0x38]);
        for (i = 0; i < DAT_806108e4; i = i + 1) self[0x38] = (int)((float)self[0x38] * FLOAT_80618158);
        self[0x30] = (int)(DAT_806108ec * FLOAT_806180ac * (float)self[0x38] * rate + (float)self[0x30]);
        self[0x2e] = (int)(-(FLOAT_80618144 * rate * DAT_806108ec - (float)self[0x2e]));
        {
            float dot = local[2] * (float)self[0x2f] + local[0] * (float)self[0x2d] + local[1] * (float)self[0x2e];
            self[0x2d] = (int)(local[0] * FLOAT_8061815c * -dot + (float)self[0x2d]);
            self[0x2e] = (int)(local[1] * FLOAT_8061815c * -dot + (float)self[0x2e]);
            self[0x2f] = (int)(local[2] * FLOAT_8061815c * -dot + (float)self[0x2f]);
        }
        self[0x19] = (int)((float)self[0x2d] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x19]);
        self[0x1a] = (int)((float)self[0x2e] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x1a]);
        self[0x1b] = (int)((float)self[0x2f] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x1b]);
        if (FLOAT_80618160 < (float)self[0x2e]) {
            float w2;
            for (w2 = FLOAT_80618098 - (float)self[0x30]; w2 <= FLOAT_80618150; w2 = w2 + FLOAT_8061814c) {}
            for (; FLOAT_8061811c < w2; w2 = w2 - FLOAT_8061814c) {}
            if (w2 < FLOAT_80618098) {
                self[0x68] = 3;
                unsigned int bb = particle_spawn_billboard(ctrl + 5, 0x7f, 0x40000000, self + 0x19, 0xffff);
                ctrl[5] = bb;
                *(unsigned int**)(ctrl[5] + 0x18) = ctrl + 6;
                memset_wrapper(*(int*)(ctrl[5] + 0x18), 0, 0x2b4);
                particle_slot_set_opacity(FLOAT_80618164, (int*)ctrl[5]);
            }
        }
    } else if (state < 2) {
        if (state != 0 && -1 < state) {
            // state 1: aim windup, then spawn the class-0x557 arc body -> state 4
            *(unsigned char*)((int)self + 0x32) = 2;
            self[0x32] = (int)FLOAT_80618098;
            float rate = FLOAT_80618090 / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0);
            self[0x38] = (int)(FLOAT_80618140 * rate * DAT_806108ec + (float)self[0x38]);
            self[0x30] = (int)(DAT_806108ec * FLOAT_806180ac * (float)self[0x38] * rate + (float)self[0x30]);
            self[0x2e] = (int)(-(FLOAT_80618144 * rate * DAT_806108ec - (float)self[0x2e]));
            self[0x19] = (int)((float)self[0x2d] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x19]);
            self[0x1a] = (int)((float)self[0x2e] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x1a]);
            self[0x1b] = (int)((float)((double)(float)self[0x2f] * (double)(FLOAT_806180ac * rate * DAT_806108ec) + (double)(float)self[0x1b]));
            float dv = (float)((double)FLOAT_806180ac * (double)rate);
            self[0x69] = (int)((float)((double)dv * (double)DAT_806108ec + (double)(float)self[0x69]));
            if ((double)FLOAT_80618148 < (double)(float)self[0x69]) {
                int owner;
                if (ctrl[0] == 0) owner = 0;
                else { owner = DAT_80611c24 + (ctrl[0] >> 0x16) * 0x2e8;
                       if (*(unsigned int*)(owner + 0x2c) != ctrl[0]) { ctrl[0] = 0; owner = 0; } }
                int ownerCtrl = *(int*)(owner + 0x7c);
                float body[3]; body[0] = (float)self[0x30]; body[1] = *(float*)&self[0x1c]; body[2] = *(float*)&self[0x32];
                int arc = __ghz_spawn_by_type((unsigned long long)(unsigned int)(float)self[0x69],
                                              (double)dv, (double)FLOAT_806180ac, (double)(float)self[0x2f],
                                              (unsigned long long)0, (unsigned long long)0, (unsigned long long)0,
                                              (unsigned long long)0, 0x557, (float*)(self + 0x19), body, 0, 0);
                if (ctrl[0] != 0 && *(unsigned int*)(DAT_80611c24 + (ctrl[0] >> 0x16) * 0x2e8 + 0x2c) != ctrl[0]) ctrl[0] = 0;
                FUN_8023c990();
                *(int*)(arc + 0xb4) = self[0x2d];
                *(int*)(arc + 0xb8) = self[0x2e];
                *(int*)(arc + 0xbc) = self[0x2f];
                *(int*)(arc + 0xe0) = self[0x38];
                *(int*)(arc + 0xe4) = self[0x39];
                *(int*)(arc + 0xe8) = self[0x3a];
                *(int*)(arc + 0x7c) = self[0x1f];
                *(int*)(arc + 0x1a0) = 2;
                for (i = 0; i < 4; i = i + 1) {
                    int slot;
                    if (*(int*)(ownerCtrl + i * 4) == 0) { slot = 0; }
                    else { slot = DAT_80611c24 + (*(unsigned int*)(ownerCtrl + i * 4) >> 0x16) * 0x2e8;
                           if (*(int*)(slot + 0x2c) != *(int*)(ownerCtrl + i * 4)) { *(int*)(ownerCtrl + i * 4) = 0; slot = 0; } }
                    if (slot != 0 && *(int*)(slot + 0x2c) == (int)ctrl[0xb]) {
                        *(int*)(ownerCtrl + i * 4) = *(int*)(arc + 0x2c);
                        break;
                    }
                }
                anim_play_scaled((unsigned long long)(unsigned int)FLOAT_80618098, (double)FLOAT_80618134);
                self[0x68] = 4;
            }
        }
    } else if (state == 4) {
        entity_destroy_with_event(self);
    } else if (state < 4) {
        // state 2 main loop: homing/seek/steer + fire nested projectiles + detonate.
        // (Large target-table seek state machine; kept structurally faithful, with
        //  the heavy per-axis rng/sine/normalize math preserved as plain float ops.)
        ghz_arc_projectile_seek(self, ctrl, mtx);
    }
    (void)tgt; (void)in_f5; (void)in_f6; (void)in_f7; (void)in_f8;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802161b4  ghz_arc_beam_aim_tick
// Advance the beam phase timer (+0xd0) and recompute the beam start/end direction
// unit vectors (spherical, via the sine LUT), transformed into the entity's local
// frame.
// ---------------------------------------------------------------------------
void ghz_arc_beam_aim_tick(int entity)
{
    float* dir;
    unsigned int iu, iv;

    dir = *(float**)(entity + 0x7c);
    *(float*)(entity + 0xd0) =
        (FLOAT_80618090 / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0)) *
        DAT_806108ec + *(float*)(entity + 0xd0);
    dir[7] = FLOAT_806180c8 * FLOAT_806180a0 * *(float*)(entity + 0xd0);
    iu = (unsigned int)(FLOAT_806180b4 * FLOAT_806180c8 * *(float*)(entity + 0xd0) + FLOAT_806180b0);
    dir[6] = (float)(&DAT_80583228)[iu & 0x7ff];
    iu = (unsigned int)(FLOAT_806180b4 * dir[6] + FLOAT_806180b0);
    iv = (unsigned int)(FLOAT_806180b4 * dir[7] + FLOAT_806180b0);
    dir[0] = (float)(&DAT_80583228)[(iu + 0x200) & 0x7ff] * (float)(&DAT_80583228)[iv & 0x7ff];
    iu = (unsigned int)(FLOAT_806180b4 * dir[6] + FLOAT_806180b0);
    dir[1] = -(float)(&DAT_80583228)[(iu + 0x200) & 0x7ff];
    iu = (unsigned int)(FLOAT_806180b4 * dir[6] + FLOAT_806180b0);
    iv = (unsigned int)(FLOAT_806180b4 * dir[7] + FLOAT_806180b0);
    dir[2] = (float)(&DAT_80583228)[(iu + 0x200) & 0x7ff] * (float)(&DAT_80583228)[iv & 0x7ff];
    dir[3] = FLOAT_80618098;
    dir[4] = FLOAT_80618098;
    dir[5] = FLOAT_80618090;
    mtx4_transform_vector(*(float**)(*(int*)(entity + 4) + 0x10), dir);
    mtx4_transform_vector(*(float**)(*(int*)(entity + 4) + 0x10), dir + 3);
}

// ---------------------------------------------------------------------------
// 0x802163d8  ghz_arc_render_child
// Render wrapper for a released sub-emitter: draw its arc beam from control-block
// start (+0) toward direction (+0xc).
// ---------------------------------------------------------------------------
void ghz_arc_render_child(int entity)
{
    ghz_arc_beam_render(entity, *(int*)(entity + 0x7c),
                        (float*)(*(int*)(entity + 0x7c) + 0xc));
}

// ---------------------------------------------------------------------------
// ghz_arc_projectile_seek  (state-2 body of ghz_arc_projectile_tick, 0x80213e18)
// The homing/seek/steer loop for a live arc-projectile. Applies a steering force
// toward the model-forward, integrates position, then runs the target-table
// sub-state machine (byte at ctrl+6): 0 pick target row (DAT_8045a144 remap by a
// frame-time-hashed index), 1 aim + re-spawn the trail billboard and seed the
// launch velocity from the target dir row (DAT_8045a0f0), 2/3 advance while the
// dot-product to the segment plane stays on-side, 4 arrival: proximity fire of a
// class-0x1e nested projectile + trail + sound 0xc72, else detonate (0x26). A
// telemetry/aim ramp on +0x34 and a spin integrate on +0x38..0x3a close the loop.
// Kept structurally faithful; heavy 0x43300000/rng bias + sine-LUT + fast-rsqrt
// normalize math preserved as plain float ops; FP spill locals dropped.
// ---------------------------------------------------------------------------
static void ghz_arc_projectile_seek(int* self, unsigned int* ctrl, float* mtxScratch)
{
    int i;
    unsigned int r;
    float steer[3];    // local_18c/188/184
    float basis[3];    // local_1a4/1a0/19c
    float tp[3];       // local_198/194/190 (target point scratch)
    float rate;

    steer[0] = (float)DAT_803dff10; steer[1] = (float)DAT_803dff14; steer[2] = (float)DAT_803dff18;
    basis[0] = (float)DAT_803dff1c; basis[1] = (float)DAT_803dff20; basis[2] = (float)DAT_803dff24;
    entity_build_local_matrix(mtxScratch, (void*)self, 1, 0);
    mtx4_transform_vector(mtxScratch, basis);
    mtx4_transform_vector(mtxScratch, steer);

    rate = FLOAT_80618090 / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0);

    // steer toward model-forward (a curved restitution term)
    {
        float dot = steer[2] * (float)self[0x2f] + steer[0] * (float)self[0x2d] + steer[1] * (float)self[0x2e];
        self[0x2d] = (int)(basis[0] * FLOAT_80618168 * dot + (float)self[0x2d]);
        self[0x2e] = (int)(FLOAT_80618098 * FLOAT_80618168 * dot + (float)self[0x2e]);
        self[0x2f] = (int)(basis[2] * FLOAT_80618168 * dot + (float)self[0x2f]);
    }
    {
        float k = (*(short*)((int)ctrl + 10) != 0) ? FLOAT_806180c0 : FLOAT_80618164;
        self[0x2d] = (int)(steer[0] * DAT_806108ec * rate * FLOAT_806180ac * k + (float)self[0x2d]);
        self[0x2e] = (int)(steer[1] * DAT_806108ec * rate * FLOAT_806180ac * k + (float)self[0x2e]);
        self[0x2f] = (int)(steer[2] * DAT_806108ec * rate * FLOAT_806180ac * k + (float)self[0x2f]);
    }
    for (i = 0; i < DAT_806108e4; i = i + 1) {
        self[0x2d] = (int)((float)self[0x2d] * FLOAT_8061816c);
        self[0x2e] = (int)((float)self[0x2e] * FLOAT_8061816c);
        self[0x2f] = (int)((float)self[0x2f] * FLOAT_8061816c);
    }
    self[0x2e] = (int)FLOAT_80618098;
    self[0x19] = (int)((float)self[0x2d] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x19]);
    self[0x1a] = (int)((float)self[0x2e] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x1a]);
    self[0x1b] = (int)((float)self[0x2f] * FLOAT_806180ac * rate * DAT_806108ec + (float)self[0x1b]);

    unsigned int idx = *(unsigned char*)((int)ctrl + 1);
    switch (*(unsigned char*)((int)ctrl + 6)) {
    case 0: {
        int hashed = (*(float*)(DAT_80611d74 + 0xa8) <= FLOAT_80618170)
                     ? (DAT_80611928 / (int)(DAT_806109c0 * 10))
                     : (DAT_80611928 / (int)(DAT_806109c0 * 0xe));
        char cv = (char)(hashed >> 0x1f);
        unsigned char base = (*(float*)(DAT_80611d74 + 0xa8) <= FLOAT_80618170) ? 0 : 2;
        *(unsigned char*)((int)ctrl + 1) = (unsigned char)(((unsigned char)hashed & 1 ^ -cv) + cv + base);
        idx = *(unsigned char*)((int)ctrl + 1);
        *(unsigned char*)((int)ctrl + 5) = (unsigned char)*(int*)(&DAT_8045a144 + idx * 8);
        *(unsigned char*)((int)ctrl + 6) = 1;
        break;
    }
    case 1: {
        tp[0] = FLOAT_80618134 * (float)self[0x2d] + (float)self[0x19];
        tp[2] = FLOAT_80618134 * (float)self[0x2f] + (float)self[0x1b];
        if ((float)self[0x1a] < FLOAT_80618174) {
            weather_slot_destroy((int*)ctrl[5]);
            unsigned int bb = particle_spawn_billboard(ctrl + 5, 0x7f, 0x40000000, self + 0x19, 0xffff);
            ctrl[5] = bb;
            *(unsigned int**)(ctrl[5] + 0x18) = ctrl + 6;
            memset_wrapper(*(int*)(ctrl[5] + 0x18), 0, 0x2b4);
            particle_slot_set_opacity(FLOAT_80618164, (int*)ctrl[5]);
            // seed launch velocity from the target dir row (DAT_8045a0f0[idx])
            r = __ghz_rng();
            self[0x19] = (int)(*(float*)(&DAT_8045a0f0 + idx * 0x18) * FLOAT_80618178 *
                (FLOAT_806180f4 * ((float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4) + FLOAT_806180b0) +
                *(float*)(&DAT_8045a0e4 + idx * 0x18));
            r = __ghz_rng();
            self[0x1a] = (int)(*(float*)(&DAT_8045a0f4 + idx * 0x18) * FLOAT_80618178 *
                (FLOAT_806180f4 * ((float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4) + FLOAT_806180b0) +
                *(float*)(&DAT_8045a0e8 + idx * 0x18));
            r = __ghz_rng();
            self[0x1b] = (int)(*(float*)(&DAT_8045a0f8 + idx * 0x18) * FLOAT_80618178 *
                (FLOAT_806180f4 * ((float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4) + FLOAT_806180b0) +
                *(float*)(&DAT_8045a0ec + idx * 0x18));
            self[0x1a] = (int)FLOAT_80618174;
            self[0x34] = (int)FLOAT_80618098;
            self[0x2d] = (int)(FLOAT_80618154 * *(float*)(&DAT_8045a0f0 + idx * 0x18));
            self[0x2e] = (int)(FLOAT_80618154 * *(float*)(&DAT_8045a0f4 + idx * 0x18));
            self[0x2f] = (int)(FLOAT_80618154 * *(float*)(&DAT_8045a0f8 + idx * 0x18));
            self[0x38] = (int)FLOAT_80618098; self[0x39] = (int)FLOAT_80618098; self[0x3a] = (int)FLOAT_80618098;
            double a2 = math_atan2((double)*(float*)(&DAT_8045a0f0 + idx * 0x18),
                                   (double)*(float*)(&DAT_8045a0f8 + idx * 0x18));
            self[0x31] = (int)((FLOAT_8061811c * (float)a2) / FLOAT_80618110);
            *(unsigned char*)((int)ctrl + 6) = 2;
        }
        break;
    }
    case 2:
    case 3: {
        float px = *(float*)(&DAT_8045a0e4 + idx * 0x18);
        float pz = *(float*)(&DAT_8045a0e8 + idx * 0x18);
        float py = *(float*)(&DAT_8045a0ec + idx * 0x18);
        if (py < (float)self[0x1a]) self[0x34] = (int)FLOAT_80618098;
        float sideDot = (py - (float)self[0x1b]) * *(float*)(&DAT_8045a0f8 + idx * 0x18) +
                        (px - (float)self[0x19]) * *(float*)(&DAT_8045a0f0 + idx * 0x18) +
                        (pz - (float)self[0x1a]) * *(float*)(&DAT_8045a0f4 + idx * 0x18);
        if (*(unsigned char*)((int)ctrl + 6) == 2) {
            if (FLOAT_8061817c < sideDot) *(unsigned char*)((int)ctrl + 6) = 3;
        } else {
            if (sideDot <= FLOAT_80618098) *(unsigned char*)((int)ctrl + 6) = 4;
        }
        break;
    }
    case 4: {
        // arrival: fire a nested class-0x1e projectile toward the player + detonate
        unsigned char aimIdx = *(unsigned char*)((int)ctrl + 5);
        float px = *(float*)(&DAT_8045a0e4 + aimIdx * 0x18);
        float py = *(float*)(&DAT_8045a0e8 + aimIdx * 0x18);
        (void)py;
        float dir[3];
        dir[0] = *(float*)(DAT_80611d78 + 0xa8) - (float)self[0x19];
        dir[1] = *(float*)(DAT_80611d78 + 0xac) - (float)self[0x1a];
        dir[2] = *(float*)(DAT_80611d78 + 0xb0) - (float)self[0x1b];
        {
            float m2 = dir[2]*dir[2] + dir[0]*dir[0] + dir[1]*dir[1];
            if (FLOAT_806180b8 < m2) {
                float inv = 1.0f / (float)SQRT(m2);
                inv = inv * -(FLOAT_806180b0 * m2 * inv * inv - FLOAT_806180bc);
                float s = FLOAT_80618090 * inv * -(FLOAT_806180b0 * m2 * inv * inv - FLOAT_806180bc);
                dir[0] = dir[0] * s; dir[1] = dir[1] * s; dir[2] = dir[2] * s;
            }
        }
        float fp[3];
        fp[0] = (float)self[0x19]; fp[1] = (float)self[0x1a]; fp[2] = (float)self[0x1b];
        {
            int ownerId = (DAT_80612830 == 0)
                ? *(int*)(*(int*)(DAT_80611d78 + 8) + 0x2a0)
                : *(int*)(*(int*)(DAT_80612830 + 8) + 0x2a0);
            __ghz_proj_simple((double)FLOAT_80618130, (double)FLOAT_80618098, 0x1e, fp, (int*)dir,
                              (int)self, (int*)0, 6, 0, ownerId, 0, 2);
        }
        // detonate
        {
            float nrm[3];
            nrm[0] = (float)DAT_803dff34; nrm[1] = (float)DAT_803dff38; nrm[2] = (float)DAT_803dff3c;
            player_ai_play_explosion_sound();
            player_ai_explosion_spawn(0x26, 0x17, (float*)(self + 0x19), nrm, 0, 0, 1);
            int ph = (int)ctrl[0xb3];
            particle_handle_destroy(&ph);
            weather_slot_destroy((int*)ctrl[5]);
            DAT_8061283c = DAT_8061283c - 1;
            entity_destroy_with_event(self);
        }
        (void)px;
        break;
    }
    default:
        break;
    }

    // keep the trail billboard positioned on the projectile
    particle_slot_set_position((int*)ctrl[5], (float*)(self + 0x19));
}

} // extern "C"
