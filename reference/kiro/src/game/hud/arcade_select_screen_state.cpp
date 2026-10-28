// Batch: batch_869 | Module: arcade-select screen state / context prompts
// Address range: 0x803046cc - 0x803057f8
// Date: 2026-09-11
//
// Screen-level logic for the arcade character-select screen (active screen ctx
// DAT_80612be4): award granting, one-time screen data init, full screen reset, view
// pan clamping, the big per-frame context-prompt (A/B/X/Y hint) dispatcher, and a
// set of member-list aggregation helpers (count / weighted sums over the linked
// list whose next pointer is @+0x40).
//
//   asel_grant_awards            0x803046cc  grant awards (award_grant_generic) until
//                                            N have been granted or index hits 0x46.
//   asel_init_screen_data        0x8030474c  fill the 0x32-entry scratch table from
//                                            DAT_80490c14 (mod DAT_80490c10), grant 6
//                                            award groups into DAT_8048021c, copy two
//                                            config blocks into the ctx.
//   asel_reset_screen            0x8030486c  full screen reset: clear the alloc pool,
//                                            reset the placement table, re-init data,
//                                            and zero the many ctx cursor/selection
//                                            fields.
//   asel_clamp_view_pan          0x803049ec  clamp the view pan offset to [0,
//                                            FLOAT_8061a634] on each axis via FUN_803151e4.
//   asel_update_context_prompts  0x80304a8c  the per-frame context-hint dispatcher:
//                                            from the cursor hit-test + mode (ctx+0x254)
//                                            + selection/roster state, register the four
//                                            button-prompt strings and set the context
//                                            id DAT_80612bec.
//   asel_reset_if_substate0      0x80305700  reset the screen only when the substate
//                                            byte (ctx+0x11854) is exactly 0.
//   member_list_count            0x80305748  count nodes in a member list (next @+0x40).
//   member_list_sum_weight_c03c  0x80305770  sum DAT_8047c03c[type] (0x140 stride) over
//                                            the list (type = **(node+0x2c)).
//   member_list_sum_weight_c038  0x803057b4  same, DAT_8047c038.
//   member_list_sum_weight_c040  0x803057f8  same, DAT_8047c040.
//
// Named helpers (award_grant_generic, get_string_by_id, hud_screen_register/remove_
// handler_current, avanim_layout_is_0x21/0x22, roster_slot_to_layout,
// select_list_reset_weights, aselgrid_reset_table, leaf_31e8) resolve through the
// shared generated headers; the raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get per-file
// externs. Definitions are at file scope (C++ linkage) to match generated_decls.h.
//
// NON-MATCHING: PPC int->float magic-bias idiom preserved (ibias helper); the dead
// f30/f31 register-spill locals the decompiler emitted in the prompt dispatcher are
// dropped (Compile-Clean rule 3); the deep if/else prompt tree is kept verbatim.

#include "common.h"
#include "hud.h"

extern "C" {

// --- hud active-screen accessors / finalizers ---
u64  FUN_803826e4(void);
u64  FUN_803826f0(void);
void FUN_80382730(void);
void FUN_8038273c(void);

// --- init / reset helpers ---
void* FUN_8031d710(void);   // alloc-pool base
int   FUN_8031d708(void);   // alloc-pool word count
void  FUN_803137a4(void);
void  FUN_8030b384(void);
void  FUN_80311f98(void);
void  FUN_803121dc(void);
void  FUN_80311ff0(void);
void  FUN_8031201c(void);
void  FUN_8038b510(int* dst, int* str);
void  FUN_80337c9c(void);
void  FUN_80337dd0(void);

// --- view pan clamp ---
void  FUN_803151e4(double dx, double dy);

// --- prompt-dispatcher helpers ---
int   FUN_80313af4(double x, double y, double z);   // hit-test point -> member ptr
int   FUN_80306f24(int layoutId);
int   FUN_80306f40(int* pSel);
int   FUN_8030a220(void);
int   FUN_80309f04(void);
bool  FUN_80332a84(int rec);
unsigned int FUN_80332b90(void);
unsigned int FUN_80332c04(void);
int   FUN_8032a600(int pRow);
unsigned int FUN_8032a5ec(int pRow);
void  FUN_80342298(short* out);

// --- raw data / constants ---
extern int   DAT_80612be4;   // active screen context base
extern int   DAT_80612c14;   // member list head
extern u32   DAT_80612c88;   // view state X (int bits)
extern u32   DAT_80612c84;   // view state Y (int bits)
extern int   DAT_80612c8c;   // pending-mode selector
extern int   DAT_80612bec;   // resolved context-prompt id (output)
extern int   DAT_806119ec;
extern float DAT_8051a834;
extern float DAT_8051a838;

extern int   DAT_80490c10;   // scratch table modulus
extern short DAT_80490c14;   // scratch table source (0x4-halfword stride units)
extern int   DAT_8048021c;   // award-group result array
extern int   DAT_80480218;   // award-group count
extern char  DAT_8048f97c;   // config block A base (0xa4 stride)
extern int   DAT_805d59a4;   // config block A index
extern int   DAT_805d59a8;   // config block B index
extern int   DAT_805d6340;   // title string slot
extern int   DAT_805d6260;
extern int   DAT_805d6180;
extern int   DAT_805d59e0;   // select-list weights base
extern int   DAT_805d59dc;
extern char  DAT_80477c80;   // per-type flags (0x31-word stride)
extern int   DAT_8047c03c;   // per-type weight table C03C (0x140 stride)
extern int   DAT_8047c038;   // per-type weight table C038
extern int   DAT_8047c040;   // per-type weight table C040

extern float FLOAT_80612c78;
extern float FLOAT_80612c74;
extern float FLOAT_80612c68;
extern float FLOAT_80612c64;
extern float FLOAT_80612c58;
extern float FLOAT_80612c54;
extern float FLOAT_8061a5f8;
extern float FLOAT_8061a604;
extern float FLOAT_8061a634;
extern double DOUBLE_8061a5e0;

} // extern "C" — definitions below use C++ linkage to match generated_decls.h

static inline float ibias(u32 x) {
    return (float)((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061a5e0);
}

// ---------------------------------------------------------------------------
// 0x803046cc  asel_grant_awards
// Grant awards (award_grant_generic) until the granted count reaches the active
// screen's target (FUN_803826e4) or the attempt index reaches 0x46.
// ---------------------------------------------------------------------------
int asel_grant_awards(void)
{
    int target = (int)FUN_803826e4();
    int granted = 0;
    int attempt = 1;
    do {
        if (award_grant_generic() != 0) {
            granted++;
        }
    } while (granted != target && (attempt = attempt + 1, attempt < 0x46));
    FUN_80382730();
    return granted;
}

// ---------------------------------------------------------------------------
// 0x8030474c  asel_init_screen_data
// Fill the 0x32-entry scratch table @ctx+0x2fa0 from the DAT_80490c14 source
// (index mod DAT_80490c10), grant 6 award groups into DAT_8048021c, then copy the
// two config blocks (A: 0xa4 bytes, B: 0x44 bytes) into the ctx.
// ---------------------------------------------------------------------------
void asel_init_screen_data(void)
{
    FUN_803826f0();
    int dstOff = 0;
    int idx = 0;
    int count = 0x32;
    do {
        int fieldOff = dstOff + 0x2fa0;
        dstOff += 4;
        int srcIdx = idx - (idx / DAT_80490c10) * DAT_80490c10;
        idx++;
        *(int*)(DAT_80612be4 + fieldOff) = (int)(short)(&DAT_80490c14)[srcIdx * 4];
        count--;
    } while (count != 0);

    int n = 0;
    int outOff = 0;
    do {
        int g = asel_grant_awards();
        n++;
        *(int*)((int)&DAT_8048021c + outOff) = g;
        outOff += 4;
    } while (n < 6);
    DAT_80480218 = 6;

    leaf_31e8((u32)(DAT_80612be4 + 0x11860),
              (u32)((int)&DAT_8048f97c + (DAT_805d59a4 + 0x1b) * 0xa4), 0xa4);
    leaf_31e8((u32)(DAT_80612be4 + 0x11904),
              (u32)((DAT_805d59a8 + 0xc) * 0x44 + 0x8046a908), 0x44);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8030486c  asel_reset_screen
// Full arcade-select screen reset: clear the alloc pool, reset the placement table,
// re-init screen data, and zero the cursor/selection/roster ctx fields. When
// fullReset != 0 also runs the heavier sub-init (0x2f18 substate 3 path).
// ---------------------------------------------------------------------------
void asel_reset_screen(int fullReset)
{
    int* pPool = (int*)FUN_8031d710();
    int poolWords = FUN_8031d708();
    int off = 0;
    if (poolWords > 0) {
        do {
            *(int*)((int)pPool + off) = 0;
            off += 4;
            poolWords--;
        } while (poolWords != 0);
    }
    aselgrid_reset_table();
    FUN_803137a4();
    if (fullReset != 0) {
        *(int*)(DAT_80612be4 + 0x204) = 3;
        FUN_8030b384();
        FUN_80311f98();
        FUN_803121dc();
    }
    FUN_80311ff0();
    FUN_8031201c();
    *(int*)(DAT_80612be4 + 0x2f18) = 0;
    *(int*)(DAT_80612be4 + 0x2f9c) = 0;
    *(int*)(DAT_80612be4 + 0x2f7c) = 0;
    int title = get_string_by_id(0xaa8);
    FUN_8038b510((int*)&DAT_805d6340, (int*)title);
    DAT_805d6260 = 0;
    DAT_805d6180 = 0;
    asel_init_screen_data();
    FUN_80337c9c();
    FUN_80337dd0();
    *(int*)(DAT_80612be4 + 0x250) = 0;
    *(int*)(DAT_80612be4 + 0x254) = 0;
    *(float*)(DAT_80612be4 + 0x260) = FLOAT_8061a604;
    *(int*)(DAT_80612be4 + 0x264) = 0;
    *(int*)(DAT_80612be4 + 0x268) = -1;
    *(unsigned short*)(DAT_80612be4 + 0x2f10) = 0xffff;
    *(int*)(DAT_80612be4 + 0x2f34) = -1;
    *(unsigned short*)(DAT_80612be4 + 0x3068) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x306a) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x306c) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x306e) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x3868) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x386a) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x386c) = 0;
    *(unsigned short*)(DAT_80612be4 + 0x386e) = 0;
    select_list_reset_weights((int*)&DAT_805d59e0);
    DAT_805d59dc = 0;
}

// ---------------------------------------------------------------------------
// 0x803049ec  asel_clamp_view_pan
// Clamp the view pan offset to [0, FLOAT_8061a634] on each axis, nudging it back
// into range via FUN_803151e4 (x nudge, y nudge).
// ---------------------------------------------------------------------------
void asel_clamp_view_pan(void)
{
    double panX = (double)(FLOAT_80612c68 * FLOAT_8061a5f8 + FLOAT_80612c78);
    double lo = (double)FLOAT_8061a604;
    double panY = (double)(FLOAT_80612c64 * FLOAT_8061a5f8 + FLOAT_80612c74);

    if (lo <= panX) {
        if ((double)FLOAT_8061a634 < panX) {
            FUN_803151e4((double)(float)((double)FLOAT_8061a634 - panX), lo);
        }
    } else {
        FUN_803151e4(-panX, lo);
    }

    lo = (double)FLOAT_8061a604;
    if (lo <= panY) {
        if ((double)FLOAT_8061a634 < panY) {
            FUN_803151e4(lo, (double)(float)((double)FLOAT_8061a634 - panY));
        }
    } else {
        FUN_803151e4(lo, -panY);
    }
}

// ---------------------------------------------------------------------------
// 0x80304a8c  asel_update_context_prompts
// Per-frame context-hint dispatcher. Hit-tests the cursor, then based on the mode
// (ctx+0x254), sub-mode (ctx+0x1185c), selection and roster state, registers the
// four button-prompt strings (handlers 0..3) and stores a context id in
// DAT_80612bec. Faithful translation of the original decision tree.
// ---------------------------------------------------------------------------
void asel_update_context_prompts(void)
{
    FUN_803826f0();

    double curX = (double)(FLOAT_80612c58 *
                  (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                  FLOAT_80612c78);
    double curY = (double)(FLOAT_80612c54 *
                  (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                  FLOAT_80612c74);
    double curZ = (double)ibias(*(u32*)(DAT_80612be4 + 0x264));

    int picked = FUN_80313af4(curX, curY, curZ);
    int ctx = DAT_80612be4;
    unsigned int flags = *(unsigned int*)(DAT_80612be4 + 0x25c);
    unsigned int dragging = flags & 0x20;
    int ctxId = 0x10;

    if (dragging == 0 ||
        (curX == (double)*(float*)(DAT_80612be4 + 0x2f94) &&
         curY == (double)*(float*)(DAT_80612be4 + 0x2f98))) {
        if ((flags & 2) == 0) {
            if ((flags & 1) == 0) {
                int mode = *(int*)(DAT_80612be4 + 0x254);
                if (picked == 0 && *(int*)(DAT_80612be4 + 0x2f9c) != 0 && dragging == 0 &&
                    *(int*)(DAT_80612be4 + 0x1185c) == -1 &&
                    (mode == 0 || mode == 2)) {
                    int s = get_string_by_id(0x19ca);
                    hud_screen_register_handler_current(2, s, 0, 0);
                } else if (mode == 1 || mode == 4) {
                    if (*(int*)(DAT_80612be4 + 0x2f34) >= 0) {
                        int s = get_string_by_id(0xa44);
                        hud_screen_register_handler_current(2, s, 0, 0);
                        unsigned int is22 = avanim_layout_is_0x22(*(int*)(DAT_80612be4 + 0x2f34));
                        if (is22 == 0) {
                            s = get_string_by_id(0xa46);
                            hud_screen_register_handler_current(0, s, 0, 0);
                            if (((&DAT_80477c80)[*(int*)(DAT_80612be4 + 0x2f34) * 0x31] & 1) != 0 &&
                                (*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0) {
                                s = get_string_by_id(0xa45);
                                hud_screen_register_handler_current(1, s, 0, 0);
                                s = get_string_by_id(0xa45);
                                hud_screen_register_handler_current(3, s, 0, 0);
                            }
                            ctxId = 0;
                        } else if (*(int*)(DAT_80612be4 + 0x2f70) == 0) {
                            ctxId = 1;
                        } else {
                            s = get_string_by_id(0xa46);
                            hud_screen_register_handler_current(0, s, 0, 0);
                            ctxId = 0;
                        }
                    }
                    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) == 0 ||
                        (*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x410000) != 0) {
                        ctxId = 1;
                        hud_screen_remove_handler_current(0);
                    }
                    if (*(int*)(DAT_80612be4 + 0x2f34) < 0) {
                        int sub = *(int*)(DAT_80612be4 + 0x1185c);
                        if (*(int*)(DAT_80612be4 + 0x254) == 1 &&
                            (*(int*)(DAT_80612be4 + 0x2f78) >= 0 ||
                             (*(int**)(DAT_80612be4 + 0x2f1c) != 0 &&
                              avanim_layout_is_0x21(**(int**)(DAT_80612be4 + 0x2f1c)) == 1)) &&
                            sub != 8) {
                            int s = get_string_by_id(0xa41);
                            hud_screen_register_handler_current(1, s, 0, 0);
                            s = get_string_by_id(0xa44);
                            hud_screen_register_handler_current(2, s, 0, 0);
                            if (*(int*)(DAT_80612be4 + 0x2f78) >= 0) {
                                int lay = roster_slot_to_layout(
                                    (unsigned int)*(unsigned char*)(DAT_80612be4 +
                                        *(int*)(DAT_80612be4 + 0x2f78) * 0xc + 0x3077));
                                if (FUN_80306f24(lay) != 0) {
                                    s = get_string_by_id(0x965);
                                    hud_screen_register_handler_current(3, s, 0, 0);
                                }
                            }
                            s = get_string_by_id(0xa50);
                            hud_screen_register_handler_current(0, s, 0, 0);
                            ctxId = 0;
                        } else if (*(int*)(DAT_80612be4 + 0x254) == 4) {
                            int s2 = *(int*)(DAT_80612be4 + 0x1185c);
                            if (s2 == 2) {
                                int s = get_string_by_id(0x19c8);
                                hud_screen_register_handler_current(1, s, 0, 0);
                                if (*(int**)(DAT_80612be4 + 0x2f1c) != 0 &&
                                    FUN_80332a84(**(int**)(DAT_80612be4 + 0x2f1c))) {
                                    s = get_string_by_id(0xa3c);
                                    hud_screen_register_handler_current(0, s, 0, 0);
                                }
                            } else if (s2 == 5) {
                                int s = get_string_by_id(0x19c8);
                                hud_screen_register_handler_current(1, s, 0, 0);
                                if (*(int*)(DAT_80612be4 + 0x2f78) >= 0) {
                                    s = get_string_by_id(0xa3c);
                                    hud_screen_register_handler_current(0, s, 0, 0);
                                }
                            } else if (s2 == 7) {
                                int pRow = DAT_80612be4 +
                                    *(short*)(*(int*)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c;
                                short tmp[4];
                                FUN_80342298(tmp);
                                int s = get_string_by_id(0xb03);
                                hud_screen_register_handler_current(1, s, 0, 0);
                                if (FUN_80309f04() != 0 &&
                                    (*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0) {
                                    s = get_string_by_id(0xa3c);
                                    hud_screen_register_handler_current(0, s, 0, 0);
                                }
                                if (*(int*)(pRow + 0xdd34) > 0) {
                                    s = get_string_by_id(0x19ca);
                                    hud_screen_register_handler_current(2, s, 0, 0);
                                }
                            } else if (s2 == 6) {
                                int s = get_string_by_id(0xb03);
                                hud_screen_register_handler_current(1, s, 0, 0);
                                if (*(int*)(DAT_80612be4 + 0x2f18) != 0) {
                                    s = get_string_by_id(0xa3c);
                                    hud_screen_register_handler_current(0, s, 0, 0);
                                }
                                if (FUN_8030a220() != 0) {
                                    s = get_string_by_id(0x19ca);
                                    hud_screen_register_handler_current(2, s, 0, 0);
                                }
                            } else {
                                int s = get_string_by_id(0xb77);
                                hud_screen_register_handler_current(0, s, 0, 0);
                            }
                        } else if (*(int*)(DAT_80612be4 + 0x1185c) == 8) {
                            int s = get_string_by_id(0x19c8);
                            hud_screen_register_handler_current(1, s, 0, 0);
                            if ((*(int*)(DAT_80612be4 + 0x2f1c) == 0 || FUN_80332b90() == 0) &&
                                (*(int*)(DAT_80612be4 + 0x2f78) < 0 || FUN_80332c04() == 0)) {
                                ctxId = 1;
                            } else {
                                s = get_string_by_id(0xa3c);
                                hud_screen_register_handler_current(0, s, 0, 0);
                                ctxId = 0x10;
                            }
                        } else if (*(int**)(DAT_80612be4 + 0x2f1c) == 0 ||
                                   avanim_layout_is_0x21(**(int**)(DAT_80612be4 + 0x2f1c)) != 0) {
                            int s = get_string_by_id(0xa3b);
                            hud_screen_register_handler_current(0, s, 0, 0);
                        } else if (*(int*)(DAT_80612be4 + 0x254) == 1) {
                            int s = get_string_by_id(0x963);
                            hud_screen_register_handler_current(1, s, 0, 0);
                            s = get_string_by_id(0xa44);
                            hud_screen_register_handler_current(2, s, 0, 0);
                            s = get_string_by_id(0xa50);
                            hud_screen_register_handler_current(0, s, 0, 0);
                            if (FUN_80306f40(*(int**)(DAT_80612be4 + 0x2f1c)) != 0 &&
                                FUN_80306f24(**(int**)(DAT_80612be4 + 0x2f1c)) != 0) {
                                s = get_string_by_id(0x965);
                                hud_screen_register_handler_current(3, s, 0, 0);
                            }
                            ctxId = 2;
                        }
                    }
                } else if (mode == 2) {
                    if ((flags & 0x100000) == 0 && (flags & 0x1000000) == 0) {
                        int s = get_string_by_id(0xa43);
                        hud_screen_register_handler_current(0, s, 0, 0);
                        if (picked != 0) {
                            s = get_string_by_id(0xa3e);
                            hud_screen_register_handler_current(2, s, 0, 0);
                            s = get_string_by_id(0xa41);
                            hud_screen_register_handler_current(1, s, 0, 0);
                            if (*(int*)(DAT_80612be4 + 0x2f18) != 0) {
                                s = get_string_by_id(0x965);
                                hud_screen_register_handler_current(3, s, 0, 0);
                            }
                        }
                    } else {
                        ctxId = 0xf;
                        if (picked != 0) {
                            int s = get_string_by_id(0xa42);
                            hud_screen_register_handler_current(0, s, 0, 0);
                        }
                        int s = get_string_by_id(0xa3f);
                        hud_screen_register_handler_current(2, s, 0, 0);
                    }
                } else if (mode == 3) {
                    short sel = *(short*)(DAT_80612be4 + 0x2f10);
                    if (sel >= 0) {
                        int s = get_string_by_id(0xa44);
                        hud_screen_register_handler_current(2, s, 0, 0);
                        s = get_string_by_id(0xa46);
                        hud_screen_register_handler_current(0, s, 0, 0);
                        int rowKind = *(int*)(ctx + sel * 0xe4 + 700);
                        if ((rowKind == 0 || rowKind == 2 || rowKind == 4) &&
                            (*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) != 0) {
                            s = get_string_by_id(0xa45);
                            hud_screen_register_handler_current(1, s, 0, 0);
                            s = get_string_by_id(0xa45);
                            hud_screen_register_handler_current(3, s, 0, 0);
                        }
                        ctxId = 0;
                    }
                    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) == 0) {
                        ctxId = 1;
                        hud_screen_remove_handler_current(0);
                    }
                    if (*(short*)(DAT_80612be4 + 0x2f10) < 0) {
                        if (*(int*)(DAT_80612be4 + 0x2f20) < 0) {
                            if (*(int*)(DAT_80612be4 + 0x2f34) < 0) {
                                int s = get_string_by_id(0xb12);
                                hud_screen_register_handler_current(0, s, 0, 0);
                            } else {
                                int s = get_string_by_id(0xa44);
                                hud_screen_register_handler_current(2, s, 0, 0);
                                s = get_string_by_id(0xa46);
                                hud_screen_register_handler_current(0, s, 0, 0);
                                ctxId = 0;
                            }
                        } else {
                            int pRow = DAT_80612be4 +
                                (*(int*)(DAT_80612be4 + 0x2f20) >> 0x10) * 0xe4 + 0x288;
                            int s = get_string_by_id(0xa44);
                            hud_screen_register_handler_current(2, s, 0, 0);
                            s = get_string_by_id(0xa52);
                            hud_screen_register_handler_current(0, s, 0, 0);
                            if (FUN_8032a600(pRow) == 0) {
                                s = get_string_by_id(0x965);
                                hud_screen_register_handler_current(3, s, 0, 0);
                            } else {
                                if (FUN_8032a5ec(pRow) == 0) {
                                    s = get_string_by_id(0xbc0);
                                    hud_screen_register_handler_current(3, s, 0, 0);
                                }
                                if (*(int*)(pRow + 0x58) < 8) {
                                    s = get_string_by_id(0xbc1);
                                    hud_screen_register_handler_current(1, s, 0, 0);
                                }
                            }
                            ctxId = 2;
                        }
                    }
                } else if (*(int*)(DAT_80612be4 + 0x2f7c) == 0) {
                    if (*(int*)(DAT_80612be4 + 0x2f9c) == 0 || dragging != 0 ||
                        picked == 0 || (*(unsigned char*)(picked + 0x14) & 2) == 0) {
                        if (picked != 0) {
                            int s = get_string_by_id(0xa48);
                            hud_screen_register_handler_current(2, s, 0, 0);
                        }
                    } else {
                        int s = get_string_by_id(0xa47);
                        hud_screen_register_handler_current(2, s, 0, 0);
                    }
                    if (picked == 0) {
                        int s = get_string_by_id(0xa4c);
                        hud_screen_register_handler_current(0, s, 0, 0);
                    } else {
                        int s = get_string_by_id(0xa49);
                        hud_screen_register_handler_current(3, s, 0, 0);
                        s = get_string_by_id(0xa4a);
                        hud_screen_register_handler_current(0, s, 0, 0);
                        s = get_string_by_id(0xa4b);
                        hud_screen_register_handler_current(1, s, 0, 0);
                        ctxId = 2;
                    }
                } else {
                    int s = get_string_by_id(0xa44);
                    hud_screen_register_handler_current(2, s, 0, 0);
                    s = get_string_by_id(0xa51);
                    hud_screen_register_handler_current(1, s, 0, 0);
                    s = get_string_by_id(0xa45);
                    hud_screen_register_handler_current(3, s, 0, 0);
                    if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 4) == 0 ||
                        (*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x410000) != 0) {
                        ctxId = 1;
                    } else {
                        s = get_string_by_id(0xa46);
                        hud_screen_register_handler_current(0, s, 0, 0);
                        ctxId = 0;
                    }
                }
            } else {
                ctxId = 7;
            }
        } else if (DAT_80612c8c == 3) {
            ctxId = 6;
        } else if (DAT_80612c8c < 3) {
            if (DAT_80612c8c == 1) {
                ctxId = 3;
            } else if (DAT_80612c8c != 0) {
                ctxId = 4;
            }
        } else if (DAT_80612c8c < 5) {
            ctxId = 5;
        }
    } else {
        ctxId = 8;
    }
    DAT_80612bec = ctxId;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80305700  asel_reset_if_substate0
// Reset the screen only when the substate byte (ctx+0x11854) is exactly 0.
// ---------------------------------------------------------------------------
void asel_reset_if_substate0(void)
{
    char sub = *(char*)(DAT_80612be4 + 0x11854);
    if (sub != 1 && sub < 1 && sub >= 0) {
        asel_reset_screen(0);
    }
}

// ---------------------------------------------------------------------------
// 0x80305748  member_list_count
// Count nodes in a member list (next @+0x40).
// ---------------------------------------------------------------------------
int member_list_count(int pList)
{
    int n = 0;
    for (; pList != 0; pList = *(int*)(pList + 0x40)) {
        n++;
    }
    return n;
}

// ---------------------------------------------------------------------------
// 0x80305770  member_list_sum_weight_c03c
// Sum DAT_8047c03c[type] (0x140 stride, type = **(node+0x2c)) over the list.
// ---------------------------------------------------------------------------
int member_list_sum_weight_c03c(int pList)
{
    int sum = 0;
    for (; pList != 0; pList = *(int*)(pList + 0x40)) {
        sum += *(int*)((int)&DAT_8047c03c + **(int**)(pList + 0x2c) * 0x140);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// 0x803057b4  member_list_sum_weight_c038
// ---------------------------------------------------------------------------
int member_list_sum_weight_c038(int pList)
{
    int sum = 0;
    for (; pList != 0; pList = *(int*)(pList + 0x40)) {
        sum += *(int*)((int)&DAT_8047c038 + **(int**)(pList + 0x2c) * 0x140);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// 0x803057f8  member_list_sum_weight_c040
// ---------------------------------------------------------------------------
int member_list_sum_weight_c040(int pList)
{
    int sum = 0;
    for (; pList != 0; pList = *(int*)(pList + 0x40)) {
        sum += *(int*)((int)&DAT_8047c040 + **(int**)(pList + 0x2c) * 0x140);
    }
    return sum;
}
