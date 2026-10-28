// worm_ghz_state.cpp
// 'GHZ1'/'GHZ2' boss/reactor set-piece (address range 0x8020530c - 0x8020649b,
// batch_647), the sibling of the ZCSC monitor set-piece in worm_smem_state.cpp.
// subsystem:game, worm-effect module.
//
// This set-piece is a two-actor ('GHZ1'/'GHZ2') scripted reactor sequence driven
// through the scene actor list. Its shared state lives in a 0x30-byte control
// block (DAT_806127e8) allocated at level init, plus a 0x80x0x80 scratch render
// target (DAT_806127e0) and two cached spark emitters (DAT_806127d4/d0). The
// per-frame master (ghz_master_update) walks the active-actor list, fires scripted
// payloads when a player enters one of the tagged sound zones, drives the
// per-actor GHZ1/GHZ2 render callbacks, and runs the ambient 3D audio.
//
// Functions:
//   ghz_object_init_dispatch (0x8020530c) master per-class object-init switch on
//                            the class id at +0x94 (0x392..0x3c9): wires the render
//                            /update callbacks + flag bits + model handles for every
//                            GHZ prop variant; case 0x396 (tag 'KYKF') also allocs
//                            the 0x40x0x40 scanline render target.
//   ghz_install_director_screen_effect (0x80205598) install the director-cam screen
//                            effect slot onto the P2/P4 view actor (once) + fire
//                            payload op1.
//   ghz_set_bird_active      (0x80205614) DAT_806127b0 = 1.
//   ghz_gz2_node_place       (0x80205620) GHZ2 sub-node: place a render node at
//                            submesh 0x13 with +/- FLOAT_80617d74 Y bias, keyed by
//                            the linked-actor tag ('js1c'/'js2c' 4CC ids).
//   ghz_gz1_break_render     (0x8020572c) GHZ1 render callback: on the break clip
//                            (player active-index state 4) resolve the 'js1j' island,
//                            clear DAT_806127b0, install the default render cb, and
//                            free/redraw the strip.
//   ghz_class_scale_lookup   (0x80205820) per-class scalar table (class id at
//                            +0x14->+0x34): returns a tuning float for each variant.
//   ghz_ambient_audio_tick   (0x80205914) per-frame ambient audio: nearest-actor
//                            proximity intensity -> 3D loop source 0x6 + zone-keyed
//                            one-shot sample (table DAT_80457ecc) + secondary 3D
//                            loop 0x56e.
//   ghz_level_preload_alloc  (0x80205c1c) alloc the 0x30 control block + 0x80x0x80
//                            scratch RT, preload ~14 models, clear 7 sound banks.
//   ghz_master_update        (0x80205d00) per-frame master: per-player zone->payload
//                            gate chain, bind the HTBJ/INAJ team actors + GHZ1/GHZ2
//                            render callbacks, run the water/mode sub-ticks, then
//                            ghz_ambient_audio_tick.
//   ghz_level_init           (0x80206028) level init: reset the control block,
//                            register the two actor banks, build 8 HUD objective
//                            slots (strings 0x1275..0x1278), and create 2 spark
//                            particle defs + emitters (disabled).
//
// Non-matching: 4CC ids (GHZ1/GHZ2/KYKF/DRGM/js1c/js2c/js1j + team HTBJ/INAJ are
// class words); PPC FP spill frames (FUN_803826f0 open / FUN_8038273c close) are
// opaque; int<->double 0x43300000 bias math is written as plain float math; the
// unsigned rng->float pattern ((uint bias)/2^32 - 0.5) is written directly. Wide
// FP-ABI helpers (entity_model_cache_alloc, level_actors_register,
// sound_update_3d_source, rng_counter_tick, anim_event_scan_active) are routed
// through file-local typed aliases; the payload/gate/sub-tick helpers get
// file-local externs.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local) ---
extern int   DAT_806127e8;      // 0x30 control block base (ptr)
extern int   DAT_806127e0;      // 0x80x0x80 scratch render target slot
extern int   DAT_806127b0;      // bird/prop-active flag
extern float FLOAT_806127dc;    // cached ambient intensity
extern int   DAT_806127d8;      // sequence counter
extern int   DAT_806127d4;      // cached spark emitter A handle
extern int   DAT_806127d0;      // cached spark emitter B handle
extern int   DAT_806127a4;      // STRK/scanline block base (from batch_646)

extern int   DAT_80612610;      // active actor count
extern int   DAT_80611d78;      // active actor/character block base
extern int   DAT_80611d74;      // local/view actor block base
extern int   DAT_806109c0;      // per-frame time unit
extern float DAT_806108ec;      // frame-time scale
extern int   DAT_806108e4;      // gameplay-active gate
extern char  DAT_805015bd;      // single-player language/mode byte
extern int   DAT_806128a0;      // HUD objective menu context

extern int   DAT_80457b50;      // actor bank 0 name table
extern int   DAT_80457c80;      // actor bank 1 name table
extern int   DAT_80457ecc;      // ambient sound table {zoneId, soundId, pitch} 0xc stride
extern int   DAT_80205594;      // blr (no-op HUD render callback)
extern char  switchdataD_80456ec4;  // jump table for entity_model_cache_alloc case 0x3c5

extern int   DAT_803dfd90; extern int DAT_803dfd94; extern int DAT_803dfd98;  // spawn pos vec (0)
extern int   DAT_803dfd9c; extern int DAT_803dfda0; extern int DAT_803dfda4;  // spawn dir vec (0)
extern int   DAT_805c62a0;      // spark particle def A
extern int   DAT_805c636c;      // spark emitter A
extern int   DAT_805c63f0;      // spark particle def B
extern int   DAT_805c64bc;      // spark emitter B

// --- tuning float pool (0x80617ca0 / 0x80617d60..0x80617de4) ---
extern float  FLOAT_80617ca0;   // 5.0f (case 0x392/0x393 fill)
extern float  FLOAT_80617c0c;   // spin seed
extern float  FLOAT_80617c94;   // 0.5f (rng centre)
extern float  FLOAT_80617cc0;   // 2^32 (unsigned rng->float divisor)
extern double DOUBLE_80617cc8;  // 2^52 unsigned int->double bias

extern float  FLOAT_80617d64;   // 6.0f  (fill-rate multiplier)
extern float  FLOAT_80617d68;   // 80.0f (rng spin span)
extern float  FLOAT_80617d70;   // 3.0f
extern float  FLOAT_80617d74;   // 0.5f
extern float  FLOAT_80617d78;   // 0.75f
extern float  FLOAT_80617d7c;   // 1.0f
extern float  FLOAT_80617d80;   // 0.0f
extern float  FLOAT_80617d84;   // 0.8f
extern float  FLOAT_80617d88;   // 0.6f
extern float  FLOAT_80617d8c;   // 0.4f
extern float  FLOAT_80617d90;   // 0.2f
extern float  FLOAT_80617d94;   // 0.25f
extern float  FLOAT_80617d98;   // -0.1f (min intensity floor)
extern float  FLOAT_80617d9c;   // 1.5f  (loop range scale)
extern float  FLOAT_80617da0;   // 50.0f (loop pitch)
extern float  FLOAT_80617da4;   // 0.45f (near-zone volume)
extern float  FLOAT_80617da8;   // 0.56f (distance fade divisor... actually 0.56)
extern float  FLOAT_80617dac;   // 2.5f  (secondary loop ramp rate scale)
extern double DOUBLE_80617db0;  // 2^52 signed int->double bias
extern float  FLOAT_80617db8;   // 0.1f  (particle size-start)
extern float  FLOAT_80617dbc;   // 0.001f
extern float  FLOAT_80617dc0;   // -4.0f
extern float  FLOAT_80617dc4;   // 2.0f
extern float  FLOAT_80617dc8;   // 20.0f
extern float  FLOAT_80617dcc;   // 0.01f
extern float  FLOAT_80617dd0;   // 5.0f (drag)
extern float  FLOAT_80617dd4;   // 100.0f
extern float  FLOAT_80617dd8;   // 15.0f
extern float  FLOAT_80617ddc;   // 7.0f
extern float  FLOAT_80617de0;   // 3.66f
extern float  FLOAT_80617de4;   // 10.0f
// --- batch_648 additions (tuning float pool continued 0x80617de8..0x80617e08) ---
extern float  FLOAT_80617de8;   // break-piece scale seed
extern float  FLOAT_80617dec;   // delayed-event delay / seed base
extern float  FLOAT_80617df0;   // rng->float divisor (0x14 span normaliser)
extern float  FLOAT_80617df4;   // vertical velocity gain
extern double DOUBLE_80617df8;  // 2^52 unsigned int->double bias (rng path)
extern float  FLOAT_80617e00;   // island seed X (2.0f family)
extern float  FLOAT_80617e04;   // island restitution
extern float  FLOAT_80617e08;   // point-pool scale gain
// --- batch_649 additions (tuning float pool continued 0x80617e0c..0x80617e30) ---
extern float  FLOAT_80617e0c;   // pool-count normaliser (jitter probability divisor)
extern float  FLOAT_80617e10;   // per-frame decay (x/y damping)
extern float  FLOAT_80617e14;   // base pitch for split sound 0x1622
extern float  FLOAT_80617e18;   // neighbour split radius^2 threshold
extern float  FLOAT_80617e1c;   // strip half-width (sprite quad offset)
extern float  FLOAT_80617e20;   // strip mid offset
extern float  FLOAT_80617e24;   // strip full offset
extern float  FLOAT_80617e28;   // large distance seed (nearest-search init, ~1e30)
extern float  FLOAT_80617e2c;   // min length^2 (normalise guard)
extern float  FLOAT_80617e30;   // angle-wrap lower bound (-PI family)

// --- batch_649 additions (raw data symbols, file-local) ---
extern int    DAT_806127bc;     // point-pool frame counter (round-robin split gate)
extern int*   DAT_80611748;     // active viewport rect block {x0,y0,?,?,?,x1,y1,?}
extern int*   DAT_80610a20;     // sprite/material descriptor table base
extern float  DAT_803dfda8;     // belt centroid accumulate seed X (0)
extern float  DAT_803dfdac;     // belt centroid accumulate seed Y (0)
extern float  DAT_803dfdb0;     // belt centroid accumulate seed Z (0)
extern int    DAT_80457f5c;     // belt path table base: 4 lanes x 0x7c, points 0x1f-stride
extern int    DAT_80457fd0;     // per-lane accept-distance^2 threshold (0x7c stride)
extern float  DAT_80457fd4;     // per-lane flow-speed scalar (0x1f-word stride)

// --- batch_648 additions (raw data symbols, file-local) ---
extern int    DAT_806127e4;     // GHZ audio/handle scratch (FUN_80355574/8035560c arg)
extern int    DAT_806127b4;     // scanline-RT bound flag (set by GHZ1 break render)
extern int*   DAT_806127b8;     // point/spark pool base (0x14-stride, ~6000 bytes)
extern int    DAT_806127cc;     // point/spark pool live count
extern int    DAT_80611928;     // per-frame secondary time unit (int)

// --- batch_650 additions (tuning float pool 0x80617e34..0x80617e50) ---
extern float  FLOAT_80617e34;   // slide-node translation gain (0.238-family)
extern float  FLOAT_80617e38;   // slide-node half-gain
extern float  FLOAT_80617e3c;   // aim-euler base offset (bias)
extern float  FLOAT_80617e40;   // MCHT aim vector Y
extern float  FLOAT_80617e44;   // enter-menu anim speed
extern float  FLOAT_80617e48;   // (reserved / pool)
extern float  FLOAT_80617e4c;   // fill-charge normaliser
extern float  FLOAT_80617e50;   // spark-jet impulse gain

// --- batch_650 raw data symbols (file-local) ---
extern int    DAT_806127c8;     // MCHT sequence/dispatch counter
extern int    DAT_805015c4;     // match-state flag word (self-destruct bit)
extern int    DAT_803dfdc0; extern int DAT_803dfdc4; extern int DAT_803dfdc8; // MCHT up-vec seed
// (DAT_806109c0, DAT_806108ec, DAT_80612610, DAT_805015bd, DAT_806127e8, DAT_80611d74/78,
//  DOUBLE_80617db0, FLOAT_806108ec are declared above in earlier batches.)

// --- batch_650 helpers reached by raw address (file-local externs) ---
extern int  FUN_8023c8a0(int classWord, int entity);   // bind linked actor render slot
extern int  FUN_8023c59c(int block, int outList, int max); // collect linked sub-nodes -> list
extern double FUN_80390324(double phase);              // fmod/wrap into [-PI,PI] family
// FUN_80387fb8: wide varargs string-format (builds objective/HUD string buffer).
extern int  FUN_80387fb8(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8, int outBuf, const char* fmt,
                         int a, int b, int c, int d, int e, int f);
// player_ai_explosion_spawn resolves through the generated headers (7 args).
// FP-spill frame FUN_803826f0/FUN_8038273c already declared (batch_647).

// subobj_island_create_axis is void in generated_decls but the ROM returns the
// created island index; route through a typed alias to read it back.
typedef int (*GhzIslandAxisFn)(unsigned int subobj, unsigned int flags,
                               float* worldPoint, float* worldAxis);
static const GhzIslandAxisFn __ghz_island_create_axis =
    (GhzIslandAxisFn)&subobj_island_create_axis;

// floor_query_with_interact is void in generated_decls, but this MCHT call site
// passes the position/radius as doubles (FP-ABI) and reads a double return (the
// resolved floor height). Route through a typed alias matching the ROM shape.
typedef double (*GhzFloorQueryFn)(double x, double radius, int posBlock, int ctxBlock,
                                  int* outType, int skipInteract);
static const GhzFloorQueryFn __ghz_floor_query =
    (GhzFloorQueryFn)&floor_query_with_interact;

// ghz_reactor_spark_jet_tick (0x802093f8) is the JBPK per-frame driver; it is
// decompiled in the next batch. Referenced here only by address.
extern void ghz_reactor_spark_jet_tick(void);
extern char DAT_8048ca9c;       // per-view-slot config table base (0x30 stride)

// forward decls for the MCHT console pair (mutually referenced before defn).
void ghz_mcht_input_tick(int entity);
void ghz_mcht_exit_menu(void);

// --- batch_651 additions ---
// forward decls (thunks that call each other / nested helpers referenced early)
void ghz_break_payload_thunk(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8);
int  ghz_mesh_nearest_vertex(int renderData, float* refPt, float* outPt);

// tuning float pool continued 0x80617e54..0x80617e74
extern float  FLOAT_80617e54;   // beam pulse span / spark seed
extern float  FLOAT_80617e58;   // beam alpha gain
extern float  FLOAT_80617e5c;   // beam length step
extern float  FLOAT_80617e60;   // packed-vertex dequant scale (short->float)
extern float  FLOAT_80617e64;   // spark respawn interval base
extern float  FLOAT_80617e68;   // player-proximity radius^2 (rotor damage)
extern float  FLOAT_80617e6c;   // gate ease rate (idle)
extern float  FLOAT_80617e70;   // gate travel span
extern float  FLOAT_80617e74;   // gate loop pitch

// raw data symbols (file-local)
extern int    DAT_803dfdcc; extern int DAT_803dfdd0; extern int DAT_803dfdd4; // spark colour seed
extern int    DAT_803dfdd8; extern int DAT_803dfddc; extern int DAT_803dfde0; // beam tip local pt
extern int    DAT_803dfde4; extern int DAT_803dfde8; extern int DAT_803dfdec; // rotor tip local pt
extern int    DAT_80611d54;     // active camera/view-matrix block (rows at +0x364/+0x370)
extern int    DAT_805c6540;     // ZLFT rotor scratch-slot table (per +0x1a0 index, 4-byte stride)
extern int    DAT_806127c4;     // ZC bark last-index (avoid-repeat)
extern int    DAT_8045814c;     // ZC bark table A (soundId, flag) 8-byte stride
extern int    DAT_80458150;     // ZC bark table A flag field
extern int    DAT_80458164;     // ZC bark table B (soundId, flag) 8-byte stride
extern int    DAT_80458168;     // ZC bark table B flag field
extern volatile float DAT_cc008000; // GX CP FIFO write port (already used in sibling files)

// helpers reached by raw address (file-local externs)
extern int  FUN_803826c0(void);        // FP spill frame open (mesh-scan variant)
extern void FUN_8038270c(void);        // FP spill frame close
extern void FUN_80382734(void);        // FP spill frame close (rotor variant)
extern int  FUN_80377928(int arg);     // JBTH LCP2 payload trigger A
extern void FUN_8037793c(void);        // JBTH LCP2 payload trigger B

// subobj_break_tick / subobj_apply_ground_impulse / switch_slot_reset are void in
// generated_decls but the ROM returns a value (r3/f1) consumed by the caller.
typedef unsigned long long (*GhzBreakTickFn)(unsigned long long p1, unsigned long long p2,
                                             double p3, double p4, double p5, double p6,
                                             double p7, unsigned long long p8);
static const GhzBreakTickFn __ghz_break_tick = (GhzBreakTickFn)&subobj_break_tick;
typedef unsigned long long (*GhzGroundImpulseFn)(void);
static const GhzGroundImpulseFn __ghz_ground_impulse =
    (GhzGroundImpulseFn)&subobj_apply_ground_impulse;
typedef unsigned long long (*GhzSwitchSlotResetFn)(int slot);
static const GhzSwitchSlotResetFn __ghz_switch_slot_reset =
    (GhzSwitchSlotResetFn)&switch_slot_reset;

// mtx4_set_rotation_x is declared (double, int); this call passes (double, float*).
typedef void (*GhzRotXFn)(double angle, float* mtx);
static const GhzRotXFn __ghz_mtx4_set_rotation_x = (GhzRotXFn)&mtx4_set_rotation_x;

// --- batch_652 additions ---
extern float FLOAT_80617e78;    // team-damage fixed magnitude (non-standard mode)
extern float FLOAT_80617e7c;    // 'LLFT' gate travel span
extern float FLOAT_80617e80;    // default gate travel span

extern int   DAT_80458184;      // JBTH break-master 10-slot table (0xc stride: state,handle,teamId)
extern int   DAT_80611c24;      // player-handle pool base (0x2e8 stride, handle>>0x16)
extern char  DAT_805015bf;      // difficulty index (damage scale select)
extern int   DAT_806127c0;      // ZLFT rotor slot ring counter (mod-4)

// helpers reached by raw address (file-local externs)
extern void FUN_803826dc(void);        // FP spill frame open (break-master variant)
extern void FUN_80382728(void);        // FP spill frame close
extern int  FUN_8022136c(int op);      // payload op trigger (break-master complete)
// FUN_80289044: shared character anim-event/state-machine fx handler (the 7 GHZ
// thunks below all tail-call it). Declared void/no-arg (event code arrives via the
// FP-spill frame return).
extern void FUN_80289044(void);

// --- batch_653 additions ---
// tuning float pool continued 0x80617e88..0x80617ed4 (+ sine LUT / bias doubles)
extern float  FLOAT_80617e88;   // drop-trail slot init value
extern float  FLOAT_80617e8c;   // health-ratio threshold / phase scale
extern float  FLOAT_80617e90;   // 0.0f family (compare/seed)
extern float  FLOAT_80617e94;   // slot-active threshold
extern float  FLOAT_80617e98;   // slot secondary threshold
extern float  FLOAT_80617e9c;   // drop-trail velocity gain
extern float  FLOAT_80617ea0;   // 1.0f family
extern float  FLOAT_80617ea4;   // active drag scalar
extern float  FLOAT_80617ea8;   // slot clamp max
extern float  FLOAT_80617eac;   // slot ramp rate
extern double DOUBLE_80617eb0;  // 2^52 unsigned int->double bias
extern float  FLOAT_80617eb8;   // arc near-distance threshold
extern float  FLOAT_80617ebc;   // arc reach span
extern float  FLOAT_80617ec0;   // arc height base
extern float  FLOAT_80617ec4;   // arc phase gain
extern float  FLOAT_80617ec8;   // arc amplitude
extern float  FLOAT_80617ecc;   // arc yaw gain
extern float  FLOAT_80617ed0;   // arc angle divisor
extern float  FLOAT_80617ed4;   // arc pitch gain
extern int    DAT_80583228;     // 0x800-entry sine LUT (&0x7ff quarter-phase)

// raw data symbols (file-local)
extern int    DAT_805c6550;     // MTHS captured: class/model id (-1 when absent)
extern int    DAT_805c6554;     // MTHS captured: pos X (+0x64)
extern int    DAT_805c6558;     // MTHS captured: pos Y (+0x68)
extern int    DAT_805c655c;     // MTHS captured: pos Z (+0x6c)
extern int    DAT_805c6560;     // MTHS captured: field +0xc0
extern int    DAT_805c6564;     // MTHS captured: field +0x70
extern int    DAT_805c6568;     // MTHS captured: field +0xc8
extern int    DAT_805c656c;     // MTHS captured: health +0x1cc
extern int    DAT_805c6570;     // drop-trail 3-slot table (0x1c stride): [+0 t0,+4 posX,+8 posY,+c posZ,+10 age,+14 f14,+18 active]
extern int    DAT_805c6574;     // drop-trail slot: pos/vel X
extern int    DAT_805c6578;     // drop-trail slot: pos/vel Y
extern int    DAT_805c657c;     // drop-trail slot: pos/vel Z
extern int    DAT_805c6580;     // drop-trail slot: age
extern int    DAT_805c6584;     // drop-trail slot: field +0x14
extern int    DAT_805c6588;     // drop-trail slot: active flag
extern int    DAT_805c65c4;     // drop-trail particle emitter def
extern int    DAT_806127f0;     // drop-trail slot ring counter (mod-3)
extern int    DAT_80612614;     // active game-mode id
extern int    DAT_803dfdf0; extern int DAT_803dfdf4; extern int DAT_803dfdf8; // emitter dir seed

// helpers reached by raw address (file-local externs)
extern void FUN_8020df34(void);        // worm break cleanup / redraw helper
// FUN_8023458c / FUN_80234560: wide-ABI (16-arg) anim-clip start/scan-by-4CC helpers.
extern void FUN_8023458c(unsigned long long p1, double p2, double p3, double p4, double p5,
                         double p6, unsigned long long p7, unsigned long long p8, int classWord,
                         int a, int b, int c, int d, int e, int f, int g);
extern void FUN_80234560(unsigned long long p1, double p2, double p3, double p4, double p5,
                         double p6, unsigned long long p7, unsigned long long p8, int classWord,
                         void* a, int b, void* c, int d, int e, int f, int g);
// forward decls (mutual references within batch)
void ghz_mths_capture_transform(void);
void ghz_mths_restore_transform(unsigned long long p1, double p2, double p3, double p4, double p5,
                                double p6, unsigned long long p7, unsigned long long p8, int p9,
                                int p10, int p11, int p12, int p13, int p14, int p15, int p16);

// mtx4_to_euler is declared (float*, float*) in generated_decls but the ROM
// writes 3 separate euler out-floats; route through a typed 4-arg alias.
typedef void (*GhzToEulerFn)(float* mtx, float* outX, float* outY, float* outZ);
static const GhzToEulerFn __ghz_mtx4_to_euler = (GhzToEulerFn)&mtx4_to_euler;

// --- helpers reached by raw address (file-local externs; not in generated_decls) ---
// anim_active_clip_slot + arc_entity_attach come from the generated headers.
extern int  FUN_8027ea98(void);        // P2/P4 view actor getter
extern int  FUN_80238f8c(void);        // spectator/replay-mode probe (-1 when none)
extern int  FUN_802212b4(int op);      // payload op setter
extern int  FUN_80221074(double a, double b, double c, double d,
                         double e, double f, double g, double h); // fire scripted payload
extern int  FUN_802213b4(int op);      // per-op payload trigger
extern int  FUN_802215a8(int slot, int stringId, int a, int b, int c); // HUD objective slot build
extern int  FUN_8022126c(int menuCtx); // commit HUD objective slot
extern int  FUN_802217d4(int* renderCb);// register HUD render callback
extern int  FUN_8036fcf4(int mode);    // register update list
extern int  FUN_80220f70(int gate);    // game-mode gate set-test
extern int  FUN_80220f30(int gate);    // game-mode gate clear-test
extern int  FUN_80364f3c(void* frame); // default render-instance draw cb
extern int  FUN_80365824(void);        // strip re-draw after break
extern int  FUN_80365ce0(int entity, int tag, int idx, int node, int* pos); // place render node
extern int  FUN_8020a9d0(double a, double b, double c, double d,
                         double e, double f, double g, double h); // water/flow sub-tick
extern int  FUN_8020a53c(void);        // mode-0x12 sub-tick
extern int  FUN_8020a9a4(void);        // level init sub-step
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  FUN_803826f0(void);        // FP spill frame open
extern void FUN_8038273c(void);        // FP spill frame close

// --- batch_648 helpers reached by raw address (file-local externs) ---
extern int  FUN_803826e8(void);        // FP spill frame open (view/entity getter variant)
extern int  FUN_8023c32c(unsigned int classWord); // resolve linked actor by 4CC class word
extern void FUN_80399500(unsigned int buf, int byteLen); // dcache flush / zero-fill range
extern void FUN_80355574(double a, int handle); // GHZ audio ramp A
extern void FUN_8035560c(double a, int handle); // GHZ audio ramp B
// weapon_start_delayed_event, object_sound_slot_accumulate, player_ai_get_active_index,
// player_enter_menu_state, input_get_buttons_held, spawn_point_find_by_entity all
// resolve through the generated headers (do NOT re-declare - C2371/C2373).

// block_alloc_dispatch / block_free_simple live in generated_externs as permissive
// int name(...). block_alloc_dispatch actually returns the pool base and takes a
// reclaim code-label; route it through a typed alias to get the right shape without
// conflicting with the permissive extern.
typedef int* (*GhzBlockAllocFn)(int byteLen, int one, void* poolSlot, void* codeLabel);
static const GhzBlockAllocFn __ghz_block_alloc = (GhzBlockAllocFn)&block_alloc_dispatch;

// subobj_spawn_break_piece: wide FP-ABI ROM helper, returns the spawned piece
// entity (int). ROM shape: (x,y,z,scale, f5..f8 spill, classWord, 8, assetId,
// 0, mode, seedFloat). Routed through a typed alias to preserve the call shape.
typedef int (*GhzBreakPieceFn)(double x, double y, double z, double scale,
                               unsigned long long f5, unsigned long long f6,
                               unsigned long long f7, unsigned long long f8,
                               int classWord, int a, int assetId, int b, int mode, float seed);
static const GhzBreakPieceFn __ghz_spawn_break_piece =
    (GhzBreakPieceFn)&subobj_spawn_break_piece;

// subobj_island_create_point is declared void in generated_decls but the ROM
// returns the created island index; route through a typed alias to read it back.
typedef int (*GhzIslandCreateFn)(unsigned int subobj, unsigned int flags, float* worldPoint);
static const GhzIslandCreateFn __ghz_island_create_point =
    (GhzIslandCreateFn)&subobj_island_create_point;

// --- batch_648 typed aliases for generated_decls sig mismatches ---
// subobj_pool_alloc / mtx4_pool_alloc are void in decls but the ROM returns a handle.
typedef int (*GhzAllocRetFn)(void);
static const GhzAllocRetFn __ghz_subobj_pool_alloc = (GhzAllocRetFn)&subobj_pool_alloc;
static const GhzAllocRetFn __ghz_mtx4_pool_alloc   = (GhzAllocRetFn)&mtx4_pool_alloc;

// subobj_build_descriptor_default: wide 13-arg FP-ABI ROM shape (decl is narrowed).
typedef void (*GhzBuildDescFn)(unsigned int f1, double p2, double p3, double p4, double p5,
                               double p6, double p7, unsigned long long p8,
                               int subHandle, int entity, int a, int b, float seed);
static const GhzBuildDescFn __ghz_build_descriptor_default =
    (GhzBuildDescFn)&subobj_build_descriptor_default;

// collision_get_mesh_stats: this call site passes only (entity, handle, 0, 4).
typedef void (*GhzMeshStatsFn)(int entity, int handle, int sub, int query);
static const GhzMeshStatsFn __ghz_collision_mesh_stats =
    (GhzMeshStatsFn)&collision_get_mesh_stats;

// collision_alloc_vertex_buffers: ROM returns the vertex buffer base + 7 args.
typedef void* (*GhzAllocVBFn)(int entity, int handle, int sub, int query,
                              int* a, int* b, int* outCount);
static const GhzAllocVBFn __ghz_collision_alloc_vb =
    (GhzAllocVBFn)&collision_alloc_vertex_buffers;

// mtx4_set_translation / mtx4_set_scale: this call site writes the pool-current
// matrix from 3 doubles (decl takes a leading float* mtx).
typedef void (*GhzMtx3Fn)(double a, double b, double c);
static const GhzMtx3Fn __ghz_mtx4_set_translation = (GhzMtx3Fn)&mtx4_set_translation;
static const GhzMtx3Fn __ghz_mtx4_set_scale       = (GhzMtx3Fn)&mtx4_set_scale;

// --- batch_649 helpers reached by raw address (file-local FP-spill frames) ---
extern int  FUN_803826d4(void);        // FP spill frame open (12-reg variant)
extern void FUN_80382720(void);        // FP spill frame close (12-reg)
extern int  FUN_803826e4(void);        // FP spill frame open (returns entity pair)
extern void FUN_80382730(void);        // FP spill frame close

// gx_set_color_update_mode is declared (int) in generated_decls but this call
// site takes no meaningful argument; route through a no-arg typed alias.
typedef void (*GhzGxColorUpdFn)(void);
static const GhzGxColorUpdFn __ghz_gx_color_update = (GhzGxColorUpdFn)&gx_set_color_update_mode;

// gx_draw_sprite_strip4 ROM shape: the leading "frameArgs" is a double (FP-ABi
// spill), followed by 7 vertex doubles, 6 u/v uints, a z float, and 4 colours.
typedef void (*GhzSpriteStrip4Fn)(double a0, double y0, double y1, double y2, double y3,
                                  double y4, double y5, double y6,
                                  unsigned int uv0, unsigned int uv1, unsigned int uv2,
                                  unsigned int uv3, unsigned int uv4, unsigned int uv5,
                                  unsigned int uv6, unsigned int uv7,
                                  float z, unsigned int c0, unsigned int c1,
                                  unsigned int c2, unsigned int c3);
static const GhzSpriteStrip4Fn __ghz_draw_sprite_strip4 =
    (GhzSpriteStrip4Fn)&gx_draw_sprite_strip4;

// LAB_802071bc: a code label inside the point-pool free path, passed by address
// as the block-alloc reclaim callback. Referenced as its absolute ROM address.
#define GHZ_LAB_802071bc ((void*)0x802071bc)

// external render-callback targets registered by ghz_object_init_dispatch (defined
// in worm_smem_state.cpp, batch_644..646). Referenced only by address.
extern void zcsc_scanline_rt_draw(void);       // 0x80205044
extern void zcsc_bind_scanline_rt(void);       // 0x80205008
extern void zcsc_monitor_render_passes(void);  // 0x80202a1c
extern void zcsc_spark_pool_tick(void);        // 0x80204628
extern void zcsc_spark_pool_render(void);      // 0x80204a00
extern void zcsc_monitor_cam_tick(void);       // 0x80202da0
extern void zcsc_slve_glow_tick(void);         // 0x80203fb0
extern void zcsc_arc_quad_render(void);        // 0x80204254
extern void zcsc_vent_emitter_tick(void);      // 0x80204d8c
extern void zcsc_xfky_set_rendertype(void);    // 0x80204fd4
extern void zcsc_flicker_jitter(void);         // 0x80204d30
extern void zcsc_ambient_loop_tick(void);      // 0x80204c90
extern void zcsc_swivel_object_init(int entity);
extern void zcsc_monitor_object_init(int entity);

// --- reduced-arity / re-ordered typed aliases for wide-ABI ROM helpers ---
// rng_counter_tick is declared void in generated_decls but the ROM returns the
// 32-bit LCG counter in r3.
typedef unsigned int (*GhzRngFn)(void);
static const GhzRngFn __ghz_rng = (GhzRngFn)&rng_counter_tick;

// sound_update_3d_source ROM order is (range, pitch, vol, handle, position,
// sampleId, flags) - distinct from the generated_decls prototype ordering.
typedef void (*GhzSound3dFn)(double range, double pitch, double vol,
                             void* handle, void* position, int sampleId, int flags);
static const GhzSound3dFn __ghz_sound3d = (GhzSound3dFn)&sound_update_3d_source;

// anim_event_scan_active is declared (int,u16) but the ROM call takes no args and
// returns the fired-event count.
typedef int (*GhzAnimScanFn)(void);
static const GhzAnimScanFn __ghz_anim_event_scan = (GhzAnimScanFn)&anim_event_scan_active;

// entity_model_cache_alloc: reduced to (entity, nodeArg, mode, jumpTable) -> handle.
typedef int (*GhzModelCacheFn)(int entity, int nodeArg, int mode, void* jumpTable);
static const GhzModelCacheFn __ghz_model_cache_alloc = (GhzModelCacheFn)&entity_model_cache_alloc;

// level_actors_register: (bank, nameTable, count).
typedef void (*GhzActorsRegFn)(int bank, void* nameTable, int count);
static const GhzActorsRegFn __ghz_level_actors_register = (GhzActorsRegFn)&level_actors_register;

// particle_def_create: 2 packed floats (as doubles) + def ptr + 5 ints.
typedef void (*GhzPDefFn)(double, double, int, int, int, int, int, int);
static const GhzPDefFn __ghz_particle_def_create = (GhzPDefFn)&particle_def_create;

// particle_emitter_create: 3 doubles + 2 def ptrs + 2 ints.
typedef void (*GhzPEmitFn)(double, double, double, int, int, int, int);
static const GhzPEmitFn __ghz_particle_emitter_create = (GhzPEmitFn)&particle_emitter_create;

// ---------------------------------------------------------------------------
// 0x8020530c  ghz_object_init_dispatch
// Master per-class object-init switch, keyed on the class id at entity+0x94.
// param_9 = entity ptr, param_10 = linked-actor descriptor (its 4CC tag at *[0]).
// ---------------------------------------------------------------------------
void ghz_object_init_dispatch(int entity, int* linkDesc)
{
    float fill = FLOAT_80617ca0;
    int handle;
    unsigned int r;
    float spin;

    switch (*(unsigned short*)(entity + 0x94)) {
    case 0x392:
    case 0x393:
        *(unsigned int*)(entity + 0x28) |= 0x10;
        *(float*)(entity + 0x1d8) = fill;
        *(float*)(entity + 0x1cc) = fill;
        *(unsigned int*)(entity + 0x9c) |= 0x208;
        break;

    case 0x396:
        if (*linkDesc == 0x58464b59) {  // 'KYKF'
            entity_register_for_update(entity, 8, (int)(intptr_t)&zcsc_scanline_rt_draw);
            entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_bind_scanline_rt);
            if (*(int*)(DAT_806127a4 + 0x6c4) == 0) {
                render_target_alloc_scratch(0x40, 0x40, (int*)(DAT_806127a4 + 0x6c4));
            }
            *(int*)(*(int*)(entity + 4) + 0x30) = 0;
        }
        // fall through
    case 0x3b2:
        *(int*)(*(int*)(entity + 4) + 0x30) = 0;
        entity_register_for_update(entity, 10, (int)(intptr_t)&zcsc_monitor_render_passes);
        break;

    case 0x39f:
        *(float*)(entity + 0x1c4) = *(float*)(entity + 0x1c4) * FLOAT_80617d64;
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_spark_pool_tick);
        entity_register_for_update(entity, 10, (int)(intptr_t)&zcsc_spark_pool_render);
        break;

    case 0x3a2:
        if (linkDesc != 0 && *linkDesc == 0x4d475244) {  // 'DRGM'
            zcsc_swivel_object_init(entity);
        }
        break;

    case 0x3a4:
        zcsc_monitor_object_init(entity);
        break;

    case 0x3b4:
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_monitor_cam_tick);
        break;

    case 0x3bd:
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_monitor_cam_tick);
        break;

    case 0x3be:
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_slve_glow_tick);
        entity_register_for_update(entity, 10, (int)(intptr_t)&zcsc_arc_quad_render);
        break;

    case 0x3c3:
        arc_entity_attach(0, entity);
        break;

    case 0x3c5:
        handle = __ghz_model_cache_alloc(entity, (int)linkDesc, 4, &switchdataD_80456ec4);
        *(int*)(entity + 0x7c) = handle;
        *(unsigned int*)(entity + 0x28) |= 4;
        *(int*)(entity + 0x1a0) = 1;
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_vent_emitter_tick);
        entity_register_for_update(entity, 0, (int)(intptr_t)&zcsc_xfky_set_rendertype);
        *(float*)(entity + 0xd0) = FLOAT_80617c0c;
        break;

    case 0x3c8:
        *(unsigned char*)(entity + 0x32) = 2;
        r = __ghz_rng();
        // int->double bias idiom collapses to (double)(uint)r / 2^32 - 0.5, scaled to +/-40 spin
        spin = FLOAT_80617d68 * ((float)((double)r / FLOAT_80617cc0) - FLOAT_80617c94);
        *(float*)(entity + 0xd0) = spin;
        *(float*)(entity + 0xc8) = spin;
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_flicker_jitter);
        break;

    case 0x3c9:
        *(unsigned char*)(entity + 0x32) = 2;
        entity_register_for_update(entity, 3, (int)(intptr_t)&zcsc_ambient_loop_tick);
        break;
    }
}

// ---------------------------------------------------------------------------
// 0x80205598  ghz_install_director_screen_effect
// Install the director-cam screen-effect slot onto the P2/P4 view actor (once)
// and fire payload op 1.
// ---------------------------------------------------------------------------
int ghz_install_director_screen_effect(void)
{
    int* viewActor;
    int actor;
    int slot;

    viewActor = (int*)FUN_8027ea98();
    if (viewActor != 0) {
        FUN_802212b4(1);
        if (*(int*)(DAT_806127e8 + 0x2c) == 0) {
            actor = *viewActor;
            slot = (int)screen_effect_alloc_slot();
            *(int*)(actor + 0x268) = slot;
            screen_effect_init_slot((int*)(actor + 0x268), (int*)actor);
            *(int*)(DAT_80611d74 + 0xe8c) = actor;
            *(int*)(DAT_806127e8 + 0x2c) = *(int*)(DAT_806127e8 + 0x2c) + 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80205614  ghz_set_bird_active
// ---------------------------------------------------------------------------
void ghz_set_bird_active(void)
{
    DAT_806127b0 = 1;
}

// ---------------------------------------------------------------------------
// 0x80205620  ghz_gz2_node_place
// GHZ2 sub-node: place a render node at submesh 0x13 with +/- Y bias, keyed by
// the linked GHZ2 actor's 4CC tag ('js2c'/'js1c' family).
// ---------------------------------------------------------------------------
void ghz_gz2_node_place(int entity)
{
    int* submesh;
    int link;
    float local[3];  // {pos, biased Y, pos}

    link = *(int*)(*(int*)(entity + 0x7c) + 0xbf0);
    if (link == 0) {
        return;
    }
    link = *(int*)(link + 8);
    if (link == 0x48475a32) {  // 'GHZ2'
        submesh = (int*)scene_node_get_submesh(0x13, 1);
        local[0] = *(float*)&submesh[0];
        local[1] = (float)submesh[1] + FLOAT_80617d74;
        local[2] = *(float*)&submesh[2];
        FUN_80365ce0(entity, 0x7363326a, 7,          // 'js2c'
                     *(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x50),
                     (int*)local);
    }
    else if (link < 0x48475a32 && 0x48475a30 < link) {  // 'GHZ1'
        submesh = (int*)scene_node_get_submesh(0x13, 0);
        local[0] = *(float*)&submesh[0];
        local[1] = (float)submesh[1] - FLOAT_80617d74;
        local[2] = *(float*)&submesh[2];
        FUN_80365ce0(entity, 0x7363316a, 1,          // 'js1c'
                     *(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x18),
                     (int*)local);
    }
}

// ---------------------------------------------------------------------------
// 0x8020572c  ghz_gz1_break_render
// GHZ1 per-frame render callback. On the break clip (state 4 for this player's
// active index) resolve the 'js1j' physics island, tear it down, install the
// default render cb, and redraw the strip.
// ---------------------------------------------------------------------------
void ghz_gz1_break_render(void)
{
    int* frame;
    int entity;
    int ctrl;
    int islandIdx;
    int activeIdx;

    frame = (int*)__save_gpr_28();
    islandIdx = -1;
    entity = *frame;
    ctrl = *(int*)(entity + 0x7c);

    if (*(int*)(ctrl + 0xbf0) != 0) {
        activeIdx = player_ai_get_active_index();
        if (*(int*)(*(int*)(ctrl + 0xbf0) + 8) == 0x48475a31 &&    // 'GHZ1'
            *(int*)(*(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x5c) + activeIdx * 4) == 4) {
            islandIdx = subobj_island_find_by_body_b(0x7363316a);  // 'js1j'
            DAT_806127b0 = 0;
            entity_set_render_type(entity, (int)(intptr_t)&FUN_80364f3c);
            entity_register_for_update(entity, 3, 0);
        }
        if (-1 < islandIdx) {
            subobj_island_pool_free(islandIdx);
        }
    }

    if (*(int*)(ctrl + 0x4c) != 0) {
        if (islandIdx < 0) {
            FUN_80364f3c(frame);
        }
        else {
            FUN_80365824();
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80205820  ghz_class_scale_lookup
// Per-class scalar table (class id at *(entity+0x14)+0x34 -> a tuning float).
// Case 0x5d also fires a scripted payload before returning the default.
// ---------------------------------------------------------------------------
double ghz_class_scale_lookup(double a, double b, double c, double d,
                              double e, double f, double g, double h, int entity)
{
    switch (*(int*)(*(int*)(entity + 0x14) + 0x34)) {
    default:                     return (double)FLOAT_80617d98;
    case 1:                      return (double)FLOAT_80617d74;
    case 2:                      return (double)FLOAT_80617d78;
    case 3: case 0x14: case 0x1f: case 0x25: case 0x5e:
                                 return (double)FLOAT_80617d7c;
    case 4:                      return (double)FLOAT_80617d78;
    case 5: case 6: case 7: case 0x13:
                                 return (double)FLOAT_80617d7c;
    case 8:                      return (double)FLOAT_80617d84;
    case 9:                      return (double)FLOAT_80617d88;
    case 10:                     return (double)FLOAT_80617d8c;
    case 0xb:                    return (double)FLOAT_80617d90;
    case 0xc: case 0x29: case 0x2a:
                                 return (double)FLOAT_80617d90;
    case 0x1c: case 0x21:        return (double)FLOAT_80617d74;
    case 0x1d: case 0x22:        return (double)FLOAT_80617d94;
    case 0x1e: case 0x20:        return (double)FLOAT_80617d78;
    case 0x23:                   return (double)FLOAT_80617d94;
    case 0x24: case 0x2b: case 0x2e: case 0x2f: case 0x30: case 0x32: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a:
    case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41:
    case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b:
    case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x52:
    case 0x53: case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
    case 0x5b: case 0x5c: case 0x61: case 0x62:
                                 return (double)FLOAT_80617d7c;
    case 0x26:                   return (double)FLOAT_80617d74;
    case 0x27:                   return (double)FLOAT_80617d78;
    case 0x42:                   return (double)FLOAT_80617d74;
    case 0x5d:
        FUN_80221074((double)FLOAT_80617d80, b, c, d, e, f, g, h);
        return (double)FLOAT_80617d7c;
    case 0x5f:                   return (double)FLOAT_80617d78;
    }
}

// ---------------------------------------------------------------------------
// 0x80205914  ghz_ambient_audio_tick
// Per-frame ambient audio driver over the 0x30 control block (DAT_806127e8):
// pick the loudest per-actor class scale, ramp the primary 3D loop (source 6),
// track the nearest actor for zone-keyed one-shots (table DAT_80457ecc), and ramp
// a secondary 3D loop (sample 0x56e).
// ---------------------------------------------------------------------------
void ghz_ambient_audio_tick(double a, double b, double c, double d,
                            double e, double f, double g, double h)
{
    int iVar;
    int stride;
    int zoneEntity;
    int actor;
    int tableIdx;
    float fVar;
    float dx, dy, dz, inv;
    double intensity;
    double dist;
    int params[7];   // [0..4] init block, [5]=pitch, [6]=vol (contiguous stack layout)
    float paramShared;
    float block0;

    if (DAT_806108e4 == 0) {
        return;
    }

    intensity = (double)FLOAT_80617d98;
    iVar = FUN_80238f8c();
    if (iVar == -1) {
        stride = 0;
        for (iVar = 0; iVar < DAT_80612610; iVar = iVar + 1) {
            if (-1 < *(int*)(DAT_80611d78 + stride + 4)) {
                a = ghz_class_scale_lookup(a, b, c, d, e, f, g, h, DAT_80611d78 + stride);
                if (intensity < a) {
                    intensity = a;
                }
            }
            stride = stride + 0x141c;
        }
    }

    // primary ambient loop, ramped toward the peak intensity.
    // rate = 1.5 * (1 / (float)(int)frameTimeUnit) * frameScale  (int->double bias idiom)
    block0 = FLOAT_80617d9c *
             (FLOAT_80617d7c / (float)(double)(int)DAT_806109c0) * DAT_806108ec;
    scalar_step_toward_snap(intensity, (double)block0, (float*)DAT_806127e8);
    __ghz_sound3d((double)*(float*)DAT_806127e8, (double)FLOAT_80617da0, (double)FLOAT_80617d7c,
                  (void*)(DAT_806127e8 + 4), 0, 6, 0);

    intensity = (double)FLOAT_80617d98;
    if (*(float*)(DAT_806127e8 + 0x14) != 0.0f) {
        actor = (int)*(float*)(DAT_806127e8 + 0x14);
        if (3 < *(int*)(DAT_806127e8 + 0x18)) {
            zoneEntity = soundpad_get_entity_id_from_zone(*(int*)(*(int*)(actor + 0x7c) + 0x678));
            tableIdx = *(int*)(DAT_806127e8 + 0x28) * 0xc;   // byte offset into 0xc-stride table
            fVar = FLOAT_80617da4;
            if (zoneEntity != 0x584 && (zoneEntity <= 0x583 || zoneEntity != 0x435)) {
                inv = FLOAT_80617d7c / DAT_806108ec;
                dy = (*(float*)(actor + 0x68) - *(float*)(DAT_806127e8 + 0x20)) * inv;
                dx = (*(float*)(actor + 0x64) - *(float*)(DAT_806127e8 + 0x1c)) * inv;
                dz = (*(float*)(actor + 0x6c) - *(float*)(DAT_806127e8 + 0x24)) * inv;
                dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
                fVar = (float)(dist / (double)FLOAT_80617da8);
            }
            intensity = (double)fVar;
            if (zoneEntity == *(int*)((char*)&DAT_80457ecc + tableIdx)) {
                paramShared = *(float*)((char*)&DAT_80457ecc + tableIdx + 8);
                sound_init_play_params((int)params);
                params[0] = *(int*)((char*)&DAT_80457ecc + tableIdx + 4);  // sound id
                *(float*)&params[5] = paramShared;   // pitch  (+0x14)
                *(float*)&params[6] = paramShared;   // volume (+0x18)
                sound_play_request(params);
                *(int*)(DAT_806127e8 + 0x28) = *(int*)(DAT_806127e8 + 0x28) + 1;
            }
        }
        *(int*)(DAT_806127e8 + 0x18) = *(int*)(DAT_806127e8 + 0x18) + 1;
        *(float*)(DAT_806127e8 + 0x1c) = *(float*)(actor + 0x64);
        *(float*)(DAT_806127e8 + 0x20) = *(float*)(actor + 0x68);
        *(float*)(DAT_806127e8 + 0x24) = *(float*)(actor + 0x6c);
    }

    // clamp intensity to [-0.1, 1.0]
    dist = intensity;
    if ((double)FLOAT_80617d7c < intensity) {
        dist = (double)FLOAT_80617d7c;
    }
    if ((double)FLOAT_80617d98 <= dist) {
        if ((double)FLOAT_80617d7c < intensity) {
            intensity = (double)FLOAT_80617d7c;
        }
    }
    else {
        intensity = (double)FLOAT_80617d98;
    }

    // secondary ambient loop (sample 0x56e), ramped
    block0 = FLOAT_80617dac *
             (FLOAT_80617d7c / (float)(double)(int)DAT_806109c0) * DAT_806108ec;
    scalar_step_toward_snap(intensity, (double)block0, (float*)(DAT_806127e8 + 8));
    __ghz_sound3d((double)(FLOAT_80617d9c * *(float*)(DAT_806127e8 + 8)), (double)FLOAT_80617da0,
                  (double)(FLOAT_80617d74 * *(float*)(DAT_806127e8 + 8) + FLOAT_80617d78),
                  (void*)(DAT_806127e8 + 0xc), 0, 0x56e, 1);
}

// ---------------------------------------------------------------------------
// 0x80205c1c  ghz_level_preload_alloc
// Alloc the 0x30 control block + 0x80x0x80 scratch RT, preload ~14 models, clear
// 7 sound banks. (from level_preload_all_resources)
// ---------------------------------------------------------------------------
void ghz_level_preload_alloc(void)
{
    DAT_806127e8 = mem_alloc(0x30, 1);
    render_target_alloc_scratch(0x80, 0x80, &DAT_806127e0);

    entity_preload_resources(0x23c);
    entity_preload_resources(0x240);
    entity_preload_resources(0x243);
    entity_preload_resources(0x23e);
    entity_preload_resources(0x245);
    entity_preload_resources(0x1fa);
    entity_preload_resources(0x24a);
    entity_preload_resources(0x24c);
    entity_preload_resources(0x249);
    entity_preload_resources(0x246);
    entity_preload_resources(0x24b);
    entity_preload_resources(0x5cb);
    entity_preload_resources(0xe5);
    sprite_load_texture_set();

    sound_clear_bank_ref(0x56e);
    sound_clear_bank_ref(0x51);
    sound_clear_bank_ref(0x631);
    sound_clear_bank_ref(0x649);
    sound_clear_bank_ref(0x64a);
    sound_clear_bank_ref(0x145);
    sound_clear_bank_ref(0x1622);
}

// ---------------------------------------------------------------------------
// 0x80205d00  ghz_master_update
// Per-frame master driver: per-player zone->payload gate chain, bind the
// HTBJ/INAJ team actors + GHZ1/GHZ2 render callbacks, run the water/mode
// sub-ticks, then the ambient audio.
// ---------------------------------------------------------------------------
void ghz_master_update(int unused0, double b, double c, double d,
                       double e, double f, double g, double h)
{
    int ctrl;
    unsigned int gate;
    int zoneNode;
    int zoneId;
    int i;
    int stride;
    int actor;

    (void)FUN_803826f0();
    FLOAT_806127dc = FLOAT_80617d80;

    stride = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        ctrl = *(int*)(DAT_80611d78 + stride + 0x14);
        if ((double)FLOAT_80617d80 < (double)*(float*)(ctrl + 0x1cc)) {
            zoneNode = *(int*)(*(int*)(ctrl + 0x7c) + 0x678);

            if (zoneNode == soundpad_find_zone_by_id(0x3f4)) {
                ghz_install_director_screen_effect();
            }
            if (zoneNode == soundpad_find_zone_by_id(0x43a) ||
                zoneNode == soundpad_find_zone_by_id(0x456)) {
                FUN_80221074((double)FLOAT_80617d80, b, c, d, e, f, g, h);
            }
            if (zoneNode == soundpad_find_zone_by_id(0x43c) ||
                zoneNode == soundpad_find_zone_by_id(0x458)) {
                FUN_80221074((double)FLOAT_80617d80, b, c, d, e, f, g, h);
            }
            if (zoneNode == soundpad_find_zone_by_id(0x43e) ||
                zoneNode == soundpad_find_zone_by_id(0x45b)) {
                FUN_80221074((double)FLOAT_80617d80, b, c, d, e, f, g, h);
            }
            if (zoneNode == soundpad_find_zone_by_id(0x43f) ||
                zoneNode == soundpad_find_zone_by_id(0x45c)) {
                FUN_80221074((double)FLOAT_80617d80, b, c, d, e, f, g, h);
            }
            if (zoneNode == soundpad_find_zone_by_id(0x441) ||
                zoneNode == soundpad_find_zone_by_id(0x45e)) {
                FUN_80221074((double)FLOAT_80617d80, b, c, d, e, f, g, h);
            }
            if (zoneNode == soundpad_find_zone_by_id(0x448)) {
                FUN_802213b4(4);
                FUN_802213b4(5);
                FUN_802213b4(6);
                FUN_802213b4(7);
                FUN_802213b4(8);
            }
        }
        stride = stride + 0x141c;
    }

    // team actor 'HTBJ' -> render cb FUN_80206ad0 (defined in a later batch)
    actor = aiactor_find_player_by_team(0x4a425448);
    if (actor != 0 && *(int*)(actor + 0x160) == 0) {
        entity_register_for_update(actor, 3, -0x7fdf9b30);   // 0x80206ad0
    }
    // team actor 'INAJ' -> render cb FUN_80206bd0 (defined in a later batch)
    actor = aiactor_find_player_by_team(0x4a414e49);
    if (actor != 0 && *(int*)(actor + 0x160) == 0) {
        entity_register_for_update(actor, 3, -0x7fdf9a30);   // 0x80206bd0
    }

    // GHZ1 render callback (gated by DAT_806127b0)
    actor = aiactor_find_entity_by_team(0x48475a31, 2);
    if (DAT_806127b0 != 0 && actor != 0) {
        if (*(int*)(actor + 0x160) == 0) {
            entity_register_for_update(actor, 3, (int)(intptr_t)&ghz_gz2_node_place);
        }
        entity_set_render_type(actor, (int)(intptr_t)&ghz_gz1_break_render);
    }
    // GHZ2 render callback
    actor = aiactor_find_entity_by_team(0x48475a32, 2);
    if (actor != 0) {
        if (*(int*)(actor + 0x160) == 0) {
            entity_register_for_update(actor, 3, (int)(intptr_t)&ghz_gz2_node_place);
        }
        entity_set_render_type(actor, (int)(intptr_t)&ghz_gz1_break_render);
    }

    // water/flow sub-tick (single-player or <2 players)
    if (((DAT_805015bd != '\n') || (DAT_80612610 < 2)) && DAT_80611d74 != 0 &&
        FUN_80220f70(1) != 0 && (gate = (unsigned int)FUN_80220f30(1)) == 0) {
        FUN_8020a9d0(0.0, b, c, d, e, f, g, h);
    }

    // mode-0x12 sub-tick
    if (DAT_80611d74 != 0 && *(int*)(DAT_80611d74 + 0xdec) == 0x12) {
        FUN_8020a53c();
    }

    ghz_ambient_audio_tick(0.0, b, c, d, e, f, g, h);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80206028  ghz_level_init
// Level init: reset the 0x30 control block, register the two actor banks, build
// 8 HUD objective slots (strings 0x1275..0x1278), and create 2 spark particle
// defs + emitters (disabled).
// ---------------------------------------------------------------------------
void ghz_level_init(void)
{
    int emitterA;
    int emitterB;
    float posVec[3];
    float dirVec[3];

    memset_wrapper(DAT_806127e8, 0, 0x30);
    FLOAT_806127dc = FLOAT_80617d80;
    *(int*)(DAT_806127e8 + 4)   = -1;
    *(int*)(DAT_806127e8 + 0xc) = -1;
    *(int*)(DAT_806127e8 + 0x10) = -1;
    DAT_806127d8 = 0;

    __ghz_level_actors_register(0, &DAT_80457b50, 8);
    __ghz_level_actors_register(1, &DAT_80457c80, 8);
    level_resource_pool_setup();
    FUN_8036fcf4(2);

    FUN_802215a8(0, 0x1275, 0, 0, 0);
    FUN_802217d4(&DAT_80205594);
    FUN_8022126c(DAT_806128a0);
    if ((DAT_805015bd != '\n') || (DAT_80612610 < 2)) {
        FUN_802215a8(1, 0x1276, 0, 0, 0);
        FUN_8022126c(DAT_806128a0);
    }
    FUN_802215a8(2, 0x1277, 0, 0, 0);
    FUN_8022126c(DAT_806128a0);
    FUN_802215a8(3, 0x1278, 0, 0, 0);
    FUN_8022126c(DAT_806128a0);
    FUN_802215a8(4, 0, 2, 2, 2);
    FUN_802215a8(5, 0, 2, 2, 2);
    FUN_802215a8(6, 0, 2, 2, 2);
    FUN_802215a8(7, 0, 2, 2, 2);
    FUN_802215a8(8, 0, 2, 2, 2);
    FUN_802215a8(9, 0, 2, 2, 2);
    FUN_802215a8(10, 0, 2, 2, 2);
    FUN_802215a8(0xb, 0, 2, 2, 2);
    FUN_8020a9a4();
    ghz_set_bird_active();

    // spark particle def/emitter A
    particle_handle_clear((unsigned int*)&DAT_806127d4);
    __ghz_particle_def_create((double)FLOAT_80617d7c, (double)FLOAT_80617d7c,
                              (int)&DAT_805c62a0, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_80617d88, FLOAT_80617d80, FLOAT_80617d80,
                                    FLOAT_80617d74, 0x70707fff, 0x7f7f7fff, 0);
    particle_def_set_size_start_end(FLOAT_80617db8, FLOAT_80617dbc, FLOAT_80617dc0, FLOAT_80617dc0);
    particle_def_set_color_end(FLOAT_80617dc4, FLOAT_80617d78, FLOAT_80617dc8, FLOAT_80617d80);
    particle_def_set_velocity_ranges(FLOAT_80617d80, FLOAT_80617d80, FLOAT_80617dcc,
                                     FLOAT_80617dc0, FLOAT_80617d7c, FLOAT_80617d70);
    particle_def_set_drag(FLOAT_80617dd0);
    __ghz_particle_emitter_create((double)FLOAT_80617d70, (double)FLOAT_80617d80,
                                  (double)FLOAT_80617dd4, (int)&DAT_805c636c, (int)&DAT_805c62a0,
                                  1, 0);
    particle_emitter_set_color_range(FLOAT_80617dd8, FLOAT_80617d80, FLOAT_80617ddc, FLOAT_80617d80);
    particle_emitter_set_lifetime_range(FLOAT_80617d74, FLOAT_80617d80);

    // spark particle def/emitter B
    particle_handle_clear((unsigned int*)&DAT_806127d0);
    __ghz_particle_def_create((double)FLOAT_80617d7c, (double)FLOAT_80617d7c,
                              (int)&DAT_805c63f0, 0, 0, 1, 0x307, 0);
    particle_def_set_size_keyframes(FLOAT_80617d88, FLOAT_80617d80, FLOAT_80617d80,
                                    FLOAT_80617d74, 0x7f4f5f7f, 0x7f4f5f7f, 0);
    particle_def_set_size_start_end(FLOAT_80617db8, FLOAT_80617dbc, FLOAT_80617dc0, FLOAT_80617dc0);
    particle_def_set_color_end(FLOAT_80617dc4, FLOAT_80617d78, FLOAT_80617dc8, FLOAT_80617d80);
    particle_def_set_velocity_ranges(FLOAT_80617d80, FLOAT_80617d80, FLOAT_80617d7c,
                                     FLOAT_80617dc0, FLOAT_80617d7c, FLOAT_80617d7c);
    particle_def_set_drag(FLOAT_80617dd0);
    __ghz_particle_emitter_create((double)FLOAT_80617d70, (double)FLOAT_80617d80,
                                  (double)FLOAT_80617dd4, (int)&DAT_805c64bc, (int)&DAT_805c63f0,
                                  0, 0);
    particle_emitter_set_color_range(FLOAT_80617de0, FLOAT_80617d80, FLOAT_80617de4, FLOAT_80617d80);
    particle_emitter_set_lifetime_range(FLOAT_80617d7c, FLOAT_80617d80);

    posVec[0] = *(float*)&DAT_803dfd90;
    posVec[1] = *(float*)&DAT_803dfd94;
    posVec[2] = *(float*)&DAT_803dfd98;
    dirVec[0] = *(float*)&DAT_803dfd9c;
    dirVec[1] = *(float*)&DAT_803dfda0;
    dirVec[2] = *(float*)&DAT_803dfda4;

    particle_emitter_spawn((unsigned int)&DAT_806127d4, (unsigned int)&DAT_805c636c, 0,
                           posVec, dirVec);
    emitterA = DAT_806127d4;
    particle_cached_disable((ParticleEmitter**)&emitterA);
    particle_emitter_spawn((unsigned int)&DAT_806127d0, (unsigned int)&DAT_805c64bc, 0,
                           posVec, dirVec);
    emitterB = DAT_806127d0;
    particle_cached_disable((ParticleEmitter**)&emitterB);
}

// ===========================================================================
// batch_648  (0x802064d0 - 0x802073a4)
// GHZ set-piece continuation: two anim-event-driven state handlers, a pair of
// audio-ramp thunks, the big GHZ scanline/panel render callback, a break-piece
// spawn+tick update, and the reactor-blast point-pool alloc/free/menu-install.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x802064d0  ghz_anim_event_state_setter
// Anim-event-driven state setter: when the active anim clip fires an event, map
// the clip id to a new GHZ prop state word (entity+0x7c +0x94) and clear/arm the
// matching flag bytes. Clip 0x1fe additionally accumulates a sound slot.
// ---------------------------------------------------------------------------
void ghz_anim_event_state_setter(int entity)
{
    int* clip;
    int ctrl;

    if (__ghz_anim_event_scan() == 0) {
        return;
    }
    clip = (int*)anim_active_clip_slot(entity);

    if (*clip == 0x22d || *clip == 0x17f) {
        ctrl = *(int*)(entity + 0x7c);
        *(unsigned int*)(ctrl + 0x94) = 0x3e;
        *(unsigned char*)(ctrl + 0x54c) = 0;
    }
    if (*clip == 0x22e || *clip == 0x187) {
        ctrl = *(int*)(entity + 0x7c);
        *(unsigned char*)(ctrl + 0x58a) = 0;
        *(unsigned int*)(ctrl + 0x94) = 0;
    }
    if (*clip == 0x1ff) {
        ctrl = *(int*)(entity + 0x7c);
        *(unsigned char*)(ctrl + 0x581) = 0;
        *(unsigned int*)(ctrl + 0x94) = 0;
    }
    if (*clip == 0x1fe) {
        ctrl = *(int*)(entity + 0x7c);
        *(unsigned int*)(ctrl + 0x94) = 0x35;
        *(unsigned char*)(ctrl + 0x54c) = 0;
        object_sound_slot_accumulate(ctrl, 9, 1000);
    }
}

// ---------------------------------------------------------------------------
// 0x802065d0  ghz_anim_break_spawn
// On the break anim clip (id 0x233) fire two event edges: the first sets the
// prop state to 0x3f, the second spawns a break piece (asset 0x751) and queues a
// delayed weapon event, then clears the per-actor pending flags.
// ---------------------------------------------------------------------------
void ghz_anim_break_spawn(int entity)
{
    int* clip;
    int ctrl;
    int piece;
    double seed;

    clip = (int*)anim_active_clip_slot(entity);
    if (*clip != 0x233) {
        return;
    }

    if (__ghz_anim_event_scan() != 0) {
        ctrl = *(int*)(entity + 0x7c);
        *(unsigned int*)(ctrl + 0x94) = 0x3f;
        *(unsigned char*)(ctrl + 0x54c) = 0;
    }
    if (__ghz_anim_event_scan() != 0) {
        clip = *(int**)(entity + 0x7c);
        seed = (double)FLOAT_80617d80;
        piece = __ghz_spawn_break_piece(seed, seed, seed, (double)FLOAT_80617de8,
                                        0, 0, 0, 0,
                                        *(int*)(*clip + 4), 8, 0x751, 0, 0x11, 5.877472e-39f);
        if (piece != 0) {
            weapon_start_delayed_event((double)FLOAT_80617dec, piece, 8);
        }
        clip[0x25] = 0;
        *(unsigned char*)((int)clip + 0x58b) = 0;
    }
}

// ---------------------------------------------------------------------------
// 0x8020669c  ghz_audio_ramp_a  /  0x802066c4  ghz_audio_ramp_b
// Two GHZ audio-ramp thunks that push the shared 0.5 base level + scratch handle
// (DAT_806127e4) into the two sound-ramp entry points.
// ---------------------------------------------------------------------------
void ghz_audio_ramp_a(void)
{
    FUN_80355574((double)FLOAT_80617d74, DAT_806127e4);
}

void ghz_audio_ramp_b(void)
{
    FUN_8035560c((double)FLOAT_80617d74, DAT_806127e4);
}

// ---------------------------------------------------------------------------
// 0x802066ec  ghz_panel_scanline_render
// GHZ panel/scanline render callback (subsystem:graphics). Resolves the linked
// 'V...' 4CC panel actor, animates the panel's scanline offset (+0x1a4) with rng
// jitter, rebuilds the collision strip face for the panel type at +0x1a0, and on
// the sprite-0x333 case builds a jittered scanline transform. The trailing
// transform block applies scale/translate matrices keyed by the panel style
// nibble (+0x1a0 & 0xf0000) into the strip face entry.
// ---------------------------------------------------------------------------
void ghz_panel_scanline_render(void)
{
    bool identityXform;
    float fVar;
    unsigned short state;
    int panel;
    int linked;
    unsigned int* pool;
    unsigned int* poolIt;
    unsigned int mtxHandle;
    int i;
    float* mtx;
    int faceIdx;
    unsigned int flags;
    int strip;
    int faceCount;
    double dvA, dvB;
    int spawnPt;
    int vbCount;

    panel = FUN_803826e8();
    mtxHandle = 0;
    faceCount = -1;   // iVar13 in decomp (strip slot index)
    faceIdx = -1;     // iVar11 in decomp (face row index)

    // resolve the paired 'V..0' panel actor and copy its +0x1a0/+0x1a4 panel words
    if (((*(unsigned int*)(panel + 0x60) & 0xff000000) == 0x56000000) &&
        (linked = FUN_8023c32c((*(unsigned int*)(panel + 0x60) & 0xff0000) | 0x56003030),
         linked != 0)) {
        *(int*)(panel + 0x1a4) = *(int*)(linked + 0x1a4);
        *(int*)(panel + 0x1a0) = *(int*)(linked + 0x1a0);
    }
    if (*(unsigned int*)(panel + 0x1a0) == 0) {
        FUN_8038273c();
        return;
    }

    // pick the face row/slot from the panel state word (+0x94)
    state = *(unsigned short*)(panel + 0x94);
    if (state == 0x1ee) {
        flags = *(unsigned int*)(*(int*)(panel + 4) + 0x30);
        if      (flags & 1)    mtxHandle = 2;
        else if (flags & 2)    mtxHandle = 3;
        else if (flags & 4)    mtxHandle = 4;
        else if (flags & 8)    mtxHandle = 5;
        else if (flags & 0x10) mtxHandle = 6;
        faceIdx = 3;
        faceCount = 3;
    }
    else if (state == 0x1eb) {
        flags = *(unsigned int*)(*(int*)(panel + 4) + 0x30);
        if      (flags & 1)    mtxHandle = 1;
        else if (flags & 2)    mtxHandle = 2;
        else if (flags & 4)    mtxHandle = 3;
        else if (flags & 8)    mtxHandle = 4;
        else if (flags & 0x10) mtxHandle = 5;
        else if (flags & 0x20) mtxHandle = 6;
        else if (flags & 0x40) mtxHandle = 7;
        faceIdx = 4;
        faceCount = 4;
    }

    // scanline offset animation (+0x1a4), rate from the frame-time ratio
    if ((*(unsigned int*)(panel + 0x1a0) & 0xff) == 1) {
        fVar = *(float*)(panel + 0x1a4);
        float rate = (FLOAT_80617d7c / (float)((double)(int)DAT_806109c0 - DOUBLE_80617db0)) *
                     (float)((double)(int)DAT_806108e4 - DOUBLE_80617db0);
        if (fVar <= FLOAT_80617d7c) {
            *(float*)(panel + 0x1a4) = -(rate - fVar);
        }
        else {
            *(float*)(panel + 0x1a4) = rate + fVar;
        }
        // clamp / rng re-seed of the scanline offset
        if (*(float*)(panel + 0x1a4) <= FLOAT_80617dcc) {
            if (*(float*)(panel + 0x1a4) < FLOAT_80617d80) {
                unsigned int r = __ghz_rng();
                *(float*)(panel + 0x1a4) =
                    -(FLOAT_80617dc0 *
                      ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0) - FLOAT_80617dcc);
            }
        }
        else {
            unsigned int r = __ghz_rng();
            *(float*)(panel + 0x1a4) =
                FLOAT_80617dc0 * ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0);
        }
        // when the offset dips below 1.0, clear the panel vertex buffers
        if (*(float*)(panel + 0x1a4) < FLOAT_80617d7c) {
            __ghz_collision_mesh_stats(*(int*)(panel + 4), mtxHandle, 0, 4);
            pool = (unsigned int*)__ghz_collision_alloc_vb(
                       *(int*)(panel + 4), mtxHandle, 0, 4, (int*)0, (int*)0, &vbCount);
            poolIt = pool;
            for (i = 0; i < vbCount; i = i + 1) {
                *poolIt = 0;
                poolIt = poolIt + 1;
            }
            FUN_80399500((unsigned int)pool, vbCount << 2);
        }
    }

    if ((*(unsigned int*)(panel + 0x1a0) & 0xff00) != 0) {
        *(unsigned int*)(*(int*)(panel + 4) + 0x30) =
            (*(unsigned int*)(*(int*)(panel + 4) + 0x30) & 0x80) | 1;
    }

    // panel-type -> strip face entry install
    flags = *(unsigned int*)(panel + 0x1a0) & 0xff00;
    if (flags == 0x300) {
        strip = collision_alloc_strip_table((int*)*(int*)(panel + 4));
        *(unsigned int*)(strip + faceIdx * 0x10) = 0xfffffffb;
    }
    else if (flags == 0x200) {
        strip = collision_alloc_strip_table((int*)*(int*)(panel + 4));
        *(int*)(strip + faceIdx * 0x10) = DAT_806127e0;
        DAT_806127b4 = 1;
    }
    else if (flags == 0x100) {
        mtxHandle = (unsigned int)__ghz_mtx4_pool_alloc();
        strip = collision_alloc_strip_table((int*)*(int*)(panel + 4));
        *(int*)(strip + faceIdx * 0x10) = sprite_get_texture_id(0x333);
        i = *(int*)(*(int*)(panel + 4) + 0x1e8);
        if (i != 0 && (*(unsigned int*)(i + faceCount * 0x30 + 0x1c) & 0x8000) != 0) {
            unsigned int r0 = __ghz_rng();
            dvA = (double)((float)((double)(int)r0 - DOUBLE_80617df8) / FLOAT_80617df0);
            unsigned int r1 = __ghz_rng();
            dvB = (double)((float)((double)(int)r1 - DOUBLE_80617df8) / FLOAT_80617df0);
            __ghz_mtx4_set_translation(dvB, dvA, (double)FLOAT_80617d80);
            collision_set_face_entry(*(int*)(panel + 4), faceCount, mtxHandle);
        }
    }
    else if (flags == 0x400) {
        strip = collision_alloc_strip_table((int*)*(int*)(panel + 4));
        spawnPt = spawn_point_find_by_entity(0x4d565453);   // 'VTSM'
        spectator_set_mode_if_ready((double)FLOAT_80617dc0, spawnPt);
        *(unsigned int*)(strip + faceIdx * 0x10) = spectator_get_render_callback();
    }

    // trailing per-style transform build into the face entry (skip the 0x100 case)
    if ((*(unsigned int*)(panel + 0x1a0) & 0xff00) != 0x100 &&
        (i = *(int*)(*(int*)(panel + 4) + 0x1e8), i != 0) &&
        (*(unsigned int*)(i + faceCount * 0x30 + 0x1c) & 0x8000) != 0) {

        float base[17];
        identityXform = (*(unsigned int*)(panel + 0x1a0) & 0xf00000) != 0x800000;
        if (identityXform) {
            mtx4_set_identity((int)base);
        }
        else {
            unsigned int cls = *(unsigned int*)(panel + 0x60);
            double yBias = (double)(float)((double)(int)((cls >> 8 & 0xff) - 0x30) - DOUBLE_80617db0);
            __ghz_mtx4_set_scale((double)FLOAT_80617d74, (double)FLOAT_80617d74, (double)FLOAT_80617d7c);
            double xBias = (double)(int)((cls & 0xff) - 0x30) - DOUBLE_80617db0;
            mtx4_write_translation((double)(float)(yBias * (double)FLOAT_80617d74),
                                   (double)(float)((double)(float)xBias * (double)FLOAT_80617d74),
                                   (double)FLOAT_80617d80, (int)base);
        }

        flags = *(unsigned int*)(panel + 0x1a0) & 0xf0000;
        if (flags == 0x30000) {
            mtx = (float*)__ghz_mtx4_pool_alloc();
            double denom = (double)(int)DAT_806109c0 - DOUBLE_80617db0;
            double num   = (double)(int)DAT_80611928 - DOUBLE_80617db0;
            double ratio = (double)(int)((int)DAT_80611928 / (int)DAT_806109c0) - DOUBLE_80617db0;
            double disp = (double)(FLOAT_80617dd4 *
                          (((float)num * (FLOAT_80617d7c / (float)denom)) - (float)ratio) -
                          FLOAT_80617d74);
            mtx4_set_identity((int)mtx);
            if (((int)DAT_80611928 / (int)DAT_806109c0) & 1) {
                disp = -disp;
            }
            mtx[4] = (float)((double)FLOAT_80617df4 * disp);
            mtx4_write_translation(disp, (double)FLOAT_80617d80, (double)FLOAT_80617d80, (int)mtx);
            if (!identityXform) {
                mtx4_multiply(base, mtx, mtx);
            }
            collision_set_face_entry(*(int*)(panel + 4), faceCount, (unsigned int)(intptr_t)mtx);
        }
        else if (flags == 0x20000) {
            mtx = (float*)__ghz_mtx4_pool_alloc();
            unsigned int r = __ghz_rng();
            double disp = (double)(FLOAT_80617dc0 *
                          ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74));
            mtx4_set_identity((int)mtx);
            mtx[4] = (float)((double)FLOAT_80617df4 * disp);
            mtx4_write_translation(disp, (double)FLOAT_80617d80, (double)FLOAT_80617d80, (int)mtx);
            if (!identityXform) {
                mtx4_multiply(base, mtx, mtx);
            }
            collision_set_face_entry(*(int*)(panel + 4), faceCount, (unsigned int)(intptr_t)mtx);
        }
        else if (flags == 0x10000) {
            mtx = (float*)__ghz_mtx4_pool_alloc();
            double denom = (double)(int)DAT_806109c0 - DOUBLE_80617db0;
            double num   = (double)(int)DAT_80611928 - DOUBLE_80617db0;
            double ratio = (double)(int)((int)DAT_80611928 / (int)DAT_806109c0) - DOUBLE_80617db0;
            double scaleY = (double)(FLOAT_80617d94 *
                            ((float)num * (float)((double)FLOAT_80617d7c / denom) - (float)ratio) +
                            FLOAT_80617d78);
            __ghz_mtx4_set_scale((double)FLOAT_80617d7c, scaleY, (double)FLOAT_80617d7c);
            mtx4_write_translation((double)FLOAT_80617d80,
                                   (double)(FLOAT_80617d74 * (float)((double)FLOAT_80617d7c - scaleY)),
                                   (double)FLOAT_80617d80, (int)mtx);
            if (!identityXform) {
                mtx4_multiply(base, mtx, mtx);
            }
            collision_set_face_entry(*(int*)(panel + 4), faceCount, (unsigned int)(intptr_t)mtx);
        }
        else if (flags == 0x40000) {
            mtx = (float*)__ghz_mtx4_pool_alloc();
            double denom = (double)(int)DAT_806109c0 - DOUBLE_80617db0;
            double num   = (double)(int)DAT_80611928 - DOUBLE_80617db0;
            double ratio = (double)(int)((int)DAT_80611928 / (int)DAT_806109c0) - DOUBLE_80617db0;
            __ghz_mtx4_set_translation((double)FLOAT_80617d80,
                                 (double)((float)num * (FLOAT_80617d7c / (float)denom) - (float)ratio),
                                 (double)FLOAT_80617d80);
            if (!identityXform) {
                mtx4_multiply(base, mtx, mtx);
            }
            collision_set_face_entry(*(int*)(panel + 4), faceCount, (unsigned int)(intptr_t)mtx);
        }
        else if (!identityXform) {
            mtx = (float*)__ghz_mtx4_pool_alloc();
            mtx4_copy(base, mtx);
            collision_set_face_entry(*(int*)(panel + 4), faceCount, (unsigned int)(intptr_t)mtx);
        }
    }

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80206fb8  ghz_break_piece_update
// GHZ break-piece spawn + per-frame tick. On first touch (subobj +0x80 == 0) it
// allocates a subobject, builds its default descriptor, plays a spawn sound
// (sample 0x55c), and (rng-gated, states 0x1e9/0x1eb) seeds a physics island
// offset from the piece origin. Thereafter it ticks the break simulation.
// ---------------------------------------------------------------------------
void ghz_break_piece_update(unsigned long long param_1, double p2, double p3, double p4,
                            double p5, double p6, double p7, unsigned long long p8)
{
    int* frame;
    int entity;
    int sub;
    unsigned int r;
    float* mtx;
    int soundParams[7];
    int soundHdr;
    float islandPt[3];
    float xform[23];

    (void)param_1;
    frame = (int*)FUN_803826f0();
    entity = *frame;
    if (entity == 0) {
        FUN_8038273c();
        return;
    }

    if (*(int*)(entity + 0x80) == 0) {
        subobj_redirty_in_aabb_wrapper();
        sub = __ghz_subobj_pool_alloc();
        *(int*)(entity + 0x80) = sub;
        if (sub != 0) {
            __ghz_build_descriptor_default(0, p2, p3, p4, p5, p6, p7, p8,
                                           *(int*)(entity + 0x80), entity, 1, -1, 4.30479e-42f);
        }

        sound_init_play_params((int)soundParams);
        soundHdr = 1;
        soundParams[0] = 0x55c;
        soundParams[1] = *(int*)(entity + 0x64);
        soundParams[2] = *(int*)(entity + 0x68);
        soundParams[3] = *(int*)(entity + 0x6c);
        *(char*)((char*)soundParams + 0x1c) = (char)soundHdr;
        sound_play_request(soundParams);

        if (*(int*)(entity + 0x80) != 0 &&
            (r = __ghz_rng(), 8 < r % 10) &&
            (*(short*)(entity + 0x94) == 0x1e9 || *(short*)(entity + 0x94) == 0x1eb)) {
            mtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
            mtx4_copy(mtx, xform);
            islandPt[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1a8);
            islandPt[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
            islandPt[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
            mtx4_write_translation((double)islandPt[0], (double)islandPt[1], (double)islandPt[2],
                                   (int)xform);
            islandPt[0] = FLOAT_80617e00;
            islandPt[1] = FLOAT_80617d90;
            islandPt[2] = FLOAT_80617d80;
            mtx4_transform_point(xform, islandPt);
            sub = __ghz_island_create_point(*(unsigned int*)(entity + 0x80), 0, islandPt);
            subobj_island_set_restitution((double)FLOAT_80617e04, sub);
            subobj_island_set_drift_gain((double)FLOAT_80617d8c, sub);
            subobj_dirty_list_add(*(unsigned int*)(entity + 0x80));
        }

        entity_register_for_update(entity, 3, 0);
        entity_register_for_update(entity, 8, 0);
        *(int*)(*(int*)(entity + 4) + 0x30) = 0;
    }

    if (*(int*)(entity + 0x80) != 0) {
        subobj_break_tick_impl(0, p2, p3, p4, p5, p6, p7, p8);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8020718c  ghz_point_pool_free
// Free the reactor-blast point pool and clear its live count.
// ---------------------------------------------------------------------------
void ghz_point_pool_free(void)
{
    block_free_simple((int)DAT_806127b8);
    DAT_806127cc = 0;
    DAT_806127b8 = 0;
}

// ---------------------------------------------------------------------------
// 0x802071cc  ghz_point_pool_free_on_input
// Button-held gate: when the menu pad's held-buttons mask matches the request
// mask (+0x24), free the point pool (menu-dismiss callback).
// ---------------------------------------------------------------------------
void ghz_point_pool_free_on_input(int param_1, int* req)
{
    unsigned int held;

    (void)param_1;
    held = input_get_buttons_held(*req);
    if ((req[9] & held) != 0) {
        ghz_point_pool_free();
    }
}

// ---------------------------------------------------------------------------
// 0x8020720c  ghz_point_pool_alloc
// Allocate the ~6000-byte reactor-blast point pool and seed each 0x14-stride
// entry: xy in [-delay, +?] via rng, a normalised z, a vertical velocity gain,
// and a fixed 5th word (1.4013e-45 = 1 as float bit pattern).
// ---------------------------------------------------------------------------
void ghz_point_pool_alloc(void)
{
    unsigned int r;
    float* pt;
    int i;
    int off;
    double dDelay, dSpanInv, dHalf, dQuarter, dGain, dBias;

    __save_gpr_28();
    DAT_806127b8 = __ghz_block_alloc(6000, 1, &DAT_806127b8, GHZ_LAB_802071bc);
    DAT_806127cc = (DAT_806127b8 != (int*)0) ? 1 : 0;

    dDelay   = (double)FLOAT_80617dec;
    dSpanInv = (double)FLOAT_80617df0;
    dHalf    = (double)FLOAT_80617d74;
    dQuarter = (double)FLOAT_80617d78;
    dGain    = (double)FLOAT_80617e08;
    dBias    = DOUBLE_80617df8;

    off = 0;
    for (i = 0; i < (int)DAT_806127cc; i = i + 1) {
        pt = (float*)((int)DAT_806127b8 + off);
        r = __ghz_rng();
        pt[0] = (float)((double)(float)((double)(int)(r % 0x14) - dBias) - dDelay);
        r = __ghz_rng();
        pt[1] = (float)((double)(float)((double)(int)(r % 0x14) - dBias) - dDelay);
        r = __ghz_rng();
        pt[2] = (float)((double)(float)((double)(int)r - dBias) / dSpanInv);
        r = __ghz_rng();
        pt[3] = (float)(dGain * (double)(float)((double)(float)((double)(float)((double)(int)r - dBias)
                                                                / dSpanInv) * dHalf + dQuarter));
        pt[4] = 1.4013e-45f;   // 0x00000001 as float
        off = off + 0x14;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802073a4  ghz_reactor_blast_menu
// Reactor-blast finale: single-player (or <2 players), when the player has a live
// object at +0x150 whose type word (+0x114) is not 0x100, alloc the point pool,
// resolve the 'VTSM' spawn point, and enter the menu state that dismisses on
// input (ghz_point_pool_free_on_input).
// ---------------------------------------------------------------------------
void ghz_reactor_blast_menu(int param_1, int player)
{
    int spawnPt;

    (void)param_1;
    if (((DAT_805015bd != '\n') || (DAT_80612610 < 2)) && player != 0 &&
        *(int*)(player + 0x150) != 0 &&
        *(int*)(*(int*)(player + 0x150) + 0x114) != 0x100) {
        ghz_point_pool_alloc();
        spawnPt = spawn_point_find_by_entity(0x4d565453);   // 'VTSM'
        player_enter_menu_state(*(unsigned int*)(player + 0x150), (unsigned int)spawnPt,
                                (unsigned int)(intptr_t)&ghz_point_pool_free_on_input, 2, 0x22, 0);
    }
}

// ===========================================================================
// batch_649  (0x8020743c - 0x802086ac)
// GHZ set-piece continuation: the reactor-blast point-pool per-frame update +
// GX render, and the 'BELT' (conveyor) path-flow subsystem that pushes physics
// islands along a 4-lane path graph, plus the timed island free/respawn helpers.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8020743c  ghz_point_seed_jitter
// Per-point jitter setter: rng-decides a "big" flag (pt+0x10) with probability
// scaled by the current pool count, and seeds the point's phase/size (pt+0xc)
// from a base gain, optionally rng-scaled when the flag is set.
// ---------------------------------------------------------------------------
void ghz_point_seed_jitter(int pt)
{
    unsigned int r;
    float value;

    r = __ghz_rng();
    *(unsigned int*)(pt + 0x10) =
        ((float)((double)(int)(DAT_806127cc) - DOUBLE_80617db0) / FLOAT_80617e0c <
         (float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0) ? 1u : 0u;

    value = FLOAT_80617e08;
    if (*(int*)(pt + 0x10) != 0) {
        r = __ghz_rng();
        value = FLOAT_80617e08 *
                (((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0) * FLOAT_80617d74 +
                 FLOAT_80617d78);
    }
    *(float*)(pt + 0xc) = value;
}

// ---------------------------------------------------------------------------
// 0x8020750c  ghz_point_pool_render
// Reactor-blast point-pool per-frame update + GX render (subsystem:graphics).
// Fills a clipped backdrop rect, binds sprite 0x3b3, then for each live point:
// integrates + damps, resolves pairwise repulsion (round-robin by frame counter),
// and either (a) draws an 8-vertex sine-warped sprite strip when settled, or
// (b) while unsettled, splits into a new point (rng-seeded), plays sound 0x1622
// with a fade handle, and reseeds; dead points are swapped out. Wraps a wide
// FP-spill frame (FUN_803826d4 / FUN_80382720).
// ---------------------------------------------------------------------------
void ghz_point_pool_render(void)
{
    float fx, fy, d2;
    unsigned int r;
    int matId;
    unsigned int uW;
    unsigned int vW;
    float* pt;
    float* other;
    int j;
    int off;
    int i;
    unsigned int soundHdl;
    int viewH;
    int viewW;
    double amp;
    double v0, v1, v2, v3, v4, v5, v6, colBias;
    int soundParams[5];
    float soundPitch;

    (void)FUN_803826d4();
    viewW = DAT_80611748[2];
    viewH = DAT_80611748[7];
    DAT_806127bc = DAT_806127bc + 1;

    __ghz_gx_color_update();
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_fill_rect_clipped((unsigned int)DAT_80611748[0], (unsigned int)DAT_80611748[5],
                         DAT_80611748[1], DAT_80611748[6], 0x507f);
    matId = sprite_get_texture_id(0x3b3);
    gx_set_active_material(matId);
    matId = sprite_get_texture_id(0x3b3);
    uW = (unsigned int)*(unsigned short*)(DAT_80610a20[0] + matId * 0x58 + 0x4e) << 4;
    matId = sprite_get_texture_id(0x3b3);
    vW = (unsigned int)*(unsigned short*)(DAT_80610a20[0] + matId * 0x58 + 0x52) << 4;

    for (i = 0; i < DAT_806127cc; i = i + 1) {
        pt = (float*)((int)DAT_806127b8 + i * 0x14);
        amp = (double)FLOAT_80617de0;

        // integrate phase with frame-time rate, damp x/y
        pt[3] = -((float)((double)FLOAT_80617d7c /
                          (double)(float)((double)(int)DAT_806109c0 - DOUBLE_80617db0)) *
                  (float)((double)(int)DAT_806108e4 - DOUBLE_80617db0) - pt[3]);
        pt[0] = (float)((double)pt[0] * (double)FLOAT_80617e10);
        pt[1] = (float)((double)pt[1] * (double)FLOAT_80617e10);

        if ((double)FLOAT_80617d80 <= (double)pt[3]) {
            // settled: pairwise repulsion pass (skip same round-robin bucket)
            off = (i + 1) * 0x14;
            for (j = i + 1; j < DAT_806127cc; j = j + 1) {
                if (j % 5 != DAT_806127bc % 5) {
                    other = (float*)((int)DAT_806127b8 + off);
                    fx = pt[0] - other[0];
                    fy = pt[1] - other[1];
                    d2 = fx * fx + fy * fy;
                    if (d2 < FLOAT_80617e18) {
                        if (FLOAT_80617dc4 <= d2) {
                            if (d2 < FLOAT_80617d7c) {
                                d2 = FLOAT_80617d7c;
                            }
                            fx = fx / d2;
                            fy = fy / d2;
                        }
                        else {
                            r = __ghz_rng();
                            fx = FLOAT_80617dc4 *
                                 ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0);
                            r = __ghz_rng();
                            fy = FLOAT_80617dc4 *
                                 ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0);
                        }
                        fx = (float)((double)fx * (double)FLOAT_80617d74);
                        fy = (float)((double)fy * (double)FLOAT_80617d74);
                        pt[0] = pt[0] + fx;
                        pt[1] = pt[1] + fy;
                        other[0] = other[0] - fx;
                        other[1] = other[1] - fy;
                    }
                }
                off = off + 0x14;
            }

            if (pt[4] == 0.0f) {
                amp = (double)(float)(amp * (double)(pt[3] * FLOAT_80617d94));
            }

            // 8-vertex sine-warped sprite strip around the point
            v0 = math_poly_trig((double)((FLOAT_80617e24 + pt[2]) - FLOAT_80617e1c));
            v1 = (double)(float)(amp * v0 + (double)((float)((double)(int)viewH - DOUBLE_80617db0) + pt[1]));
            v0 = math_poly_trig((double)(FLOAT_80617e24 + pt[2]));
            v2 = (double)(float)(amp * v0 + (double)((float)((double)(int)viewW - DOUBLE_80617db0) + pt[0]));
            v0 = math_poly_trig((double)((FLOAT_80617e20 + pt[2]) - FLOAT_80617e1c));
            v3 = (double)(float)(amp * v0 + (double)((float)((double)(int)viewH - DOUBLE_80617db0) + pt[1]));
            v0 = math_poly_trig((double)(FLOAT_80617e20 + pt[2]));
            v4 = (double)(float)(amp * v0 + (double)((float)((double)(int)viewW - DOUBLE_80617db0) + pt[0]));
            v0 = math_poly_trig((double)((FLOAT_80617e1c + pt[2]) - FLOAT_80617e1c));
            v5 = (double)(float)(amp * v0 + (double)((float)((double)(int)viewH - DOUBLE_80617db0) + pt[1]));
            v0 = math_poly_trig((double)(FLOAT_80617e1c + pt[2]));
            v6 = (double)(float)(amp * v0 + (double)((float)((double)(int)viewW - DOUBLE_80617db0) + pt[0]));
            v0 = math_poly_trig((double)(pt[2] - FLOAT_80617e1c));
            colBias = (double)(float)(amp * v0 + (double)((float)((double)(int)viewH - DOUBLE_80617db0) + pt[1]));
            v0 = math_poly_trig((double)pt[2]);
            double lastX = (double)(float)(amp * v0 + (double)((float)((double)(int)viewW - DOUBLE_80617db0) + pt[0]));
            __ghz_draw_sprite_strip4((double)FLOAT_80617dc4, lastX, colBias, v6, v5, v4, v3, v2,
                                     0, 0, uW, 0, 0, vW, uW, vW, (float)v1,
                                     0x3f7f7f3f, 0x3f7f7f3f, 0x3f7f7f3f, 0x3f7f7f3f);
        }
        else if (pt[4] != 0.0f) {
            // unsettled + active: split into a new point + play growth sound
            if (DAT_806127cc < 299) {
                r = __ghz_rng();
                soundPitch = FLOAT_80617d88 *
                             ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0) +
                             FLOAT_80617e14;
                sound_init_play_params((int)soundParams);
                soundParams[0] = 0x1622;
                *(float*)((char*)soundParams + 0x14) = soundPitch;
                soundHdl = sound_play_request(soundParams);
                if ((int)soundHdl >= 0) {
                    sound_register_fade_out((double)FLOAT_80617d7c, (double)FLOAT_80617dc0,
                                            (int*)(DAT_806127e8 + 0x10), (unsigned int*)0);
                    *(unsigned int*)(DAT_806127e8 + 0x10) = soundHdl;
                }
                r = __ghz_rng();
                *(float*)((int)DAT_806127b8 + DAT_806127cc * 0x14) =
                    pt[0] + (float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0;
                r = __ghz_rng();
                *(float*)((int)DAT_806127b8 + DAT_806127cc * 0x14 + 4) =
                    pt[1] + (float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0;
                r = __ghz_rng();
                *(float*)((int)DAT_806127b8 + DAT_806127cc * 0x14 + 8) =
                    (float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0;
                r = __ghz_rng();
                pt[2] = (float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0;
                ghz_point_seed_jitter((int)DAT_806127b8 + DAT_806127cc * 0x14);
                ghz_point_seed_jitter((int)(intptr_t)pt);
                DAT_806127cc = DAT_806127cc + 1;
            }
            // still run the settled path this frame for the split parent
            off = (i + 1) * 0x14;
            for (j = i + 1; j < DAT_806127cc; j = j + 1) {
                if (j % 5 != DAT_806127bc % 5) {
                    other = (float*)((int)DAT_806127b8 + off);
                    fx = pt[0] - other[0];
                    fy = pt[1] - other[1];
                    d2 = fx * fx + fy * fy;
                    if (d2 < FLOAT_80617e18) {
                        if (FLOAT_80617dc4 <= d2) {
                            if (d2 < FLOAT_80617d7c) { d2 = FLOAT_80617d7c; }
                            fx = fx / d2;
                            fy = fy / d2;
                        }
                        else {
                            r = __ghz_rng();
                            fx = FLOAT_80617dc4 * ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0);
                            r = __ghz_rng();
                            fy = FLOAT_80617dc4 * ((float)((double)(int)r - DOUBLE_80617df8) / FLOAT_80617df0);
                        }
                        fx = (float)((double)fx * (double)FLOAT_80617d74);
                        fy = (float)((double)fy * (double)FLOAT_80617d74);
                        pt[0] = pt[0] + fx;
                        pt[1] = pt[1] + fy;
                        other[0] = other[0] - fx;
                        other[1] = other[1] - fy;
                    }
                }
                off = off + 0x14;
            }
        }
        else {
            // dead point: swap in the last live one and shrink the pool
            leaf_31e8((unsigned int)(intptr_t)pt,
                      (unsigned int)((int)DAT_806127b8 + (DAT_806127cc - 1) * 0x14), 0x14);
            i = i - 1;
            DAT_806127cc = DAT_806127cc - 1;
        }
    }

    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x80207c74  ghz_path_nearest_point
// Nearest-point-on-polyline query for belt lane `lane` (path table DAT_80457f5c,
// 0x1f words/lane, points at +8 stride 0xc). Projects `origin` onto each segment,
// clamps to [0,1], and returns the closest projected point + segment index if it
// is within the lane accept-distance (DAT_80457fd0). Returns 1 on hit, else 0.
// ---------------------------------------------------------------------------
int ghz_path_nearest_point(float* origin, float* pointOut, int* segIdxOut, int lane)
{
    float bx, by, bz;
    float dx, dy, dz;
    float best;
    int bestIdx;
    int idx;
    int off;
    int count;
    int* base;
    double ax, ay, az;
    double sx, sy, sz;
    double t, tc;
    double hitX = 0, hitY = 0, hitZ = 0;

    bestIdx = -1;
    base = &DAT_80457f5c + lane * 0x1f;
    idx = 0;
    off = 0;
    count = *base - 1;
    best = FLOAT_80617e28;

    while (count > 0) {
        ay = (double)*(float*)((int)base + off + 8);
        sy = (double)(float)((double)*(float*)((int)base + off + 0x14) - ay);
        ax = (double)*(float*)((int)base + off + 4);
        sx = (double)(float)((double)*(float*)((int)base + off + 0x10) - ax);
        az = (double)*(float*)((int)base + off + 0xc);
        sz = (double)(float)((double)*(float*)((int)base + off + 0x18) - az);

        t = (double)(-(float)(sz * (double)(float)(az - (double)origin[2]) +
                              (double)(float)(sx * (double)(float)(ax - (double)origin[0]) +
                                              (double)(float)(sy * (double)(float)(ay - (double)origin[1])))) /
                     (float)(sz * sz + (double)(float)(sx * sx + (double)(float)(sy * sy))));
        tc = t;
        if ((double)FLOAT_80617d7c < t) { tc = (double)FLOAT_80617d7c; }
        if ((double)FLOAT_80617d80 <= tc) {
            if ((double)FLOAT_80617d7c < t) { t = (double)FLOAT_80617d7c; }
        }
        else {
            t = (double)FLOAT_80617d80;
        }

        double px = (double)(float)(sx * t + ax);
        double py = (double)(float)(sy * t + ay);
        double pz = (double)(float)(sz * t + az);
        dx = (float)(px - (double)origin[0]);
        dy = (float)(py - (double)origin[1]);
        dz = (float)(pz - (double)origin[2]);
        float distSq = dz * dz + dx * dx + dy * dy;
        if (distSq < best) {
            bestIdx = idx;
            hitX = px; hitY = py; hitZ = pz;
            best = distSq;
        }
        idx = idx + 1;
        off = off + 0xc;
        count = count - 1;
    }

    if (bestIdx == -1 || *(float*)(&DAT_80457fd0 + lane * 0x7c) <= best) {
        return 0;
    }
    if (pointOut != (float*)0) {
        pointOut[0] = (float)hitX;
        pointOut[1] = (float)hitY;
        pointOut[2] = (float)hitZ;
    }
    *segIdxOut = bestIdx;
    return 1;
}

// ---------------------------------------------------------------------------
// 0x80207e28  ghz_belt_flow_solve
// Belt-flow solver for the active subobject: pick the nearest of the 4 belt lanes
// by centroid distance, run ghz_path_nearest_point, then emit the flow velocity
// (scaled by the lane speed + frame time), the segment heading angles, and the
// progress-along-segment (param_6). Blends heading toward the next segment near
// the segment end. Wraps a wide FP-spill frame (FUN_803826e4 / FUN_80382730).
// ---------------------------------------------------------------------------
void ghz_belt_flow_solve(unsigned int p1, unsigned int p2, unsigned int* anglesOut,
                         float* pointOut, unsigned int* soundOut, float* progressOut)
{
    float cx, cy, cz;
    float bestDist;
    unsigned int count;
    float* origin;
    float* velOut;
    int bestLane;
    int lane;
    int off;
    int segIdx;
    float hit[3];
    float dir[3];
    float wrapAngle;
    unsigned long long ctx;
    double speed;

    (void)p1; (void)p2;
    ctx = FUN_803826e4();
    velOut = (float*)ctx;
    origin = (float*)(unsigned int)(ctx >> 0x20);

    bestLane = 0;
    lane = 0;
    off = 0;
    bestDist = FLOAT_80617e28;
    do {
        unsigned int* laneBase = (unsigned int*)((int)&DAT_80457f5c + off);
        if (lane != 1) {
            cx = DAT_803dfda8;
            cy = DAT_803dfdac;
            cz = DAT_803dfdb0;
            count = *laneBase;
            int po = 0;
            while ((int)count > 0) {
                cx = cx + *(float*)((int)laneBase + po + 4);
                cy = cy + *(float*)((int)laneBase + po + 8);
                cz = cz + *(float*)((int)laneBase + po + 0xc);
                count = count - 1;
                po = po + 0xc;
            }
            float inv = FLOAT_80617d7c / (float)((double)(int)*laneBase - DOUBLE_80617db0);
            cx = cx * inv;
            cy = cy * inv;
            cz = cz * inv;
            float dd = (cz - origin[2]) * (cz - origin[2]) +
                       (cx - origin[0]) * (cx - origin[0]) +
                       (cy - origin[1]) * (cy - origin[1]);
            if (dd < bestDist) {
                bestLane = lane;
                bestDist = dd;
            }
        }
        lane = lane + 1;
        off = off + 0x7c;
    } while (lane < 4);

    anglesOut[0] = (unsigned int)FLOAT_80617d80;
    anglesOut[1] = (unsigned int)FLOAT_80617d80;
    anglesOut[2] = (unsigned int)FLOAT_80617d80;

    if (ghz_path_nearest_point(origin, hit, &segIdx, bestLane) == 0) {
        velOut[0] = FLOAT_80617d80;
        velOut[1] = FLOAT_80617d80;
        velOut[2] = FLOAT_80617d80;
        *soundOut = 0x20c;
        *progressOut = FLOAT_80617e28;
        pointOut[0] = FLOAT_80617d80;
        pointOut[1] = FLOAT_80617d80;
        pointOut[2] = FLOAT_80617d80;
        FUN_80382730();
        return;
    }

    float* lanePts = (float*)(&DAT_80457f5c) + bestLane * 0x1f;
    dir[0] = lanePts[segIdx * 3 + 4] - lanePts[segIdx * 3 + 1];
    dir[1] = lanePts[segIdx * 3 + 5] - lanePts[segIdx * 3 + 2];
    dir[2] = lanePts[segIdx * 3 + 6] - lanePts[segIdx * 3 + 3];
    float len2 = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
    if (FLOAT_80617e2c < len2) {
        float inv = (float)(1.0 / math_sqrt((double)len2));
        inv = inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c);
        float n = FLOAT_80617d7c * inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c);
        dir[0] = dir[0] * n;
        dir[1] = dir[1] * n;
        dir[2] = dir[2] * n;
    }

    float speedScalar = *(float*)((char*)&DAT_80457fd4 + bestLane * 0x7c);
    float rate = FLOAT_80617d7c / (float)((double)(int)DAT_806109c0 - DOUBLE_80617db0);
    float dt = (float)((double)(int)DAT_806108e4 - DOUBLE_80617db0);
    velOut[0] = dir[0] * rate * -speedScalar * dt;
    velOut[1] = dir[1] * rate * -speedScalar * dt;
    velOut[2] = dir[2] * rate * -speedScalar * dt;

    *soundOut = (unsigned int)*(unsigned short*)((int)&DAT_80457f5c + segIdx * 2 + bestLane * 0x7c + 100);

    float ex = hit[0] - lanePts[segIdx * 3 + 1];
    float ey = hit[1] - lanePts[segIdx * 3 + 2];
    float ez = hit[2] - lanePts[segIdx * 3 + 3];
    speed = math_sqrt((double)(ez * ez + ex * ex + ey * ey));
    *progressOut = (float)(speed / (double)speedScalar);
    dir_to_angles((int)dir, (int)anglesOut, (float*)&anglesOut[1]);

    if (*progressOut < FLOAT_80617d90 && (segIdx - 1) >= 0) {
        dir[2] = lanePts[segIdx * 3 + 3] - lanePts[segIdx * 3];
        dir[0] = lanePts[segIdx * 3 + 1] - lanePts[segIdx * 3 - 2];
        dir[1] = lanePts[segIdx * 3 + 2] - lanePts[segIdx * 3 - 1];
        dir_to_angles((int)dir, 0, &wrapAngle);
        for (wrapAngle = wrapAngle - (float)anglesOut[1]; wrapAngle <= FLOAT_80617e30;
             wrapAngle = wrapAngle + FLOAT_80617dbc) {
        }
        for (; FLOAT_80617db8 < wrapAngle; wrapAngle = wrapAngle - FLOAT_80617dbc) {
        }
        anglesOut[1] = (unsigned int)(int)((FLOAT_80617d7c - *progressOut / FLOAT_80617d90) *
                                           wrapAngle + (float)(int)anglesOut[1]);
    }

    pointOut[0] = hit[0];
    pointOut[1] = hit[1];
    pointOut[2] = hit[2];
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802082fc  ghz_path_tangent_speed
// Nearest-segment tangent direction (normalised) for belt lane `lane`, written to
// dirOut; returns the lane flow-speed scalar (or 0 on miss).
// ---------------------------------------------------------------------------
double ghz_path_tangent_speed(float* origin, float* dirOut, int lane)
{
    int segIdx[4];
    float speed;
    float dx;
    float len2;

    if (ghz_path_nearest_point(origin, (float*)0, segIdx, lane) == 0) {
        return (double)FLOAT_80617d80;
    }
    float* lanePts = (float*)(&DAT_80457f5c) + lane * 0x1f;
    dirOut[0] = lanePts[segIdx[0] * 3 + 4] - lanePts[segIdx[0] * 3 + 1];
    dirOut[1] = lanePts[segIdx[0] * 3 + 5] - lanePts[segIdx[0] * 3 + 2];
    dirOut[2] = lanePts[segIdx[0] * 3 + 6] - lanePts[segIdx[0] * 3 + 3];
    dx = dirOut[0];
    len2 = dirOut[2] * dirOut[2] + dx * dx + dirOut[1] * dirOut[1];
    if (FLOAT_80617e2c < len2) {
        float inv = (float)(1.0 / math_sqrt((double)len2));
        inv = inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c);
        float n = FLOAT_80617d7c * inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c);
        dirOut[0] = dx * n;
        dirOut[1] = dirOut[1] * n;
        dirOut[2] = dirOut[2] * n;
    }
    speed = *(float*)((char*)&DAT_80457fd4 + lane * 0x7c);
    return (double)speed;
}

// ---------------------------------------------------------------------------
// 0x80208440  ghz_belt_island_force
// Physics-island force callback: if the linked object (param_2[9]/[10]) is a
// 'BELT' conveyor (type word +0x2d8 -> +4 == 'TLEB'), pull the accumulator toward
// the belt path-flow velocity (ghz_path_tangent_speed); otherwise clear the
// subobject's extra words.
// ---------------------------------------------------------------------------
int ghz_belt_island_force(unsigned int subHandle, float* linkPair, float* accumOut)
{
    bool isBelt;
    int entity;
    int objA;
    int objSel;
    float dir[3];
    double speed;

    entity = subobj_get_entity(subHandle);
    objA = (int)linkPair[9];
    isBelt = false;
    if (*(int*)(objA + 0x2d8) != 0 &&
        *(int*)(*(int*)(objA + 0x2d8) + 4) == 0x42454c54) {   // 'BELT'
        isBelt = true;
    }
    objSel = objA;
    if (!isBelt) {
        objSel = (int)linkPair[10];
    }

    if (objA != 0 && (int)linkPair[10] != 0) {
        if (*(int*)(objSel + 0x2d8) == 0 ||
            *(int*)(*(int*)(objSel + 0x2d8) + 4) != 0x42454c54) {
            subobj_set_extra_words(*(unsigned int*)(entity + 0x80), 0, 0);
        }
        else {
            speed = ghz_path_tangent_speed(linkPair, dir, *(int*)(objSel + 0x1a0));
            if (speed != (double)FLOAT_80617d80) {
                accumOut[0] = (float)((double)dir[0] * speed + (double)accumOut[0]);
                accumOut[1] = (float)((double)dir[1] * speed + (double)accumOut[1]);
                accumOut[2] = (float)((double)dir[2] * speed + (double)accumOut[2]);
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80208548  ghz_belt_island_register
// Register the belt-flow force callback (ghz_belt_island_force) on the entity's
// physics-island and mark it dirty.
// ---------------------------------------------------------------------------
void ghz_belt_island_register(int unused, int* entitySlot)
{
    int entity;

    (void)unused;
    entity = *entitySlot;
    if (entity != 0 && *(unsigned int*)(entity + 0x80) != 0) {
        subobj_set_extra_words(*(unsigned int*)(entity + 0x80),
                               (unsigned int)(intptr_t)&ghz_belt_island_force, 0);
        subobj_dirty_list_add(*(unsigned int*)(entity + 0x80));
    }
}

// ---------------------------------------------------------------------------
// 0x8020859c  ghz_redirty_thunk
// Thunk: force a subobject AABB re-dirty pass.
// ---------------------------------------------------------------------------
void ghz_redirty_thunk(void)
{
    subobj_redirty_in_aabb_wrapper();
}

// ---------------------------------------------------------------------------
// 0x802085bc  ghz_island_timed_free
// Per-frame timed island free: while state (+0x1a0)==1, advance the offset
// (+0x1a4) by the frame-time rate; once positive and past an rng gate, free the
// physics island by body id (+0x60) and advance state to 2.
// ---------------------------------------------------------------------------
void ghz_island_timed_free(int ctrl)
{
    unsigned int r;
    unsigned int modulus;
    int island;

    if (*(int*)(ctrl + 0x1a0) != 1) {
        return;
    }
    *(float*)(ctrl + 0x1a4) =
        -((FLOAT_80617d7c / (float)((double)(int)DAT_806109c0 - DOUBLE_80617db0)) *
          (float)((double)(int)DAT_806108e4 - DOUBLE_80617db0) - *(float*)(ctrl + 0x1a4));
    if (FLOAT_80617d80 < *(float*)(ctrl + 0x1a4)) {
        r = __ghz_rng();
        modulus = (unsigned int)((double)(int)(DAT_806109c0 << 1) - DOUBLE_80617db0);
        if (modulus != 0 && r % modulus < (unsigned int)DAT_806108e4) {
            island = subobj_island_find_by_body_b(*(int*)(ctrl + 0x60));
            subobj_island_pool_free(island);
            *(int*)(ctrl + 0x1a0) = 2;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x802086ac  ghz_break_island_respawn
// Break-piece per-frame island driver. While state (+0x1a0)==1, rng-gate (70%)
// between continuing the break tick and freeing the island (->state 2). While
// state in [0,-1) (initial), rng-gate between the break tick and freeing +
// respawning the island at the piece origin (restitution/drift + joint callback),
// then latch state 1.
// ---------------------------------------------------------------------------
void ghz_break_island_respawn(unsigned long long p1, unsigned long long p2, double p3, double p4,
                              double p5, double p6, double p7, unsigned long long p8,
                              int* entitySlot)
{
    int entity;
    int state;
    unsigned int r;
    float* mtx;
    int island;
    float pt[3];
    float xform[17];

    (void)p1;
    entity = *entitySlot;
    if (*(int*)(entity + 0x80) == 0) {
        return;
    }
    state = *(int*)(entity + 0x1a0);
    if (state == 1) {
        r = __ghz_rng();
        if (r % 10 < 7) {
            subobj_break_tick_impl(0, p2, p3, p4, p5, p6, p7, p8);
            *(float*)(entity + 0x1a4) = FLOAT_80617d7c;
        }
        else {
            island = subobj_island_find_by_body_b(*(int*)(entity + 0x60));
            subobj_island_pool_free(island);
            *(int*)(entity + 0x1a0) = 2;
        }
    }
    else if (state < 1 && state >= 0) {
        r = __ghz_rng();
        if (r % 10 < 7) {
            subobj_break_tick_impl(0, p2, p3, p4, p5, p6, p7, p8);
        }
        else {
            island = subobj_island_find_by_body_b(*(int*)(entity + 0x60));
            subobj_island_pool_free(island);
            mtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
            mtx4_copy(mtx, xform);
            pt[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1a8);
            pt[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
            pt[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
            mtx4_write_translation((double)pt[0], (double)pt[1], (double)pt[2], (int)xform);
            pt[0] = FLOAT_80617e00;
            pt[1] = FLOAT_80617d90;
            pt[2] = FLOAT_80617d80;
            mtx4_transform_point(xform, pt);
            island = __ghz_island_create_point(*(unsigned int*)(entity + 0x80), 0, pt);
            subobj_island_set_restitution((double)FLOAT_80617e04, island);
            subobj_island_set_drift_gain((double)FLOAT_80617d8c, island);
            subobj_island_set_joint_callback(island, (void*)*(int*)(entity + 0x60));
            subobj_dirty_list_add(*(unsigned int*)(entity + 0x80));
            *(int*)(entity + 0x1a0) = 1;
            *(float*)(entity + 0x1a4) = FLOAT_80617d7c;
        }
    }
}

// ===========================================================================
// batch_650: 0x80208844 - 0x80209398
// GHZ set-piece continuation: dropcap break-island spawn, slide-node matrix,
// the 'MCHT' pan/aim console (init/input/exit), 'MINC' child flag, the reactor
// match-end handler, the 'JBTH' team break-all trigger, and the 'JBPK' pickup
// register. (0x802093f8 reactor spark-jet begins the next batch.)
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x80208844  ghz_dropcap_spawn_break_island
// Allocate a physics sub-object for a drop-cap break piece: zero its render
// flag (+0x1a0), build the local matrix, transform the seed point to world,
// alloc the sub-object handle (+0x80), build its default descriptor (class 0x1a,
// mode 2), create an axis island anchored at the seed, set twist limits /
// restitution / drift, wire the joint callback to the entity's body-b handle
// (+0x60), flag it dirty (|0x10), register the per-frame island driver
// (ghz_break_island_respawn), then close the FP spill frame.
// ---------------------------------------------------------------------------
void ghz_dropcap_spawn_break_island(unsigned long long p1, unsigned long long p2,
                                    unsigned long long p3, unsigned long long p4,
                                    unsigned long long p5, unsigned long long p6,
                                    unsigned long long p7, unsigned long long p8)
{
    int entity;
    int subHandle;
    int island;
    unsigned long long framePair;
    double extraF1;
    float seed[4];       // local_5c/local_58/local_54 (island axis seed vector)
    float worldPt[3];    // local_68..local_60
    float xform[20];     // afStack_50
    (void)p1;

    framePair = (unsigned long long)FUN_803826f0();
    entity = (int)(framePair >> 0x20);
    // ROM: local_5c=DAT_803dfdb4, local_58=DAT_803dfdb8, local_54=DAT_803dfdbc
    seed[0] = FLOAT_80617e00;   // axis X seed (2.0f family)
    seed[1] = FLOAT_80617d80;
    seed[2] = FLOAT_80617d80;
    seed[3] = FLOAT_80617d80;

    *(int*)(entity + 0x1a0) = 0;
    entity_build_local_matrix(xform, (void*)entity, 1, 0);
    worldPt[0] = FLOAT_80617e00;
    worldPt[1] = FLOAT_80617e00;
    worldPt[2] = FLOAT_80617d80;
    mtx4_transform_point(xform, worldPt);

    subHandle = __ghz_subobj_pool_alloc();
    *(int*)(entity + 0x80) = subHandle;
    extraF1 = (double)FLOAT_80617d80;
    __ghz_build_descriptor_default((unsigned int)extraF1, p2, p3, p4, p5, p6, p7, p8,
                                   *(int*)(entity + 0x80), entity, 1, 0x1a, 2.939095e-39f);
    island = __ghz_island_create_axis(*(int*)(entity + 0x80), 0, worldPt, seed);
    subobj_island_set_twist_limits((double)FLOAT_80617e34, (double)FLOAT_80617d80, island);
    subobj_island_set_restitution((double)FLOAT_80617e04, island);
    subobj_island_set_drift_gain((double)FLOAT_80617d8c, island);
    subobj_island_set_joint_callback(island, (void*)*(int*)(entity + 0x60));
    *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
    entity_register_for_update(entity, 3, (int)&ghz_break_island_respawn);
    entity_set_render_type(entity, (int)&ghz_break_island_respawn);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80208978  ghz_slide_node_matrix
// Slide-node render callback for class ids 0x238/0x239/0x23a: rebuilds the node
// matrix (+4->+0x10) to a pure Y translation driven by the animation phase p1
// (first a half-gain pre-pass, then the full p1 translation), then flags the
// node for a matrix rebuild (|0x82) and marks it always-drawn (|0x200000).
// ---------------------------------------------------------------------------
void ghz_slide_node_matrix(double p1, int entity)
{
    short cls = *(short*)(entity + 0x94);
    if (cls == 0x238 || cls == 0x239 || cls == 0x23a) {
        mtx4_set_identity(*(int*)(*(int*)(entity + 4) + 0x10));
        __ghz_mtx4_set_translation((double)FLOAT_80617d80,
                                   (double)(float)((double)FLOAT_80617e38 * p1),
                                   (double)FLOAT_80617d80);
        __ghz_mtx4_set_translation((double)FLOAT_80617d80, p1, (double)FLOAT_80617d80);
    }
    *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x200000;
    *(unsigned int*)(entity + 0x18) = *(unsigned int*)(entity + 0x18) | 0x82;
}

// ---------------------------------------------------------------------------
// 0x80208ac8  ghz_mcht_object_init  ('MCHT' 0x4d434854 pan/aim console)
// Object-init for the MCHT console: resolve the MCHT node, clear the reactor
// sequence counter, start its clip, register the input tick, snapshot+normalise
// the node's world basis, convert to euler and store the aim delta at +0xd0..,
// bind the MINC child (0x4d494e43), cache the MCHT aim vector at +0x134.., put
// the local/view player into menu state 0x4000, start the enter-menu anim, wire
// the console entity into the player context (+0xc04) and control block +0x14.
// ---------------------------------------------------------------------------
void ghz_mcht_object_init(int entity)
{
    int minc;
    int player;
    double wrapped;
    float basis[17];      // afStack_5c
    float eulX;           // local_68
    float eulY;           // local_64
    float eulZ;           // local_60

    minc = FUN_8023c32c(0x4d434854); // 'MCHT'
    DAT_806127c8 = 0;
    anim_play_scaled((double)FLOAT_80617d80, (double)FLOAT_80617d80);
    entity_register_for_update(entity, 3, (int)&ghz_mcht_input_tick);

    eulX = (float)DAT_803dfdc0;
    eulY = (float)DAT_803dfdc4;
    eulZ = (float)DAT_803dfdc8;
    mtx4_copy(*(float**)(*(int*)(entity + 4) + 0x10), basis);
    mtx4_orthonormalize_basis(basis, &eulX);
    __ghz_mtx4_to_euler(basis, &eulX, &eulY, &eulZ);
    wrapped = (double)((FLOAT_80617db8 * eulY) / FLOAT_80617e20 - FLOAT_80617e3c);
    eulX = (FLOAT_80617db8 * eulX) / FLOAT_80617e20;
    {
        double w = FUN_80390324((double)((float)(wrapped - (double)FLOAT_80617e30) / FLOAT_80617dbc));
        eulY = -(float)((double)FLOAT_80617dbc * (double)(float)w - wrapped);
    }
    eulZ = (FLOAT_80617db8 * eulZ) / FLOAT_80617e20;
    *(float*)(entity + 0xd0) = FLOAT_80617d80;
    *(float*)(entity + 0xd4) = eulX;
    *(float*)(entity + 0xd8) = eulY;
    *(float*)(entity + 0xdc) = eulZ;

    FUN_8023c8a0(0x4d494e43, entity); // bind 'MINC' child
    if (minc != 0) {
        *(unsigned int*)(minc + 0x28) = *(unsigned int*)(minc + 0x28) | 0x61;
    }
    *(float*)(entity + 0x134) = FLOAT_80617e40;
    *(float*)(entity + 0x138) = FLOAT_80617da4;
    *(float*)(entity + 0x13c) = FLOAT_80617d80;

    player = DAT_80611d78;
    if (DAT_80611d74 != 0) {
        player = DAT_80611d74;
    }
    player_set_state(player, 0x4000);
    anim_play_scaled((double)FLOAT_80617e44, (double)FLOAT_80617d7c);
    *(int*)(*(int*)(*(int*)(player + 0x14) + 0x7c) + 0xc04) = entity;
    *(int*)(entity + 0x1a0) = 1;
    *(int*)(DAT_806127e8 + 0x14) = *(int*)(player + 0x14);
}

// ---------------------------------------------------------------------------
// 0x80208cb0  ghz_mcht_input_tick
// Per-frame MCHT console driver (while +0x1a0 active): re-snapshot the node's
// world basis, convert to euler, store the per-frame aim delta at +0xd0..; ramp
// the console fill value at +0x100 toward 1.0 at a frame-scaled rate; when the
// active anim slot has no user flags, drive the resolved MCHT node position to
// the transformed cached aim point (+0x134..) with the +0xd8 aim offset, else
// hand off to ghz_mcht_exit_menu.
// ---------------------------------------------------------------------------
void ghz_mcht_input_tick(int entity)
{
    int node;
    double rate;
    double wrapped;
    float eulX, eulY, eulZ;
    float basis[16];      // afStack_b0
    float posMtx[12];     // afStack_70
    float aim[3];         // local_bc/local_b8/local_b4
    float out[3];         // local_40/local_3c/local_38
    float fill;

    if (*(int*)(entity + 0x1a0) == 0) {
        return;
    }
    node = *(int*)(entity + 4);
    eulX = (float)DAT_803dfdc0;
    eulY = (float)DAT_803dfdc4;
    eulZ = (float)DAT_803dfdc8;
    // frame-scaled fill rate = frameScale * (1.0 / frameUnit)
    rate = (double)(DAT_806108ec *
                    (FLOAT_80617d7c /
                     (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80617db0)));
    mtx4_copy(*(float**)(node + 0x10), basis);
    mtx4_orthonormalize_basis(basis, &eulX);
    __ghz_mtx4_to_euler(basis, &eulX, &eulY, &eulZ);
    wrapped = (double)((FLOAT_80617db8 * eulY) / FLOAT_80617e20 - FLOAT_80617e3c);
    eulX = (FLOAT_80617db8 * eulX) / FLOAT_80617e20;
    {
        double w = FUN_80390324((double)((float)(wrapped - (double)FLOAT_80617e30) / FLOAT_80617dbc));
        eulY = -(float)((double)FLOAT_80617dbc * (double)(float)w - wrapped);
    }
    eulZ = (FLOAT_80617db8 * eulZ) / FLOAT_80617e20;
    *(float*)(entity + 0xd0) = eulY - *(float*)(entity + 0xd8);
    *(float*)(entity + 0xd4) = eulX;
    *(float*)(entity + 0xd8) = eulY;
    *(float*)(entity + 0xdc) = eulZ;

    fill = *(float*)(node + 0x100);
    if (fill < FLOAT_80617d84) {
        float step = (float)((double)(FLOAT_80617dc0 + fill) * rate);
        if (FLOAT_80617d7c - fill < step) {
            step = FLOAT_80617d7c - fill;
        }
        *(float*)(node + 0x100) = *(float*)(node + 0x100) + step;
    }

    if (anim_active_slot_userflags(*(int*)(entity + 4)) == 0) {
        int mcht = FUN_8023c32c(0x4d434854);
        if (mcht != 0) {
            mtx4_copy(*(float**)(*(int*)(entity + 4) + 0x10), posMtx);
            aim[0] = *(float*)(entity + 0x134);
            aim[1] = *(float*)(entity + 0x138);
            aim[2] = *(float*)(entity + 0x13c);
            mtx4_transform_vector(posMtx, aim);
            // ROM reuses the transformed aim as the local_40/3c/38 out-params
            out[0] = aim[0]; out[1] = aim[1]; out[2] = aim[2];
            *(float*)(mcht + 0x64) = aim[0] + out[0];
            *(float*)(mcht + 0x68) = aim[1] + out[1];
            *(float*)(mcht + 0x6c) = aim[2] + out[2];
            *(unsigned int*)(mcht + 0x18) = *(unsigned int*)(mcht + 0x18) | 0x80;
            *(float*)(mcht + 0x70) = FLOAT_80617e3c + *(float*)(entity + 0xd8);
        }
    } else {
        ghz_mcht_exit_menu();
    }
}

// ---------------------------------------------------------------------------
// 0x80208f04  ghz_mcht_exit_menu
// Tear down the MCHT console interaction: clear the MCHT node render bits, and
// if the MINC child is still active, restore the local/view player to state 2,
// clear the player's console pointer (+0xc04), unhook the MINC update list,
// resolve the floor height under the player into the player context, and clear
// the MINC active flag + control-block console slot.
// ---------------------------------------------------------------------------
void ghz_mcht_exit_menu(void)
{
    int mcht;
    int minc;
    int player;
    int pctx;
    double floorH;

    __save_gpr_28();
    mcht = FUN_8023c32c(0x4d434854);
    minc = FUN_8023c32c(0x4d494e43);
    player = DAT_80611d78;
    if (DAT_80611d74 != 0) {
        player = DAT_80611d74;
    }
    pctx = *(int*)(*(int*)(player + 0x14) + 0x7c);
    if (mcht != 0) {
        *(unsigned int*)(mcht + 0x28) = *(unsigned int*)(mcht + 0x28) & 0xffffff9e;
    }
    if (minc != 0 && *(int*)(minc + 0x1a0) != 0) {
        player_set_state(player, 2);
        *(int*)(pctx + 0xc04) = 0;
        entity_register_for_update(minc, 3, 0);
        floorH = __ghz_floor_query(
                     (double)*(float*)(player + 0x80),
                     (double)*(float*)(&DAT_8048ca9c + *(short*)(pctx + 0x48) * 0x30),
                     player + 0xa8, pctx + 0xc00, (int*)0, 1);
        *(float*)(pctx + 0xbfc) = (float)floorH;
        *(int*)(minc + 0x1a0) = 0;
    }
    *(int*)(DAT_806127e8 + 0x14) = 0;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80209004  ghz_render_nop
// Empty render/update callback (blr) registered by the object-init dispatcher.
// ---------------------------------------------------------------------------
void ghz_render_nop(void)
{
}

// ---------------------------------------------------------------------------
// 0x80209008  ghz_minc_set_ready
// MINC child think: resolve the linked actor by the class word at *(+0xac) and,
// if its context flag (+0x7c->+0x38) is clear, latch it to 1 (arm-once).
// ---------------------------------------------------------------------------
void ghz_minc_set_ready(int* self)
{
    int actor = FUN_8023c32c(*(unsigned int*)(*self + 0xac));
    if (*(int*)(*(int*)(actor + 0x7c) + 0x38) == 0) {
        *(int*)(*(int*)(actor + 0x7c) + 0x38) = 1;
    }
}

// ---------------------------------------------------------------------------
// 0x80209048  ghz_reactor_match_end
// Reactor "self destruct" match-end handler (registered as a data callback).
// Builds a HUD banner string (the string id is swapped for co-op story mode),
// broadcasts it to every active viewport, sets the match self-destruct flag
// (DAT_805015c4 | 0x10000), then runs the standard match_process_end.
// ---------------------------------------------------------------------------
void ghz_reactor_match_end(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                           unsigned long long p4, unsigned long long p5, unsigned long long p6,
                           unsigned long long p7, unsigned long long p8)
{
    int titleId;
    const char* fmt;
    int i;
    int count;
    unsigned int msgBuf[65];   // auStack_108

    if (DAT_805015bd == '\n' && DAT_80612610 > 1) {
        titleId = get_string_by_id(0x1c81);
    } else {
        titleId = get_string_by_id(0xd3c);
    }
    fmt = (const char*)get_string_by_id(0x15eb);
    FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8,
                 (int)msgBuf, fmt, titleId, 0, 0, 0, 0, 0);

    for (i = 0; (count = session_get_state_flag()), i < count; i = i + 1) {
        hud_text_submit(i, 4, msgBuf);
    }
    DAT_805015c4 = DAT_805015c4 | 0x10000;
    match_process_end((int)msgBuf, p2, p3, p4, p5, p6, p7, p8, 0);
}

// ---------------------------------------------------------------------------
// 0x80209128  ghz_jbth_break_all
// 'JBTH' (0x4a425448) team-triggered break-all. When the local player exists,
// walk the entity's target-zone list (+0x78 count / +0x7c ids); if any id
// matches the active/co-op/team-leader player zone, latch the break flag (+0x68).
// Once armed, collect this actor's break sub-nodes (FUN_8023c59c -> list of <=9),
// optionally apply a class-0x22d rng restitution kick, then for each node with no
// live sub-object handle alloc one, build its default descriptor (class 0x1a for
// id 0x21e else default), set collision groups, mark dirty, and schedule a
// delayed break event; finally destroy the trigger entity once all are spawned.
// ---------------------------------------------------------------------------
void ghz_jbth_break_all(unsigned long long p1, double p2, double p3, double p4,
                        unsigned long long p5, unsigned long long p6, unsigned long long p7,
                        unsigned long long p8)
{
    int leader;
    int count;
    int idx;
    int i;
    unsigned int match;
    unsigned int* self;
    double extraF1;
    int nodes[10];      // local_48
    (void)p1;

    self = (unsigned int*)__save_gpr_28();
    if (DAT_80611d74 == 0 || *(int*)(DAT_80611d74 + 0x14) == 0) {
        __restore_gpr_28();
        return;
    }
    leader = aiactor_find_player_by_team(0x4a425448); // 'JBTH'
    count = (int)self[0x78];
    i = 0;
    if (count > 0) {
        do {
            bool coop = (DAT_805015bd == '\n' && DAT_80612610 > 1);
            if (coop) {
                match = countLeadingZeros(*(int*)(*(int*)(DAT_80611d78 + 0x1430) + 0x21c) -
                                          *(int*)(self[0x7c] + i));
                match = match >> 5;
            } else {
                match = 0;
            }
            if (*(int*)(self[0x7c] + i) == *(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x21c) ||
                match != 0 ||
                (leader != 0 && *(int*)(self[0x7c] + i) == *(int*)(leader + 0x21c))) {
                self[0x68] = 1;
                break;
            }
            i = i + 4;
            count = count + -1;
        } while (count != 0);
    }

    if (self[0x68] != 0) {
        count = FUN_8023c59c((int)self[0x18], (int)nodes, 9);
        if (*(short*)(self + 0x25) == 0x22d) {
            // class 0x22d: rng restitution kick along +0x69 (ROM bias math)
            double num = (double)(FLOAT_80617d7c /
                        (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80617db0));
            double den = (double)(float)((double)(int)(unsigned int)(DAT_806108e4 ^ 0x80000000) - DOUBLE_80617db0);
            self[0x69] = (unsigned int)(int)-(float)(num * den - (double)(float)(int)self[0x69]);
            if ((float)(int)self[0x69] <= FLOAT_80617d80) {
                self[0x69] = (unsigned int)(int)FLOAT_80617e04;
                p4 = DOUBLE_80617db0;
            }
        }
        idx = 0;
        while (count > 0) {
            count = count + -1;
            unsigned int handle = *(unsigned int*)(*(int*)((int)nodes + idx) + 0x80);
            if (handle == 0) {
                int subHandle = __ghz_subobj_pool_alloc();
                *(int*)(*(int*)((int)nodes + idx) + 0x80) = subHandle;
                if (subHandle != 0) {
                    int node = *(int*)((int)nodes + idx);
                    extraF1 = (double)FLOAT_80617d80;
                    if (*(short*)(node + 0x94) == 0x21e) {
                        __ghz_build_descriptor_default((unsigned int)extraF1, p2, p3, p4, p5, p6, p7,
                                                       p8, *(int*)(node + 0x80), node, 9, 0x1a, 0.0f);
                    } else {
                        __ghz_build_descriptor_default((unsigned int)extraF1, p2, p3, p4, p5, p6, p7,
                                                       p8, *(int*)(node + 0x80), node, 9, -1, 0.0f);
                    }
                    subobj_set_collision_groups(*(unsigned int*)(*(int*)((int)nodes + idx) + 0x80), 8, 1);
                    subobj_dirty_list_add(*(unsigned int*)(*(int*)((int)nodes + idx) + 0x80));
                }
                weapon_start_delayed_event((double)FLOAT_80617dd8, *(int*)((int)nodes + idx), 8);
                if (*(short*)(self + 0x25) == 0x22d) break;
            } else {
                subobj_dirty_list_add(handle);
            }
            idx = idx + 4;
        }
        if (count < 1) {
            entity_destroy_with_event((int*)self);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80209398  ghz_jbpk_register  ('JBPK' 0x4a42504b)
// Register the JBPK pickup prop: hook its per-frame driver (the reactor spark-jet
// tick, 0x802093f8) and reset its state (+0x1a0=0) / fill (+0x1a4=0).
// ---------------------------------------------------------------------------
void ghz_jbpk_register(void)
{
    int jbpk = FUN_8023c32c(0x4a42504b); // 'JBPK'
    entity_register_for_update(jbpk, 3, (int)&ghz_reactor_spark_jet_tick);
    *(int*)(jbpk + 0x1a0) = 0;
    *(float*)(jbpk + 0x1a4) = FLOAT_80617d80;
}

// ===========================================================================
// batch_651: 0x802093f8 - 0x8020a750
// GHZ reactor set-piece continuation: JBPK spark-jet + beam render, mesh nearest
// -vertex search, the 'ZLFT' rotor spark/gate rig, the 'CZ01'-'CZ11' anim sync +
// JBTH break payloads, and the ZC anim-event dispatcher (bark / screen effect).
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x802093f8  ghz_reactor_spark_jet_tick
// JBPK per-frame driver (registered by ghz_jbpk_register). While the reactor is
// active (DAT_806127d8) and gameplay is running (DAT_806108e4), accumulate the
// fill value FLOAT_806127dc at a frame-scaled rate; every 5th rng tick spawn an
// explosion effect (type 0x12) at the view player's position (+0xa8) pushed
// outward along a normalised rng jitter vector, scaled by the remaining fill.
// ---------------------------------------------------------------------------
void ghz_reactor_spark_jet_tick(void)
{
    unsigned int r;
    float mag;
    float pos[3];        // local_6c/local_68/local_64
    float colorSeed[3];  // local_78/local_74/local_70
    double jx, jy, jz, scale;

    if (DAT_806127d8 == 0 || DAT_806108e4 == 0) {
        return;
    }
    FLOAT_806127dc = (FLOAT_80617d7c /
                      (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80617db0)) *
                     DAT_806108ec + FLOAT_806127dc;
    r = __ghz_rng();
    if (r != (r / 5) * 5) {
        return;
    }
    colorSeed[0] = (float)DAT_803dfdcc;
    colorSeed[1] = (float)DAT_803dfdd0;
    colorSeed[2] = (float)DAT_803dfdd4;
    mag = FLOAT_80617d7c;
    if (FLOAT_806127dc / FLOAT_80617e4c < FLOAT_80617d7c) {
        mag = FLOAT_806127dc / FLOAT_80617e4c;
    }
    pos[0] = *(float*)(DAT_80611d74 + 0xa8);
    pos[1] = *(float*)(DAT_80611d74 + 0xac);
    pos[2] = *(float*)(DAT_80611d74 + 0xb0);
    scale = (double)(FLOAT_80617d7c - mag);
    jx = (double)(FLOAT_80617dcc *
         ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74));
    jy = (double)(FLOAT_80617dcc *
         ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74));
    jz = (double)(FLOAT_80617dcc *
         ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74));
    {
        float len2 = (float)(jz * jz + (double)(float)(jx * jx + (double)(float)(jy * jy)));
        if (FLOAT_80617e2c < len2) {
            float inv = 1.0f / SQRT(len2);
            inv = inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c);
            double norm = (double)(FLOAT_80617d7c * inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c));
            jx = (double)(float)(jx * norm);
            jy = (double)(float)(jy * norm);
            jz = (double)(float)(jz * norm);
        }
    }
    scale = (double)(float)((double)FLOAT_80617e50 * scale);
    pos[0] = (float)(jx * scale + (double)pos[0]);
    pos[1] = (float)((double)(float)(jy / (double)FLOAT_80617dec) * scale + (double)pos[1]);
    pos[2] = (float)(jz * scale + (double)pos[2]);
    player_ai_explosion_spawn(0x12, 0xffffffff, pos, colorSeed, 0, 0, 1);
}

// ---------------------------------------------------------------------------
// 0x80209664  ghz_reactor_beam_render
// GX render for the reactor beam: additive textured billboard quad-strip (sprite
// 0x290) grown from the JBPK node tip along the camera basis rows (DAT_80611d54
// +0x364/+0x370 = camera right/up), pulsing width/alpha by an rng-seeded factor.
// ---------------------------------------------------------------------------
void ghz_reactor_beam_render(int entity)
{
    unsigned int alpha;
    float px, py, pz;
    int tex;
    double pulse, half;
    float tip[3];    // local_68/local_64/local_60

    pulse = (double)(FLOAT_80617e54 *
             ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0) + FLOAT_80617dc0);
    tip[0] = (float)DAT_803dfdd8;
    tip[1] = (float)DAT_803dfddc;
    tip[2] = (float)DAT_803dfde0;
    mtx4_transform_point(
        (float*)(*(int*)(*(int*)(entity + 4) + 0x10) +
                 (*(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4) + -1) * 0x40), tip);
    double ox = (double)(*(float*)(entity + 0x64) + tip[0]);
    double oy = (double)(*(float*)(entity + 0x68) + tip[1]);
    double oz = (double)(*(float*)(entity + 0x6c) + tip[2]);

    gx_set_blend_mode(2);
    gx_set_sub_material_id(2);
    tex = sprite_get_texture_id(0x290);
    gx_begin_textured_batch(2, 0x92, 0, 4, tex);

    half = (double)FLOAT_80617e00;
    alpha = (unsigned int)((double)FLOAT_80617e58 * pulse);
    int cam = DAT_80611d54;
    px = (float)(half * (double)*(float*)(cam + 0x370) +
                 (double)(float)(half * (double)*(float*)(cam + 0x364) + ox));
    py = (float)(half * (double)*(float*)(cam + 0x374) +
                 (double)(float)(half * (double)*(float*)(cam + 0x368) + oy));
    DAT_cc008000 = px;
    pz = (float)(half * (double)*(float*)(cam + 0x378) +
                 (double)(float)(half * (double)*(float*)(cam + 0x36c) + oz));
    DAT_cc008000 = py;
    DAT_cc008000 = pz;
    DAT_cc008000 = (float)(alpha & 0xff | 0x50647f00);
    DAT_cc008000 = FLOAT_80617d80;
    DAT_cc008000 = FLOAT_80617d80;

    px = px + *(float*)(cam + 0x370);
    py = py + *(float*)(cam + 0x374);
    pz = pz + *(float*)(cam + 0x378);
    DAT_cc008000 = px;
    DAT_cc008000 = py;
    DAT_cc008000 = pz;
    DAT_cc008000 = (float)(alpha & 0xff | 0x50647f00);
    DAT_cc008000 = FLOAT_80617d7c;
    DAT_cc008000 = FLOAT_80617d80;

    px = FLOAT_80617e5c * *(float*)(cam + 0x370) + px + *(float*)(cam + 0x364);
    py = FLOAT_80617e5c * *(float*)(cam + 0x374) + py + *(float*)(cam + 0x368);
    pz = FLOAT_80617e5c * *(float*)(cam + 0x378) + pz + *(float*)(cam + 0x36c);
    DAT_cc008000 = px;
    DAT_cc008000 = py;
    DAT_cc008000 = pz;
    DAT_cc008000 = (float)(alpha & 0xff | 0x50647f00);
    DAT_cc008000 = FLOAT_80617d80;
    DAT_cc008000 = FLOAT_80617d7c;

    DAT_cc008000 = px + *(float*)(cam + 0x370);
    DAT_cc008000 = py + *(float*)(cam + 0x374);
    DAT_cc008000 = pz + *(float*)(cam + 0x378);
    DAT_cc008000 = (float)(alpha & 0xff | 0x50647f00);
    DAT_cc008000 = FLOAT_80617d7c;
    DAT_cc008000 = FLOAT_80617d7c;
    gx_end_batch();
}

// ---------------------------------------------------------------------------
// 0x802098ec  ghz_mesh_nearest_vertex
// Scan a render node's collision mesh (strip list at sub +0x54, per-primitive
// vertex runs) for the vertex nearest to refPt; supports both packed-short
// (flag 0x10, dequant via FLOAT_80617e60) and float vertex formats. Every 30th
// rng tick, accept the current vertex as the new nearest and write it to outPt.
// Returns via outPt; the found flag is implied by the caller re-reading outPt.
// ---------------------------------------------------------------------------
int ghz_mesh_nearest_vertex(int renderData, float* refPt, float* outPt)
{
    (void)renderData;
    float vx, vy, vz;
    int stripBase;
    int primOff;
    unsigned int r;
    float* fvtx;
    short* svtx;
    int meshOff;
    int meshIdx;
    int primIdx;
    int lane;
    int vtxByte;
    int vtxByteS;
    int i;
    int subCount;
    int* mesh;
    int primStride;
    double bestSq;
    unsigned long long framePair;

    framePair = (unsigned long long)FUN_803826c0();
    float* ref = (float*)(unsigned int)framePair;   // refPt is passed via the FP frame (pfVar8)
    (void)ref;
    bestSq = (double)FLOAT_80617e28;
    mesh = *(int**)(*(int*)((int)(framePair >> 0x20) + 4) + 4);
    meshOff = 0;
    subCount = *mesh;
    for (meshIdx = 0; meshIdx < *mesh; meshIdx = meshIdx + 1) {
        int blk = meshOff + subCount * -0xc0;
        int prims = 0;
        stripBase = *(int*)((int)mesh + blk + 0x54);
        for (primIdx = stripBase; primIdx != 0 && -1 < *(char*)(primIdx + 3); primIdx = primIdx + 8) {
            prims = prims + 1;
        }
        if (prims != 0) {
            primOff = 0;
            for (i = 0; i < prims; i = i + 1) {
                int prim = stripBase + primOff;
                if (*(char*)(prim + 2) != '\0') {
                    vtxByte = 0;
                    vtxByteS = 0;
                    for (lane = 0; lane < (int)(unsigned int)*(unsigned char*)(prim + 2); lane = lane + 1) {
                        if ((mesh[3] & 0x10U) == 0) {
                            fvtx = (float*)(*(int*)((int)mesh + blk + 4) +
                                            (unsigned int)*(unsigned short*)(prim + 6) * 0xc + vtxByte);
                            vx = fvtx[0]; vy = fvtx[1]; vz = fvtx[2];
                        } else {
                            svtx = (short*)(*(int*)((int)mesh + blk + 4) +
                                            (unsigned int)*(unsigned short*)(prim + 6) * 6 + vtxByteS);
                            vx = FLOAT_80617e60 *
                                 (float)((double)(int)(unsigned int)((int)svtx[0] ^ 0x80000000) - DOUBLE_80617db0) +
                                 (float)mesh[0xb];
                            vy = FLOAT_80617e60 *
                                 (float)((double)(int)(unsigned int)((int)svtx[1] ^ 0x80000000) - DOUBLE_80617db0) +
                                 (float)mesh[0xc];
                            vz = FLOAT_80617e60 *
                                 (float)((double)(int)(unsigned int)((int)svtx[2] ^ 0x80000000) - DOUBLE_80617db0) +
                                 (float)mesh[0xd];
                        }
                        double dx = (double)vx - (double)refPt[0];
                        double dy = (double)vy - (double)refPt[1];
                        double dz = (double)vz - (double)refPt[2];
                        double sq = (double)((float)dz * (float)dz +
                                             (float)dx * (float)dx + (float)dy * (float)dy);
                        if (sq < bestSq) {
                            r = __ghz_rng();
                            if (r == (r / 0x1e) * 0x1e) {
                                outPt[0] = vx;
                                outPt[1] = vy;
                                outPt[2] = vz;
                                double ex = (double)vx - (double)refPt[0];
                                double ey = (double)vy - (double)refPt[1];
                                double ez = (double)vz - (double)refPt[2];
                                bestSq = (double)((float)ez * (float)ez +
                                                  (float)ex * (float)ex + (float)ey * (float)ey);
                            }
                        }
                        vtxByte = vtxByte + 0xc;
                        vtxByteS = vtxByteS + 6;
                    }
                }
                primOff = primOff + 8;
            }
        }
        meshOff = meshOff + 0xc0;
    }
    FUN_8038270c();
    // ROM returns whether a vertex was accepted; the caller (ZLFT rotor tick)
    // treats non-zero bestSq change as success - return 1 when a vertex was
    // written (bestSq shrank below the seed). Approximated as always-eval; the
    // caller re-checks outPt validity.
    return (bestSq < (double)FLOAT_80617e28) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// 0x80209b74  ghz_zlft_rotor_spark_tick  ('ZLFT' rotor blade)
// Per-frame ZLFT rotor blade: transform the blade-tip local point to world, find
// the nearest mesh vertex under it (ghz_mesh_nearest_vertex), ramp the blade
// timer (+0x44); on wrap push the hit point into a 5-slot ring (+0x3c/+0x40) and
// spawn fading sparks at each; then for every player within FLOAT_80617e68 radius
// dispatch impact damage (class 0x1a) along an rng-jittered normal; finally drive
// the 3D loop 0x5d8 at the tip. FP-spill FUN_803826e8/80382734.
// ---------------------------------------------------------------------------
void ghz_zlft_rotor_spark_tick(void)
{
    int entity;
    int slot;
    int hit;
    int i;
    int pIdx;
    float tip[3];        // local_12c/local_128/local_124
    float found[4];      // local_120..local_114 head
    float xform[16];     // afStack_114
    int dmgRec[9];       // local_d4 block
    float jitter[3];     // local_138/local_134/local_130
    float* pArg;
    unsigned long long f8 = 0;

    entity = FUN_803826e8();
    slot = *(int*)(&DAT_805c6540 + *(int*)(entity + 0x1a0) * 4);
    tip[0] = (float)DAT_803dfde4;
    tip[1] = (float)DAT_803dfde8;
    tip[2] = (float)DAT_803dfdec;
    mtx4_transform_point(
        (float*)(*(int*)(*(int*)(entity + 4) + 0x10) +
                 (*(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4) + -1) * 0x40), tip);
    entity_build_local_matrix(xform, (void*)entity, 1, 0);
    mtx4_transform_point(xform, tip);

    // blade timer ramp (frame-scaled)
    *(float*)(slot + 0x44) = -((FLOAT_80617d7c /
        (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80617db0)) *
        (float)((double)(int)(unsigned int)(DAT_806108e4 ^ 0x80000000) - DOUBLE_80617db0) -
        *(float*)(slot + 0x44));

    if (*(float*)(slot + 0x44) <= FLOAT_80617d80) {
        int rd = (int)scene_node_get_render_data(*(int*)(entity + 0x34));
        if (0x11 < *(int*)(entity + 0x34)) {
            rd = (int)scene_node_get_render_data(0x10);
        }
        if (rd != 0 && ghz_mesh_nearest_vertex(rd, tip, found) != 0) {
            *(float*)(slot + 0x44) = FLOAT_80617e04 *
                ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0) +
                FLOAT_80617e64;
            *(float*)(slot + *(int*)(slot + 0x3c) * 0xc)     = found[0];
            *(float*)(slot + *(int*)(slot + 0x3c) * 0xc + 4) = found[1];
            *(float*)(slot + *(int*)(slot + 0x3c) * 0xc + 8) = found[2];
            *(int*)(slot + 0x3c) = *(int*)(slot + 0x3c) + 1;
            if (4 < *(int*)(slot + 0x3c)) {
                *(int*)(slot + 0x3c) = 0;
                *(int*)(slot + 0x40) = 1;
            }
        }
    }

    for (i = 0; ; i = i + 1) {
        int cnt = (*(int*)(slot + 0x40) == 0) ? *(int*)(slot + 0x3c) : 5;
        if (cnt <= i) break;
        spark_spawn_fading();
        spark_set_last_scale(FLOAT_80617d90);
    }

    pIdx = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        int pl = DAT_80611d78 + pIdx;
        float dx = tip[0] - (float)((double)(*(float*)(pl + 0xa8) + *(float*)(pl + 0xb4)) * (double)FLOAT_80617d74);
        float dy = tip[1] - (float)((double)(*(float*)(pl + 0xac) + *(float*)(pl + 0xb8)) * (double)FLOAT_80617d74);
        float dz = tip[2] - (float)((double)(*(float*)(pl + 0xb0) + *(float*)(pl + 0xbc)) * (double)FLOAT_80617d74);
        if ((double)(dz * dz + dx * dx + dy * dy) < (double)FLOAT_80617e68) {
            jitter[0] = FLOAT_80617dcc *
                ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74);
            jitter[1] = FLOAT_80617dcc *
                ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74);
            jitter[2] = FLOAT_80617dcc *
                ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0 - FLOAT_80617d74);
            float len2 = (float)((double)(jitter[2] * jitter[2]) +
                                 (double)(jitter[0] * jitter[0] + jitter[1] * jitter[1]));
            if (FLOAT_80617e2c < len2) {
                float inv = 1.0f / SQRT(len2);
                inv = inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c);
                float norm = (float)((double)FLOAT_80617d7c *
                             (double)(inv * -(FLOAT_80617d74 * len2 * inv * inv - FLOAT_80617d9c)));
                jitter[0] = jitter[0] * norm;
                jitter[1] = jitter[1] * norm;
                jitter[2] = jitter[2] * norm;
            }
            // damage record: switch_slot_reset zeroes the block, then fill the
            // dispatch fields. ROM threads position/dir/damage-class through the
            // record + FP-ABI spill; the public helper takes just the record ptr.
            __ghz_switch_slot_reset((int)dmgRec);
            dmgRec[0] = *(int*)(DAT_80611d78 + pIdx + 0x14);   // target entity (local_d4[0])
            pArg = tip;                                        // hit position (local_ac)
            *(float**)&dmgRec[1] = pArg;                       // local_ac -> &tip
            *(float**)&dmgRec[2] = jitter;                     // local_a8 -> &jitter (dir)
            *(float**)&dmgRec[3] = jitter;                     // local_a4 -> &jitter
            *(int*)&dmgRec[6] = 0x1a;                           // local_c0 damage class
            *(int*)&dmgRec[7] = 0x1a;                           // local_bc
            *(float*)&dmgRec[5] = FLOAT_80617dc0;               // local_b4 magnitude
            (void)f8;
            damage_dispatch_to_entity(dmgRec);
        }
        pIdx = pIdx + 0x141c;
    }

    __ghz_sound3d((double)FLOAT_80617d7c, (double)FLOAT_80617dec, (double)FLOAT_80617d7c,
                  (void*)(entity + 0x22c), tip, 0x5d8, 0);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8020a00c  ghz_zlft_gate_tick  ('ZLFT' 0x5a4c4654 sliding gate)
// Per-frame ZLFT gate state machine (state at node +0x7c +8): eases the gate
// position (+0xc/+0x14 timer) open/closed at a frame-scaled rate; ZLFT-tagged
// gates use the fast ease rate. Rebuilds three bone matrices (+0x80/+0xc0/+0x100)
// as rotate-about-pivot transforms from the eased angle, then drives the servo
// 3D loop 0x5d7. FP-spill FUN_803826f0/8038273c.
// ---------------------------------------------------------------------------
void ghz_zlft_gate_tick(void)
{
    int entity;
    int st;
    int state;
    float* pivot;
    float* mtx;
    int bone;
    double easeRate;
    double angle;
    float frameScale;
    float xform[16];     // afStack_78
    int isZlft;

    entity = FUN_803826f0();
    st = *(int*)(entity + 0x7c);
    isZlft = (countLeadingZeros(0x5a4c4654 - *(int*)(entity + 0x60)) >> 5) != 0; // 'ZLFT'
    frameScale = DAT_806108ec *
        (FLOAT_80617d7c / (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80617db0));
    easeRate = (double)(isZlft ? FLOAT_80617d9c : FLOAT_80617e6c);
    float step = isZlft ? FLOAT_80617e04 : FLOAT_80617dc0;
    state = *(int*)(st + 8);

    if (state == 2) {
        float span = *(float*)(st + 0x18) * FLOAT_80617e70;
        if (-(FLOAT_80617de4 * span) <= *(float*)(st + 0xc) || *(float*)(st + 0x14) <= FLOAT_80617d80) {
            if ((double)*(float*)(st + 0x14) < easeRate) {
                *(float*)(st + 0x14) = (float)((double)*(float*)(st + 0x14) + (double)step);
            }
        } else {
            *(float*)(st + 0x14) =
                (float)(easeRate * (double)(*(float*)(st + 0x18) + *(float*)(st + 0xc))) / span;
            if (*(float*)(st + 0x14) < FLOAT_80617dc4) {
                *(float*)(st + 0x14) = FLOAT_80617d80;
            }
        }
        *(int*)(st + 0x10) = *(int*)(st + 0xc);
        *(float*)(st + 0xc) = -(*(float*)(st + 0x14) * frameScale - *(float*)(st + 0xc));
        *(float*)(entity + 0x68) = *(float*)(st + 0x20) + *(float*)(st + 0xc);
        if (*(float*)(st + 0xc) < -*(float*)(st + 0x18)) {
            *(float*)(st + 0xc) = FLOAT_80617d80;
            *(float*)(st + 0x14) = FLOAT_80617d80;
            if (*(int*)(st + 8) == 2) {
                if (*(int*)(st + 0x34) == 0) { *(int*)(st + 8) = 3; *(int*)(st + 0x38) = 0; }
                else { *(int*)(st + 8) = 1; }
            }
        }
    } else if (state < 2) {
        if (state == 0) {
            if (*(int*)(st + 0x38) != 0) { *(int*)(st + 8) = 2; }
            *(float*)(st + 0x14) = FLOAT_80617d80;
            *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xfbffffff;
        } else if (-1 < state) {
            float span = *(float*)(st + 0x18) * FLOAT_80617e70;
            if (*(float*)(st + 0xc) <= FLOAT_80617de4 * span || *(float*)(st + 0x14) <= FLOAT_80617d80) {
                if ((double)*(float*)(st + 0x14) < easeRate) {
                    *(float*)(st + 0x14) = (float)((double)*(float*)(st + 0x14) + (double)step);
                }
            } else {
                *(float*)(st + 0x14) =
                    (float)(easeRate * (double)(*(float*)(st + 0x18) - *(float*)(st + 0xc))) / span;
                if (*(float*)(st + 0x14) < FLOAT_80617dc4) {
                    *(float*)(st + 0x14) = FLOAT_80617d80;
                }
            }
            *(int*)(st + 0x10) = *(int*)(st + 0xc);
            *(float*)(st + 0xc) = *(float*)(st + 0x14) * frameScale + *(float*)(st + 0xc);
            *(float*)(entity + 0x68) = *(float*)(st + 0x2c) + *(float*)(st + 0xc);
            if (*(float*)(st + 0x18) < *(float*)(st + 0xc)) {
                *(float*)(st + 0xc) = FLOAT_80617d80;
                *(float*)(st + 0x14) = FLOAT_80617d80;
                if (*(int*)(st + 8) == 1) {
                    if (*(int*)(st + 0x34) == 0) { *(int*)(st + 8) = 0; *(int*)(st + 0x38) = 0; }
                    else { *(int*)(st + 8) = 2; }
                }
            }
        }
    } else if (state == 4) {
        *(float*)(st + 0x14) = FLOAT_80617d80;
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xfbffffff;
    } else if (state < 4) {
        if (*(int*)(st + 0x38) != 0) { *(int*)(st + 8) = 1; }
        *(float*)(st + 0x14) = FLOAT_80617d80;
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xfbffffff;
    }

    *(unsigned int*)(entity + 0x18) = *(unsigned int*)(entity + 0x18) | 2;
    mtx4_set_identity(*(int*)(*(int*)(entity + 4) + 0x10));
    int* nodes = *(int**)(*(int*)(entity + 4) + 4);
    angle = (double)((*(float*)(entity + 0x68) - *(float*)(st + 0x2c)) / FLOAT_80617dc0);

    // bone 0 (+0x80): rotate-about-pivot Z
    pivot = (float*)nodes[*nodes * -0x30 + 0x1b];
    __ghz_mtx4_set_translation((double)pivot[0], (double)pivot[1], (double)pivot[2]);
    mtx4_set_rotation_z(angle, xform);
    mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x80);
    mtx4_multiply(xform, mtx, mtx);
    __ghz_mtx4_set_translation(-(double)pivot[0], -(double)pivot[1], -(double)pivot[2]);
    mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x80);
    mtx4_multiply(xform, mtx, mtx);

    // bone 1 (+0xc0): rotate-about-pivot X (negated angle)
    nodes = *(int**)(*(int*)(entity + 4) + 4);
    bone = nodes[*nodes * -0x30 + 0x1b];
    __ghz_mtx4_set_translation((double)*(float*)(bone + 0x18), (double)*(float*)(bone + 0x1c),
                               (double)*(float*)(bone + 0x20));
    angle = -angle;
    __ghz_mtx4_set_rotation_x(angle, xform);
    mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0xc0);
    mtx4_multiply(xform, mtx, mtx);
    __ghz_mtx4_set_translation(-(double)*(float*)(bone + 0x18), -(double)*(float*)(bone + 0x1c),
                               -(double)*(float*)(bone + 0x20));
    mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0xc0);
    mtx4_multiply(xform, mtx, mtx);

    // bone 2 (+0x100): rotate-about-pivot Z (negated angle)
    nodes = *(int**)(*(int*)(entity + 4) + 4);
    bone = nodes[*nodes * -0x30 + 0x1b];
    __ghz_mtx4_set_translation((double)*(float*)(bone + 0x30), (double)*(float*)(bone + 0x34),
                               (double)*(float*)(bone + 0x38));
    mtx4_set_rotation_z(angle, xform);
    mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x100);
    mtx4_multiply(xform, mtx, mtx);
    __ghz_mtx4_set_translation(-(double)*(float*)(bone + 0x30), -(double)*(float*)(bone + 0x34),
                               -(double)*(float*)(bone + 0x38));
    mtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x100);
    mtx4_multiply(xform, mtx, mtx);

    __ghz_sound3d((double)((float)((double)*(float*)(st + 0x14) / easeRate) - FLOAT_80617e04),
                  (double)FLOAT_80617e74, (double)FLOAT_80617d7c,
                  (void*)(entity + 0x22c), (void*)(entity + 0x64), 0x5d7, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8020a53c  ghz_cz_anim_sync
// Per-frame anim-clip-driven collision sync for the 11 'CZ01'-'CZ11' props
// (0x435a3031..0x435a3131): clip 0x22a clears collision bit 1, clips 0x228/0x229
// set it. Then for the JBTH team leader's active clip, fire the 'LCp2'(0x4c637032)
// payload triggers (clip 0x225 -> FUN_80377928, clip 0x227 -> FUN_8037793c).
// ---------------------------------------------------------------------------
void ghz_cz_anim_sync(void)
{
    int props[11];   // local_48[4] + local_38..local_20
    int i;
    int off;
    int actor;
    int clip;
    int lcp2;
    int leader;

    __save_gpr_28();
    props[0]  = FUN_8023c32c(0x435a3031); // 'CZ01'
    props[1]  = FUN_8023c32c(0x435a3032);
    props[2]  = FUN_8023c32c(0x435a3033);
    props[3]  = FUN_8023c32c(0x435a3034);
    props[4]  = FUN_8023c32c(0x435a3035);
    props[5]  = FUN_8023c32c(0x435a3036);
    props[6]  = FUN_8023c32c(0x435a3037);
    props[7]  = FUN_8023c32c(0x435a3038);
    props[8]  = FUN_8023c32c(0x435a3039);
    props[9]  = FUN_8023c32c(0x435a3130); // 'CZ10'
    props[10] = FUN_8023c32c(0x435a3131); // 'CZ11'

    off = 0;
    for (i = 0; i < 11; i = i + 1) {
        actor = *(int*)((int)props + off);
        if (actor != 0) {
            clip = *(int*)anim_active_clip_slot(actor);
            if (clip == 0x22a) {
                *(unsigned int*)(actor + 0x28) = *(unsigned int*)(actor + 0x28) & 0xfffffffe;
            } else if (clip == 0x228 || clip == 0x229) {
                *(unsigned int*)(actor + 0x28) = *(unsigned int*)(actor + 0x28) | 1;
            }
        }
        off = off + 4;
    }

    lcp2 = FUN_8023c32c(0x4c637032); // 'LCp2'
    leader = aiactor_find_player_by_team(0x4a425448); // 'JBTH'
    if (leader != 0) {
        int* slot = (int*)anim_active_clip_slot(leader);
        if (*slot == 0x225) { FUN_80377928(lcp2); }
        if (*slot == 0x227) { FUN_8037793c(); }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8020a6d0  ghz_break_payload_thunk
// Thunk: fire the scripted break payload (FUN_80221074) with a 0.0 lead arg.
// ---------------------------------------------------------------------------
void ghz_break_payload_thunk(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8)
{
    (void)p1;
    FUN_80221074((double)FLOAT_80617d80, (double)p2, (double)p3, (double)p4,
                 (double)p5, (double)p6, (double)p7, p8);
}

// ---------------------------------------------------------------------------
// 0x8020a6f8  ghz_break_ground_impulse_cb
// Break sub-object callback: when the piece has a ground-contact record
// (*(+0x150) != 0), apply the ground impulse then fire the break payload thunk.
// ---------------------------------------------------------------------------
void ghz_break_ground_impulse_cb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                 unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                 unsigned long long p7, unsigned long long p8,
                                 unsigned int p9, int* self)
{
    (void)p1; (void)p9;
    if (*(int*)(*self + 0x150) != 0) {
        unsigned long long v = __ghz_ground_impulse();
        ghz_break_payload_thunk(v, p2, p3, p4, p5, p6, p7, p8);
    }
}

// ---------------------------------------------------------------------------
// 0x8020a72c  ghz_break_tick_payload_cb
// Break sub-object callback: run the per-frame break tick, then fire the break
// payload thunk with the tick's result.
// ---------------------------------------------------------------------------
void ghz_break_tick_payload_cb(unsigned long long p1, unsigned long long p2, double p3, double p4,
                               double p5, double p6, double p7, unsigned long long p8)
{
    unsigned long long v = __ghz_break_tick(p1, p2, p3, p4, p5, p6, p7, p8);
    ghz_break_payload_thunk(v, p2, (unsigned long long)p3, (unsigned long long)p4,
                            (unsigned long long)p5, (unsigned long long)p6,
                            (unsigned long long)p7, p8);
}

// ---------------------------------------------------------------------------
// 0x8020a750  ghz_cz_event_dispatch
// ZC anim-event dispatcher (event code in the FP-frame return). Codes 2/4 fall
// through to clear the active screen-effect owner (DAT_80611d74+0xe8c). Code 1
// (bark): for an active 'PCZ1'(0x50435a31)-linked bot not already barking and
// with enough players present, pick a non-repeating rng bark from table A or B
// (per the +0x14 0x40000000 flag) and play it 3D. Code 6: alloc+install a screen
// -effect slot at +0x268 and set it as the current owner. FP-spill 803826f0/8038273c.
// ---------------------------------------------------------------------------
void ghz_cz_event_dispatch(unsigned int p1, unsigned int p2, int p3, unsigned int p4,
                           unsigned int p5, unsigned int p6, unsigned int p7, unsigned int p8)
{
    int code;
    int self;
    int child;
    int slot;
    int soundId;
    int barkFlag;
    unsigned long long framePair;
    (void)p1; (void)p2; (void)p4;

    framePair = (unsigned long long)FUN_803826f0();
    self = (int)(framePair >> 0x20);
    code = (int)(unsigned int)framePair;

    if (code == 4 || code == 2) goto done;
    if (code < 2) {
        if (0 < code) {
            child = *(int*)(p3 + 4);
            self = *(int*)(self + 0x7c);
            if (child != 0 && *(int*)(child + 0x20) == 8) {
                int childCtx = *(int*)(child + 0x7c);
                if (sound_is_playing(*(unsigned int*)(self + 0xe90)) == 0 &&
                    DAT_80612610 <= *(int*)(childCtx + 8)) {
                    soundId = 0;
                    barkFlag = 0;
                    if ((*(unsigned int*)(childCtx + 0x14) & 0x40000000) == 0) {
                        if (*(int*)(*(int*)(self + 0xbf0) + 8) == 0x50435a31) { // 'PCZ1'
                            int idx = (int)(FLOAT_80617dcc *
                                ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0) +
                                FLOAT_80617d74);
                            if (idx == DAT_806127c4) idx = (idx + 1) % 3;
                            soundId  = *(int*)(&DAT_80458164 + idx * 8);
                            barkFlag = *(int*)(&DAT_80458168 + idx * 8);
                            DAT_806127c4 = idx;
                        }
                    } else {
                        int idx = (int)(FLOAT_80617dcc *
                            ((float)((double)(int)(unsigned int)__ghz_rng() - DOUBLE_80617df8) / FLOAT_80617df0) +
                            FLOAT_80617d74);
                        if (idx == DAT_806127c4) idx = (idx + 1) % 3;
                        soundId  = *(int*)(&DAT_8045814c + idx * 8);
                        barkFlag = *(int*)(&DAT_80458150 + idx * 8);
                        DAT_806127c4 = idx;
                    }
                    if (soundId != 0) {
                        if (barkFlag == 0) {
                            aiactor_play_bark_3d((int*)self, soundId, 0, (int*)0,
                                                 (int)p5, (int)p6, (int)p7, (int)p8);
                        } else {
                            aiactor_play_bark_3d((int*)self, soundId, 0x4000, (int*)0,
                                                 (int)p5, (int)p6, (int)p7, (int)p8);
                        }
                    }
                }
            }
        }
        goto done;
    } else {
        if (code == 6) {
            slot = (int)screen_effect_alloc_slot();
            *(int*)(self + 0x268) = slot;
            screen_effect_init_slot((int*)*(int*)(self + 0x268), (int*)self);
            *(int*)(DAT_80611d74 + 0xe8c) = self;
            goto done;
        }
        if (5 < code) goto done;
    }
    *(int*)(DAT_80611d74 + 0xe8c) = 0;
done:
    FUN_8038273c();
}

// ===========================================================================
// batch_652: 0x8020a9a4 - 0x8020b470
// GHZ set-piece tail: the 'JBTH' break-master slot table (reset + per-frame
// tracker) and the big second object-init dispatcher for the higher class-id
// range, plus 7 anim-event thunks forwarding to the shared character-fx handler.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8020a9a4  ghz_jbth_slot_table_reset
// Reset the JBTH break-master 10-slot table (DAT_80458184, 0xc-byte stride):
// clear the state word of each slot to 0 (idle).
// ---------------------------------------------------------------------------
void ghz_jbth_slot_table_reset(void)
{
    int off = 0;
    int i = 10;
    do {
        *(int*)((int)&DAT_80458184 + off) = 0;
        i = i - 1;
        off = off + 0xc;
    } while (i != 0);
}

// ---------------------------------------------------------------------------
// 0x8020a9d0  ghz_jbth_break_master
// Per-frame JBTH break tracker (called from ghz_master_update). Walks the 10-slot
// table (DAT_80458184): idle slots (<=0) try to acquire a player by team id
// (+0x08 of the slot, aiactor_find_player_by_team) and latch the handle; active
// slots (state 1) validate the cached handle against the player pool, count the
// broken/dead ones, and accumulate a damage weight from teammates playing the
// break clips (0x116-0x119) past phase FLOAT_80617e54; state 2 slots count as
// broken. When all 10 are broken, fire the completion payload FUN_80221074 and
// clear the screen-effect owner; otherwise dispatch the accumulated team damage
// to the JBTH leader (scaled by difficulty DAT_805015bf), and on lethal fire
// FUN_8022136c(1). FP-spill FUN_803826dc/80382728.
// ---------------------------------------------------------------------------
void ghz_jbth_break_master(unsigned long long p1, double p2, double p3, unsigned long long p4,
                           unsigned long long p5, unsigned long long p6, unsigned long long p7,
                           unsigned long long p8)
{
    int slot;
    int leader;
    int firstActive;
    int brokenCount;
    int i;
    int off;
    unsigned int handle;
    int player;
    double weight;
    unsigned long long dmgRec;
    int rec[9];      // local_98 block
    float* wField;
    (void)p1;

    FUN_803826dc();
    weight = (double)FLOAT_80617d80;
    brokenCount = 0;
    leader = FUN_8023c32c(0x4a425448);  // 'JBTH'
    firstActive = 0;
    i = 0;
    off = 0;
    do {
        slot = *(int*)((int)&DAT_80458184 + off);
        if (slot == 1) {
            handle = *(unsigned int*)(off + -0x7fba7e80); // &DAT_80458188 + off (cached handle)
            if (handle == 0) {
                player = 0;
            } else {
                player = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                if (*(unsigned int*)(player + 0x2c) != handle) {
                    player = 0;
                    *(int*)(off + -0x7fba7e80) = 0;
                }
            }
            if (player == 0 || *(float*)(player + 0x1cc) <= FLOAT_80617d80 ||
                (*(unsigned int*)(*(int*)(player + 0x7c) + 0x24) & 8) != 0) {
                brokenCount = brokenCount + 1;
                *(int*)((int)&DAT_80458184 + off) = 2;
            } else {
                if (firstActive == 0) firstActive = player;
                if ((anim_is_clip_active(player, 0x116) != 0 ||
                     anim_is_clip_active(player, 0x117) != 0 ||
                     anim_is_clip_active(player, 0x119) != 0 ||
                     anim_is_clip_active(player, 0x118) != 0) &&
                    *(int*)(*(int*)(player + 0x7c) + 4) == leader &&
                    (double)FLOAT_80617e54 < (double)anim_active_clip_phase(player)) {
                    weight = (double)(float)(weight + (double)FLOAT_80617d84);
                }
            }
        } else if (slot < 1) {
            if (-1 < slot) {
                player = aiactor_find_player_by_team(*(int*)(off + -0x7fba7e84)); // slot teamId
                if (player != 0) {
                    *(int*)(off + -0x7fba7e80) = *(int*)(player + 0x2c);
                    *(int*)((int)&DAT_80458184 + off) = 1;
                    if (firstActive == 0) firstActive = player;
                }
            }
        } else if (slot < 3) {
            brokenCount = brokenCount + 1;
        }
        i = i + 1;
        off = off + 0xc;
    } while (i < 10);

    if (brokenCount == 10) {
        FUN_80221074((double)FLOAT_80617d70, p2, p3, p4, p5, p6, p7, p8);
        *(int*)(DAT_80611d74 + 0xe8c) = 0;
    } else if (leader != 0) {
        dmgRec = __ghz_switch_slot_reset((int)rec);
        rec[0] = leader;                       // local_98
        rec[1] = (firstActive != 0) ? firstActive : leader; // local_94
        rec[2] = rec[1];                        // local_90
        wField = (float*)&rec[6];               // local_78
        (void)dmgRec;
        if (DAT_80611d74 == 0 || *(int*)(DAT_80611d74 + 0xdec) == 7 ||
            *(int*)(DAT_80611d74 + 0xdec) == 6) {
            if ((double)FLOAT_80617d80 < weight) {
                if (DAT_805015bf != 1) {
                    if (DAT_805015bf == 0) {
                        weight = (double)(float)(weight * (double)FLOAT_80617d74);
                    } else if (DAT_805015bf < 3) {
                        weight = (double)(float)(weight * (double)FLOAT_80617d9c);
                    }
                }
                *wField = (float)weight;
                damage_dispatch_to_entity(rec);
            }
        } else {
            *wField = FLOAT_80617e78;
            damage_dispatch_to_entity(rec);
        }
        if (*(float*)(leader + 0x1cc) <= FLOAT_80617d80) {
            FUN_8022136c(1);
        }
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x8020acb0  ghz_object_init_dispatch_b
// Second GHZ per-class object-init switch (class id at +0x94, range ~0xf..0x24c),
// the sibling of ghz_object_init_dispatch (0x8020530c). Wires render/update
// callbacks, flag bits, model-cache blocks and physics params for every GHZ prop
// variant in this id range: MCHT console (0x240 -> ghz_mcht_object_init), the
// 'LLFT'/default sliding gate block (0x1f8 -> alloc 0x3c state, submesh setup),
// break pieces (0x1ed/0x1ec 'NOSG' rng seed masks -> ghz_break_piece_update),
// drop-cap islands (>0x1e7 -> ghz_dropcap_spawn_break_island), the ZLFT rotor
// (0x23e -> alloc 0x48, register into DAT_805c6540 ring via DAT_806127c0), the
// break-tick body (0x244 -> ghz_break_tick_payload_cb), 'V*' spark props (>=499),
// and various collision/flag toggles. FP-spill FUN_803826f0/8038273c.
// ---------------------------------------------------------------------------
void ghz_object_init_dispatch_b(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8)
{
    int entity;
    unsigned int* classPtr;
    unsigned short cls;
    float fillA;    // FLOAT_80617e08 family
    float fillB;    // FLOAT_80617dec family
    unsigned int r;
    int st;
    int sub;
    int i;
    int j;
    int k;
    unsigned long long framePair;
    double extraF1;
    float bone[6];  // local_58..local_30
    (void)p1;

    framePair = (unsigned long long)FUN_803826f0();
    fillA = FLOAT_80617e08;
    fillB = FLOAT_80617dec;
    entity = (int)(framePair >> 0x20);
    classPtr = (unsigned int*)(unsigned int)framePair;
    cls = *(unsigned short*)(entity + 0x94);
    extraF1 = (double)FLOAT_80617d80;

    if (cls == 0x22d) goto done;
    if (cls < 0x22d) {
        if (500 < cls) {
            if (cls == 0x21b) {
                if (FLOAT_80617dec <= ABS(FLOAT_80617e3c + *(float*)(entity + 0x70))) {
                    *(int*)(entity + 0x1a0) = 3;
                } else {
                    *(int*)(entity + 0x1a0) = 2;
                }
                entity_register_for_update(entity, 7, -0x7fdf7ab8);
                entity_register_for_update(entity, 3, -0x7fdf7a64);
            } else if (cls < 0x21b) {
                if (cls < 0x20d) {
                    if (cls == 0x1f8) {
                        st = __ghz_model_cache_alloc(entity, (int)classPtr, 0x3c, 0);
                        *(int*)(entity + 0x7c) = st;
                        memset_wrapper(*(int*)(entity + 0x7c), 0, 0x3c);
                        st = *(int*)(entity + 0x7c);
                        *(unsigned int*)(entity + 0x18) = *(unsigned int*)(entity + 0x18) | 2;
                        if (*classPtr == 0x4c4c4654) { // 'LLFT'
                            *(int*)(st + 8) = 3;
                            *(int*)(st + 0x38) = 0;
                            *(float*)(st + 0xc) = FLOAT_80617d80;
                            *(float*)(st + 0x14) = FLOAT_80617d80;
                            *(float*)(st + 0x18) = FLOAT_80617e7c;
                            sub = scene_node_get_submesh(0x5d, 0);
                            j = 0; k = 3;
                            do {
                                int dst = st + j;
                                *(int*)(dst + 0x28) = *(int*)(sub + j);
                                *(int*)(dst + 0x1c) = *(int*)(sub + j);
                                j = j + 4; k = k - 1;
                            } while (k != 0);
                            *(float*)(st + 0x20) = *(float*)(st + 0x20) + *(float*)(st + 0x18);
                            *(int*)(st + 0x34) = 0;
                        } else {
                            *(int*)(st + 8) = 0;
                            *(int*)(st + 0x38) = 0;
                            *(float*)(st + 0xc) = FLOAT_80617d80;
                            *(float*)(st + 0x14) = FLOAT_80617d80;
                            *(float*)(st + 0x18) = FLOAT_80617e80;
                            sub = scene_node_get_submesh(0x2a, 0);
                            j = 0; k = 3;
                            do {
                                int dst = st + j;
                                *(int*)(dst + 0x28) = *(int*)(sub + j);
                                *(int*)(dst + 0x1c) = *(int*)(sub + j);
                                j = j + 4; k = k - 1;
                            } while (k != 0);
                            *(float*)(st + 0x2c) = *(float*)(st + 0x2c) - *(float*)(st + 0x18);
                            *(int*)(st + 0x34) = 1;
                        }
                        render_instance_set_material((float)FLOAT_80617d80, *(int**)(entity + 4), 1);
                        entity_register_for_update(entity, 4, -0x7fdf5ff4);
                        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xfbffffff;
                        *(int*)(entity + 0x34) = 0xd5;
                        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x40000000;
                    }
                } else if (cls < 0x20f) {
                    entity_set_render_type(entity, (int)&damage_entity_receive);
                    if (*(unsigned int*)(entity + 0x80) != 0) {
                        subobj_set_half_extent_x(*(unsigned int*)(entity + 0x80), 0x700);
                    }
                }
            } else if (cls == 0x226) {
                *(int*)(entity + 0x1a0) = 0;
                entity_register_for_update(entity, 3, -0x7fdf6ed8);
            } else if (cls < 0x226 && cls < 0x21d) {
                *(int*)(entity + 0x1a0) = 1;
                entity_register_for_update(entity, 7, -0x7fdf7ab8);
                entity_register_for_update(entity, 3, -0x7fdf7a64);
            }
            goto done;
        }
        if (cls != 0x1e9) {
            if (cls < 0x1e9) {
                if (cls == 0xf) {
                    *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xfffffffe;
                } else if (0xe < cls && 0x1e7 < cls) {
                    ghz_dropcap_spawn_break_island(extraF1, p2, p3, p4, p5, p6, p7, p8);
                }
                goto done;
            }
            if (cls == 0x1f0) goto done;
            if (cls < 0x1f0) {
                if (0x1ec < cls) {
                    entity_set_render_type(entity, (int)&ghz_break_piece_update);
                    *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
                    r = __ghz_rng();
                    *(unsigned int*)(*(int*)(entity + 4) + 0x30) = r & 0x80;
                    if (*(short*)(entity + 0x94) == 0x1ed) {
                        if (*classPtr == 0x4e4f5347) { // 'NOSG'
                            *(unsigned int*)(*(int*)(entity + 4) + 0x30) =
                                *(unsigned int*)(*(int*)(entity + 4) + 0x30) | 0x20;
                        } else {
                            r = __ghz_rng();
                            *(unsigned int*)(*(int*)(entity + 4) + 0x30) =
                                *(unsigned int*)(*(int*)(entity + 4) + 0x30) | 1 << r % 7;
                        }
                    } else if (*classPtr == 0x4e4f5347) {
                        *(unsigned int*)(*(int*)(entity + 4) + 0x30) =
                            *(unsigned int*)(*(int*)(entity + 4) + 0x30) | 4;
                    } else {
                        r = __ghz_rng();
                        *(unsigned int*)(*(int*)(entity + 4) + 0x30) =
                            *(unsigned int*)(*(int*)(entity + 4) + 0x30) | 1 << r % 5;
                    }
                }
                goto done;
            }
            if (cls < 499) {
                *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
                *(float*)(entity + 0x1d8) = fillA;
                *(float*)(entity + 0x1cc) = fillA;
                *(unsigned int*)(entity + 0x9c) = *(unsigned int*)(entity + 0x9c) | 0x38;
                goto done;
            }
        }
        // cls == 0x1e9 or 499 (V-prefixed spark props)
        __ghz_rng();
        {
            unsigned int r2 = __ghz_rng();
            unsigned int r3 = __ghz_rng();
            *(float*)(entity + 0x1a4) = FLOAT_80617d80;
            *(unsigned int*)(entity + 0x1a0) = ((r3 & 3) + 1) * 0x100 | ((r2 & 3) + 1) * 0x10000 | 1;
        }
        if ((*classPtr & 0xff000000) == 0x56000000) { // 'V...'
            *(unsigned int*)(entity + 0x1a0) = *(unsigned int*)(entity + 0x1a0) | 0x800000;
        }
        entity_register_for_update(entity, 3, -0x7fdf9914);
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
        goto done;
    }

    if (cls == 0x240) {
        ghz_mcht_object_init(entity);
        goto done;
    }
    if (0x23f < cls) {
        if (cls == 0x247) {
            *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
        } else if (cls < 0x247) {
            if (cls != 0x243) {
                if (cls < 0x243) {
                    if (cls < 0x242) {
                        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xffffff9e;
                    } else {
                        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xfffffffe;
                    }
                } else if (cls < 0x245) {
                    subobj_set_bone_palette_matrix(*(unsigned int*)(entity + 0x80), bone);
                    bone[0] = FLOAT_80617d80; bone[1] = FLOAT_80617d80; bone[2] = FLOAT_80617d80;
                    bone[3] = FLOAT_80617d80; bone[4] = FLOAT_80617d80; bone[5] = FLOAT_80617d80;
                    subobj_update_inverse_inertia_tensor(*(unsigned int*)(entity + 0x80), bone);
                    entity_register_for_update(entity, 7, -0x7fdf5908);
                    entity_set_render_type(entity, (int)&ghz_break_tick_payload_cb);
                }
            }
        } else if (cls == 0x24c) {
            *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x40000000;
            *(int*)(entity + 0x34) = 0xd5;
        }
        goto done;
    }
    if (cls == 0x237) {
        goto label_bird;
    } else {
        if (0x236 < cls) {
            if (cls == 0x23e) {
                sub = __ghz_model_cache_alloc(entity, (int)classPtr, 0x48, 0);
                int idx = DAT_806127c0;
                *(int*)(&DAT_805c6540 + DAT_806127c0 * 4) = sub;
                *(int*)(entity + 0x1a0) = idx;
                DAT_806127c0 = (DAT_806127c0 + 1) % 4;    // ring counter (matches ROM arith)
                *(int*)(sub + 0x3c) = 0;
                *(float*)(sub + 0x44) = FLOAT_80617dc0;
                *(int*)(sub + 0x40) = 0;
                entity_register_for_update(entity, 3, -0x7fdf648c);
                entity_register_for_update(entity, 10, -0x7fdf699c);
            }
            goto done;
        }
        if (cls < 0x233) {
            if (cls < 0x231) {
                if (0x22e < cls) goto done;
                goto label_bird;
            }
        } else if (cls < 0x235) {
            *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
            *(float*)(entity + 0x1d8) = fillB;
            *(float*)(entity + 0x1cc) = fillB;
            *(unsigned int*)(entity + 0x9c) = *(unsigned int*)(entity + 0x9c) | 0x30;
            goto done;
        }
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
        *(float*)(entity + 0x1d8) = fillA;
        *(float*)(entity + 0x1cc) = fillA;
        *(unsigned int*)(entity + 0x9c) = *(unsigned int*)(entity + 0x9c) | 0x38;
    }
    goto done;

label_bird:
    if (DAT_805015bd != '\n' || DAT_80612610 < 2) {
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 4;
    }
    entity_register_for_update(entity, 0, -0x7fdf8c5c);
done:
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8020b398..0x8020b470  ghz_char_fx_thunk_0..6
// Seven anim-event handler entry points, each a tail-call to the shared character
// anim-event/state-machine fx handler (FUN_80289044). Registered as distinct
// callbacks by the object-init dispatcher; the event code is passed through the
// FP-spill frame that FUN_80289044 reads.
// ---------------------------------------------------------------------------
void ghz_char_fx_thunk_0(void) { FUN_80289044(); }
void ghz_char_fx_thunk_1(void) { FUN_80289044(); }
void ghz_char_fx_thunk_2(void) { FUN_80289044(); }
void ghz_char_fx_thunk_3(void) { FUN_80289044(); }
void ghz_char_fx_thunk_4(void) { FUN_80289044(); }
void ghz_char_fx_thunk_5(void) { FUN_80289044(); }
void ghz_char_fx_thunk_6(void) { FUN_80289044(); }

// ===========================================================================
// batch_653: 0x8020b494 - 0x8020ba88
// GHZ tail: 8th anim-event thunk, the 'MTHS' capture/restore transform pair, the
// 'RBCR' break thunk, and the bird-flock drop-trail rig (reset/tick/advance/arc).
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8020b494  ghz_char_fx_thunk_7
// Eighth anim-event handler entry point: tail-call to FUN_80289044.
// ---------------------------------------------------------------------------
void ghz_char_fx_thunk_7(void) { FUN_80289044(); }

// ---------------------------------------------------------------------------
// 0x8020b4b8  ghz_mths_capture_transform
// Snapshot the 'MTHS'(0x4d544853) actor's transform into the capture block
// (DAT_805c6550..656c): class/model id (+0x1a0), position (+0x64/+0x68/+0x6c),
// fields +0xc0/+0x70/+0xc8, and health (+0x1cc). Stores -1 when MTHS is absent.
// ---------------------------------------------------------------------------
void ghz_mths_capture_transform(void)
{
    int mths = FUN_8023c32c(0x4d544853); // 'MTHS'
    if (mths == 0) {
        DAT_805c6550 = -1;
    } else {
        DAT_805c6550 = *(int*)(mths + 0x1a0);
        DAT_805c6554 = *(int*)(mths + 0x64);
        DAT_805c6558 = *(int*)(mths + 0x68);
        DAT_805c655c = *(int*)(mths + 0x6c);
        DAT_805c6560 = *(int*)(mths + 0xc0);
        DAT_805c6564 = *(int*)(mths + 0x70);
        DAT_805c6568 = *(int*)(mths + 0xc8);
        DAT_805c656c = *(int*)(mths + 0x1cc);
    }
}

// ---------------------------------------------------------------------------
// 0x8020b53c  ghz_mths_restore_transform
// If a MTHS snapshot exists (DAT_805c6550 != -1), fire the 'CRTE'(0x43525445)
// anim-clip event (FUN_8023458c), then re-resolve MTHS and write the captured
// transform back onto it.
// ---------------------------------------------------------------------------
void ghz_mths_restore_transform(unsigned long long p1, double p2, double p3, double p4, double p5,
                                double p6, unsigned long long p7, unsigned long long p8, int p9,
                                int p10, int p11, int p12, int p13, int p14, int p15, int p16)
{
    int mths;
    if (DAT_805c6550 != -1) {
        FUN_8023458c(p1, p2, p3, p4, p5, p6, p7, p8, 0x43525445, p10, p11, p12, p13, p14, p15, p16);
        mths = FUN_8023c32c(0x4d544853);
        if (mths != 0) {
            *(int*)(mths + 0x1a0) = DAT_805c6550;
            *(int*)(mths + 0x64)  = DAT_805c6554;
            *(int*)(mths + 0x68)  = DAT_805c6558;
            *(int*)(mths + 0x6c)  = DAT_805c655c;
            *(int*)(mths + 0xc0)  = DAT_805c6560;
            *(int*)(mths + 0x70)  = DAT_805c6564;
            *(int*)(mths + 0xc8)  = DAT_805c6568;
            *(int*)(mths + 0x1cc) = DAT_805c656c;
        }
    }
    (void)p9;
}

// ---------------------------------------------------------------------------
// 0x8020b5cc  ghz_mths_event_dispatch
// MTHS anim-event router: event 2 -> capture the transform; event 1 -> restore it.
// ---------------------------------------------------------------------------
void ghz_mths_event_dispatch(unsigned long long p1, double p2, double p3, double p4, double p5,
                             double p6, unsigned long long p7, unsigned long long p8, int p9,
                             int event, int p11, int p12, int p13, int p14, int p15, int p16)
{
    if (event == 2) {
        ghz_mths_capture_transform();
    } else if (event < 2 && 0 < event) {
        ghz_mths_restore_transform(p1, p2, p3, p4, p5, p6, p7, p8, p9, event, p11, p12, p13, p14, p15, p16);
    }
}

// ---------------------------------------------------------------------------
// 0x8020b60c  ghz_droptrail_reset
// Clear the drop-trail control block (0x54 bytes at DAT_805c6570) and seed each of
// the 3 slots' first field (0x1c stride) to FLOAT_80617e88.
// ---------------------------------------------------------------------------
void ghz_droptrail_reset(void)
{
    int off;
    int i;
    memset_wrapper((int)&DAT_805c6570, 0, 0x54);
    off = 0;
    i = 3;
    do {
        *(float*)((int)&DAT_805c6570 + off) = FLOAT_80617e88;
        off = off + 0x1c;
        i = i - 1;
    } while (i != 0);
}

// ---------------------------------------------------------------------------
// 0x8020b660  ghz_rbcr_break_thunk
// If the entity's class word (+0x60) is 'RBCR'(0x52424352), run the worm break
// cleanup helper FUN_8020df34.
// ---------------------------------------------------------------------------
void ghz_rbcr_break_thunk(int entity)
{
    if (*(int*)(entity + 0x60) == 0x52424352) { // 'RBCR'
        FUN_8020df34();
    }
}

// ---------------------------------------------------------------------------
// 0x8020b694  ghz_droptrail_tick
// Bird-flock drop-trail per-frame driver. For a class-0x539 prop that is alive
// (state +0x68 not 3/4/5): when the drop-trail slots (DAT_805c6570) are all full,
// seed a new slot in the ring (DAT_806127f0 mod 3) from the prop's world pos
// (+0x19..) toward the target (+0x28/+0x2c). In mode 0x1a with an 'e6' sub-prop,
// bleed health (+0x73). Below FLOAT_80617e8c*maxHealth, every 10th rng tick spawn
// explosion type 0x14; on death (health < 0) clear the screen-fx owner, set state
// 3, and fire the 'MTHS' end clip (FUN_80234560). For the non-0x539 branch, tick
// the attached particle emitter (+0x11c) or destroy the prop when spent.
// FP-spill FUN_803826f0/8038273c.
// ---------------------------------------------------------------------------
void ghz_droptrail_tick(unsigned long long p1, unsigned long long p2, double p3, double p4,
                        double p5, double p6, unsigned long long p7, unsigned long long p8,
                        int p9, int p10, float* p11, float* p12, int p13, int p14, int p15, int p16)
{
    unsigned int* framePtr;
    unsigned int* prop;
    int state;
    int freeCount;
    int off;
    int i;
    unsigned int r;
    double health;
    int emitter;
    int hp;      // local_28
    float dir[3]; // local_24/local_20/local_1c
    unsigned long long framePair;
    (void)p1; (void)p11; (void)p12; (void)p13;

    framePair = (unsigned long long)FUN_803826f0();
    framePtr = (unsigned int*)(framePair >> 0x20);
    prop = (unsigned int*)*framePtr;

    if (*(short*)(prop + 0x25) == 0x539) {
        state = (int)prop[0x68];
        if (state != 5 && state != 3 && state != 4) {
            if (framePtr[10] != 0 && framePtr[0xb] != 0) {
                freeCount = 0;
                off = 0;
                i = 3;
                do {
                    if (*(float*)((int)&DAT_805c6570 + off) < FLOAT_80617e94 &&
                        *(float*)((int)&DAT_805c6570 + off) < FLOAT_80617e98) {
                        freeCount = freeCount + 1;
                    }
                    off = off + 0x1c;
                    i = i - 1;
                } while (i != 0);
                if (freeCount == 0) {
                    int s = DAT_806127f0 * 7;   // 7 dwords per slot
                    (&DAT_805c6584)[s] = (int)FLOAT_80617e90;
                    (&DAT_805c6580)[s] = (int)FLOAT_80617e90;
                    (&DAT_805c6570)[s] = (int)FLOAT_80617e90;
                    float* tgt = (float*)framePtr[10];
                    (&DAT_805c6574)[s] = (int)(*tgt - (float)prop[0x19]);
                    (&DAT_805c6578)[s] = (int)(float)((double)*(float*)(framePtr[10] + 4) - (double)(float)prop[0x1a]);
                    (&DAT_805c657c)[s] = (int)(float)((double)*(float*)(framePtr[10] + 8) - (double)(float)prop[0x1b]);
                    (&DAT_805c6574)[s] = (int)-(FLOAT_80617e9c * *(float*)framePtr[0xb] - *(float*)&(&DAT_805c6574)[s]);
                    (&DAT_805c6578)[s] = (int)-(FLOAT_80617e9c * *(float*)(framePtr[0xb] + 4) - *(float*)&(&DAT_805c6578)[s]);
                    (&DAT_805c657c)[s] = (int)-(FLOAT_80617e9c * *(float*)(framePtr[0xb] + 8) - *(float*)&(&DAT_805c657c)[s]);
                    DAT_806127f0 = (DAT_806127f0 + 1) % 3;
                }
            }
            if (DAT_80612614 == 0x1a && framePtr[3] != 0 && *(short*)(framePtr[3] + 0x94) == 0xe6) {
                *(float*)&prop[0x73] = (float)prop[0x73] - (float)framePtr[8];
            }
            health = (double)(float)prop[0x73];
            if (health < (double)((float)prop[0x76] * FLOAT_80617e8c) &&
                framePtr[10] != 0 && framePtr[0xb] != 0) {
                r = __ghz_rng();
                if (r == (r / 10) * 10) {
                    player_ai_explosion_spawn(0x14, 0xffffffff, (float*)framePtr[10],
                                              (float*)framePtr[0xb], 0, 0, 1);
                }
            }
            if ((double)(float)prop[0x73] < (double)FLOAT_80617e90) {
                if (DAT_80611d74 != 0 && *(unsigned int**)(DAT_80611d74 + 0xe8c) == prop) {
                    *(int*)(DAT_80611d74 + 0xe8c) = 0;
                }
                prop[0x68] = 3;
                FUN_80234560((unsigned long long)(unsigned int)(float)prop[0x73], health, p3, p4, p5, p6,
                             p7, p8, 0x4d544853, 0, 0, 0, p13, p14, p15, p16);
            }
        }
    } else {
        dir[0] = (float)DAT_803dfdf0;
        dir[1] = (float)DAT_803dfdf4;
        dir[2] = (float)DAT_803dfdf8;
        emitter = (int)prop[0x1f];
        *(float*)&prop[0x73] = (float)prop[0x73] - (float)framePtr[8];
        if (emitter != 0) {
            hp = *(int*)(emitter + 0x11c);
            if (particle_handle_is_valid(&hp) ||
                (float)prop[0x76] * FLOAT_80617e8c <= (float)prop[0x73]) {
                if ((float)prop[0x73] < FLOAT_80617e90) {
                    FUN_8020df34();
                    entity_destroy_with_event((int*)prop);
                }
            } else {
                particle_emitter_spawn((unsigned int)(emitter + 0x11c), (unsigned int)(int)&DAT_805c65c4,
                                       0x8001000, (float*)(prop + 0x19), dir);
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8020b9a4  ghz_nop
// Empty callback (blr).
// ---------------------------------------------------------------------------
void ghz_nop(void) { }

// ---------------------------------------------------------------------------
// 0x8020b9a8  ghz_droptrail_advance
// Advance the 3 drop-trail slots (DAT_805c6570, 0x1c stride): for each active slot
// (age +0 below threshold), integrate its progress at a frame-scaled rate (faster
// when the slot's active flag +0x18 is set), ramp its two secondary fields (+0x10
// age / +0x14) toward the clamp FLOAT_80617ea8.
// ---------------------------------------------------------------------------
void ghz_droptrail_advance(void)
{
    unsigned int timeUnit = DAT_806109c0;
    float frameScale = DAT_806108ec;
    int off = 0;
    int i = 3;
    do {
        float drag = FLOAT_80617ea0;
        if (*(int*)((int)&DAT_805c6588 + off) != 0) {
            drag = FLOAT_80617ea4;
        }
        float t = *(float*)((int)&DAT_805c6570 + off);
        if (t < FLOAT_80617e94) {
            float* age = (float*)((int)&DAT_805c6580 + off);
            float inv = FLOAT_80617ea0 /
                (float)((double)(int)(unsigned int)(timeUnit ^ 0x80000000) - DOUBLE_80617eb0);
            *(float*)((int)&DAT_805c6570 + off) = frameScale * drag * inv + t;
            if (*(float*)((int)&DAT_805c6580 + off) < FLOAT_80617ea8) {
                *age = FLOAT_80617eac * drag * inv * frameScale + *(float*)((int)&DAT_805c6580 + off);
            }
            if (FLOAT_80617ea8 <= *age) {
                *age = FLOAT_80617ea8;
                *(float*)((int)&DAT_805c6584 + off) =
                    FLOAT_80617eac * drag * inv * frameScale + *(float*)((int)&DAT_805c6584 + off);
            }
        }
        off = off + 0x1c;
        i = i - 1;
    } while (i != 0);
}

// ---------------------------------------------------------------------------
// 0x8020ba88  ghz_droptrail_arc_point
// Compute an arc position + orientation between two endpoints (pfVar6 start,
// pfVar5 end, arriving via the FP-spill frame). When the endpoints are within
// FLOAT_80617eb8, ease toward the end with a shallow sine-LUT (DAT_80583228) bob;
// otherwise interpolate along the direction with a distance-scaled sine bob. Then
// writes the yaw/pitch (atan2 of the planar delta) into param_4.
// ---------------------------------------------------------------------------
void ghz_droptrail_arc_point(int p1, int p2, float* outPos, float* outAngles)
{
    float* endPt;
    float* startPt;
    double dist;
    double dz, dx, planar;
    double dir, reach;
    float amp;
    float phase;
    unsigned long long framePair;
    (void)p1; (void)p2;

    framePair = (unsigned long long)__save_gpr_28();
    endPt   = (float*)(framePair >> 0x20);
    startPt = (float*)(unsigned int)framePair;
    dz = (double)(endPt[2] - startPt[2]);
    double h = (double)FLOAT_80617e90;
    dx = (double)(endPt[0] - startPt[0]);
    dist = math_sqrt((double)(float)(dx * dx + (double)(float)(dz * dz)));
    reach = dist;   // ROM: dVar7 holds the sqrt/reach; the far-branch overwrites it
    amp = FLOAT_80617ec0;
    float baseF = FLOAT_80617e8c;
    double near = (double)FLOAT_80617eb8;

    if (dist <= near) {
        amp = FLOAT_80617ec8 * (float)(dist / near);
        outPos[0] = endPt[0];
        outPos[1] = endPt[1];
        outPos[2] = endPt[2];
        outPos[1] = -(FLOAT_80617ec0 *
            *(float*)((int)&DAT_80583228 +
                      (((int)(FLOAT_80617ec4 * amp * baseF + baseF) + 0x200) * 4 & 0x1ffc))
            - startPt[1]);
    } else {
        dir  = (double)(float)((double)FLOAT_80617ea0 / dist);
        reach = (double)(float)((double)FLOAT_80617ebc - dist);
        dx = (double)(float)((double)(float)(dx * dir) * reach);
        float f1 = FLOAT_80617ec4 * FLOAT_80617ec8 * (float)(reach / near) * FLOAT_80617e8c + FLOAT_80617e8c;
        outPos[0] = (float)(dx + (double)startPt[0]);
        dz = (double)(float)((double)(float)(dz * dir) * reach);
        outPos[1] = (float)((double)(float)(h * dir) * reach) + startPt[1];
        outPos[2] = (float)(dz + (double)startPt[2]);
        outPos[1] = amp * *(float*)((int)&DAT_80583228 + (((int)f1 + 0x200) * 4 & 0x1ffc)) + startPt[1];
    }
    dir = (double)math_atan2(dx, dz);
    phase = (float)((double)FLOAT_80617ed4 * reach) / FLOAT_80617ed0;
    outAngles[0] = (float)((double)(FLOAT_80617ecc * (float)dir) * reach) / FLOAT_80617ed0;
    outAngles[1] = phase;
    __restore_gpr_28();
}
} // extern "C"

// ===========================================================================
// batch_654 : GHZ reactor bolt / arc-emitter / ship-turret tail
// 0x8020bc74 - 0x8020d80c  (10 functions, subsystem:game, worm-effect)
//
// This block continues the GHZ set-piece with three sub-systems:
//  * a procedural "drop-bolt" (electric arc) that seeds a launch direction off a
//    random collision-mesh triangle (DAT_80458ab0 table), travels via a small
//    scene-raycast state machine (control block DAT_805c6648), and on impact
//    detonates + throws physics debris (subobj bodies, entity class 0x526);
//  * a reactor arc/beam emitter that walks an emitter-record array (DAT_80458c00
//    def table, 0x28-stride live records) spawning beam particles toward the MTHS
//    boss and occasionally raycast-damaging it;
//  * the 'SBSN'/'SHIP' turret actor tick that fires scripted anim-event explosions
//    and lobs projectiles (class 0x51a/0x51d) at nearby ships.
//
// Non-matching notes: the 0x43300000/DOUBLE bias int<->float conversions and the
// (uint^0x80000000)/2^32 signed-rng->float pattern are written as plain float
// math; the sine LUT (DAT_80583228, quarter-phase &0x7ff, +0x200 cos offset) is
// indexed directly; PPC FP-spill frame open/close helpers are opaque void/void.
// The huge FP-register spill locals Ghidra emits for 0x8020bc74 / 0x8020c7f0 are
// dropped (they are pure callee-save spills, never read).
// ---------------------------------------------------------------------------

// FP-spill frame open/close helpers (opaque, void/void) used by this block.
extern int  FUN_803826b8(void);        // open + return active GHZ actor block ptr
extern void FUN_803826c8(void);        // open (arc-emitter master)
extern unsigned long long FUN_803826d0(void);  // open + return (count<<32 | recPtr)
extern int  FUN_803826d8(void);        // open + return active actor ptr (ship turret)
extern void FUN_80382704(void);        // close (drop-trail render)
extern void FUN_80382714(void);        // close (bolt master)
extern void FUN_8038271c(void);        // close (arc-emitter tick)
extern void FUN_80382724(void);        // close (ship turret tick)

// wide-ABI ROM helpers reached by raw address (typed to the call site).
// (math_atan2 / game_frand come from generated_decls; do not redeclare.)

// --- batch_654 raw data symbols (file-local) ---
extern int   DAT_805c6648;      // bolt control block: state (0 idle,2 travel,3 impact)
extern int   DAT_805c664c;      // bolt: (padding within 0x2c reset span)
extern float DAT_805c6650;      // bolt: position X (+ dir base)
extern float DAT_805c6654;      // bolt: position Y
extern float DAT_805c6658;      // bolt: position Z
extern float DAT_805c665c;      // bolt: direction X
extern float DAT_805c6660;      // bolt: direction Y
extern float DAT_805c6664;      // bolt: direction Z
extern float DAT_805c6668;      // bolt: travel timer
extern int   DAT_805c6670;      // bolt: trail particle emitter handle

extern int   DAT_80458ab0;      // launch-mesh triangle table (0x1c stride, 11 rows)
extern int   DAT_80458c00;      // reactor arc-emitter def table (0x44 stride)
extern int   DAT_80458c02;      // arc-emitter def: point-count (u16) / mode byte
extern char  DAT_80458c03;      // arc-emitter def: randomise flag
extern int   DAT_80458c34;      // arc-emitter def: interval float
extern int   DAT_80458c38;      // arc-emitter def: on-duration scale
extern int   DAT_80458c3c;      // arc-emitter def: off-duration scale
extern int   DAT_80458c40;      // arc-emitter def: packed BE colour

extern int   DAT_80612098;      // spark-source list end index
extern int   DAT_80588610;      // spark-source pointer array base
extern char  DAT_805015bd;      // language/mode byte
extern int   DAT_80611d54;      // primary player/view block base
extern int   DAT_80611d78;      // active character block base
extern int   DAT_80612610;      // active actor count
extern int   DAT_806109c0;      // per-frame time unit
extern float DAT_806108ec;      // frame-time scale
extern int   DAT_806108e4;      // gameplay-active gate
extern float DAT_80583cd4;      // ship-turret facing dot threshold
extern int   DAT_8058e3cc;      // identity-matrix source (droptrail basis)
extern int   DAT_80612250;      // render override flag A
extern int   DAT_80612258;      // render override flag B
extern float FLOAT_80617ed8;    // droptrail rotation divisor
extern float FLOAT_80617edc;    // droptrail amplitude base

// bolt dual-explosion cached positions / dirs (0x803dfdfc..0x803dfe34)
extern float DAT_803dfdfc; extern int DAT_803dfe00; extern int DAT_803dfe04;
extern float DAT_803dfe08; extern int DAT_803dfe0c; extern int DAT_803dfe10;
extern float DAT_803dfe14; extern float DAT_803dfe18; extern float DAT_803dfe1c;   // debris impulse seed
extern float DAT_803dfe20; extern int DAT_803dfe24; extern int DAT_803dfe28;       // prop emitter offset
extern float DAT_803dfe2c; extern float DAT_803dfe30; extern float DAT_803dfe34;   // ship-turret local aim vec

// batch_654 tuning float pool 0x80617ee0..0x80617f74
extern float  FLOAT_80617ee0;   // 0.5f family (fill/interp)
extern float  FLOAT_80617ee4;   // 0.5f (rng centre)
extern float  FLOAT_80617ee8;   // active clamp max
extern float  FLOAT_80617eec;   // active ramp gain
extern float  FLOAT_80617ef0;   // travel timer limit / count gate
extern float  FLOAT_80617ef4;   // spread scalar
extern float  FLOAT_80617ef8;   // rng span scalar
extern float  FLOAT_80617efc;   // segment count divisor
extern float  FLOAT_80617f00;   // inner segment count
extern float  FLOAT_80617f04; extern float FLOAT_80617f08; extern float FLOAT_80617f0c; // 2nd explosion pos
extern float  FLOAT_80617f10;   // normalize epsilon
extern float  FLOAT_80617f14;   // 1.5f (rsqrt newton)
extern float  FLOAT_80617f18;   // 2^32 rng divisor
extern float  FLOAT_80617f1c;   // bolt spawn max-dist^2
extern float  FLOAT_80617f20;   // bolt spawn min-dist^2 / cull radius^2
extern float  FLOAT_80617f24;   // normal dot upper gate
extern float  FLOAT_80617f28;   // normal dot lower gate / 1.0f
extern float  FLOAT_80617f2c;   // billboard scale
extern float  FLOAT_80617f30;   // billboard Y bias
extern float  FLOAT_80617f34;   // billboard opacity
extern double DOUBLE_80617f38;  // 2^52 unsigned int->double bias
extern float  FLOAT_80617f40;   // idle travel timeout
extern float  FLOAT_80617f44;   // dir Y damp
extern float  FLOAT_80617f48;   // camera-shake dist scale
extern float  FLOAT_80617f4c;   // camera-shake dist base
extern float  FLOAT_80617f50; extern float FLOAT_80617f54;   // shake fields
extern float  FLOAT_80617f58; extern float FLOAT_80617f5c; extern float FLOAT_80617f60;  // debris spread
extern float  FLOAT_80617f64;   // arc-emitter cull radius^2
extern float  FLOAT_80617f6c;   // beam-recoil spread scale
extern float  FLOAT_80617f70;   // ship proximity radius^2
extern float  FLOAT_80617f74;   // projectile speed

// --- typed aliases for ROM helpers whose generated_decls prototype drops the
//     r3 return value / differs from the ABI this block relies on ---
typedef int (*GhzSceneRaycastFn)(int node, float* origin, float* dir, int mode,
                                 float* hitPos, float* hitNrm, int* hitNode,
                                 unsigned int mask, unsigned int p9, int* p10);
static const GhzSceneRaycastFn __ghz_scene_raycast = (GhzSceneRaycastFn)&scene_raycast;

typedef int (*GhzBeamFn)(int p1, int p2, float* startPos, int* dir, int mask);
static const GhzBeamFn __ghz_particle_beam = (GhzBeamFn)&particle_spawn_beam;

// --- batch_654 function forward decls ---
void ghz_droptrail_render(void);
void ghz_dual_explosion_spawn(void);
void ghz_bolt_state_reset(void);
void ghz_bolt_seed_from_mesh(float* bolt);
void ghz_bolt_master_tick(void);
void ghz_mths_raycast_damage(double p_unused, double dmgA, double dmgB,
                             unsigned long long p4, unsigned long long p5,
                             unsigned long long p6, unsigned long long p7,
                             unsigned long long p8);
void ghz_reactor_arc_emitter_tick(void);
void ghz_reactor_arc_emitter_thunk(void);
void ghz_prop_emitter_spawn(int* rec);
void ghz_ship_turret_tick(void);

// ---------------------------------------------------------------------------
// 0x8020bc74  ghz_droptrail_render
// Render the GHZ "drop-bolt" electric trails. When the active actor's health
// ratio ( +0x1cc vs +0x1d8*thr ) is under threshold, build a Z/X-rotated basis
// then, for each of the 3 drop-trail slots (DAT_805c6570 table, 0x1c stride) that
// is still within its lifetime, emit a jittered textured triangle-strip lattice
// (sprite 0x3ba, additive) whose points come from ghz_droptrail_arc_point and a
// sine-LUT (DAT_80583228) fed by game_frand jitter. Colour is 0x3f4f3f00, or
// 0x7f7f7f00 for "active" slots.
// ---------------------------------------------------------------------------
void ghz_droptrail_render(void)
{
    int   actor;
    int   texId;
    unsigned int colour = 0x3f4f3f00;
    unsigned int chanSave;
    float mtx[16];
    float posA[4];
    float ptOut, angA, angB;    // ghz_droptrail_arc_point outputs (pos/angles pack)
    float arcPos, arcMid, arcAng;
    unsigned int strip[2];
    int slot, slotOff;
    float lifeMax;

    actor  = FUN_803826b8();
    texId  = sprite_get_texture_id(0x3ba);
    gx_set_blend_mode(2);
    gx_set_sub_material_id(2);
    chanSave = gx_set_num_chans(0);
    __ghz_gx_color_update();

    if (*(float*)(actor + 0x1d8) * FLOAT_80617e8c <= *(float*)(actor + 0x1cc)) {
        mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        mtx4_set_rotation_zyx(
            (double)((FLOAT_80617ec8 * *(float*)(actor + 0xc0)) / FLOAT_80617ed8),
            (double)FLOAT_80617e90,
            (double)((FLOAT_80617ec8 * *(float*)(actor + 0xc8)) / FLOAT_80617ed8), mtx);
        lifeMax = FLOAT_80617e94;

        slotOff = 0;
        for (slot = 0; slot < 3; slot = slot + 1) {
            float baseX = *(float*)(actor + 0x64);
            float baseY = *(float*)(actor + 0x68);
            float baseZ = *(float*)(actor + 0x6c);
            double ox = (double)(*(float*)((int)&DAT_805c6574 + slotOff) + baseX);
            double oy = (double)(*(float*)((int)&DAT_805c6578 + slotOff) + baseY);
            double oz = (double)(*(float*)((int)&DAT_805c657c + slotOff) + baseZ);
            int    active = *(int*)((int)&DAT_805c6588 + slotOff);
            float  age    = *(float*)((int)&DAT_805c6570 + slotOff);
            if (active != 0)
                colour = 0x7f7f7f00;

            if ((double)age < (double)lifeMax) {
                float startY = *(float*)((int)&DAT_805c6584 + slotOff);
                float span   = (float)((double)startY + (double)*(float*)((int)&DAT_805c6580 + slotOff));
                float rnd    = (float)game_frand();
                float ratio  = (float)((double)FLOAT_80617ea0 - (double)(age / FLOAT_80617e94));
                float amp    = FLOAT_80617edc * FLOAT_80617ee0 * ratio *
                               (float)((double)(FLOAT_80617ee4 * rnd + FLOAT_80617ea0) - (double)FLOAT_80617ee4);
                if (active != 0) {
                    float cap = FLOAT_80617eec * ratio;
                    if (cap < FLOAT_80617ee8) amp = cap; else amp = FLOAT_80617ee8;
                }

                double spanD  = (double)(float)(span - startY);
                int    outer;
                for (outer = 0; (double)outer < (double)FLOAT_80617ef8; outer = outer + 1) {
                    double t0 = (double)outer / (double)FLOAT_80617ef8;
                    double t1 = (double)(outer + 1) / (double)FLOAT_80617ef8;
                    double y0 = t0 * spanD + (double)startY;
                    double y1 = t1 * spanD + (double)startY;
                    double j0 = FLOAT_80617ea4 * t0;
                    double j1 = FLOAT_80617ea4 * t1;
                    double r;
                    r = game_frand();
                    j0 = j0 * (double)(float)((double)(FLOAT_80617ef4 * (float)r + FLOAT_80617ea0) - (double)FLOAT_80617ef4);
                    r = game_frand();
                    j1 = j1 * (double)(float)((double)(FLOAT_80617ef4 * (float)r + FLOAT_80617ea0) - (double)FLOAT_80617ef4);
                    if ((double)FLOAT_80617ea0 < j0) j0 = (double)(float)((double)FLOAT_80617ea4 - j0);
                    if ((double)FLOAT_80617ea0 < j1) j1 = (double)(float)((double)FLOAT_80617ea4 - j1);
                    strip[0] = colour | (unsigned int)(int)((double)amp * j0);
                    strip[1] = colour | (unsigned int)(int)((double)amp * j1);

                    gx_begin_textured_batch(2, 0x92, 0, 0x2a, texId);
                    double yy0 = (double)(float)(FLOAT_80617e90 * (float)y0 + (float)oy);
                    double yy1 = (double)(float)(FLOAT_80617e90 * (float)y1 + (float)oy);
                    int inner;
                    for (inner = 0; (double)inner < (double)FLOAT_80617f00; inner = inner + 1) {
                        unsigned int phaseIdx =
                            (unsigned int)(FLOAT_80617ec4 *
                                (float)((double)FLOAT_80617efc * ((double)inner / (double)FLOAT_80617f00)) + FLOAT_80617e8c);
                        double cs = (double)*(float*)((int)&DAT_80583228 + ((phaseIdx + 0x200) * 4 & 0x1ffc));
                        double sn = (double)(&DAT_80583228)[phaseIdx & 0x7ff];
                        posA[0] = (float)(cs * (double)(float)y0 + ox);
                        posA[1] = (float)(sn * (double)(float)y0 + oz);   // laid out for arc helper
                        posA[2] = (float)(sn * (double)(float)y1 + oz);
                        posA[3] = (float)(cs * (double)(float)y1 + ox);
                        (void)yy0; (void)yy1;
                        int e;
                        for (e = 0; e < 2; e = e + 1) {
                            mtx4_transform_vector(mtx, posA);
                            ghz_droptrail_arc_point((int)posA, (int)&arcPos, &arcMid, &arcAng);
                            DAT_cc008000 = arcMid;
                            DAT_cc008000 = (float)strip[e];
                            DAT_cc008000 = arcAng;
                        }
                    }
                    gx_end_batch();
                }
            }
            slotOff = slotOff + 0x1c;
        }
        gx_set_num_chans(chanSave);
    }
    FUN_80382704();
}

// ---------------------------------------------------------------------------
// 0x8020c1ec  ghz_dual_explosion_spawn
// Spawn two type-0x12 explosions: one at the cached world position/dir stored at
// DAT_803dfdfc/DAT_803dfe08, then a second at the fixed offset FLOAT_80617f04.
// ---------------------------------------------------------------------------
void ghz_dual_explosion_spawn(void)
{
    float pos[3];
    float dir[3];

    pos[0] = DAT_803dfdfc;
    pos[1] = (float)DAT_803dfe00;
    pos[2] = (float)DAT_803dfe04;
    dir[0] = DAT_803dfe08;
    dir[1] = (float)DAT_803dfe0c;
    dir[2] = (float)DAT_803dfe10;
    player_ai_explosion_spawn(0x12, 0xffffffff, pos, dir, 0, 0, 1);

    pos[0] = FLOAT_80617f04;
    pos[1] = FLOAT_80617f08;
    pos[2] = FLOAT_80617f0c;
    player_ai_explosion_spawn(0x12, 0xffffffff, pos, dir, 0, 0, 1);
}

// ---------------------------------------------------------------------------
// 0x8020c2ac  ghz_bolt_state_reset
// Zero the 0x2c-byte drop-bolt control block (DAT_805c6648..).
// ---------------------------------------------------------------------------
void ghz_bolt_state_reset(void)
{
    memset_wrapper((void*)&DAT_805c6648, 0, 0x2c);
}

// ---------------------------------------------------------------------------
// 0x8020c2dc  ghz_bolt_seed_from_mesh
// Pick a random triangle from the launch-mesh table (DAT_80458ab0, 0x1c stride,
// 11 rows), compute its face normal (winding chosen by an rng bit), pick a random
// barycentric-ish point on it, reject if too close to the player or to an existing
// spark source, then latch the bolt origin/direction into the control block and
// spawn a billboard puff. 'bolt' == &DAT_805c6648.
// ---------------------------------------------------------------------------
void ghz_bolt_seed_from_mesh(float* bolt)
{
    unsigned int rowSel;
    unsigned int rngBit;
    int row;
    double ax, ay, az;         // base vertex
    double e0x, e0y, e0z;      // edge 0
    double e1x, e1y, e1z;      // edge 1
    double nx, ny, nz;         // normal
    double u, v;

    rowSel = __ghz_rng();
    rngBit = __ghz_rng();
    row = (rowSel % 0xb) * 0x1c;
    if (*(int*)(&DAT_80458ab0 + row) == 0)
        return;

    if ((rngBit & 1) == 0) {
        ax  = (double)*(float*)(&DAT_80458ab0 + row + 0x10);
        ay  = (double)*(float*)(&DAT_80458ab0 + row + 0x14);
        az  = (double)*(float*)(&DAT_80458ab0 + row + 0x18);
        e0x = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x2c) - ax);
        e1x = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x2c) - (double)*(float*)(&DAT_80458ab0 + row + 0x20));
        ay  = (double)*(float*)(&DAT_80458ab0 + row + 0x14);
        e0y = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x30) - (double)*(float*)(&DAT_80458ab0 + row + 0x14));
        e1y = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x30) - (double)*(float*)(&DAT_80458ab0 + row + 0x24));
        e0z = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x34) - (double)*(float*)(&DAT_80458ab0 + row + 0x18));
        e1z = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x34) - (double)*(float*)(&DAT_80458ab0 + row + 0x28));
    } else {
        ax  = (double)*(float*)(&DAT_80458ab0 + row + 4);
        ay  = (double)*(float*)(&DAT_80458ab0 + row + 8);
        az  = (double)*(float*)(&DAT_80458ab0 + row + 0xc);
        e0x = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x20) - ax);
        e0y = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x24) - ay);
        e1x = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x10) - ax);
        e1y = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x14) - ay);
        e0z = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x28) - az);
        e1z = (double)(float)((double)*(float*)(&DAT_80458ab0 + row + 0x18) - az);
    }

    nz = (double)(float)(e0x * e1y - (double)(float)(e0y * e1x));
    nx = (double)(float)(e0y * e1z - (double)(float)(e0z * e1y));
    ny = (double)(float)(e0z * e1x - (double)(float)(e0x * e1z));
    {
        float mag2 = (float)(nz * nz + (double)(float)(nx * nx + (double)(float)(ny * ny)));
        if (FLOAT_80617f10 < mag2) {
            float inv = 1.0f / SQRT(mag2);
            inv = inv * -(FLOAT_80617e8c * mag2 * inv * inv - FLOAT_80617f14);
            double s = (double)(FLOAT_80617ea0 * inv * -(FLOAT_80617e8c * mag2 * inv * inv - FLOAT_80617f14));
            nx = (double)(float)(nx * s);
            ny = (double)(float)(ny * s);
            nz = (double)(float)(nz * s);
        }
        if (ny < (double)FLOAT_80617e90) { nx = -nx; ny = -ny; nz = -nz; }
    }

    {
        unsigned int r;
        double bary;
        r = __ghz_rng();
        bary = (double)((float)((double)r - DOUBLE_80617f38) / FLOAT_80617f18);
        if ((rngBit & 1) == 0) {
            r = __ghz_rng();
            u = -bary * (double)((float)((double)r - DOUBLE_80617f38) / FLOAT_80617f18);
        } else {
            r = __ghz_rng();
            u = (double)((float)((double)FLOAT_80617ea0 - bary) *
                         ((float)((double)r - DOUBLE_80617f38) / FLOAT_80617f18));
        }
        double px = (double)(float)(e1x * u + (double)(float)(e0x * bary + ax));
        double py = (double)(float)(e1y * u + (double)(float)(e0y * bary + ay));
        double pz = (double)(float)(e1z * u + (double)(float)(e0z * bary + az));

        float dx = (float)(px - (double)*(float*)(DAT_80611d54 + 0x27c));
        float dz = (float)(py - (double)*(float*)(DAT_80611d54 + 0x284));
        float pd = dx * dx + dz * dz;
        if ((pd <= FLOAT_80617f1c) && (FLOAT_80617f20 <= pd)) {
            int off = DAT_80612610 << 2;
            int n   = DAT_80612098 - DAT_80612610;
            if (DAT_80612610 < DAT_80612098) {
                do {
                    float sx = (float)(px - (double)*(float*)(**(int**)((int)&DAT_80588610 + off) + 0x64));
                    float sz = (float)(py - (double)*(float*)(**(int**)((int)&DAT_80588610 + off) + 0x6c));
                    if (sx * sx + sz * sz < FLOAT_80617f20)
                        return;
                    off = off + 4;
                    n = n + -1;
                } while (n != 0);
            }
            unsigned int r0, r1;
            r0 = __ghz_rng();
            r1 = __ghz_rng();
            unsigned int idxA = (unsigned int)((double)FLOAT_80617ec4 *
                (double)(FLOAT_80617ef8 * ((float)((double)r0 - DOUBLE_80617f38) / FLOAT_80617f18)) + (double)FLOAT_80617e8c);
            (void)idxA;
            unsigned int iu = (unsigned int)((double)FLOAT_80617ec4 * bary + (double)FLOAT_80617e8c);
            unsigned int iv = (unsigned int)((double)FLOAT_80617ec4 *
                (double)(FLOAT_80617ec8 * ((float)((double)r1 - DOUBLE_80617f38) / FLOAT_80617f18)) + (double)FLOAT_80617e8c);
            double cs  = (double)*(float*)((int)&DAT_80583228 + ((iv + 0x200) * 4 & 0x1ffc));
            double snsn = (double)((float)(&DAT_80583228)[iu & 0x7ff] * (float)(&DAT_80583228)[iv & 0x7ff]);
            double cssn = (double)(*(float*)((int)&DAT_80583228 + ((iu + 0x200) * 4 & 0x1ffc)) * (float)(&DAT_80583228)[iv & 0x7ff]);
            double dirDot = (double)(float)(cssn * nz + (double)(float)(snsn * nx + (double)(float)(cs * ny)));
            if ((dirDot < (double)FLOAT_80617f24) && ((double)FLOAT_80617f28 < dirDot)) {
                double scl = (double)FLOAT_80617f2c;
                bolt[5] = (float)(scl * snsn);
                bolt[6] = (float)(scl * cs);
                bolt[7] = (float)(scl * cssn);
                bolt[2] = (float)((double)FLOAT_80617f28 * (double)bolt[5] + px);
                bolt[3] = (float)((double)FLOAT_80617f28 * (double)bolt[6] + pz);
                bolt[4] = (float)((double)FLOAT_80617f28 * (double)bolt[7] + py);
                bolt[3] = bolt[3] + FLOAT_80617f30;
                bolt[8] = FLOAT_80617e90;
                *(int*)bolt = 2;
                *(int*)(bolt + 1) = 0;
                if (*(int*)(bolt + 10) != 0)
                    weather_slot_destroy(*(int**)(bolt + 10));
                *(int*)(bolt + 10) = particle_spawn_billboard((int*)(bolt + 10), 0x7e, 0, (int*)(bolt + 2), 0xffff);
                particle_slot_set_opacity(FLOAT_80617f34, *(int**)(bolt + 10));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8020c7f0  ghz_bolt_master_tick
//   0 -> ghz_bolt_seed_from_mesh (find a launch point);  <2 -> travel by stepping
//   the cached direction (or time-out to idle);  ==2 -> raycast the world along
//   the direction, on hit: detonate (0x27 explosion + camera shake), throw a burst
//   of physics-debris entities (class 0x526, subobj bodies) with random impulses;
//   ==3 -> reset to idle.  Trail particle position tracks the bolt each frame.
// (The ROM debris loop runs `for(iVar6=0; iVar6<0; ...)` i.e. count comes from an
// FP-spilled register never restored here; preserved as a 0-iteration guard.)
// ---------------------------------------------------------------------------
void ghz_bolt_master_tick(void)
{
    float dt;
    float endPt[3];
    float dir[3];
    float hitPt[3];
    float hitNrm[3];
    int   nodes[10];

    FUN_803826c8();
    dt = (FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000) - DOUBLE_80617eb0)) * DAT_806108ec;
    DAT_805c6668 = DAT_805c6668 + dt;

    if (DAT_805c6648 == 2) {
        int hit = 0;
        dir[0] = DAT_805c665c * dt;
        DAT_805c6660 = -(FLOAT_80617f44 * dt * DAT_806108ec - DAT_805c6660);
        dir[1] = DAT_805c6660 * dt;
        dir[2] = DAT_805c6664 * dt;

        float aabb[6];
        aabb[0] = dir[0] + DAT_805c6650; if (DAT_805c6650 < aabb[0]) aabb[0] = DAT_805c6650;
        aabb[1] = dir[1] + DAT_805c6654; if (DAT_805c6654 < aabb[1]) aabb[1] = DAT_805c6654;
        aabb[2] = dir[2] + DAT_805c6658; if (DAT_805c6658 < aabb[2]) aabb[2] = DAT_805c6658;
        aabb[3] = dir[0] + DAT_805c6650; if (aabb[3] < DAT_805c6650) aabb[3] = DAT_805c6650;
        aabb[4] = dir[1] + DAT_805c6654; if (aabb[4] < DAT_805c6654) aabb[4] = DAT_805c6654;
        aabb[5] = dir[2] + DAT_805c6658; if (aabb[5] < DAT_805c6658) aabb[5] = DAT_805c6658;

        int cnt = scene_query_nodes_in_frustum(0, (int)aabb, (int)nodes, 10);
        int i;
        for (i = 0; i < cnt; i = i + 1) {
            int r = __ghz_scene_raycast(nodes[i], &DAT_805c6650, dir, 0,
                                        endPt, hitNrm, (int*)0, 0x2000070, 0, (int*)0);
            if (r != 0) { hit = 1; break; }
        }

        if (hit == 0) {
            if (DAT_805c6668 <= FLOAT_80617ef0) {
                DAT_805c6650 = DAT_805c6650 + dir[0];
                DAT_805c6654 = DAT_805c6654 + dir[1];
                DAT_805c6658 = DAT_805c6658 + dir[2];
            } else {
                DAT_805c6648 = 0;
            }
        } else {
            __ghz_rng();
            float sx = endPt[0] - *(float*)(DAT_80611d54 + 0x27c);
            float sy = endPt[1] - *(float*)(DAT_80611d54 + 0x280);
            float sz = endPt[2] - *(float*)(DAT_80611d54 + 0x284);
            DAT_805c6648 = 3;
            DAT_805c6650 = endPt[0];
            DAT_805c6654 = endPt[1];
            DAT_805c6658 = endPt[2];
            double shk = (double)(FLOAT_80617f48 * (FLOAT_80617f4c - (sz * sz + sx * sx + sy * sy)));
            player_ai_explosion_spawn(0x27, 0xffffffff, endPt, hitNrm, 0, 0, 1);
            if ((double)FLOAT_80617e90 < shk) {
                player_apply_camera_shake((double)(float)((double)FLOAT_80617e8c * shk), shk,
                                          (double)(float)((double)FLOAT_80617ee4 * shk), DAT_80611d78);
                double cap = (double)FLOAT_80617f54;
                *(float*)(DAT_80611d78 + 0xe54) = FLOAT_80617f50;
                if (cap < shk) shk = cap;
                *(float*)(DAT_80611d78 + 0xe58) = (float)shk;
            }
            /* debris burst: ROM iteration count arrives via an FP-spilled reg not
               restored in this frame; the loop is a no-op guard here. */
        }
        particle_slot_set_position((int*)DAT_805c6670, &DAT_805c6650);
    } else if (DAT_805c6648 < 2) {
        if (DAT_805c6648 == 0) {
            ghz_bolt_seed_from_mesh((float*)&DAT_805c6648);
        } else if (-1 < DAT_805c6648) {
            DAT_805c6650 = DAT_805c665c * dt + DAT_805c6650;
            DAT_805c6654 = DAT_805c6660 * dt + DAT_805c6654;
            DAT_805c6658 = DAT_805c6664 * dt + DAT_805c6658;
        particle_slot_set_position((int*)DAT_805c6670, &DAT_805c6650);
    } else if (DAT_805c6648 < 2) {
        }
    } else if (DAT_805c6648 < 4) {
        DAT_805c6648 = 0;
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// 0x8020d024  ghz_mths_raycast_damage
// Raycast the MTHS boss entity (4CC 'MTHS' 0x4d544853) from the FP-spilled origin
// and, on a self-hit, dispatch a damage record (built via switch_slot_reset scratch)
// at the hit point. Damage magnitudes arrive in the FP args (dmgA/dmgB).
// ---------------------------------------------------------------------------
void ghz_mths_raycast_damage(double p_unused, double dmgA, double dmgB,
                             unsigned long long p4, unsigned long long p5,
                             unsigned long long p6, unsigned long long p7,
                             unsigned long long p8)
{
    unsigned long long org;
    unsigned long long dmgRec;
    float rayLen;
    float hitPt[3];
    float hitNrm[3];
    int   rec[8];
    int   mths;
    (void)p_unused; (void)dmgA; (void)dmgB; (void)p4; (void)p5; (void)p6; (void)p7; (void)p8;

    org    = FUN_803826f0();
    rayLen = FLOAT_80617e88;
    mths   = FUN_8023c32c(0x4d544853);
    if (mths != 0) {
        float* origin = (float*)(unsigned int)org;
        int r = entity_raycast_single(mths, origin, origin, (float*)0, (float*)0, 1, 0,
                                      hitNrm, hitPt, &rayLen, 0, (int*)0);
        if (r == mths) {
            dmgRec = __ghz_switch_slot_reset((int)rec);
            (void)dmgRec;
            rec[0] = mths;
            rec[4] = (int)hitNrm;
            rec[5] = (int)hitPt;
            rec[6] = 0;                 // FLOAT_80617e90 (0.0f) distance field
            damage_dispatch_to_entity(rec);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8020d0e4  ghz_reactor_arc_emitter_tick
// Walk the live reactor arc-emitter record array (count/recPtr via FP frame, 0x28
// stride). Each record is gated by mode (DAT_80612614) and player proximity; when
// due it advances its phase timer and spawns beam particles (particle_spawn_beam)
// along a forward-basis built from its direction, colouring them from the def
// table (DAT_80458c00, 0x44 stride) with a byte-swapped packed colour. In mode 2
// it also occasionally calls ghz_mths_raycast_damage. When the on/off interval
// elapses it re-aims (toward MTHS for the 5/6 sub-types, else random) and flips
// the on/off phase.
// ---------------------------------------------------------------------------
void ghz_reactor_arc_emitter_tick(void)
{
    unsigned long long hdr;
    int   recPtr;
    int   mths;
    int   recOff;
    int   i;
    double in_f5 = 0.0, in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    float basis[16];
    float ptA[3];
    float ptB[3];

    hdr    = FUN_803826d0();
    recPtr = (int)(hdr >> 0x20);
    mths   = FUN_8023c32c(0x4d544853);

    recOff = 0;
    for (i = 0; i < (int)hdr; i = i + 1) {
        unsigned short* rec = (unsigned short*)(recPtr + recOff);
        unsigned int type = (unsigned int)*rec;

        if ((((type != 6) || (DAT_80612614 == 0x1a)) && ((type != 5) || (DAT_80612614 == 2)))) {
            float px = *(float*)(rec + 2);
            float py = *(float*)(rec + 4);
            float pz = *(float*)(rec + 6);
            int near;
            if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
                near = 1;
            } else {
                float dx = px - *(float*)(DAT_80611d78 + 0xa8);
                float dy = py - *(float*)(DAT_80611d78 + 0xac);
                float dz = pz - *(float*)(DAT_80611d78 + 0xb0);
                near = (dz * dz + dx * dx + dy * dy < FLOAT_80617f64);
            }
            if (near) {
                int    defOff = type * 0x44;
                double prevTimer = (double)*(float*)(rec + 8);
                unsigned short* def = (unsigned short*)(&DAT_80458c00 + defOff);
                *(float*)(rec + 8) =
                    (FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000) - DOUBLE_80617eb0)) *
                    DAT_806108ec + *(float*)(rec + 8);

                if (((*rec != 5) && (*rec != 6)) || (mths != 0)) {
                    if (rec[1] == 1) {
                        int nowSeg  = (int)((double)*(float*)(rec + 8) / (double)*(float*)(&DAT_80458c34 + defOff));
                        int prevSeg = (int)(prevTimer / (double)*(float*)(&DAT_80458c34 + defOff));
                        if (prevSeg < nowSeg) {
                            mtx4_basis_from_forward(basis, (float*)(rec + 10));
                            if ((&DAT_80458c02)[defOff] == '\0') {
                                int vo = 0;
                                int k;
                                for (k = 0; k < (int)(unsigned int)*def; k = k + 1) {
                                    ptA[0] = *(float*)((int)def + vo + 4);
                                    ptA[1] = *(float*)((int)def + vo + 8);
                                    ptA[2] = *(float*)((int)def + vo + 0xc);
                                    mtx4_transform_vector(basis, ptA);
                                    ptA[0] = ptA[0] + *(float*)(rec + 2);
                                    ptA[1] = (float)((double)ptA[1] + (double)*(float*)(rec + 4));
                                    ptA[2] = (float)((double)ptA[2] + (double)*(float*)(rec + 6));
                                    int beam = __ghz_particle_beam(0, 2, ptA, (int*)(rec + 10), 0xffff);
                                    unsigned int c = *(unsigned int*)(&DAT_80458c40 + defOff);
                                    particle_slot_set_sub_count((int*)beam,
                                        c << 0x18 | (c & 0xff00) << 8 | c >> 0x18 | c >> 8 & 0xff00);
                                    if ((DAT_80612614 == 2) && ((game_rand() & 3) == 0) &&
                                        (*rec != 5) && (*rec != 6)) {
                                        ghz_mths_raycast_damage((double)FLOAT_80617ee0, 0, 0,
                                                                0, in_f5, in_f6, in_f7, in_f8);
                                    }
                                    vo = vo + 0xc;
                                }
                            } else {
                                int idx = (int)(*(float*)(rec + 8) / *(float*)(&DAT_80458c34 + defOff));
                                idx = idx - (idx / (int)(unsigned int)*def) * (unsigned int)*def;
                                ptB[0] = *(float*)(def + idx * 6 + 2);
                                ptB[1] = *(float*)(def + idx * 6 + 4);
                                ptB[2] = *(float*)(def + idx * 6 + 6);
                                mtx4_transform_vector(basis, ptB);
                                ptB[0] = ptB[0] + *(float*)(rec + 2);
                                ptB[1] = (float)((double)ptB[1] + (double)*(float*)(rec + 4));
                                ptB[2] = (float)((double)ptB[2] + (double)*(float*)(rec + 6));
                                int beam = __ghz_particle_beam(0, 2, ptB, (int*)(rec + 10), 0xffff);
                                unsigned int c = *(unsigned int*)(&DAT_80458c40 + defOff);
                                particle_slot_set_sub_count((int*)beam,
                                    c << 0x18 | (c & 0xff00) << 8 | c >> 0x18 | c >> 8 & 0xff00);
                                if ((DAT_80612614 == 2) && ((game_rand() & 3) == 0) &&
                                    (*rec != 5) && (*rec != 6)) {
                                    ghz_mths_raycast_damage((double)FLOAT_80617ee0, 0, 0,
                                                            0, in_f5, in_f6, in_f7, in_f8);
                                }
                            }
                        }
                        if (*(float*)(&DAT_80458c38 + defOff) * *(float*)(rec + 0x10) < *(float*)(rec + 8)) {
                            if ((*rec == 5) || (*rec == 6)) {
                                int m = FUN_8023c32c(0x4d544853);
                                if (m != 0) {
                                    *(float*)(rec + 10) = *(float*)(m + 0x64) - *(float*)(rec + 2);
                                    *(float*)(rec + 0xc) = *(float*)(m + 0x68) - *(float*)(rec + 4);
                                    *(float*)(rec + 0xe) = *(float*)(m + 0x6c) - *(float*)(rec + 6);
                                    float ex = *(float*)(rec + 10);
                                    float mag2 = *(float*)(rec + 0xe) * *(float*)(rec + 0xe) + ex * ex +
                                                 *(float*)(rec + 0xc) * *(float*)(rec + 0xc);
                                    if (FLOAT_80617f10 < mag2) {
                                        float inv = 1.0f / SQRT(mag2);
                                        inv = inv * -(FLOAT_80617e8c * mag2 * inv * inv - FLOAT_80617f14);
                                        double s = (double)(FLOAT_80617ea0 * inv * -(FLOAT_80617e8c * mag2 * inv * inv - FLOAT_80617f14));
                                        *(float*)(rec + 10) = (float)((double)ex * s);
                                        *(float*)(rec + 0xc) = (float)((double)*(float*)(rec + 0xc) * s);
                                        *(float*)(rec + 0xe) = (float)((double)*(float*)(rec + 0xe) * s);
                                    }
                                }
                            } else {
                                double ang = (double)math_atan2(
                                    (double)(*(float*)(DAT_80611d54 + 0x27c) - *(float*)(rec + 2)),
                                    (double)(*(float*)(DAT_80611d54 + 0x284) - *(float*)(rec + 6)));
                                ang = (double)(float)((double)FLOAT_80617ec8 *
                                        (double)(float)(game_frand() - (double)FLOAT_80617e8c) + ang);
                                double r2 = game_frand();
                                unsigned int iu = (unsigned int)((double)FLOAT_80617ec4 * ang + (double)FLOAT_80617e8c);
                                unsigned int iv = (unsigned int)((double)FLOAT_80617ec4 *
                                    (double)((float)((double)FLOAT_80617ec8 * r2) * FLOAT_80617f6c) + (double)FLOAT_80617e8c);
                                *(float*)(rec + 10) = (float)(&DAT_80583228)[iu & 0x7ff] * (float)(&DAT_80583228)[iv & 0x7ff];
                                *(int*)(rec + 0xc) = *(int*)((int)&DAT_80583228 + ((iv + 0x200) * 4 & 0x1ffc));
                                *(float*)(rec + 0xe) = *(float*)((int)&DAT_80583228 + ((iu + 0x200) * 4 & 0x1ffc)) *
                                                       (float)(&DAT_80583228)[iv & 0x7ff];
                            }
                            *(float*)(rec + 8) = FLOAT_80617e90;
                            if ((&DAT_80458c03)[defOff] == '\0') {
                                *(float*)(rec + 0x12) = FLOAT_80617ea0;
                            } else {
                                *(float*)(rec + 0x12) = (float)game_frand();
                            }
                            rec[1] = 0;
                        }
                    } else if ((rec[1] == 0) &&
                               (*(float*)(&DAT_80458c3c + defOff) * *(float*)(rec + 0x12) < *(float*)(rec + 8))) {
                        *(float*)(rec + 8) = FLOAT_80617e90;
                        if ((&DAT_80458c03)[defOff] == '\0') {
                            *(float*)(rec + 0x10) = FLOAT_80617ea0;
                        } else {
                            *(float*)(rec + 0x10) = (float)game_frand();
                        }
                        rec[1] = 1;
                    }
                }
            }
        }
        recOff = recOff + 0x28;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x8020d720  ghz_reactor_arc_emitter_thunk
// Tail-call wrapper for the arc-emitter tick (distinct actor callback slot).
// ---------------------------------------------------------------------------
void ghz_reactor_arc_emitter_thunk(void)
{
    ghz_reactor_arc_emitter_tick();
}

// ---------------------------------------------------------------------------
// 0x8020d74c  ghz_prop_emitter_spawn
// Per-prop emitter setup: resolve the prop's linked 4CC (rec[0]+0xac). If it is the
// MTHS class 0x539, run ghz_nop and mark its render state 3; otherwise, if its
// render-data slot has no live particle handle, spawn the drop-trail emitter
// (DAT_805c65c4) at the linked entity's position with the cached offset DAT_803dfe20.
// ---------------------------------------------------------------------------
void ghz_prop_emitter_spawn(int* rec)
{
    int ent;
    int rdata;
    int handle;
    float off[3];

    ent = FUN_8023c32c(*(unsigned int*)(*rec + 0xac));
    if (ent != 0) {
        if (*(short*)(ent + 0x94) == 0x539) {
            ghz_nop();
            *(int*)(ent + 0x1a0) = 3;
        } else {
            rdata = *(int*)(ent + 0x7c);
            if (rdata != 0) {
                handle = *(int*)(rdata + 0x11c);
                if (!particle_handle_is_valid(&handle)) {
                    off[0] = DAT_803dfe20;
                    off[1] = (float)DAT_803dfe24;
                    off[2] = (float)DAT_803dfe28;
                    particle_emitter_spawn(rdata + 0x11c, (int)&DAT_805c65c4, 0x8001000,
                                           (float*)(ent + 0x64), off);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8020d80c  ghz_ship_turret_tick
// Per-frame tick for the 'SBSN'/'SHIP' turret actor. Fires scripted anim-event
// explosions (event frames 0x80 -> type 0x27, 0xb6 -> type 0x1d while playing the
// 'SBSN' clip), builds the muzzle matrix, scans nearby 'SHIP' actors (proximity
// radius^2 FLOAT_80617f70) and — if one is inside a forward cone — arms a shot;
// keeps the linked trail emitter positioned; and when the fire cooldown (+0x1a4)
// elapses lobs a projectile (class 0x51a/0x51d -> proj type 0x2e, else 0x2d).
// ---------------------------------------------------------------------------
void ghz_ship_turret_tick(void)
{
    int   actor;
    int   trail;
    float* barrelA;
    float* barrelB;
    float* muzzleBase = (float*)0;
    int    canFire = 0;
    float  aim[3];
    float  evPos[3];
    float  evDir[3];
    float  mtx[16];
    float  pt[3];
    int    ships[31];
    int    firePts[2];
    int    firePtsExt[6];
    int    projType;
    int*   modelHdr;

    actor = FUN_803826d8();
    trail = *(int*)(actor + 0x7c);
    aim[0] = DAT_803dfe2c;
    aim[1] = DAT_803dfe30;
    aim[2] = DAT_803dfe34;

    if (*(int*)(actor + 0x60) == 0x4c4e5342) {   /* 'SBSN' active clip */
        int lo, hi;
        int slot = anim_active_clip_slot(actor);
        anim_get_frame_times(slot, &lo, &hi);
        evPos[0] = *(float*)(actor + 0x64) + *(float*)(actor + 0x1a8);
        evPos[1] = *(float*)(actor + 0x68) + *(float*)(actor + 0x1ac);
        evPos[2] = *(float*)(actor + 0x6c) + *(float*)(actor + 0x1b0);
        int f = hi;
        while (f = f + 1, f <= lo) {
            if (f == 0xb6) {
                evDir[0] = FLOAT_80617e90; evDir[1] = FLOAT_80617f28; evDir[2] = FLOAT_80617e90;
                player_ai_explosion_spawn(0x1d, 0xffffffff, evPos, evDir, actor, 0, 1);
            } else if ((f < 0xb6) && (f == 0x80)) {
                evDir[0] = FLOAT_80617e90; evDir[1] = FLOAT_80617ea0; evDir[2] = FLOAT_80617e90;
                player_ai_explosion_spawn(0x27, 0xffffffff, evPos, evDir, actor, 0, 1);
            }
        }
    }

    __ghz_mtx4_set_translation((double)*(float*)(actor + 0x64), (double)*(float*)(actor + 0x68),
                               (double)*(float*)(actor + 0x6c));
    mtx4_multiply(*(float**)(*(int*)(actor + 4) + 0x10), mtx, mtx);
    mtx4_transform_vector(mtx, aim);

    if (DAT_806108e4 != 0) {
        int m10 = *(int*)(*(int*)(actor + 4) + 0x10);
        *(int*)(actor + 0xe0) = *(int*)(actor + 0xb4);
        *(int*)(actor + 0xe4) = *(int*)(actor + 0xb8);
        *(int*)(actor + 0xe8) = *(int*)(actor + 0xbc);
        *(float*)(actor + 0xb4) = *(float*)(m10 + 0x30) + *(float*)(actor + 0x64);
        *(float*)(actor + 0xb8) = *(float*)(m10 + 0x34) + *(float*)(actor + 0x68);
        *(float*)(actor + 0xbc) = *(float*)(m10 + 0x38) + *(float*)(actor + 0x6c);
    }

    int n = FUN_8023c59c(0x53484950, (int)ships, 0x1e);   /* 'SHIP' */
    int off = 0;
    if (0 < n) {
        do {
            int other = *(int*)((int)ships + off);
            if (actor != other) {
                float dx = *(float*)(actor + 0x64) - *(float*)(other + 0x64);
                float dy = *(float*)(actor + 0x68) - *(float*)(other + 0x68);
                float dz = *(float*)(actor + 0x6c) - *(float*)(other + 0x6c);
                if (dz * dz + dx * dx + dy * dy < FLOAT_80617f70) {
                    float rx = *(float*)(other + 0x64) - *(float*)(actor + 0x64);
                    float ry = *(float*)(other + 0x68) - *(float*)(actor + 0x68);
                    float rz = *(float*)(other + 0x6c) - *(float*)(actor + 0x6c);
                    float mag2 = rz * rz + rx * rx + ry * ry;
                    if (FLOAT_80617f10 < mag2) {
                        float inv = 1.0f / SQRT(mag2);
                        inv = inv * -(FLOAT_80617e8c * mag2 * inv * inv - FLOAT_80617f14);
                        float s = FLOAT_80617ea0 * inv * -(FLOAT_80617e8c * mag2 * inv * inv - FLOAT_80617f14);
                        rx = rx * s; ry = ry * s; rz = rz * s;
                    }
                    if (DAT_80583cd4 < rz * aim[2] + rx * aim[0] + ry * aim[1])
                        canFire = 1;
                }
            }
            off = off + 4;
            n = n + -1;
        } while (n != 0);
    }

    *(float*)(actor + 0x1a4) =
        -((FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0)) *
          (float)((double)(CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000)) - DOUBLE_80617eb0) -
          *(float*)(actor + 0x1a4));

    if (trail != 0) {
        int h = *(int*)(trail + 0x11c);
        if (particle_handle_is_valid(&h)) {
            int hp = *(int*)(trail + 0x11c);
            particle_cached_set_position((ParticleEmitter**)&hp, (Vec3*)(actor + 0x64));
            int hv = *(int*)(trail + 0x11c);
            particle_cached_set_velocity((ParticleEmitter**)&hv, (Vec3*)aim);
        }
        int* mh = *(int**)(*(int*)(actor + 4) + 4);
        int base = mh[*mh * -0x30 + 0x1b];
        if (*(short*)(actor + 0x94) == 0x51a) {
            barrelA = (float*)(base + 0x30);
            barrelB = (float*)(base + 0x48);
        } else {
            muzzleBase = (float*)base;
            barrelA = muzzleBase + 6;
            barrelB = muzzleBase + 0xc;
        }
        pt[0] = barrelA[0]; pt[1] = barrelA[1]; pt[2] = barrelA[2];
        mtx4_transform_point(mtx, pt);
        particle_slot_set_position(*(int**)(trail + 0xf4), pt);
        pt[0] = barrelB[0]; pt[1] = barrelB[1]; pt[2] = barrelB[2];
        mtx4_transform_point(mtx, pt);
        particle_slot_set_position(*(int**)(trail + 0xf8), pt);
        if (muzzleBase != (float*)0) {
            pt[0] = muzzleBase[0]; pt[1] = muzzleBase[1]; pt[2] = muzzleBase[2];
            mtx4_transform_point(mtx, pt);
            particle_slot_set_position(*(int**)(trail + 0xfc), pt);
        }
    }

    modelHdr = *(int**)(*(int*)(actor + 4) + 4);
    if ((*(short*)(actor + 0x94) == 0x51a) || (*(short*)(actor + 0x94) == 0x51d)) {
        int base = modelHdr[*modelHdr * -0x30 + 0x1b];
        projType = 0x2e;
        firePtsExt[0] = base + 0x18;
        firePtsExt[1] = base + 0x60;
        firePtsExt[2] = base + 0x78;
        firePtsExt[3] = base + 0x90;
        firePtsExt[4] = base + 0xa8;
        firePtsExt[5] = base + 0xc0;
        firePts[0] = base;
        firePts[1] = base + 0x18;
    } else {
        projType = 0x2d;
        firePts[0] = modelHdr[*modelHdr * -0x30 + 0x7b];
        firePts[1] = modelHdr[*modelHdr * -0x30 + 0xab];
    }

    if (*(float*)(actor + 0x1a4) < FLOAT_80617e90) {
        *(int*)(actor + 0x1a0) = *(int*)(actor + 0x1a0) + 1;
        if (canFire) {
            *(float*)(actor + 0x1a4) = *(float*)(actor + 0x1a4) + FLOAT_80617ee4;
            float* fp = (float*)firePts[*(int*)(actor + 0x1a0) % 2];
            pt[0] = fp[0]; pt[1] = fp[1]; pt[2] = fp[2];
            mtx4_transform_point(mtx, pt);
            player_ai_projectile_spawn_simple((void*)0, (double)FLOAT_80617e90,
                                              (void*)(intptr_t)projType, (void*)pt, (int*)aim, actor,
                                              (int*)0, 6, (void*)0, *(int*)(actor + 0x34), 0, 2);
            (void)FLOAT_80617f74;
        } else {
            *(float*)(actor + 0x1a4) = FLOAT_80617e90;
        }
        (void)firePtsExt;
    }
    FUN_80382724();
}

// ===========================================================================
// batch_655 : GHZ debris burst / guided-missile / object-init dispatcher C
// 0x8020df34 - 0x80210a4c  (10 functions, subsystem:game, worm-effect)
//
// Continuation of the GHZ set-piece:
//  * ghz_debris_burst_spawn: on an actor break, spawn 30 debris-fragment entities
//    (class picked from table DAT_80458f6c by the +0x94 class word) with random
//    velocities/spins, biased by the parent's frame velocity, then a type-0x10
//    explosion.
//  * ghz_debris_physics_tick / ghz_debris_impact_tick / ghz_debris_force_tick:
//    per-fragment velocity+spin integration, expire->explosion (0x28/0x2f), and a
//    subobj rigid-body force application variant.
//  * ghz_missile_tick (0x8020e7d0): the big guided-projectile per-frame state
//    machine (state at +0x1a0/word[0x68]): spin-up, homing/steer toward the
//    nearest valid player, fire sub-projectiles (class 0x32), spawn spark/streak
//    trails, and on the terminal state spawn the payload entity (class 0x53a).
//  * ghz_object_init_dispatch_c (0x802100e4): the third GHZ object-init switch
//    (class 0x511..0x53d) wiring render/update callbacks, flags, billboards and
//    subobj bodies per prop variant - sibling of ghz_object_init_dispatch(_b).
//  * small helpers: ghz_effect_progress_ratio, ghz_mths_set_state2,
//    ghz_emitter_bones_spawn, ghz_outy_damage_gate.
//
// Non-matching: 4CC ids ('MTHS' 0x4d544853, 'OUTY' 0x5954554f, 'ZPCT'/team words,
// 'CEL1'/'CEL6' 0x43454c31/36); 0x43300000 bias int<->float math and
// (uint^0x80000000)/2^32 rng->float written as plain float math; wide FP-ABI
// spawn helpers routed through file-local int-returning aliases; PPC FP-spill
// frame open/close helpers opaque.
// ---------------------------------------------------------------------------

// FP-spill frame open/close helpers (opaque) used by this block.
// (FUN_803826e4 / FUN_80382730 already declared in earlier batches.)
extern int  FUN_803826e8(void);                 // open + return actor ptr
extern void FUN_80382734(void);                 // close (emitter-bones spawn)
// GPR-spill intrinsics for the guided-missile frame.
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

// int-returning / wide-FP spawn helpers (generated_decls narrows/returns void).
typedef int (*GhzSpawnActFn)(double f1, double f2, double f3, double f4, double f5,
                             double f6, double f7, unsigned long long f8, int typeData,
                             int classId, float* pos, float* vel, int a, int b);
static const GhzSpawnActFn __ghz_spawn_with_activation = (GhzSpawnActFn)&entity_spawn_with_activation;

typedef int (*GhzSpawnTypeFn)(unsigned long long f1, double f2, double f3, double f4, double f5,
                              double f6, double f7, unsigned long long f8, int classId,
                              int* fields, float* xform, int a, int b);
static const GhzSpawnTypeFn __ghz_spawn_by_type = (GhzSpawnTypeFn)&entity_spawn_by_type;

// player_ai_projectile_spawn_simple: ROM returns the spawned entity in r3.
typedef int (*GhzProjSimpleFn)(double accuracy, double f2, int projType, float* pos, int* dir,
                               int owner, int* soundDef, int weaponMode, int a, int ownerId,
                               int b, int c);
static const GhzProjSimpleFn __ghz_proj_simple = (GhzProjSimpleFn)&player_ai_projectile_spawn_simple;

// weather_particle_spawn_streak: ROM returns a handle in r3.
typedef int (*GhzStreakFn)(double lifetime, double f2, int player, float* pos, float* vel);
static const GhzStreakFn __ghz_spawn_streak = (GhzStreakFn)&weather_particle_spawn_streak;

// FUN_802325b8 (find actor by 4CC -> ptr) / FUN_80232740 (predicate on ptr).
extern int  FUN_802325b8(int classWord);
extern int  FUN_80232740(int actorPtr);

// --- batch_655 raw data symbols (file-local) ---
extern int   DAT_80458f6c;      // debris class table (per class-group, 0x18 stride; [0]=modulus)
extern int   DAT_805c6674;      // emitter def A (bone spawn)
extern int   DAT_805c66f8;      // emitter def B (center spawn)
extern int   DAT_805c677c;      // missile ground-trail cached emitter def
extern int   DAT_805c6800;      // 0x538-prop emitter def
extern int   DAT_805c67dc;      // missile aim/damp cached scalar
extern int   DAT_80566f4c;      // spark burst emitter def
extern int   DAT_80567790;      // payload burst emitter def A
extern int   DAT_8056770c;      // payload burst emitter def B
extern int   DAT_805675cc;      // payload burst emitter def C
extern int   DAT_80612808;      // cached emitter handle (emitter-bones / 0x538)
extern int   DAT_80612804;      // cached emitter handle (missile ground trail)
extern int   DAT_806127f4;      // cached 'OUTY' actor ptr (damage gate)
extern float FLOAT_806127f8;    // missile spark-spawn timer
extern float FLOAT_806127fc;    // missile 3D-audio source scalar
extern float FLOAT_80612800;    // missile terminal-state effect timer
extern float FLOAT_806127f4;    // (alias slot; not used as float here)

// batch_655 tuning float pool 0x80617f78..0x80617ff0
extern float FLOAT_80617f78; extern float FLOAT_80617f7c; extern float FLOAT_80617f80;
extern float FLOAT_80617f84; extern float FLOAT_80617f88; extern float FLOAT_80617f8c;
extern float FLOAT_80617f90; extern float FLOAT_80617f94; extern float FLOAT_80617f98;
extern float FLOAT_80617f9c; extern float FLOAT_80617fa0; extern float FLOAT_80617fa4;
extern float FLOAT_80617fa8; extern float FLOAT_80617fac; extern float FLOAT_80617fb0;
extern float FLOAT_80617fb4; extern float FLOAT_80617fb8; extern float FLOAT_80617fbc;
extern float FLOAT_80617fc0; extern float FLOAT_80617fc4; extern float FLOAT_80617fc8;
extern float FLOAT_80617fcc; extern float FLOAT_80617fd0; extern float FLOAT_80617fd4;
extern float FLOAT_80617fd8; extern float FLOAT_80617fdc; extern float FLOAT_80617fe0;
extern float FLOAT_80617fe4; extern float FLOAT_80617fe8; extern float FLOAT_80617fec;
extern float FLOAT_80617ff0;

// batch_655 cached position/dir vectors (0x803dfe38..0x803dfed0)
extern float DAT_803dfe38; extern int DAT_803dfe3c; extern int DAT_803dfe40;   // burst explosion dir
extern float DAT_803dfe44; extern int DAT_803dfe48; extern int DAT_803dfe4c;
extern float DAT_803dfe50; extern int DAT_803dfe54; extern int DAT_803dfe58;
extern float DAT_803dfe5c; extern int DAT_803dfe60; extern int DAT_803dfe64;
extern float DAT_803dfe68; extern int DAT_803dfe6c; extern int DAT_803dfe70;
extern float DAT_803dfe74; extern int DAT_803dfe78; extern int DAT_803dfe7c;
extern float DAT_803dfe80; extern float DAT_803dfe84; extern float DAT_803dfe88;
extern float DAT_803dfe8c; extern float DAT_803dfe90; extern float DAT_803dfe94;
extern float DAT_803dfe98; extern int DAT_803dfe9c; extern int DAT_803dfea0;
extern float DAT_803dfea4; extern int DAT_803dfea8; extern int DAT_803dfeac;
extern float DAT_803dfeb0; extern int DAT_803dfeb4; extern int DAT_803dfeb8;
extern float DAT_803dfebc; extern int DAT_803dfec0; extern int DAT_803dfec4;
extern float DAT_803dfec8; extern int DAT_803dfecc; extern int DAT_803dfed0;
extern float DAT_803dfed4; extern float DAT_803dfed8; extern int DAT_803dfedc;

// --- batch_655 function forward decls ---
void ghz_debris_burst_spawn(void);
void ghz_debris_physics_tick(int* self);
void ghz_emitter_bones_spawn(void);
double ghz_effect_progress_ratio(void);
void ghz_mths_set_state2(void);
void ghz_missile_tick(void);
void ghz_object_init_dispatch_c(void);
void ghz_debris_impact_tick(unsigned long long p1, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8, int* rec);
void ghz_debris_force_tick(int self);
void ghz_outy_damage_gate(int* eventData);

// ---------------------------------------------------------------------------
// 0x8020df34  ghz_debris_burst_spawn
// On an actor break, spawn 30 debris-fragment entities (class cycled from table
// DAT_80458f6c, selected group by the +0x94 class word via countLeadingZeros)
// with random velocities + spins, biased by the parent's per-frame velocity
// (+0xb4..+0xbc minus previous +0xe0..+0xe8), then a type-0x10 explosion.
// ---------------------------------------------------------------------------
void ghz_debris_burst_spawn(void)
{
    unsigned long long info;
    int actor;
    unsigned int grp;
    float frameScale;
    double biasX, biasY, biasZ;
    float basePos[3];
    float expDir[3];
    int i, cyc;
    int spawned;

    info  = FUN_803826e4();
    actor = (int)(info >> 0x20);
    expDir[0] = DAT_803dfe38;
    expDir[1] = (float)DAT_803dfe3c;
    expDir[2] = (float)DAT_803dfe40;
    grp = countLeadingZeros(0x51e - (unsigned int)*(unsigned short*)(actor + 0x94));
    basePos[0] = *(float*)(actor + 0x64) + *(float*)(actor + 0x1a8);
    basePos[1] = *(float*)(actor + 0x68) + *(float*)(actor + 0x1ac);
    basePos[2] = *(float*)(actor + 0x6c) + *(float*)(actor + 0x1b0);

    if (DAT_806108e4 < 1) {
        frameScale = (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
    } else {
        frameScale = (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0) / DAT_806108ec;
    }
    biasX = (double)((*(float*)(actor + 0xb4) - *(float*)(actor + 0xe0)) * frameScale);
    biasY = (double)((*(float*)(actor + 0xb8) - *(float*)(actor + 0xe4)) * frameScale);
    biasZ = (double)((*(float*)(actor + 0xbc) - *(float*)(actor + 0xe8)) * frameScale);

    cyc = 0;
    for (i = 0; i < 0x1e; i = i + 1) {
        float vel[3];
        vel[0] = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) * FLOAT_80617ec8;
        vel[1] = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) * FLOAT_80617ec8;
        vel[2] = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) * FLOAT_80617ec8;

        int classId = *(int*)((int)(&DAT_80458f6c + (grp >> 5) * 0x18) + (cyc + 1) * 4);
        int frag = __ghz_spawn_with_activation(0, 0, 0, 0, 0, 0, 0, 0, 1, classId, basePos, vel, 0, 0);
        if (frag != 0) {
            *(float*)(frag + 0xb4) = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            *(float*)(frag + 0xb8) = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            *(float*)(frag + 0xbc) = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            *(float*)(frag + 0xb4) = *(float*)(frag + 0xb4) * FLOAT_80617f34;
            *(float*)(frag + 0xb8) = *(float*)(frag + 0xb8) * FLOAT_80617f34;
            *(float*)(frag + 0xbc) = *(float*)(frag + 0xbc) * FLOAT_80617f34;
            if ((int)info != 0) {
                *(float*)(frag + 0xb4) = (float)((double)*(float*)(frag + 0xb4) + biasX);
                *(float*)(frag + 0xb8) = (float)((double)*(float*)(frag + 0xb8) + biasY);
                *(float*)(frag + 0xbc) = (float)((double)*(float*)(frag + 0xbc) + biasZ);
            }
            *(unsigned char*)(frag + 0x32) = 2;
            *(float*)(frag + 0xe0) = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            *(float*)(frag + 0xe4) = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            *(float*)(frag + 0xe8) = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            *(float*)(frag + 0xe0) = *(float*)(frag + 0xe0) * FLOAT_80617f78;
            *(float*)(frag + 0xe4) = *(float*)(frag + 0xe4) * FLOAT_80617f78;
            *(float*)(frag + 0xe8) = *(float*)(frag + 0xe8) * FLOAT_80617f78;
            *(int*)(frag + 0xc0) = *(int*)(frag + 0xe0);
            *(int*)(frag + 0x70) = *(int*)(frag + 0xe4);
            *(int*)(frag + 0xc8) = *(int*)(frag + 0xe8);
        }
        int modulus = *(int*)(&DAT_80458f6c + (grp >> 5) * 0x18);
        spawned = cyc + 1;
        cyc = spawned - (spawned / modulus) * modulus;
    }
    player_ai_explosion_spawn(0x10, 0, basePos, expDir, 0, 0, 1);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8020e3f0  ghz_debris_physics_tick
// Per-frame debris-fragment velocity + spin integration (fields word[0x19..0x1c],
// word[0x2d..0x3a]), scaled by the frame delta, then weapon_dropped_fade_tick.
// ---------------------------------------------------------------------------
void ghz_debris_physics_tick(int* self)
{
    float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
    float* f = (float*)self;

    f[0x2e] = -(FLOAT_80617f44 * DAT_806108ec * dt - f[0x2e]);
    f[0x19] = f[0x2d] * DAT_806108ec * dt + f[0x19];
    f[0x1a] = f[0x2e] * DAT_806108ec * dt + f[0x1a];
    f[0x1b] = f[0x2f] * DAT_806108ec * dt + f[0x1b];
    f[0x30] = dt * f[0x38] * DAT_806108ec + f[0x30];
    f[0x1c] = dt * f[0x39] * DAT_806108ec + f[0x1c];
    f[0x32] = dt * f[0x3a] * DAT_806108ec + f[0x32];
    *(unsigned char*)((int)self + 0x32) = 2;
    weapon_dropped_fade_tick(self);
}

// ---------------------------------------------------------------------------
// 0x8020e590  ghz_emitter_bones_spawn
// Build the actor local matrix, and on first activation (+0x1a0 != 0) spawn a
// per-bone emitter (DAT_805c6674) at each model bone plus a center emitter
// (DAT_805c66f8); then keep the cached emitter (DAT_80612808) positioned at, and
// moving with, model bone 0x280.
// ---------------------------------------------------------------------------
void ghz_emitter_bones_spawn(void)
{
    int actor;
    int model;
    int boneOff;
    int i;
    int cachedA, cachedB;
    float mtx[22];
    float boneA[3];
    float boneB[3];
    float trailPos[3];
    float trailVel[3];
    float emitA[3];
    float centerPos[3];
    float centerVel[3];

    actor = FUN_803826e8();
    trailPos[0] = DAT_803dfe44; trailPos[1] = (float)DAT_803dfe48; trailPos[2] = (float)DAT_803dfe4c;
    trailVel[0] = DAT_803dfe50; trailVel[1] = (float)DAT_803dfe54; trailVel[2] = (float)DAT_803dfe58;
    boneB[0]    = DAT_803dfe5c; boneB[1]    = (float)DAT_803dfe60; boneB[2]    = (float)DAT_803dfe64;
    entity_build_local_matrix(mtx, (void*)actor, 1, 0);

    if (*(int*)(actor + 0x1a0) != 0) {
        boneOff = 0x40;
        emitA[0]    = DAT_803dfe68; emitA[1]    = (float)DAT_803dfe6c; emitA[2]    = (float)DAT_803dfe70;
        centerVel[0]= DAT_803dfe74; centerVel[1]= (float)DAT_803dfe78; centerVel[2]= (float)DAT_803dfe7c;
        model = *(int*)(actor + 4);
        for (i = 1; i < *(int*)(*(int*)(model + 4) + 4); i = i + 1) {
            model = *(int*)(actor + 4);
            boneA[0] = *(float*)(*(int*)(model + 0x10) + boneOff + 0x30);
            int m10 = *(int*)(*(int*)(actor + 4) + 0x10);
            boneA[1] = *(float*)(m10 + boneOff + 0x34);
            boneA[2] = *(float*)(m10 + boneOff + 0x38);
            mtx4_transform_point(mtx, boneA);
            particle_emitter_spawn(0, (int)&DAT_805c6674, 1, boneA, emitA);
            boneOff = boneOff + 0x40;
        }
        // model here == *(actor+4)+0x10 base from last iteration
        centerPos[0] = DAT_803dfe68; centerPos[1] = (float)DAT_803dfe6c; centerPos[2] = (float)DAT_803dfe70;
        mtx4_transform_point(*(float**)(*(int*)(actor + 4) + 0x10), centerPos);
        mtx4_transform_point(mtx, centerPos);
        particle_emitter_spawn(0, (int)&DAT_805c66f8, 1, centerPos, centerVel);
        *(int*)(actor + 0x1a0) = 0;
    }

    mtx4_transform_point((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + 0x280), trailPos);
    mtx4_transform_point(mtx, trailPos);
    mtx4_transform_vector((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + 0x280), boneB);
    mtx4_transform_vector(mtx, boneB);
    cachedA = DAT_80612808;
    particle_cached_set_position((ParticleEmitter**)&cachedA, (Vec3*)trailPos);
    cachedB = DAT_80612808;
    particle_cached_set_velocity((ParticleEmitter**)&cachedB, (Vec3*)boneB);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8020e788  ghz_effect_progress_ratio
// Return the missile terminal-effect timer as a 0..1 progress ratio.
// ---------------------------------------------------------------------------
double ghz_effect_progress_ratio(void)
{
    return (double)(FLOAT_80612800 / FLOAT_80617f40);
}

// ---------------------------------------------------------------------------
// 0x8020e798  ghz_mths_set_state2
// Set the MTHS boss (4CC 'MTHS' 0x4d544853) render/update state field +0x1a0 = 2.
// ---------------------------------------------------------------------------
void ghz_mths_set_state2(void)
{
    int mths = FUN_8023c32c(0x4d544853);
    if (mths != 0)
        *(int*)(mths + 0x1a0) = 2;
}

// ---------------------------------------------------------------------------
// 0x8020e7d0  ghz_missile_tick
// Guided-projectile per-frame state machine (state word[0x68], substate word[0x69]).
// Integrates torque/angular velocity, orients the trail bones, and per state:
//   0: free-fall/idle spin;  1: locked;  2: homing toward stored target;
//   <0: acquire nearest valid player, steer, and fire a class-0x32 sub-projectile;
//   3: charge windup with a sine-LUT wobble + periodic spark bursts;
//   4: detonation approach;  5: terminal - spawn streak + payload entity (0x53a)
//   and destroy self;  else (<5): ground-trail cached emitter + spark bursts +
//   time-gated payload burst rings.  Closes with a 3D audio source update.
// (Large FP-register spill locals dropped; ROM debris no-op loops preserved as
// guards. Heavy 0x43300000/rng bias math written as plain float math.)
// ---------------------------------------------------------------------------
void ghz_missile_tick(void)
{
    int* self;
    int i, io;
    float torque[3];
    float aim[3];
    float basisMtx[17];
    float streakPos[3];
    float streakDir[3];
    float sprPos[4];
    int handleA, handleB, handleC, handleD;
    int spawn;

    self = (int*)__save_gpr_25();
    torque[0] = DAT_803dfe80; torque[1] = DAT_803dfe84; torque[2] = DAT_803dfe88;
    aim[0]    = DAT_803dfe8c; aim[1]    = DAT_803dfe90; aim[2]    = DAT_803dfe94;

    if (DAT_806108e4 != 0) {
        // per-sub-node draw flag update
        io = 0;
        for (i = 0; i < self[7]; i = i + 1) {
            unsigned short* flags = (unsigned short*)(*(int*)(self[0x7e] + io) + 0x28);
            if ((double)(float)self[0x73] <= (double)FLOAT_80617e90)
                *flags = *flags & 0xf3ff;
            else
                *flags = *flags | 0xc00;
            io = io + 4;
        }
        ghz_droptrail_advance();

        if (self[0x68] != 5) {
            float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
            float rndT[3];
            rndT[0] = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            rndT[1] = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            rndT[2] = FLOAT_80617ea4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c);
            self[0x38] = (float)((double)rndT[0] * (double)(FLOAT_80617ea0 * FLOAT_80617e9c * dt * DAT_806108ec) + (double)(float)self[0x38]);
            self[0x39] = (float)((double)rndT[1] * (double)(FLOAT_80617ea0 * FLOAT_80617e9c * dt * DAT_806108ec) + (double)(float)self[0x39]);
            self[0x3a] = (float)((double)rndT[2] * (double)(FLOAT_80617ea0 * FLOAT_80617e9c * dt * DAT_806108ec) + (double)(float)self[0x3a]);
            for (i = 0; i < DAT_806108e4; i = i + 1) {
                self[0x38] = (float)self[0x38] * FLOAT_80617f7c;
                self[0x39] = (float)self[0x39] * FLOAT_80617f7c;
                self[0x3a] = (float)self[0x3a] * FLOAT_80617f7c;
                self[0x39] = (float)self[0x39] * FLOAT_80617f80;
            }
            self[0x30] = FLOAT_80617ea0 * DAT_806108ec * (float)self[0x38] * dt + (float)self[0x30];
            self[0x1c] = FLOAT_80617ea0 * DAT_806108ec * (float)self[0x39] * dt + (float)self[0x1c];
            self[0x32] = FLOAT_80617ea0 * DAT_806108ec * (float)self[0x3a] * dt + (float)self[0x32];

            if ((self[0x68] != 3) && (self[0x68] != 4)) {
                self[0x38] = FLOAT_80617ea0 * (float)((double)FLOAT_80617f84 * (double)(DAT_806108ec * -(float)self[0x30] * dt)) + (float)self[0x38];
                float wob = (float)self[0x34] - (float)self[0x1c];
                for (; wob <= FLOAT_80617f88; wob = wob + FLOAT_80617f5c) { }
                for (; FLOAT_80617ed8 < wob; wob = wob - FLOAT_80617f5c) { }
                self[0x39] = FLOAT_80617f84 * FLOAT_80617ea0 * DAT_806108ec * dt * wob * FLOAT_80617e8c + (float)self[0x39];
                float clamp = (DAT_80612614 == 0x1a) ? FLOAT_80617f8c : FLOAT_80617f90;
                float v = (float)self[0x39];
                float hi = (clamp < v) ? clamp : v;
                float lo = v;
                if (-clamp <= hi) { lo = (clamp < v) ? clamp : v; }
                self[0x39] = lo;
                self[0x3a] = FLOAT_80617f84 * FLOAT_80617ea0 * DAT_806108ec * -(float)self[0x32] * dt + (float)self[0x3a];
            }
            self[0x33] = FLOAT_80617ea0 * dt * DAT_806108ec + (float)self[0x33];
            if (FLOAT_80617ee4 < (float)self[0x33]) {
                unsigned int r = __ghz_rng();
                *(unsigned int*)(self[1] + 0x30) = *(unsigned int*)(self[1] + 0x30) ^ 1 << (r % 7 + 1);
                self[0x33] = FLOAT_80617e90;
            }
            if (self[0x68] != 0) {
                if ((self[10] & 0x1000) == 0)
                    mtx4_transform_vector((float*)(self + 0x9d), torque);
                self[0x2d] = torque[0] * FLOAT_80617ea0 * FLOAT_80617ea4 * dt * DAT_806108ec + (float)self[0x2d];
                self[0x2e] = torque[1] * FLOAT_80617ea0 * FLOAT_80617ea4 * dt * DAT_806108ec + (float)self[0x2e];
                self[0x2f] = torque[2] * FLOAT_80617ea0 * FLOAT_80617ea4 * dt * DAT_806108ec + (float)self[0x2f];
                self[0x2e] = -(FLOAT_80617ea0 * FLOAT_80617ea4 * dt * DAT_806108ec - (float)self[0x2e]);
                int st = self[0x68];
                if ((st == 3) || (st == 4)) {
                    float k = (st == 4) ? FLOAT_80617f6c : FLOAT_80617ea0;
                    self[0x2d] = (float)((double)DAT_806108ec * (double)(FLOAT_80617f40 * k * dt) + (double)(float)self[0x2d]);
                    self[0x2f] = DAT_806108ec * (FLOAT_80617f94 * k) * dt + (float)self[0x2f];
                    self[0x2e] = -(DAT_806108ec * (FLOAT_80617ea4 * k) * dt - (float)self[0x2e]);
                }
            }
            int steps = (int)FLOAT_80617ea0;
            for (i = 0; i < DAT_806108e4 * steps; i = i + 1) {
                self[0x2d] = (float)self[0x2d] * FLOAT_80617f98;
                self[0x2e] = (float)self[0x2e] * FLOAT_80617f98;
                self[0x2f] = (float)self[0x2f] * FLOAT_80617f98;
            }
            self[0x19] = (float)self[0x2d] * FLOAT_80617ea0 * dt * DAT_806108ec + (float)self[0x19];
            self[0x1a] = (float)self[0x2e] * FLOAT_80617ea0 * dt * DAT_806108ec + (float)self[0x1a];
            self[0x1b] = (float)self[0x2f] * FLOAT_80617ea0 * dt * DAT_806108ec + (float)self[0x1b];
        }

        int st = self[0x68];
        if (st == 3) {
            *(int*)(self[1] + 0x30) = 1;
            self[0x34] = FLOAT_80617fa4;
            float wob = FLOAT_80617efc + (float)self[0x1c];
            float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
            if (FLOAT_80617e9c <= ABS(wob)) {
                int idx = (int)(FLOAT_80617ec4 * ((FLOAT_80617ec8 * wob) / FLOAT_80617ed8) + FLOAT_80617e8c);
                self[0x32] = -(FLOAT_80617ea0 * FLOAT_80617ea4 * DAT_806108ec * dt *
                               *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)) - (float)self[0x32]);
                unsigned int ci = (unsigned int)(FLOAT_80617ec4 * ((FLOAT_80617ec8 * (FLOAT_80617efc + (float)self[0x1c])) / FLOAT_80617ed8) + FLOAT_80617e8c);
                self[0x30] = FLOAT_80617ea0 * FLOAT_80617ea4 * DAT_806108ec * dt * (float)(&DAT_80583228)[ci & 0x7ff] + (float)self[0x30];
            } else {
                self[0x32] = -(FLOAT_80617ea0 * FLOAT_80617ea4 * dt * DAT_806108ec - (float)self[0x32]);
            }
            self[0x3a] = FLOAT_80617e90;
            if ((float)self[0x1a] < FLOAT_80617fc8) {
                self[0x69] = FLOAT_80617e90;
                self[0x68] = 4;
                sound_play_stereo_pair((double)FLOAT_80617fcc, (double)FLOAT_80617ea0, (double)FLOAT_80617fd0);
            }
            FLOAT_806127f8 = dt * DAT_806108ec + FLOAT_806127f8;
            if (FLOAT_80617fc0 < FLOAT_806127f8) {
                FLOAT_806127f8 = FLOAT_806127f8 - FLOAT_80617fc0;
                float spawnVel[3];
                spawnVel[0] = DAT_803dfea4; spawnVel[1] = (float)DAT_803dfea8; spawnVel[2] = (float)DAT_803dfeac;
                float rr = FLOAT_80617ef8 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                float rad = FLOAT_80617fc4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                unsigned int ci = (unsigned int)((double)FLOAT_80617ec4 * (double)rr + (double)FLOAT_80617e8c);
                sprPos[1] = FLOAT_80617ef0;
                sprPos[0] = rad * (float)(&DAT_80583228)[ci & 0x7ff];
                sprPos[2] = rad * *(float*)((int)&DAT_80583228 + ((ci + 0x200) * 4 & 0x1ffc));
                mtx4_transform_vector((float*)(self + 0x9d), &sprPos[0]);
                sprPos[0] = sprPos[0] + (float)self[0x19];
                sprPos[1] = sprPos[1] + (float)self[0x1a];
                sprPos[2] = sprPos[2] + (float)self[0x1b];
                particle_emitter_spawn(0, (int)&DAT_80566f4c, 0x1000, &sprPos[0], spawnVel);
                particle_emitter_set_size_range((double)FLOAT_80617ed0, (double)FLOAT_80617ed0);
            }
        } else if (st < 3) {
            if (st == 1) {
                self[0x34] = FLOAT_80617fa4;
                *(unsigned int*)(self[1] + 0x30) = *(unsigned int*)(self[1] + 0x30) & 0xfffffffe;
            } else if (st < 1) {
                if (-1 < st) {
                    double dx = (double)(aim[0] - (float)self[0x19]);
                    double dz = (double)(aim[2] - (float)self[0x1b]);
                    double dy = (double)(aim[1] - (float)self[0x1a]);
                    double ang = (double)math_atan2(dx, dz);
                    double d2 = (double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
                    self[0x34] = (FLOAT_80617ed8 * (float)ang) / FLOAT_80617ec8 - FLOAT_80617f9c;
                    if (d2 < (double)FLOAT_80617fa0) {
                        *(unsigned int*)(self[1] + 0x30) = *(unsigned int*)(self[1] + 0x30) & 0xfffffffe;
                        particle_entity_disable_all();
                        self[0x34] = FLOAT_80617fa4;
                    }
                    if (d2 < (double)FLOAT_80617ea0) {
                        self[0x69] = FLOAT_80617e90;
                        self[0x68] = 1;
                    }
                    float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
                    if ((double)FLOAT_80617f10 < d2) {
                        double inv = 1.0 / SQRT(d2);
                        float f1 = (float)(inv * -(double)((float)((double)FLOAT_80617e8c * d2) * (float)(inv * inv) - FLOAT_80617f14));
                        double s = (double)(FLOAT_80617ea0 * f1 * -((float)((double)FLOAT_80617e8c * d2) * f1 * f1 - FLOAT_80617f14));
                        dx = (double)(float)(dx * s);
                        dy = (double)(float)(dy * s);
                        dz = (double)(float)(dz * s);
                    }
                    self[0x19] = (float)(dx * (double)(FLOAT_80617ea0 * dt * DAT_806108ec * FLOAT_80617f00) + (double)(float)self[0x19]);
                    self[0x1a] = (float)(dy * (double)(FLOAT_80617ea0 * dt * DAT_806108ec * FLOAT_80617f00) + (double)(float)self[0x1a]);
                    self[0x1b] = (float)(dz * (double)(FLOAT_80617ea0 * dt * DAT_806108ec * FLOAT_80617f00) + (double)(float)self[0x1b]);
                }
            } else {
                // st == 2: acquire nearest valid player and fire a class-0x32 projectile
                int chosen = -1;
                int po = 0;
                float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
                for (i = 0; i < DAT_80612610; i = i + 1) {
                    if ((double)FLOAT_80617e90 < (double)(float)self[0x73])
                        *(int**)(DAT_80611d78 + po + 0xe8c) = self;
                    float pa = *(float*)(DAT_80611d78 + po + 0xa8);
                    if (((double)pa < (double)FLOAT_80617fa8) && ((double)FLOAT_80617fac < (double)pa)) {
                        float pb = *(float*)(DAT_80611d78 + po + 0xb0);
                        if ((pb < FLOAT_80617fb0) && (FLOAT_80617fb4 < pb)) {
                            if ((chosen == -1) || ((__ghz_rng() & 1) != 0))
                                chosen = i;
                        }
                    }
                    po = po + 0x141c;
                }
                self[0x69] = -(FLOAT_80617ea0 * dt * DAT_806108ec - (float)self[0x69]);
                if (((float)self[0x69] < FLOAT_80617e90) && (chosen != -1)) {
                    aim[0] = FLOAT_80617fb8 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) + aim[0];
                    aim[2] = FLOAT_80617fb8 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) + aim[2];
                    float m2 = aim[2] * aim[2] + aim[0] * aim[0] + aim[1] * aim[1];
                    if (FLOAT_80617f10 < m2) {
                        float f1 = 1.0f / SQRT(m2);
                        f1 = f1 * -(FLOAT_80617e8c * m2 * f1 * f1 - FLOAT_80617f14);
                        float s = FLOAT_80617ea0 * f1 * -(FLOAT_80617e8c * m2 * f1 * f1 - FLOAT_80617f14);
                        aim[0] = aim[0] * s; aim[1] = aim[1] * s; aim[2] = aim[2] * s;
                    }
                    self[0x69] = FLOAT_80617fbc * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                    float rng = (float)self[0x71] * FLOAT_80617e8c;
                    float fp[3];
                    fp[0] = aim[0] * rng + (float)self[0x19];
                    fp[1] = aim[1] * rng + (float)self[0x1a];
                    fp[2] = aim[2] * rng + (float)self[0x1b];
                    int proj = __ghz_proj_simple((double)FLOAT_80617f14, (double)FLOAT_80617e90, 0x32, fp, (int*)aim,
                                                 (int)self, (int*)0, 6, 0,
                                                 *(int*)(DAT_80611d78 + chosen * 0x141c + 0xdec), 0, 2);
                    if (proj != 0)
                        *(int*)(proj + 0x19c) = *(int*)(DAT_80611d78 + chosen * 0x141c + 0x14);
                }
                FLOAT_806127f8 = FLOAT_80617e90;
                self[0x34] = FLOAT_80617fa4;
                *(unsigned int*)(self[1] + 0x30) = *(unsigned int*)(self[1] + 0x30) & 0xfffffffe;
                if ((float)self[0x73] < (float)self[0x76] * FLOAT_80617e8c) {
                    unsigned int rr = __ghz_rng();
                    unsigned int md = (unsigned int)((float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0) *
                        (FLOAT_80617ee4 + (FLOAT_80617ea4 * (float)self[0x73]) / (float)self[0x76]));
                    if (md != 0 && rr == (rr / md) * md) {
                        FLOAT_806127f8 = FLOAT_806127f8 - FLOAT_80617fc0;
                        float bv[3];
                        bv[0] = DAT_803dfe98; bv[1] = (float)DAT_803dfe9c; bv[2] = (float)DAT_803dfea0;
                        float rr2 = FLOAT_80617ef8 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                        float rad = FLOAT_80617fc4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                        unsigned int ci = (unsigned int)((double)FLOAT_80617ec4 * (double)rr2 + (double)FLOAT_80617e8c);
                        float bp[3];
                        bp[1] = FLOAT_80617ef0;
                        bp[0] = rad * (float)(&DAT_80583228)[ci & 0x7ff];
                        bp[2] = rad * *(float*)((int)&DAT_80583228 + ((ci + 0x200) * 4 & 0x1ffc));
                        mtx4_transform_vector((float*)(self + 0x9d), bp);
                        bp[0] = bp[0] + (float)self[0x19];
                        bp[1] = bp[1] + (float)self[0x1a];
                        bp[2] = bp[2] + (float)self[0x1b];
                        particle_emitter_spawn(0, (int)&DAT_80566f4c, 0x1000, bp, bv);
                        particle_emitter_set_size_range((double)FLOAT_80617ed0, (double)FLOAT_80617ed0);
                    }
                }
            }
        } else if (st == 5) {
            float p19[3];
            p19[0] = (float)self[0x19]; p19[1] = (float)self[0x1a]; p19[2] = (float)self[0x1b];
            streakDir[0] = *(float*)(DAT_80611d54 + 0x27c) - p19[0];
            streakDir[1] = *(float*)(DAT_80611d54 + 0x280) - p19[1];
            streakDir[2] = *(float*)(DAT_80611d54 + 0x284) - p19[2];
            float m2 = streakDir[2]*streakDir[2] + streakDir[0]*streakDir[0] + streakDir[1]*streakDir[1];
            if (FLOAT_80617f10 < m2) {
                float f1 = 1.0f / SQRT(m2);
                f1 = f1 * -(FLOAT_80617e8c * m2 * f1 * f1 - FLOAT_80617f14);
                float s = FLOAT_80617ea0 * f1 * -(FLOAT_80617e8c * m2 * f1 * f1 - FLOAT_80617f14);
                streakDir[0] = streakDir[0] * s; streakDir[1] = streakDir[1] * s; streakDir[2] = streakDir[2] * s;
            }
            int streak = __ghz_spawn_streak((double)FLOAT_80617fe0, (double)FLOAT_80617ea0, *(int*)DAT_80611d74, p19, streakDir);
            int team = aiactor_find_player_by_team(0x5054435a);   /* 'ZPCT' */
            if (team != 0)
                ((int*)DAT_80611d74)[0x3a3] = team;
            float payXform[3];
            payXform[0] = (float)self[0x30]; payXform[1] = (float)self[0x1c]; payXform[2] = (float)self[0x32];
            __ghz_spawn_by_type((unsigned long long)(unsigned int)streak, 0,0,0,0,0,0, 0, 0x53a, self + 0x19, payXform, 0, 0);
            entity_destroy_with_event(self);
        } else if (st < 5) {
            // st == 4: ground-trail cached emitter + spark bursts + payload burst rings
            entity_build_local_matrix(basisMtx, (void*)self, 1, 0);
            float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
            self[0x69] = dt * DAT_806108ec + (float)self[0x69];
            FLOAT_806127f8 = dt * DAT_806108ec + FLOAT_806127f8;
            io = 0;
            for (i = 0; i < DAT_80612610; i = i + 1) {
                int slot = DAT_80611d78 + io;
                if ((*(int*)(slot + 0x14) != 0) && (FLOAT_80617e90 < *(float*)(*(int*)(slot + 0x14) + 0x1cc))) {
                    *(float*)(slot + 0xe54) = (float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18;
                    *(float*)(slot + 0xe58) = FLOAT_80617fd4;
                }
                io = io + 0x141c;
            }
            DAT_805c67dc = FLOAT_80617ea4 * (FLOAT_80617fc8 - (float)self[0x1a]);
            float gp[3];
            gp[0] = (float)self[0x19] + (float)self[0x70] / FLOAT_80617f14;
            gp[1] = FLOAT_80617fd8;
            gp[2] = (float)self[0x1b] + (float)self[0x6c];
            handleA = DAT_80612804;
            if (particle_handle_is_valid(&handleA)) {
                handleB = DAT_80612804;
                particle_cached_set_drag(FLOAT_80617ea0 + (FLOAT_80617fc8 - (float)self[0x1a]), (ParticleEmitter**)&handleB);
                handleC = DAT_80612804;
                particle_cached_set_position((ParticleEmitter**)&handleC, (Vec3*)gp);
            } else {
                float gv[3];
                gv[0] = DAT_803dfeb0; gv[1] = (float)DAT_803dfeb4; gv[2] = (float)DAT_803dfeb8;
                particle_emitter_spawn((unsigned int)(uintptr_t)&DAT_80612804, (int)&DAT_805c677c, 0, gp, gv);
            }
            if (FLOAT_80617fc0 < FLOAT_806127f8) {
                FLOAT_806127f8 = FLOAT_806127f8 - FLOAT_80617fc0;
                float sv[3];
                sv[0] = DAT_803dfebc; sv[1] = (float)DAT_803dfec0; sv[2] = (float)DAT_803dfec4;
                float rr = FLOAT_80617ef8 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                float rad = FLOAT_80617fc4 * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                unsigned int ci = (unsigned int)((double)FLOAT_80617ec4 * (double)rr + (double)FLOAT_80617e8c);
                float sp[3];
                sp[1] = FLOAT_80617ef0;
                sp[0] = rad * (float)(&DAT_80583228)[ci & 0x7ff];
                sp[2] = rad * *(float*)((int)&DAT_80583228 + ((ci + 0x200) * 4 & 0x1ffc));
                mtx4_transform_vector((float*)(self + 0x9d), sp);
                sp[0] = sp[0] + (float)self[0x19];
                sp[1] = sp[1] + (float)self[0x1a];
                sp[2] = sp[2] + (float)self[0x1b];
                particle_emitter_spawn(0, (int)&DAT_80566f4c, 0x1000, sp, sv);
                particle_emitter_set_size_range((double)FLOAT_80617ed0, (double)FLOAT_80617ed0);
            }
            if (FLOAT_80617f40 < (float)self[0x69]) {
                int k;
                for (k = 0; k < 10; k = k + 1) {
                    float bp[3];
                    float bv[3];
                    bv[0] = FLOAT_80617efc; bv[1] = FLOAT_80617e90; bv[2] = FLOAT_80617ea0;
                    float rad = (float)(FLOAT_80617fdc * (double)(float)self[0x70]) * ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18);
                    unsigned int ci = (unsigned int)((double)FLOAT_80617ec4 * (double)(float)(FLOAT_80617ef8 * ((float)((double)(CONCAT44(0x43300000, (unsigned int)k ^ 0x80000000)) - DOUBLE_80617eb0) / FLOAT_80617ef0)) + (double)FLOAT_80617e8c);
                    bp[1] = FLOAT_80617e90;
                    bp[0] = rad * (float)(&DAT_80583228)[ci & 0x7ff];
                    bp[2] = rad * *(float*)((int)&DAT_80583228 + ((ci + 0x200) * 4 & 0x1ffc));
                    mtx4_transform_point(basisMtx, bp);
                    particle_emitter_spawn(0, (int)&DAT_80567790, 0x1000, bp, bv);
                    particle_emitter_set_size_range((double)FLOAT_80617ed0, (double)FLOAT_80617ed0);
                    particle_emitter_set_drag((double)FLOAT_80617ea4);
                    particle_emitter_spawn(0, (int)&DAT_8056770c, 0x1000, bp, bv);
                    particle_emitter_set_size_range((double)FLOAT_80617ed0, (double)FLOAT_80617ed0);
                    particle_emitter_set_drag((double)FLOAT_80617ea4);
                    particle_emitter_spawn(0, (int)&DAT_805675cc, 0x1000, bp, bv);
                    particle_emitter_set_size_range((double)FLOAT_80617ed0, (double)FLOAT_80617ed0);
                    particle_emitter_set_drag((double)FLOAT_80617ea4);
                }
                self[0x69] = FLOAT_80617e90;
                self[0x68] = 5;
                FLOAT_80612800 = FLOAT_80617f40;
            }
        }
        {
            float dt = FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0);
            scalar_step_toward_snap((double)FLOAT_80617e90, (double)(FLOAT_80617e8c * dt * DAT_806108ec), &FLOAT_806127fc);
            __ghz_sound3d((double)FLOAT_806127fc, (double)FLOAT_80617fe4, (double)FLOAT_80617ea0,
                          self + 0x8b, self + 0x19, 0x193, 0);
        }
    }
    (void)handleD; (void)spawn; (void)streakPos;
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x802100e4  ghz_object_init_dispatch_c
// Third GHZ object-init switch (class word +0x94 in 0x511..0x53d): wires the
// weapon/anim slot table, render/update callbacks, collision + render flags,
// billboard/subobj resources and per-variant tuning for each prop class. Sibling
// of ghz_object_init_dispatch / _b. Damage magnitudes for 0x53d arrive via FP args.
// ---------------------------------------------------------------------------
void ghz_object_init_dispatch_c(unsigned long long param_1, double param_2, unsigned long long param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8)
{
    int entity;
    int rdata;
    unsigned int r;
    int i, io;
    float dmgSeed;
    (void)param_1;

    entity = FUN_803826f0();
    switch (*(unsigned short*)(entity + 0x94)) {
    case 0x511:
        *(int*)(entity + 0x38) = 1; *(int*)(entity + 0x3c) = 2; *(int*)(entity + 0x40) = 3;
        *(int*)(entity + 0x44) = 7; *(int*)(entity + 0x34) = *(int*)(entity + 0x3c);
        *(unsigned char*)(entity + 0x30) = 4;
        break;
    case 0x512:
        *(int*)(entity + 0x38) = 3; *(int*)(entity + 0x3c) = 4; *(int*)(entity + 0x40) = 7;
        *(int*)(entity + 0x34) = *(int*)(entity + 0x40); *(unsigned char*)(entity + 0x30) = 3;
        break;
    case 0x513:
    case 0x514:
    case 0x518:
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x40000000;
        *(int*)(entity + 0x34) = 0xd5;
        break;
    case 0x515:
        *(int*)(entity + 0x38) = 9; *(int*)(entity + 0x3c) = 0xc; *(int*)(entity + 0x40) = 0xd;
        *(int*)(entity + 0x44) = 4; *(int*)(entity + 0x34) = *(int*)(entity + 0x3c);
        *(unsigned char*)(entity + 0x30) = 4;
        break;
    case 0x516:
        *(int*)(entity + 0x38) = 1; *(int*)(entity + 0x3c) = 2; *(int*)(entity + 0x40) = 3;
        *(int*)(entity + 0x34) = *(int*)(entity + 0x3c); *(unsigned char*)(entity + 0x30) = 3;
        break;
    case 0x517:
        *(int*)(entity + 0x38) = 3; *(int*)(entity + 0x3c) = 4; *(int*)(entity + 0x40) = 5;
        *(int*)(entity + 0x44) = 6; *(int*)(entity + 0x48) = 7; *(int*)(entity + 0x4c) = 8;
        *(int*)(entity + 0x50) = 0x1a; *(int*)(entity + 0x54) = 10; *(int*)(entity + 0x58) = 6;
        *(int*)(entity + 0x34) = *(int*)(entity + 0x58); *(unsigned char*)(entity + 0x30) = 9;
        break;
    case 0x519:
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x40020000;
        *(int*)(entity + 0x38) = 0x13; *(int*)(entity + 0x3c) = 0x17; *(int*)(entity + 0x40) = 0x19;
        *(int*)(entity + 0x34) = *(int*)(entity + 0x38); *(unsigned char*)(entity + 0x30) = 3;
        break;
    case 0x51a:
    case 0x51d:
    case 0x51e:
    case 0x51f: {
        rdata = *(int*)(entity + 0x7c);
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x40000000;
        *(int*)(entity + 0x34) = 0xd5;
        *(int*)(entity + 0x1a0) = 0;
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x4000410;
        entity_register_for_update(entity, 3, -0x7fdf27f4);
        if (rdata != 0) {
            *(float*)(entity + 0x1cc) = FLOAT_80617e9c;
            *(float*)(entity + 0x1d8) = FLOAT_80617e9c;
            entity_set_render_type(entity, (int)&ghz_droptrail_tick);
            entity_register_for_update(entity, 0xc, -0x7fdf49a0);
            *(int*)(rdata + 0xf4) = particle_spawn_billboard(rdata + 0xf4, 0x7f, 0, (int*)(entity + 0x64), 0xffff);
            unsigned char b1 = ((*(short*)(entity + 0x94) == 0x51a) || (*(short*)(entity + 0x94) == 0x51d)) ? 1 : 0;
            *(unsigned int*)(*(int*)(*(int*)(rdata + 0xf4) + 0x18) + 0x2b0) = 2 - (unsigned int)b1;
            particle_slot_set_opacity(FLOAT_80617f34, *(int**)(rdata + 0xf4));
            *(int*)(rdata + 0xf8) = particle_spawn_billboard(rdata + 0xf8, 0x7f, 0, (int*)(entity + 0x64), 0xffff);
            b1 = ((*(short*)(entity + 0x94) == 0x51a) || (*(short*)(entity + 0x94) == 0x51d)) ? 1 : 0;
            *(unsigned int*)(*(int*)(*(int*)(rdata + 0xf8) + 0x18) + 0x2b0) = 2 - (unsigned int)b1;
            particle_slot_set_opacity(FLOAT_80617f34, *(int**)(rdata + 0xf8));
            if ((*(short*)(entity + 0x94) == 0x51e) || (*(short*)(entity + 0x94) == 0x51f)) {
                *(int*)(rdata + 0xfc) = particle_spawn_billboard(rdata + 0xfc, 0x7f, 0, (int*)(entity + 0x64), 0xffff);
                b1 = ((*(short*)(entity + 0x94) == 0x51a) || (*(short*)(entity + 0x94) == 0x51d)) ? 1 : 0;
                *(unsigned int*)(*(int*)(*(int*)(rdata + 0xfc) + 0x18) + 0x2b0) = 2 - (unsigned int)b1;
                particle_slot_set_opacity(FLOAT_80617f34, *(int**)(rdata + 0xfc));
            }
        }
        break;
    }
    case 0x52d: case 0x52e: case 0x52f: case 0x530: case 0x531:
    case 0x532: case 0x533: case 0x534: case 0x535:
        weapon_start_delayed_event((double)FLOAT_80617f40, entity, 8);
        entity_register_for_update(entity, 3, -0x7fdf1c10);
        break;
    case 0x538: {
        float p[3];
        p[0] = DAT_803dfec8; p[1] = (float)DAT_803dfecc; p[2] = (float)DAT_803dfed0;
        *(int*)(entity + 0x1a0) = 1;
        particle_emitter_spawn((unsigned int)(uintptr_t)&DAT_80612808, (int)&DAT_805c6800, 0x200000, p, p);
        entity_register_for_update(entity, 4, -0x7fdf1a70);
        break;
    }
    case 0x539: {
        *(unsigned char*)(entity + 0x32) = 2;
        r = __ghz_rng();
        *(unsigned int*)(*(int*)(entity + 4) + 0x30) = r & 0xfe;
        *(unsigned int*)(*(int*)(entity + 4) + 0x30) = *(unsigned int*)(*(int*)(entity + 4) + 0x30) | 1;
        *(int*)(entity + 0x34) = 0xd5; *(int*)(entity + 0x38) = 0xd5;
        *(unsigned char*)(entity + 0x30) = 1;
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x40020010;
        *(int*)(entity + 0x1a0) = 0;
        entity_register_for_update(entity, 3, -0x7fdf1830);
        io = 0;
        for (i = 0; i < *(int*)(entity + 0x1e8); i = i + 1) {
            int h = *(int*)(*(int*)(entity + 0x1fc) + io);
            if (particle_handle_is_valid(&h)) {
                int hh = *(int*)(*(int*)(entity + 0x1fc) + io);
                particle_cached_set_size_range(FLOAT_80617ea4, FLOAT_80617ea4, (ParticleEmitter**)&hh);
            }
            io = io + 4;
        }
        entity_set_render_type(entity, (int)&ghz_droptrail_tick);
        entity_register_for_update(entity, 10, -0x7fdf438c);
        *(float*)(entity + 0x1d8) = FLOAT_80617fec;
        *(float*)(entity + 0x1cc) = FLOAT_80617fec;
        *(int*)(entity + 0x268) = (int)screen_effect_alloc_slot();
        screen_effect_init_slot(*(int**)(entity + 0x268), (int*)entity);
        FLOAT_806127fc = FLOAT_80617e90;
        break;
    }
    case 0x53a: {
        io = 0;
        for (i = 0; i < *(int*)(entity + 0x1e8); i = i + 1) {
            int h = *(int*)(*(int*)(entity + 0x1fc) + io);
            if (particle_handle_is_valid(&h)) {
                int hh = *(int*)(*(int*)(entity + 0x1fc) + io);
                particle_cached_set_gravity(FLOAT_80617fe8, (ParticleEmitter**)&hh);
                int hf = *(int*)(*(int*)(entity + 0x1fc) + io);
                particle_emitter_set_flags((unsigned int)hf, 0x21000, 0);
            }
            io = io + 4;
        }
        *(int*)(*(int*)(entity + 4) + 0x30) = 3;
        *(int*)(entity + 0x34) = 0xd5; *(int*)(entity + 0x38) = 0xd5;
        break;
    }
    case 0x53d: {
        *(int*)(entity + 0x1a0) = 0;
        *(float*)(entity + 0x1cc) = FLOAT_80617ea4;
        *(float*)(entity + 0x1d8) = FLOAT_80617ea4;
        *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x10;
        io = 0;
        for (i = 0; i < *(int*)(entity + 0x1e8); i = i + 1) {
            int h = *(int*)(*(int*)(entity + 0x1fc) + io);
            if (particle_handle_is_valid(&h)) {
                int hh = *(int*)(*(int*)(entity + 0x1fc) + io);
                particle_cached_set_size_range(FLOAT_80617ff0, FLOAT_80617ff0, (ParticleEmitter**)&hh);
                int he = *(int*)(*(int*)(entity + 0x1fc) + io);
                particle_handle_set_entity(&he, (unsigned int)entity);
            }
            io = io + 4;
        }
        entity_set_render_type(entity, (int)0x80210780);   /* &ghz_debris_impact_tick */
        entity_register_for_update(entity, 3, -0x7fdef768);
        *(int*)(entity + 0x80) = __ghz_subobj_pool_alloc();
        dmgSeed = 2.943579e-39f;
        __ghz_build_descriptor_default(0, (double)FLOAT_80617ff0, param_3, param_4, param_5,
                                       param_6, param_7, param_8, *(int*)(entity + 0x80), entity, 3, -1, dmgSeed);
        break;
    }
    default:
        break;
    }
    (void)param_2;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80210780  ghz_debris_impact_tick  (also the 0x53d render callback)
// Decrement the fragment life (word[0x73] -= rec[8]); on the 0x1a event bump the
// substate; on expiry (life <= 0 or event 0x24) destroy the entity and spawn an
// explosion: type 0x28 (+ payload op) for the 'CEL1'..'CEL6' material words, else
// type 0x2f.
// ---------------------------------------------------------------------------
void ghz_debris_impact_tick(unsigned long long p1, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8, int* rec)
{
    int* ent = (int*)*rec;
    (void)p1;
    ent[0x73] = (float)ent[0x73] - (float)rec[8];
    if (rec[6] == 0x1a) {
        ent[0x69] = (float)ent[0x69] + (float)rec[8];
        ent[0x68] = 1;
    }
    if (((float)ent[0x73] <= FLOAT_80617e90) || (rec[6] == 0x24)) {
        entity_destroy_with_event(ent);
        int mat = *(int*)ent[0xb6];
        if ((mat == 0x43454c31) || ((unsigned int)(mat + 0xbcbab3ce) < 4) || (mat == 0x43454c36)) {
            player_ai_explosion_spawn(0x28, rec[6], (float*)(*rec + 0x64), (float*)rec[0xc], rec[1], 0, 1);
            FUN_80221074((double)FLOAT_80617e90, p2, p3, p4, p5, p6, p7, p8);
        } else {
            player_ai_explosion_spawn(0x2f, rec[6], (float*)(*rec + 0x64), (float*)rec[0xc], rec[1], 0, 1);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80210898  ghz_debris_force_tick
// 0x538-prop per-frame: while active (+0x1a0 != 0), apply a random-jittered force
// at a point on the subobj rigid body (+0x80), decaying the +0x1a4 timer.
// ---------------------------------------------------------------------------
void ghz_debris_force_tick(int self)
{
    if (*(int*)(self + 0x1a0) != 0) {
        float force[3];
        float pt[3];
        force[0] = DAT_803dfed4;
        force[1] = DAT_803dfed8;
        force[2] = (float)DAT_803dfedc;
        pt[0] = *(float*)(self + 0x64) + *(float*)(self + 0x1a8);
        pt[1] = (*(float*)(self + 0x68) + *(float*)(self + 0x1ac)) - *(float*)(self + 0x1ac);
        pt[2] = *(float*)(self + 0x6c) + *(float*)(self + 0x1b0);
        pt[0] = FLOAT_80617ea4 * *(float*)(self + 0x1c0) *
                ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) + pt[0];
        pt[2] = FLOAT_80617ea4 * *(float*)(self + 0x1c8) *
                ((float)((double)(CONCAT44(0x43300000, __ghz_rng())) - DOUBLE_80617f38) / FLOAT_80617f18 - FLOAT_80617e8c) + pt[2];
        *(float*)(self + 0x1a4) =
            -(FLOAT_80617e9c *
              (FLOAT_80617ea0 / (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80617eb0)) *
              (float)((double)(CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000)) - DOUBLE_80617eb0) -
              *(float*)(self + 0x1a4));
        if (*(float*)(self + 0x1a4) < FLOAT_80617e90) {
            *(float*)(self + 0x1a4) = FLOAT_80617e90;
            *(int*)(self + 0x1a0) = 0;
        }
        float sc = (*(float*)(self + 0x1a4) < FLOAT_80617ea0) ? *(float*)(self + 0x1a4) : FLOAT_80617ea0;
        force[1] = force[1] * sc;
        subobj_add_force_at_point_scaled(*(unsigned int*)(self + 0x80), pt, force);
    }
}

// ---------------------------------------------------------------------------
// 0x80210a4c  ghz_outy_damage_gate
// Damage filter: only apply damage while the 'OUTY' (0x5954554f) actor predicate
// holds (cached ptr DAT_806127f4). Forwards to damage_apply_simple.
// ---------------------------------------------------------------------------
void ghz_outy_damage_gate(int* eventData)
{
    if (DAT_806127f4 == 0)
        DAT_806127f4 = FUN_802325b8(0x5954554f);
    if (FUN_80232740(DAT_806127f4))
        damage_apply_simple(eventData);
}
