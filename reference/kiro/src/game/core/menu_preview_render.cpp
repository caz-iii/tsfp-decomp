// menu_preview_render.cpp
// Rendering layer for the character/loadout preview-bot subsystem (recovered in
// batch_812; companion to menu_preview_bots.cpp from batch_811). These are the
// HUD element render callbacks that set up each preview panel's viewport and
// camera, draw the preview bot / prop / death particles, and overlay the team,
// weapon and control-hint sprites and text.
//
// Slot/panel state lives in the same per-slot arrays as the bot subsystem
// (DAT_805cca.. stride *0x5b / *0x16c, mode byte DAT_80612b18[panel]); the active
// camera/player record is DAT_80611d54 and the viewport table is at
// -0x7fa33130 (0x8058ced0), stride 0x44.
//
// These are non-matching decompilations: the original PowerPC bodies spill a
// large number of paired-single (fp/ps) registers to the stack as calling-
// convention artifacts. Those write-only spill temporaries are omitted here; the
// meaningful control flow, math and draw calls are preserved. Helpers are called
// with their shared-header (generated_decls) signatures.
//
// NON-MATCHING decompilation.

#include "common.h"
#include "menu.h"
#include "hud.h"
#include "gx_draw.h"
#include "entity.h"
#include "weapon.h"
#include "sprite.h"
#include "text.h"
#include "character.h"
#include "render.h"

// ---------------------------------------------------------------------------
// Raw ROM symbols (per-file externs).
// ---------------------------------------------------------------------------
extern int   DAT_80611d54;   // active camera/player record (matrices at +0x204/+0x208/+0x20c)
extern int   DAT_80611748;   // viewport geometry record (+0xc width, +0x20 height)
extern int   DAT_80611014;   // active preview panel count
extern int   DAT_806119ec;   // active panel index / side flag
extern int   DAT_80612614;   // menu/session state (0x65 running)
extern int  *DAT_80612b38;   // mode-7 backdrop entity
extern int   DAT_80612acc;   // extra render entity (weapon/effect)
extern int   DAT_80612250;   // reflect-render flag
extern int   DAT_80612258;   // reflect-render aux flag
extern int   DAT_805cc614;   // active game mode
extern int   DAT_80612020;   // timer sample
extern int   DAT_80612b3c;   // preview animation timer
extern int   DAT_805cc4b8;   // asset table modulus
extern int   DAT_80610a20;   // sprite/material table base ptr holder
extern char  DAT_80612b18;   // per-panel display mode byte

extern int   DAT_806109c0;   // frame delta (raw)
extern int   DAT_806109c4;   // screen width (raw)
extern int   DAT_806109c8;   // screen height (raw)
extern int   DAT_8060f950;   // camera param
extern int   DAT_8060f954;   // camera param

// per-slot arrays (indexed [panel*0x5b] as int[] or +panel*0x16c as byte offset)
extern int   DAT_805cca48;   // slot: fade-in timer
extern int   DAT_805cca4c;   // slot: alpha timer
extern int   DAT_805cca60;   // slot: prop entity
extern int   DAT_805cca74;   // slot: bot actor
extern int   DAT_805cca920;  // (guard; not used)
extern int   DAT_805cc920;   // slot weapon type [+0x16c]
extern int   DAT_805cc924;   // slot weapon subtype [+0x16c]
extern int   DAT_805cca28;   // slot variant seed [+0x16c]
extern int   DAT_805cca2c;   // slot selected column [+0x16c]
extern int   DAT_805cca30;   // slot selected row [+0x16c]
extern int   DAT_805cca3c;   // slot team-edit anim [+0x16c]
extern int   DAT_805cca64;   // slot scroll pos A [+0x16c]
extern int   DAT_805cca68;   // slot scroll pos B [+0x16c]
extern int   DAT_805cca6c;   // slot scroll pos C [+0x16c]
extern int   DAT_805cc198;   // sprite id table
extern char  DAT_805cc4bc;   // asset byte table
extern char  DAT_805cc4fc;   // asset byte table (alt)
extern int   DAT_80583228;   // sine/wobble table
extern int   DAT_80611034;   // format scratch buffer
extern int   DAT_8058e3cc;   // transposed view matrix dest

// data tables / pointers referenced by the overlays
extern int   PTR_FUN_80476484;      // preview-panel element vtable marker
extern int   DAT_803f2c90;          // team color table (stride 0x14)
extern int   DAT_803f2c94;          // team sprite id table (stride 0x14)
extern int   DAT_803f2dd8;          // (mode descriptor; shared)
extern int   DAT_80476468;          // stat sprite id table (stride 4)
extern int   DAT_80472510;          // stat label string id table (stride 2)
extern int   DAT_80472468;          // (guard)
extern int   PTR_DAT_80476430;      // stat value string ptr table (stride 8)
extern int   PTR_DAT_80476434;      // stat unit string ptr table (stride 8)

// float / double constants
extern float FLOAT_80611a64, FLOAT_80611a68, FLOAT_80611a70;
extern float FLOAT_80619f08, FLOAT_80619f0c, FLOAT_80619f30, FLOAT_80619f38, FLOAT_80619f3c;
extern float FLOAT_80619f50, FLOAT_80619f54, FLOAT_80619f58, FLOAT_80619f6c, FLOAT_80619f84, FLOAT_80619f88;
extern float FLOAT_80619fc0, FLOAT_80619fe0, FLOAT_80619ee8, FLOAT_80619eec, FLOAT_80619ef0, FLOAT_80619ef4, FLOAT_80619ff4;
extern float FLOAT_806104a0, FLOAT_806104a4, FLOAT_806108ec;
extern float FLOAT_8061a008, FLOAT_8061a014, FLOAT_8061a034, FLOAT_8061a03c;
extern float FLOAT_8061a04c, FLOAT_8061a060, FLOAT_8061a064, FLOAT_8061a068, FLOAT_8061a06c, FLOAT_8061a070, FLOAT_8061a074;
extern float FLOAT_8061a078, FLOAT_8061a07c, FLOAT_8061a080, FLOAT_8061a084, FLOAT_8061a088, FLOAT_8061a08c, FLOAT_8061a090;
extern float FLOAT_8061a094, FLOAT_8061a098, FLOAT_8061a09c, FLOAT_8061a0a0, FLOAT_8061a0a4, FLOAT_8061a0a8, FLOAT_8061a0ac, FLOAT_8061a0b0;
extern double DOUBLE_80619f00, DOUBLE_80619f20;

// ---- frame-actor accessor pairs (return the panel/actor index) ----
extern int  FUN_803826b4(void);
extern int  FUN_803826dc(void);
extern int  FUN_803826e8(void);
extern void FUN_80382700(void);
extern void FUN_80382728(void);
extern void FUN_80382734(void);

// ---- neighbouring helpers not (yet) in generated headers ----
extern int  FUN_8035ae48(int a, int b, int c, int d, int e, int f, int g, int h); // world render into preview
extern void FUN_8035afec(void);          // clear/placeholder render
extern int  FUN_80390324(double x);       // frac/round helper
extern unsigned int FUN_803825c0(double x); // float->fixed color helper
extern void FUN_8038b4a0(int dst, int src); // string append
extern int  weapon_def_get_hud_icon(unsigned int index);

// Viewport table base (records stride 0x44).
#define VIEWPORT_TABLE_BASE (-0x7fa33130)
// Per-panel reflect-matrix pointer table (stride 0x40).
#define REFLECT_MTX_BASE    (-0x7fa33020)

// ---------------------------------------------------------------------------
// 0x802cffc0  menu_preview_calc_viewport_rect
// Compute the screen-space rect (x0,y0,x1,y1 in outRect[0..3]) for a preview
// panel from its stored normalized geometry (panel+0x5c/0x60 origin,
// +0xac/+0xb0 size). In widescreen with special panel layouts, the left edge and
// right edge are snapped to the full framebuffer.
// ---------------------------------------------------------------------------
void menu_preview_calc_viewport_rect(int panel, float *outRect)
{
    outRect[0] = *(float *)(panel + 0x5c) * FLOAT_80611a68 + FLOAT_80611a70;
    outRect[1] = *(float *)(panel + 0x60) * FLOAT_80611a64;
    outRect[2] = *(float *)(panel + 0xac) + *(float *)(panel + 0x5c) * FLOAT_80611a68 + FLOAT_80611a70;
    outRect[3] = *(float *)(panel + 0x60) * FLOAT_80611a64 + *(float *)(panel + 0xb0);

    if ((is_widescreen() & 0xff) != 0) {
        if (DAT_80611014 == 1) {
            outRect[0] = FLOAT_80619f08;
            outRect[2] = (float)(int)DAT_806109c4;
        } else if ((*(int *)(panel + 0x38) == 0) || (*(int *)(panel + 0x38) == 2)) {
            outRect[0] = FLOAT_80619f08;
        } else {
            outRect[2] = (float)(int)DAT_806109c4;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x802d00d4  menu_preview_setup_camera
// Configure the active viewport and build the preview camera's view/projection
// matrices for a panel. Chooses lookat eye/target offsets by panel count and
// side, sets the viewport rect + depth params, builds the lookat and a
// framebuffer-aspect-corrected perspective, multiplies them into the active
// camera matrices, and stores a transposed copy for reflections.
// ---------------------------------------------------------------------------
void menu_preview_setup_camera(int panel)
{
    unsigned int side = *(unsigned int *)(panel + 0x38);
    float rect[4];
    menu_preview_calc_viewport_rect(panel, rect);

    float eye[3];
    float target[3];
    float up[3];
    eye[0]    = FLOAT_80619ff4;
    target[0] = FLOAT_80619ff4;

    if (DAT_80611014 < 3) {
        if (DAT_80611014 < 2) {
            eye[0]    = FLOAT_80619f08;
            eye[2]    = FLOAT_8061a070;
            target[0] = FLOAT_80619f08;
            target[1] = FLOAT_8061a04c;
            target[2] = FLOAT_8061a074;
        } else {
            if ((side & 1) != 0) { eye[0] = FLOAT_8061a068; }
            eye[2] = FLOAT_8061a070;
            if ((side & 1) != 0) { target[0] = FLOAT_8061a068; }
            target[1] = FLOAT_8061a04c;
            target[2] = FLOAT_8061a074;
        }
    } else {
        if ((side & 1) != 0) { eye[0] = FLOAT_8061a068; }
        eye[2] = FLOAT_80619ee8;
        if ((side & 1) != 0) { target[0] = FLOAT_8061a068; }
        target[1] = FLOAT_8061a06c;
        target[2] = FLOAT_80619f08;
    }
    eye[1] = FLOAT_80619fc0;
    up[0]  = FLOAT_80619f08;
    up[1]  = FLOAT_80619ee8;
    up[2]  = FLOAT_80619f08;

    float bob = FLOAT_80619ee8 - *(float *)(panel + 0x1b4);
    eye[2]    = FLOAT_80619f88 * bob + eye[2];
    target[1] = FLOAT_80619f88 * bob + target[1];

    viewport_save_active();
    viewport_set_active(side * 0x44 + VIEWPORT_TABLE_BASE);
    viewport_set_depth_params_active((double)FLOAT_8061a078, (double)FLOAT_8061a07c, (double)FLOAT_80619f08);
    viewport_set_rect_active((int)rect[0], (int)rect[1], (int)rect[2], (int)rect[3]);
    viewport_set_gx_active();

    mtx4_build_lookat_points(*(float **)(DAT_80611d54 + 0x208), eye, target, up);

    // Framebuffer-aspect-corrected FOV term (int->double conversions are the
    // original 0x43300000-pair idiom; expressed here as direct casts).
    float w      = (float)(int)*(int *)(DAT_80611748 + 0xc);
    float h      = (float)(int)*(int *)(DAT_80611748 + 0x20);
    float fbW    = (float)(int)DAT_806109c8;
    float fbH    = (float)(int)DAT_806109c4;
    float camW   = (float)(int)DAT_8060f954;
    float camH   = (float)(int)DAT_8060f950;
    float fov    = (w * ((fbW / camW) / (fbH / camH))) / h;
    mtx4_set_perspective((double)fov, (double)FLOAT_8061a080, (double)FLOAT_806104a0,
                         (double)FLOAT_806104a4, *(float **)(DAT_80611d54 + 0x204));

    mtx4_multiply(*(float **)(DAT_80611d54 + 0x208), *(float **)(DAT_80611d54 + 0x204),
                  *(float **)(DAT_80611d54 + 0x20c));
    gx_set_reflect_matrix(*(float **)(DAT_80611d54 + 0x204));
    mtx4_transpose(*(float **)(DAT_80611d54 + 0x208), (float *)&DAT_8058e3cc);

    int cam = DAT_80611d54;
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    float lerp = player_get_camera_lerp_factor(DAT_80611d54);
    *(float *)(cam + 0x268) = (h / fbH) * (lerp / *(float *)(cam + 900));
}

// ---------------------------------------------------------------------------
// 0x802d041c  menu_preview_render_scene
// Set up the camera then render the preview world (FUN_8035ae48) and restore the
// prior viewport.
// ---------------------------------------------------------------------------
void menu_preview_render_scene(int panel)
{
    menu_preview_setup_camera(panel);
    menu_preview_is_mode7();
    FUN_8035ae48(panel, 0, 0, 0, 0, 0, 0, 0);
    viewport_swap_active_saved();
    viewport_set_gx_active();
}

// ---------------------------------------------------------------------------
// 0x802d045c  menu_preview_render_if_active
// Only render the panel's scene when the session is running, the element is a
// preview-panel element (vtable PTR_FUN_80476484), and a valid selection exists.
// In mode-7 also require the backdrop entity to be present and enabled.
// ---------------------------------------------------------------------------
void menu_preview_render_if_active(int panel)
{
    if ((DAT_80612614 == 0x65) &&
        (*(void ***)(panel + 4) == (void **)&PTR_FUN_80476484)) {
        int sel = *(int *)(&DAT_805cc920 + *(int *)(panel + 0x38) * 0x16c);
        char m7 = menu_preview_is_mode7();
        if (-1 < sel) {
            if (m7 == '\0') {
                menu_preview_render_scene(panel);
            } else if ((DAT_80612b38 != 0) &&
                       ((*(unsigned int *)((int)DAT_80612b38 + 0x28) & 0x80000000) == 0)) {
                menu_preview_render_scene(panel);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x802d0500  menu_preview_render_bot
// Render the active panel's preview bot (and its prop / an extra effect entity)
// with a multipass material draw, fading in via the slot alpha timer. If the bot
// is the special "class 0xa0" character, also render its death particle
// emitters.
// ---------------------------------------------------------------------------
void menu_preview_render_bot(void)
{
    int panel = FUN_803826dc();
    int savedMtx = *(int *)(DAT_80611d54 + 0x208);
    int prevBlend = gx_set_blend_mode(4);
    unsigned int prevSub = gx_set_sub_material_id(1);
    unsigned int prevColor = gx_set_color_update_mode(0);

    int slot = *(int *)(panel + 0x38);
    float alpha = *(float *)(panel + 0x1b4) * (float)(&DAT_805cca4c)[slot * 0x5b];
    float fadeIn = FLOAT_80619ee8 - (float)(&DAT_805cca48)[slot * 0x5b];
    *(int *)(DAT_80611d54 + 0x208) = slot * 0x40 + REFLECT_MTX_BASE;
    float appear = alpha * fadeIn;

    menu_preview_setup_camera(panel);
    char m7 = menu_preview_is_mode7();

    int propEnt = 0;
    int botEnt  = (int)DAT_80612b38;
    if (m7 == '\0') {
        propEnt = (&DAT_805cca60)[slot * 0x5b];
        botEnt  = (&DAT_805cca74)[slot * 0x5b];
    }

    if (botEnt != 0) {
        *(unsigned int *)(botEnt + 0x10) |= 1;
        entity_force_bone_update(botEnt);
        entity_render_per_player();
    }
    if (propEnt != 0) {
        *(unsigned int *)(propEnt + 0x10) |= 1;
        entity_force_bone_update(propEnt);
        entity_render_per_player();
    }
    if (DAT_80612acc != 0) {
        *(unsigned int *)(DAT_80612acc + 0x10) |= 1;
        *(unsigned short *)(DAT_80612acc + 0x14) &=
            ~(unsigned short)(1 << *(int *)(DAT_80611d54 + 0x214));
        entity_force_bone_update(DAT_80612acc);
        entity_render_per_player();
        gx_set_blend_mode(4);
        gx_set_sub_material_id(1);
        render_instance_draw_multipass(*(int **)(DAT_80612acc + 4), alpha, 0);
    }

    if (appear < FLOAT_80619ee8) {
        FUN_8035afec();
    } else {
        if (botEnt != 0) {
            gx_set_blend_mode(4);
            render_instance_draw_multipass(*(int **)(botEnt + 4), alpha, 0);
        }
        if (propEnt != 0) {
            gx_set_blend_mode(4);
            render_instance_draw_multipass(*(int **)(propEnt + 4), alpha, 0);
        }
    }

    if ((&DAT_805cca74)[slot * 0x5b] != 0) {
        int bb = *(int *)((&DAT_805cca74)[slot * 0x5b] + 0x7c);
        if ((*(short *)(bb + 0x48) == 0xa0) && ((*(unsigned int *)(bb + 0xc6c) & 4) != 0)) {
            gx_set_sub_material_id(1);
            gx_set_blend_mode(0xb);
            for (int i = 0; i < 4; i++) {
                int handle[1];
                handle[0] = *(int *)(bb + 0xc68 + i * 4 + 0xbc);
                if (particle_handle_is_valid(handle)) {
                    particle_emitter_render_dispatch(*(int *)(bb + 0xc68 + i * 4 + 0xbc));
                }
            }
        }
    }

    viewport_swap_active_saved();
    viewport_set_gx_active();
    *(int *)(DAT_80611d54 + 0x208) = savedMtx;
    gx_set_blend_mode(prevBlend);
    gx_set_sub_material_id(prevSub);
    gx_set_color_update_mode(prevColor);
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x802d0794  menu_preview_render_extra_entity
// Render a single extra entity (the scoped frame actor) in its own viewport with
// the standard preview perspective. rectT/rectB/rectR give the sub-rect.
// ---------------------------------------------------------------------------
void menu_preview_render_extra_entity(int a1, int a2, int rectT, int rectB, int rectR)
{
    (void)a1; (void)a2;
    long long scoped = (long long)FUN_803826e8();
    int ent = (int)((unsigned long long)scoped >> 0x20);
    if (ent != 0) {
        viewport_save_active();
        viewport_set_active(DAT_806119ec * 0x44 + VIEWPORT_TABLE_BASE);
        viewport_set_depth_params_active((double)FLOAT_8061a078, (double)FLOAT_8061a07c, (double)FLOAT_80619f08);
        viewport_set_rect_active((int)scoped, rectT, rectB, rectR);
        viewport_set_gx_active();
        gx_set_scissor_rect((unsigned int)(int)scoped, (unsigned int)rectT, rectB, rectR);

        float w   = (float)(int)*(int *)(DAT_80611748 + 0xc);
        float h   = (float)(int)*(int *)(DAT_80611748 + 0x20);
        float fbW = (float)(int)DAT_806109c8;
        float fbH = (float)(int)DAT_806109c4;
        float camW = (float)(int)DAT_8060f954;
        float camH = (float)(int)DAT_8060f950;
        float fov = (w * ((fbW / camW) / (fbH / camH))) / h;
        mtx4_set_perspective((double)fov, (double)FLOAT_8061a080, (double)FLOAT_806104a0,
                             (double)FLOAT_806104a4, *(float **)(DAT_80611d54 + 0x204));
        mtx4_multiply(*(float **)(DAT_80611d54 + 0x208), *(float **)(DAT_80611d54 + 0x204),
                      *(float **)(DAT_80611d54 + 0x20c));
        gx_set_reflect_matrix(*(float **)(DAT_80611d54 + 0x204));

        int cam = DAT_80611d54;
        float lerp = player_get_camera_lerp_factor(DAT_80611d54);
        *(float *)(cam + 0x268) = (h / fbH) * (lerp / *(float *)(cam + 900));

        *(unsigned int *)(ent + 0x10) |= 1;
        entity_force_bone_update(ent);
        entity_render_per_player();
        gx_set_blend_mode(4);
        render_instance_draw_multipass(*(int **)(ent + 4), 0.0f, 0);
        viewport_swap_active_saved();
        viewport_set_gx_active();
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802d09a4  menu_preview_screen_exit
// Tear down the preview bots and either pop the preview screen (co-op modes
// 0xb/0xc/0xd) or push the next one.
// ---------------------------------------------------------------------------
void menu_preview_screen_exit(void)
{
    menu_preview_all_destroy();
    if ((DAT_805cc614 == 0xc) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xb)) {
        hud_screen_pop_with_restore();
    } else {
        hud_screen_push();
    }
}

// ---------------------------------------------------------------------------
// 0x802d0a04  menu_preview_render_mode2_sprite
// For a panel in display mode 2, draw a single positioned sprite quad (texture
// 0x281) sized/placed from the panel geometry and the animation timer.
// ---------------------------------------------------------------------------
void menu_preview_render_mode2_sprite(void)
{
    int panel = FUN_803826e8();
    if ((&DAT_80612b18)[*(int *)(panel + 0x38)] == '\x02') {
        float timer = (float)(int)DAT_80612b3c;
        float aspect = (float)(int)DAT_806109c4 / (float)(int)DAT_806109c8;
        float cy = *(float *)(panel + 0x60) * FLOAT_80611a64 + FLOAT_8061a014;
        float halfW = FLOAT_8061a084 * timer * aspect;

        int tex = sprite_get_texture_id(0x281);
        int mat = *(int *)&DAT_80610a20 + tex * 0x58;
        unsigned int u1 = (unsigned int)*(unsigned short *)(mat + 0x4e);
        unsigned int v1 = (unsigned int)*(unsigned short *)(mat + 0x52);

        float rect[4];
        menu_preview_calc_viewport_rect(panel, rect);

        float x0, x1;
        if (DAT_80611014 == 1) {
            x0 = FLOAT_8061a088 + rect[0];
            x1 = x0 + halfW;
        } else if ((DAT_806119ec & 1) == 0) {
            x1 = rect[2];
            x0 = x1 - halfW;
        } else {
            x0 = rect[0];
            x1 = x0 + halfW;
        }

        float pad = FLOAT_8061a03c;
        gx_set_sub_material_id(1);
        gx_set_active_material(tex);
        gx_draw_sprite_quad_c4(0, (double)(x0 - pad), (double)(cy - pad),
                               (double)(x1 + pad), (double)(cy + pad),
                               u1 << 4, v1 << 4, 0);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802d0c6c  menu_preview_render_team_overlay
// Draw the per-panel team / class / stat overlay sprites and labels. The layout
// branches on the panel's display-mode byte (DAT_80612b18): mode 4 = team-color
// row of icons, mode 2 = a scrolling ribbon of character/weapon thumbnails,
// modes >=2 (else) = a 7-row stat table with value/unit text. This is a large
// UI-composition routine; the geometry is derived from the panel rect, the
// animation timer (DAT_80612b3c) and per-slot scroll positions.
// ---------------------------------------------------------------------------
void menu_preview_render_team_overlay(void)
{
    int panel = FUN_803826b4();
    float frameDt = (float)(int)DAT_806109c0;
    float timer   = (float)(int)DAT_80612b3c;
    float widthN  = (float)(int)DAT_80612020;
    float aspect  = (float)(int)DAT_806109c4 / (float)(int)DAT_806109c8;
    float ribbon  = FLOAT_8061a084 * timer * aspect;
    float baseY   = FLOAT_80619ee8 / frameDt;   // normalised inverse-frame term
    (void)widthN; (void)baseY;

    float rect[4];
    menu_preview_calc_viewport_rect(panel, rect);

    float x0, x1;
    if (DAT_80611014 == 1) {
        x0 = FLOAT_8061a088 + rect[0];
        x1 = x0 + ribbon;
    } else if ((DAT_806119ec & 1) == 0) {
        x1 = rect[2];
        x0 = x1 - ribbon;
    } else {
        x0 = rect[0];
        x1 = x0 + ribbon;
    }

    gx_set_sub_material_id(1);
    unsigned char mode = (&DAT_80612b18)[*(int *)(panel + 0x38)];

    if (mode == 4) {
        // Team-color icon row.
        int slotOff = *(int *)(panel + 0x38) * 0x16c;
        float rowY = FLOAT_8061a014 + (*(float *)(panel + 0x60) * FLOAT_80611a64 + FLOAT_80619f50);
        float stepY = FLOAT_80619f50 + FLOAT_8061a084 * timer;
        int col = 0, tblOff = 0;
        while (col < mode_team_count()) {
            int tex = sprite_get_texture_id(*(int *)((int)&DAT_803f2c94 + tblOff));
            gx_set_active_material(tex);
            int mat = *(int *)&DAT_80610a20 + tex * 0x58;
            unsigned int u1 = (unsigned int)*(unsigned short *)(mat + 0x4e) << 4;
            unsigned int v1 = (unsigned int)*(unsigned short *)(mat + 0x52) << 4;
            if (col == *(int *)(&DAT_805cca2c + slotOff)) {
                color_scale_lo((double)*(float *)(panel + 0x1b4), *(unsigned int *)((int)&DAT_803f2c90 + tblOff));
                gx_draw_sprite_quad_c4(0, (double)x0, (double)rowY, (double)x1, (double)(rowY + stepY), u1, v1, 0);
            } else {
                unsigned int c = color_scale_rgb((double)FLOAT_8061a090, *(unsigned int *)((int)&DAT_803f2c90 + tblOff));
                color_scale_lo((double)*(float *)(panel + 0x1b4), c);
                gx_draw_sprite_rect((unsigned int)x0, (unsigned int)rowY, (unsigned int)x1, (unsigned int)(rowY + stepY), 0, 0, u1, v1);
            }
            rowY += stepY;
            tblOff += 0x14;
            col++;
        }
    } else if (mode == 2) {
        // Scrolling thumbnail ribbon (approximate faithful structure).
        int slotOff = *(int *)(panel + 0x38) * 0x16c;
        float scroll = (float)FUN_80390324((double)*(float *)(&DAT_805cca6c + slotOff));
        int count = (DAT_80611014 < 3) ? 5 : 3;
        float y = FLOAT_8061a014 + *(float *)(panel + 0x60) * FLOAT_80611a64;
        for (int i = 0; i < count; i++) {
            int assetIdx = (unsigned char)(&DAT_805cc4bc)[i] +
                           *(int *)(&DAT_805cc920 + i * 4 + 8 + slotOff);
            int tex = sprite_get_texture_id((&DAT_805cc198)[assetIdx]);
            int mat = *(int *)&DAT_80610a20 + tex * 0x58;
            unsigned int u1 = (unsigned int)*(unsigned short *)(mat + 0x4e) << 4;
            unsigned int v1 = (unsigned int)*(unsigned short *)(mat + 0x52) << 4;
            gx_set_active_material(tex);
            gx_draw_sprite_quad_c4(0, (double)x0, (double)y, (double)x1, (double)(y + ribbon), u1, v1, 0);
            y += ribbon;
        }
        (void)scroll;
    } else if (mode >= 2) {
        // 7-row stat table with value/unit text.
        float y  = FLOAT_80619f38 * FLOAT_80611a64 + FLOAT_8061a014 +
                   *(float *)(panel + 0x60) * FLOAT_80611a64 + FLOAT_80619f50;
        float rowH = (float)(int)DAT_80612b3c * (DAT_80611014 == 1 ? FLOAT_8061a060 : FLOAT_8061a064);
        for (int row = 0; row < 7; row++) {
            int tex = sprite_get_texture_id(*(int *)((int)&DAT_80476468 + row * 4));
            int mat = *(int *)&DAT_80610a20 + tex * 0x58;
            unsigned int u1 = (unsigned int)*(unsigned short *)(mat + 0x4e) << 4;
            unsigned int v1 = (unsigned int)*(unsigned short *)(mat + 0x52) << 4;
            unsigned char *valStr  = *(unsigned char **)((int)&PTR_DAT_80476430 + row * 8);
            unsigned char *unitStr = *(unsigned char **)((int)&PTR_DAT_80476434 + row * 8);
            unsigned char *labelStr = (unsigned char *)get_string_by_id((int)*(short *)((int)&DAT_80472510 + row * 2));
            gx_set_active_material(tex);
            gx_set_sub_material_id(1);

            if (row == *(int *)(&DAT_805cca30 + *(int *)(panel + 0x38) * 0x16c)) {
                gx_draw_sprite_quad_c4(0, (double)x0, (double)y, (double)x1, (double)(y + rowH), u1, v1, 0);
            } else {
                gx_draw_gradient_line_clipped(0, (double)y, (double)(y + rowH), (double)x1,
                                              u1, v1, 0x7f7f7f00, 0x7f7f7f00, 0x7f7f7f00, 0x7f7f7f00,
                                              (unsigned int)x0, (unsigned int)y, (unsigned int)x1, (unsigned int)(y + rowH));
            }

            unsigned int textColor = 0x64646400;
            text_set_font_table(DAT_80611014 < 2 ? 0xb : 0xf);
            if (valStr != 0) {
                float cw = text_get_char_width(valStr);
                text_draw_char((double)(x1 - cw), (double)y, valStr, textColor);
            }
            text_set_font_table(DAT_80611014 < 2 ? 10 : 0xf);
            if (unitStr != 0) {
                text_draw_char((double)x0, (double)y, unitStr, textColor);
            }
            text_set_font_table(0xf);
            text_draw_char((double)x0, (double)(y + rowH), labelStr, textColor);
            y += rowH;
        }
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// 0x802d1b14  menu_preview_render_panel
// Top-level per-panel render callback: set the scissor to the panel rect, fill
// the background, draw the preview bot (or the mode-7 backdrop), the team/stat
// overlay, the panel's label (character / "empty"), an optional "press A" hint,
// the team-swap D-pad indicator, and — in mode 7 — advance the backdrop object's
// spin. panel index from the frame accessor.
// ---------------------------------------------------------------------------
void menu_preview_render_panel(void)
{
    int panel = FUN_803826dc();
    int sel = *(int *)(&DAT_805cc920 + *(int *)(panel + 0x38) * 0x16c);
    char m7 = menu_preview_is_mode7();

    float rect[4];
    menu_preview_calc_viewport_rect(panel, rect);
    gx_set_scissor_rect((unsigned int)(int)rect[0], (unsigned int)(int)rect[1], (int)rect[2], (int)rect[3]);
    gx_fill_rect_blend((unsigned int)(int)rect[0], (unsigned int)(int)rect[1], (int)rect[2] + 1, (int)rect[3] + 1);

    if (-1 < sel) {
        double fade = (double)*(float *)(panel + 0xf0);
        unsigned int labelColor = (FUN_803825c0((double)(float)((double)FLOAT_80619f0c * fade)) & 0xff) | 0x7f7f7f00;
        unsigned int shadow = FUN_803825c0((double)(float)((double)FLOAT_80619f58 * fade)) & 0xff;
        unsigned char *label = (unsigned char *)get_string_by_id(0x17b8);

        if (m7 == '\0') {
            int scored = sel;
            if ((DAT_805cc614 == 7) || (DAT_805cc614 == 6) ||
                ((scored = mode_player_scored(sel)) != 0)) {
                int off = *(int *)(panel + 0x38) * 0x16c;
                unsigned char *def = weapon_def_find_record(*(int *)(&DAT_805cc920 + off),
                                                            *(int *)(&DAT_805cc924 + off));
                int icon = weapon_def_get_hud_icon((unsigned int)((int)(def + 0x7fb8b984) / 0x30));
                label = (unsigned char *)get_string_by_id(icon);
            }
            (void)scored;
            menu_preview_render_bot();
            gx_set_scissor_rect((unsigned int)(int)rect[0], (unsigned int)(int)rect[1], (int)rect[2], (unsigned int)rect[3]);
            menu_preview_render_team_overlay();
        } else {
            if ((DAT_80612b38 == 0) || ((*(unsigned int *)((int)DAT_80612b38 + 0x28) & 0x80000000) != 0)) {
                goto done;
            }
            label = (unsigned char *)get_string_by_id(0xa30);
            menu_preview_render_bot();
            gx_set_scissor_rect((unsigned int)(int)rect[0], (unsigned int)(int)rect[1], (int)rect[2], (unsigned int)rect[3]);
            menu_preview_render_mode2_sprite();
        }

        text_set_font_table((1 < DAT_80611014) ? 4 : 9);
        if ((DAT_80611014 == 1) || ((DAT_806119ec & 1) != 0)) {
            text_apply_alpha((double)(FLOAT_8061a060 * ((rect[2] - rect[0]) - FLOAT_8061a088)), label, 0);
            float cw = text_get_char_width(label);
            text_draw_char_shadowed((double)((rect[2] - FLOAT_8061a088) - cw),
                                    (double)(FLOAT_8061a014 + rect[1]), label, labelColor, shadow);
            text_set_color_rgb((double)FLOAT_80619ee8, (double)FLOAT_80619ee8, (double)FLOAT_80619ee8);
        } else {
            text_apply_alpha((double)(FLOAT_8061a060 * ((rect[2] - rect[0]) - FLOAT_8061a088)), label, 0);
            text_draw_char_shadowed((double)(FLOAT_8061a088 + rect[0]),
                                    (double)(FLOAT_8061a014 + rect[1]), label, labelColor, shadow);
            text_set_color_rgb((double)FLOAT_80619ee8, (double)FLOAT_80619ee8, (double)FLOAT_80619ee8);
        }

        // Team-swap D-pad hint (edit-teams modes only).
        if ((menu_loadout_can_edit_teams() != 0) &&
            ((&DAT_80612b18)[DAT_806119ec] == '\x02') &&
            (DAT_805cc614 != 7) && (DAT_805cc614 != 6)) {
            float anim = *(float *)(&DAT_805cca3c + *(int *)(panel + 0x38) * 0x16c);
            if (DAT_80611014 == 1) { anim = FLOAT_80619ee8; }
            if ((double)anim < (double)FLOAT_80619ee8) {
                float dir = FLOAT_80619eec * (rect[2] - rect[0]);
                bool leftSide = ((DAT_806119ec & 1) == 0) && (DAT_80611014 != 1);
                if (leftSide) { dir = -dir; }
                float wobble = (float)((double)FLOAT_80619ee8 - (double)anim) * dir;
                float scale = (DAT_80611014 == 1) ? (FLOAT_80619f30 * *(float *)(panel + 0xf0))
                                                   : (FLOAT_8061a084 * *(float *)(panel + 0xf0));
                float y = FLOAT_8061a014 + rect[1];
                float bottom = rect[3] - FLOAT_80619f6c;
                menu_dpad_indicator_render(0, (double)(FLOAT_8061a088 + rect[0] + wobble), (double)y,
                                           (double)((rect[2] - FLOAT_8061a088) + wobble), (double)bottom);
                (void)scale;
            }
        }

        // Mode-7 backdrop spin.
        if (m7 != '\0') {
            if ((&DAT_80612b18)[DAT_806119ec] == '\x06') {
                float *m = (float *)subobj_get_object_matrix(*(unsigned int *)((int)DAT_80612b38 + 0x80));
                float force[3];
                force[0] = FLOAT_8061a0a8 * m[0];
                force[1] = FLOAT_8061a0a8 * m[1];
                force[2] = FLOAT_8061a0a8 * m[2];
                subobj_add_linear_force(*(unsigned int *)((int)DAT_80612b38 + 0x80), force);
            } else {
                *(float *)((int)DAT_80612b38 + 0x1a4) += FLOAT_806108ec;
                float ang = (float)angle_wrap_clamp((double)*(float *)((int)DAT_80612b38 + 0x1a4),
                                                    (double)FLOAT_8061a0ac, (double)FLOAT_8061a0b0);
                *(float *)((int)DAT_80612b38 + 0x1a4) = ang;
                float rot[16];
                mtx4_set_rotation_y((double)((FLOAT_80619f3c * ang) / FLOAT_8061a0b0), rot);
                subobj_build_orthonormal_matrix(*(unsigned int *)((int)DAT_80612b38 + 0x80), rot, (float *)0);
            }
        }
    }

    gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
done:
    FUN_80382728();
}
