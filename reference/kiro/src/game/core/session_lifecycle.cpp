// session_lifecycle.cpp
// Game-mode session lifecycle. Address range 0x801c1714 - 0x801c1e48
// (batch_588). This is the family of per-game-mode dispatch entry points that
// bring a session up and tear it down. The active mode id lives in the global
// DAT_80612614 (values ~0x64..0x6c: 0x64 in-game level, 0x65/0x66 front-end
// menus, 0x67/0x68 special screens, 0x69..0x6b attract/movie, etc.). Each
// stage walks a long, fixed sequence of subsystem entry points, branching by
// mode where a stage differs.
//
// Stages (in call order across a session):
//   session_reset_all_systems   (0x801c171c) - zero/reset every subsystem
//   session_alloc_all_systems   (0x801c17a8) - allocate pools/buffers
//   session_preload_by_mode     (0x801c1888) - preload resources for the mode
//   session_init_level_systems  (0x801c197c) - init level-scoped systems
//   session_setup_by_mode       (0x801c1a3c) - per-mode scene/actor setup
//   session_teardown_by_mode    (0x801c1cfc) - per-mode shutdown/free
//   session_tick_by_mode        (0x801c1e48) - per-mode per-frame update
//   session_get_state_flag      (0x801c1714) - read session state flag
//
// Non-matching: the original functions thread the PPC ABI return registers
// (r3:r4 / f1) from each call into the next as dead "accumulator" plumbing -
// the callees are independent void init/reset routines, so that register
// threading carries no data and is dropped here. Each subsystem entry point is
// called in the original order and under the original mode predicate; helpers
// are called with their real (narrow) prototypes. Frontier helpers not yet in
// the shared headers are declared file-locally.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
extern int DAT_80612614;     // active game-mode id
extern int DAT_80611930;     // level-setup variant selector (0 => single-view)
extern int DAT_80540a44;     // head of the per-frame reset entity list
extern int DAT_80611c50;     // current node cursor for the reset list walk
extern int DAT_80611d54;     // primary player slot pointer (setup path)
extern int DAT_806109c4;     // framebuffer width  (viewport right = -1)
extern int DAT_806109c8;     // framebuffer height (viewport bottom = -1)
extern int DAT_806108e4;     // saved game-rand state selector
extern int DAT_805015c0;     // global feature flags (0x200 cutscene, 0x20000000 paused)
extern int DAT_80612610;     // session state flag (session_get_state_flag)

extern float FLOAT_80616ea8;
extern float FLOAT_80616eac;
extern float FLOAT_8060fad0;

extern int s_movies_attract_thp_8044f088;   // "movies/attract.thp"

// --- frame render/update pipeline globals (batch_589) ---
extern char DAT_805015bd;    // region/mode selector ('\n' special)
extern int  DAT_80611934;    // match phase/state (7,5 in-play; 8 restart; 3 charsel)
extern int  DAT_80611750;    // input suppression flag
extern int *DAT_80611d74;    // active local-player context table
extern int  DAT_80611930;    // (declared above region) level-setup variant
extern int  DAT_80612250;    // render pass flag A
extern int  DAT_80612258;    // render pass flag B
extern int  DAT_80612608;    // debug frame-pool HUD enable
extern int  DAT_80612028;    // arcade progress counter
extern int  DAT_806109c0;    // arcade progress denominator
extern int  DAT_805015c4;    // arcade feature flags (0x8000 = timed)
extern float FLOAT_80616eb0; // 0.0f
extern float FLOAT_80616eb4; // 1.0f
extern float FLOAT_80616eb8; // fade alpha scale (255)
extern float FLOAT_8061192c; // arcade time constant
extern double DOUBLE_80616ec0; // int->double magic-bias constant
extern unsigned char DAT_8058e3cc; // transposed view matrix scratch
} // extern "C"

// frame_pool_init_wrapper / game_phase_state_machine / load_string_table /
// texture_stream_update resolve through the shared generated headers with a
// permissive prototype; called with their real argument shape below.

// The lifecycle stages call dozens of subsystem entry points that are declared
// in the shared generated headers. A handful of frontier raw-address helpers
// have no name yet; they are declared here with a permissive prototype so the
// original call sequence is preserved verbatim.
extern "C" {
extern int FUN_80253674(void); extern int FUN_8036fb18(void); extern int FUN_8023870c(void);
extern int FUN_8035c684(void); extern int FUN_8037da9c(void); extern int FUN_80365b90(void);
extern int FUN_802386ec(void); extern int FUN_8036fbb4(void); extern int FUN_8031216c(void);
extern int FUN_80354880(void); extern int FUN_80311f04(void); extern int FUN_802e108c(void);
extern int FUN_80364774(void); extern int FUN_802f2afc(void); extern int FUN_8022b1c8(void);
extern int FUN_80279264(void); extern int FUN_80365b18(void); extern int FUN_802348e8(void);
extern int FUN_8037dad0(void); extern int FUN_802f2b8c(void); extern int FUN_8024786c(void);
extern int FUN_802449c8(void); extern int FUN_8022199c(void); extern int FUN_80232830(void);
extern int FUN_80354884(void); extern int FUN_80312248(void); extern int FUN_8035af68(void);
extern int FUN_802e12f0(void); extern int FUN_802347d8(void); extern int FUN_8035c664(void);
extern int FUN_802f2e54(void); extern int FUN_8036f488(void); extern int FUN_80244994(void);
extern int FUN_802218f8(void); extern int FUN_802322f4(void); extern int FUN_803548ac(void);
extern int FUN_8031296c(void); extern int FUN_802e18dc(void); extern int FUN_802f2ed8(void);
extern int FUN_8035c72c(void); extern int FUN_802218e4(void); extern int FUN_802322e8(void);
extern int FUN_803071a4(void); extern int FUN_802e1550(void); extern int FUN_8035b0e4(void);
extern int FUN_801c5390(void); extern int FUN_802581ec(void); extern int FUN_802ef838(void);
extern int FUN_8035c5b8(void);

// --- frame render/update pipeline frontier helpers (batch_589) ---
extern void FUN_801c2dc8(void); extern void FUN_801c5404(void); extern void FUN_801c8600(void);
extern void FUN_801dd408(void); extern void FUN_801e2c10(void); extern void FUN_801ec7f8(void);
extern void FUN_80209004(void); extern void FUN_80234a00(void); extern void FUN_80244b5c(void);
extern void FUN_8028e0cc(void); extern void FUN_802963d8(void); extern void FUN_8029ad44(void);
extern int  FUN_802a460c(void); extern void FUN_802e181c(void); extern double FUN_802ee824(void);
extern int  FUN_802ef5a8(int);  extern int  FUN_802ef5fc(int);  extern int  FUN_802ef7f4(int);
extern void FUN_802ef83c(void); extern void FUN_802efde8(void); extern void FUN_802f0d88(void);
extern void FUN_802f3098(void); extern void FUN_802f329c(void); extern void FUN_802f3774(void);
extern void FUN_802f3914(void); extern void FUN_8030b8bc(void); extern void FUN_8031c4cc(void);
extern void FUN_803548b0(void); extern int  FUN_80357a48(void); extern void FUN_80357a58(void);
extern void FUN_803585f0(void); extern void FUN_80358634(void); extern void FUN_8035af44(void);
extern void FUN_8035b0c0(void); extern void FUN_8035c448(void); extern void FUN_8035cd34(void);
extern void FUN_8036deb0(void); extern unsigned int FUN_803825c0(double);
extern void FUN_80354908(void);
}

// ===========================================================================
// Address: 0x801c1714 | Name: session_get_state_flag | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Read the current session state flag (DAT_80612610).
// ===========================================================================
int session_get_state_flag(void)
{
    return DAT_80612610;
}

// ===========================================================================
// Address: 0x801c171c | Name: session_reset_all_systems | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Reset every subsystem to its default/empty state. Mode-independent; runs the
// full teardown-to-defaults sequence in fixed order.
// ===========================================================================
void session_reset_all_systems(void)
{
    gx_reset_env_tex_matrices();
    cutscene_reset_playback_id();
    pack_clear_lru_state();
    render_target_group_activate();
    lens_flare_state_reset();
    timer_bank_reset();
    expr_reset_time();
    render_entity_pool_reset();
    entity_system_reset();
    sound_volume_reset_defaults();
    sound_init_fade_slots();
    FUN_80253674();
    sound_init_persistent_slots();
    player_ai_target_table_reset();
    aiactor_bark_reset_state();
    aiactor_bark_playback_reset();
    object_anim_defs_reset_state();
    fluid_reset();
    character_fx_init_weapon();
    rumble_clear_state();
    FUN_8036fb18();
    light_probe_pool_clear();
    player_system_reset();
    viewport_commit_to_gx();
    sound_system_shutdown();
    subobj_pool_clear_state();
    FUN_8023870c();
    FUN_8035c684();
}

// ===========================================================================
// Address: 0x801c17a8 | Name: session_alloc_all_systems | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Allocate every subsystem's pools/buffers for a new session. Mostly
// mode-independent; the memory-card buffers are only allocated for the
// front-end modes (0x65/0x66/0x6a) and one extra system for mode 0x64.
// ===========================================================================
void session_alloc_all_systems(void)
{
    gx_reset_env_tex_matrices();
    pack_open_level_paks();
    cutscene_reset_playback_id();
    lightning_generate_all();
    arc_noise_init();
    spawn_pack_clear_ready();
    stream_queue_init();
    lens_flare_alloc_buffers();
    sprite_system_init();
    water_surface_system_reset();
    sound_volume_reset_defaults();
    sound_init_fade_slots();
    FUN_80253674();
    sound_init_persistent_slots();
    player_ai_target_table_reset();
    aiactor_bark_reset_state();
    object_anim_defs_reset_state();
    fluid_alloc_banks(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    notif_system_init();
    if ((DAT_80612614 - 0x65U < 2) || (DAT_80612614 == 0x6a)) {
        memcard_alloc_buffers(0, 0, 0, 0, 0, 0, 0, 0);
        rumble_clear_state();
    }
    timer_bank_init();
    render_entity_pool_init();
    entity_system_alloc();
    player_system_alloc();
    viewport_commit_frame();
    sound_system_init();
    aiactor_bark_system_setup();
    subobj_pool_alloc_buffers();
    FUN_8037da9c();
    character_fx_register_callback();
    player_ai_system_alloc();
    aiactor_system_alloc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    FUN_80365b90();
    light_probe_pool_reset();
    FUN_802386ec();
    FUN_8036fbb4();
    if (DAT_80612614 == 100) {
        FUN_8031216c();
    }
}

// ===========================================================================
// Address: 0x801c1888 | Name: session_preload_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Preload the resources needed for the current mode. Menu/special modes take
// short mode-specific paths; the in-game level mode (default) runs the full
// asset-preload sequence. All paths converge on the cutscene render dispatch
// and the level resource preload.
// ===========================================================================
void session_preload_by_mode(void)
{
    sprite_load_all_visible();
    render_target_queue_animation(0, 0);
    notif_stub_fba0();

    if (DAT_80612614 == 0x68) {
        FUN_80354880();
    } else if (DAT_80612614 < 0x68) {
        if (DAT_80612614 == 0x66) {
            FUN_80311f04();
            weather_stub_nop();
        } else if (0x65 < DAT_80612614) {
            boot_preload_strings(0);
        } else if (100 < DAT_80612614) {
            weather_stub_nop();
            lens_flare_slot_reset_init();
            FUN_802e108c();
        } else {
            goto full_preload;
        }
    } else if (DAT_80612614 < 0x6c) {
        if (0x69 < DAT_80612614) {
            sprite_load_all_visible();
        }
    } else {
    full_preload:
        weather_stub_nop();
        decal_preload_sprites();
        character_fx_preload_resources();
        particle_overlay_stub_nop();
        water_surface_preload_resources();
        weather_system_preload();
        character_burn_stub_nop();
        weapon_system_preload_resources();
        lens_flare_slot_reset_init();
        player_save_controller_config();
        scene_load_tileset_data();
        soundpad_system_init();
        object_preload_shell_and_weapons(0, 0.0, 0.0, 0, 0, 0, 0, 0);
        shell_casing_stub();
        aiactor_preload_resources(0, 0, 0, 0, 0, 0, 0, 0);
        FUN_80364774();
        FUN_802f2afc();
        particle_zone_light_stub();
        FUN_8022b1c8();
        screen_effect_load_mode_textures(0);
    }

    cutscene_render_dispatch_if_enabled(0, 0, 0, 0, 0, 0, 0, 0);
    level_preload_all_resources();
}

// ===========================================================================
// Address: 0x801c197c | Name: session_init_level_systems | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Initialise the level-scoped systems after resources are loaded. Runs the
// fixed init/reset sequence; the memory-card save-restore branch runs only when
// the profile system reports one is pending.
// ===========================================================================
void session_init_level_systems(void)
{
    frame_pool_init_wrapper(3);
    anim_pose_eval_frame();
    player_ai_pool_reset();
    aiactor_system_reset();
    FUN_80279264();
    FUN_80365b18();
    FUN_802348e8();
    text_system_init();
    weather_system_reset();
    FUN_8037dad0();
    water_surface_level_init();
    lens_flare_nop();
    character_system_reset();
    decal_system_reset();
    scene_level_load_finalize();
    weather_system_spawn();
    debug_line_clear_all();
    particle_zone_light_init();
    soundpad_update();
    spawn_system_reset();
    screen_effect_system_init();
    object_system_reset();
    particle_system_reset();
    player_ai_pool_reset();
    shell_casing_pool_clear();
    player_character_system_init();
    sprite_grid_init();
    FUN_802f2b8c();
    music_load_track(0);
    weapon_system_pool_reset();
    particle_pool_reset();
    cutscene_system_startup();
    if (FUN_8024786c() != 0) {
        FUN_802449c8();
    }
    FUN_8022199c();
    FUN_80232830();
    spectator_create_slot();
    arc_pool_init();
}

// ===========================================================================
// Address: 0x801c1a3c | Name: session_setup_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-mode scene/actor setup. Each mode allocates and configures the specific
// systems it needs. The default (in-game level) path additionally splits on
// DAT_80611930: variant 0 sets up the single-view level (primary player slot,
// viewport rect/depth from the framebuffer size), while the non-zero variant
// runs the full multi-system level setup.
// ===========================================================================
void session_setup_by_mode(void)
{
    sprite_finalize_load();
    if (DAT_80612614 == 0x67) {
        text_system_init_wrapper();
    }
    render_state_snapshot();

    if (DAT_80612614 == 0x68) {
        frame_pool_init_wrapper(3);
        player_character_system_alloc();
        load_string_table();
        FUN_80354884();
        music_load_level_bank();
        goto done;
    }
    if (DAT_80612614 < 0x68) {
        if (DAT_80612614 == 0x66) {
            charselect_stub_noop();
            frame_pool_init_wrapper(3);
            player_character_system_alloc();
            load_string_table();
            music_load_level_bank();
            player_ai_pool_alloc();
            weather_system_alloc();
            arc_pool_alloc(0, 0, 0, 0, 0, 0, 0, 0);
            FUN_80312248();
            goto done;
        }
        if (0x65 < DAT_80612614) {
            frame_pool_init_wrapper(1);
            player_character_system_alloc();
            music_load_level_bank();
            boot_splash_setup();
            load_string_table();
            goto done;
        }
        if (100 < DAT_80612614) {
            charselect_stub_noop();
            frame_pool_init_wrapper(1);
            water_surface_level_load();
            player_character_system_alloc();
            sprite_anim_resolve_ids();
            sprite_grid_init_wrapper();
            weather_system_alloc();
            arc_pool_alloc(0, 0, 0, 0, 0, 0, 0, 0);
            FUN_8035af68();
            load_string_table();
            FUN_802e12f0();
            music_load_level_bank();
            scene_system_init();
            player_ai_pool_alloc();
            goto done;
        }
    } else if (DAT_80612614 < 0x6c) {
        if (DAT_80612614 < 0x6a) {
            frame_pool_init_wrapper(3);
            player_character_system_alloc();
            thp_movie_play_fullscreen(0, 0.0, 0.0, 0, 0, 0, 0, 0, 1);
        } else {
            frame_pool_init_wrapper(1);
            player_character_system_alloc();
            load_string_table();
            FUN_802e12f0();
        }
        goto done;
    }

    if (DAT_80611930 == 0) {
        int slot;
        int rectRight;
        int rectBottom;
        double depthA;
        double depthB;
        double depthC;

        frame_pool_init_wrapper(2);
        slot = player_slot_alloc(0);
        player_slot_set_entity(slot, 0x10);
        DAT_80611d54 = slot;
        depthC = (double)FLOAT_80616eac;
        depthA = (double)(FLOAT_80616ea8 + FLOAT_8060fad0);
        depthB = -depthA;
        viewport_set_depth_params(depthA, depthB, depthC, slot + 0x224);
        rectRight = DAT_806109c4 - 1;
        rectBottom = DAT_806109c8 - 1;
        viewport_set_rect((int *)(slot + 0x224), 0, 0, rectRight, rectBottom);
        spawn_pack_frame(0, 0.0f, 0.0f);
        water_surface_level_load();
        sprite_anim_resolve_ids();
        music_load_level_bank();
        weather_system_alloc();
        FUN_8035af68();
        decal_system_alloc();
        scene_system_init();
        cutscene_init();
        particle_zone_light_init();
        weather_system_alloc_and_init();
        debug_line_system_init();
        shell_casing_system_alloc();
        player_ai_pool_alloc();
        weapon_system_pool_reset();
        sprite_grid_init_wrapper();
        arc_pool_alloc(0, 0, 0, 0, 0, 0, 0, 0);
        load_string_table();
    } else {
        frame_pool_init_wrapper(3);
        spawn_pack_frame(0, 0.0f, 0.0f);
        FUN_802347d8();
        weather_system_alloc();
        FUN_8035c664();
        FUN_8035af68();
        water_surface_level_load();
        sprite_anim_resolve_ids();
        shell_casing_system_alloc();
        character_system_alloc();
        decal_system_alloc();
        scene_system_init();
        weather_system_alloc_and_init();
        debug_line_system_init();
        particle_zone_light_alloc();
        soundpad_build_zone_graph();
        soundpad_init_level();
        charselect_stub_noop();
        spawn_system_alloc();
        screen_effect_system_reset();
        particle_pool_alloc();
        object_preload_and_reset(0, 0.0, 0.0, 0, 0, 0, 0, 0);
        player_ai_pool_alloc();
        shell_casing_system_reset();
        player_character_system_alloc();
        load_string_table();
        sprite_grid_init_wrapper();
        FUN_802f2e54();
        music_load_level_bank();
        weapon_system_reset();
        cutscene_init();
        FUN_8036f488();
        if (FUN_8024786c() != 0) {
            FUN_80244994();
        }
        FUN_802218f8();
        FUN_802322f4();
        spectator_create_slot();
        arc_pool_alloc(0, 0, 0, 0, 0, 0, 0, 0);
        FUN_80354884();
        aiactor_render_debug_gate(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

done:
    charselect_stub_noop();
}

// ===========================================================================
// Address: 0x801c1cfc | Name: session_teardown_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-mode session shutdown. Menu/special modes take short shutdown paths; the
// in-game level mode runs the full subsystem shutdown sequence. All paths
// converge on the shared level-unload/reset tail.
// ===========================================================================
void session_teardown_by_mode(void)
{
    if (DAT_80612614 == 0x68) {
        music_queue_track(0);
        player_slot_clear_active();
        FUN_803548ac();
        goto common_tail;
    }
    if (DAT_80612614 < 0x68) {
        if (DAT_80612614 == 0x66) {
            music_queue_track(0);
            player_slot_clear_active();
            FUN_8031296c();
            weather_system_shutdown();
            goto common_tail;
        }
        if (0x65 < DAT_80612614) {
            music_queue_track(0);
            player_slot_clear_active();
            goto common_tail;
        }
        if (100 < DAT_80612614) {
            music_queue_track(0);
            player_slot_clear_active();
            FUN_802e18dc();
            resource_pool_shutdown();
            goto common_tail;
        }
    } else if (DAT_80612614 < 0x6c) {
        if (DAT_80612614 < 0x6a) {
            player_slot_clear_active();
        } else {
            player_slot_clear_active();
            FUN_802e18dc();
        }
        goto common_tail;
    }

    resource_pool_shutdown();
    music_queue_track(0);
    shell_casing_destroy_all();
    character_system_stub();
    player_slot_clear_active();
    player_ai_pool_stub();
    object_weapon_render_post_nop();
    particle_stub_nop();
    hud_worldmsg_stub_a();
    soundpad_clear_zone_data();
    soundpad_shutdown();
    particle_zone_light_free();
    scene_system_shutdown();
    FUN_802f2ed8();
    particle_pool_shutdown();
    weather_system_shutdown();
    debug_line_stub_nop();

common_tail:
    level_actor_unload_all();
    water_surface_clear_params_dirty();
    aiactor_clear_visible_node_scratch();
    viewport_commit_frame_stub();
    decal_free_buffer_if_idle();
    spawn_system_clear_count();
    player_ai_system_shutdown();
    entity_system_shutdown();
    render_entity_stub_empty();
    FUN_8035c72c();
    weather_system_shutdown();
    sprite_overlay_stub_nop();
    timer_bank_stub_nop();
    lens_flare_buffers_clear();
    sound_reset_streams();
    notif_stub_fca0();
    FUN_802218e4();
    FUN_802322e8();
    func_text_8b20();
    pack_close_streaming_handles();
    block_alloc_clear_ptrs();
    mem_heap_reset_counters();
}

// ===========================================================================
// Address: 0x801c1e48 | Name: session_tick_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-frame session update. Restores the game-rand state, pumps the weather and
// texture streamer, clears a per-frame flag on every node of the reset list,
// runs the game-phase state machine, then dispatches the mode-specific entity
// update. The in-game level path splits on the "paused/cutscene-only" feature
// bit (0x20000000): the full path ticks every gameplay system, the reduced path
// ticks only scene/entity/cutscene playback.
// ===========================================================================
void session_tick_by_mode(void)
{
    game_rand_state_save_restore(DAT_806108e4);
    weather_system_update();
    texture_stream_update();

    for (DAT_80611c50 = DAT_80540a44; DAT_80611c50 != 0;
         DAT_80611c50 = *(int *)DAT_80611c50) {
        *(unsigned short *)(*(int *)(DAT_80611c50 + 4) + 0x76) = 0;
    }

    camera_shake_reset_all();
    game_phase_state_machine();

    if (DAT_80612614 == 0x68) {
        entity_update_dispatch();
        entity_process_dirty_list();
        return;
    }
    if (DAT_80612614 < 0x68) {
        if (DAT_80612614 == 0x66) {
            entity_update_dispatch();
            entity_process_dirty_list();
            FUN_803071a4();
            sound_update_music_volume();
            return;
        }
        if (0x65 < DAT_80612614) {
            return;
        }
        if (100 < DAT_80612614) {
            entity_update_dispatch();
            entity_process_dirty_list();
            FUN_802e1550();
            sound_update_music_volume();
            sound_update_fade_slots();
            return;
        }
    } else if (DAT_80612614 < 0x6c) {
        if (DAT_80612614 < 0x6a) {
            return;
        }
        FUN_802e1550();
        return;
    }

    particle_zone_light_animate();
    if ((DAT_805015c0 & 0x20000000) == 0) {
        FUN_8035b0e4();
        entity_rebuild_category_lists();
        water_surface_per_frame_update();
        scene_update_visible_nodes();
        FUN_801c5390();
        spectator_tick_fade();
        fluid_update();
        sound_update_music_volume();
        sound_update_fade_slots();
        aiactor_bark_scheduler_tick();
        aiactor_bark_playback_tick();
        entity_update_dispatch();
        aiactor_pre_tick();
        entity_process_dirty_list();
        aiactor_tick_update();
        player_ai_behavior_tick();
        particle_system_tick();
        shell_casing_tick_all();
        FUN_8035c5b8();
        particle_update_all();
        FUN_802581ec();
        FUN_802ef838();
        if ((DAT_805015c0 & 0x200) != 0) {
            cutscene_tick();
        }
    } else {
        FUN_8035b0e4();
        entity_rebuild_category_lists();
        scene_update_visible_nodes();
        entity_update_dispatch();
        entity_process_dirty_list();
        cutscene_playback_update();
        player_ai_behavior_tick();
    }
}

// ===========================================================================
// Address: 0x801c1fd8 | Name: session_frame_update_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Post-tick per-mode frame update: activate the primary viewport, run the
// entity post-transform pass, pump the audio streamer and music, then the
// mode-specific gameplay update. The in-game default path splits on the
// paused/cutscene-only feature bit (0x20000000): the full path runs decals,
// particles, gibs, weather, targeting, muzzle FX, AI post-tick, soundpad and
// scoring; the reduced path runs a trimmed set.
// ===========================================================================
void session_frame_update_by_mode(void)
{
    notif_dismiss_all();

    if (DAT_80612614 == 0x68) {
        viewport_set_active(DAT_80611d54 + 0x224);
        FUN_803548b0();
        entity_targeting_stub();
        entity_post_transform_pass();
        sound_stream_update_tick();
        music_stop();
        return;
    }
    if (DAT_80612614 < 0x68) {
        if (DAT_80612614 == 0x66) {
            viewport_set_active(DAT_80611d54 + 0x224);
            FUN_8030b8bc();
            camera_per_frame_update();
            entity_targeting_stub();
            entity_post_transform_pass();
            sound_stream_update_tick();
            music_stop();
            render2d_debug_freeze_check();
            render2d_stub_nop();
            expr_advance_time_paused();
            particle_system_main_tick();
            return;
        }
        if (0x65 < DAT_80612614) {
            viewport_set_active(DAT_80611d54 + 0x224);
            sound_stream_update_tick();
            music_stop();
            boot_splash_update();
            return;
        }
        if (100 < DAT_80612614) {
            viewport_set_active(DAT_80611d54 + 0x224);
            entity_targeting_stub();
            entity_post_transform_pass();
            sound_stream_update_tick();
            music_stop();
            render2d_debug_freeze_check();
            render2d_stub_nop();
            expr_advance_time_paused();
            particle_system_main_tick();
            return;
        }
    } else if (DAT_80612614 < 0x6c) {
        if (DAT_80612614 < 0x6a) {
            camera_per_frame_update();
            return;
        }
        viewport_set_active(DAT_80611d54 + 0x224);
        return;
    }

    viewport_set_active(DAT_80611d54 + 0x224);
    scene_per_frame_update();
    if ((DAT_805015c0 & 0x20000000) == 0) {
        decal_tick_uv_scroll();
        particle_system_main_tick();
        gib_trail_tick();
        weather_system_tick();
        debug_line_tick_width();
        entity_targeting_stub();
        entity_post_transform_pass();
        object_weapon_muzzle_particles_emit(0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0);
        aiactor_update_zone_arrival();
        player_ai_post_tick_room_sync();
        object_weapon_render_pre_nop();
        FUN_803585f0();
        sound_stream_update_tick();
        music_stop();
        timer_bank_tick();
        expr_advance_time_paused();
        FUN_80234a00();
        soundpad_check_player_zone_changes();
        soundpad_update_proximity();
        FUN_802f3914();
        if ((DAT_805015bd == '\n') && (DAT_80612614 == 0x38)) {
            FUN_803548b0();
        }
        if (DAT_805015bd != '\n') {
            score_calculate_rankings(0);
        }
    } else {
        player_ai_post_tick_room_sync();
        decal_tick_uv_scroll();
        particle_system_main_tick();
        weather_system_tick();
        entity_targeting_stub();
        entity_post_transform_pass();
        sound_stream_update_tick();
        particle_update_all();
        music_stop();
        timer_bank_tick();
        expr_advance_time_paused();
    }
}

// ===========================================================================
// Address: 0x801c21d8 | Name: session_camera_update_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Activate the primary viewport, then run the per-frame camera update for the
// gameplay modes. The in-game level path additionally updates scene portal
// visibility; menu/movie modes (0x66, 0x69, 0x6a-0x6b) skip parts.
// ===========================================================================
void session_camera_update_by_mode(void)
{
    viewport_set_active(DAT_80611d54 + 0x224);
    if (DAT_80612614 == 0x69) {
        return;
    }
    if (DAT_80612614 < 0x69) {
        if (DAT_80612614 == 0x66) {
            return;
        }
        if ((0x65 < DAT_80612614) || (100 < DAT_80612614)) {
            camera_per_frame_update();
            return;
        }
    } else if (DAT_80612614 < 0x6c) {
        camera_per_frame_update();
        return;
    }
    camera_per_frame_update();
    scene_portal_visibility_update();
}

// ===========================================================================
// Address: 0x801c2240 | Name: session_handle_pause_input | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Poll the pause/menu controller input during gameplay. Handles the special
// "save-and-reset" region combo (0x38 mode), opening the pause menu on the
// Start button (0x100) or the split-screen toggle (0x8000), gated on match
// phase (7/5), feature bits (0x60000000) and the controller-removed overlay.
// ===========================================================================
void session_handle_pause_input(void)
{
    bool overlayShown;
    short removedMask;
    int allowOverlay;
    unsigned int held;
    int i;
    int limit;

    overlayShown = false;
    removedMask = input_get_removed_mask();
    if ((removedMask != 0) && (input_can_show_overlay() != 0)) {
        overlayShown = true;
    }
    if ((DAT_805015c0 & 0x200) != 0) {
        return;
    }
    if ((DAT_805015bd == '\n') && (DAT_80612614 == 0x38)) {
        if (input_get_buttons_held(*DAT_80611d74) == 0) {
            return;
        }
        match_reset_with_save();
        return;
    }
    if ((DAT_80611934 != 7) && (DAT_80611934 != 5)) {
        return;
    }
    if ((DAT_805015c0 & 0x60000000) != 0) {
        return;
    }
    if (DAT_80611750 != 0) {
        return;
    }

    if ((overlayShown && (FUN_802ef5fc(DAT_80611d74[1]) == 0)) ||
        ((held = input_get_buttons_held(*DAT_80611d74), (held & 0x100) != 0) &&
         (FUN_802ef5a8(DAT_80611d74[1]) == 0))) {
        if ((FLOAT_80616eb0 < *(float *)(DAT_80611d74[5] + 0x1cc)) || (DAT_805015bd != '\n')) {
            if (overlayShown) {
                render_target_restore_active();
                for (i = 0; limit = session_get_state_flag(), i < limit; i = i + 1) {
                    FUN_802f3774();
                }
                render_target_save_active();
                FUN_802f329c();
            } else {
                FUN_802f329c();
            }
            input_clear_buttons(*DAT_80611d74, 0x100);
        }
    } else {
        held = input_get_buttons_held(*DAT_80611d74);
        if (((held & 0x8000) != 0) && (camera_get_split_mask() == 0) &&
            (FUN_802ef5a8(DAT_80611d74[1]) == 0)) {
            FUN_802f3098();
            input_clear_buttons(*DAT_80611d74, 0x8000);
        }
    }
}

// ===========================================================================
// Address: 0x801c2410 | Name: session_player_camera_tick | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-frame player + camera update. When the local-player context reports a
// spectator/detached slot (context[1] < 0) it runs the reduced player tick;
// otherwise it activates the viewport, runs pause input, player tick, camera
// update, weapon recoil render, HUD tick and portal visibility for the in-game
// modes.
// ===========================================================================
void session_player_camera_tick(void)
{
    if (DAT_80611d74[1] < 0) {
        if (DAT_80612614 != 0x69) {
            if (DAT_80612614 < 0x69) {
                if (100 < DAT_80612614) {
                    return;
                }
            } else if (DAT_80612614 < 0x6c) {
                return;
            }
        }
        if ((DAT_80611d74[3] & 0x2000U) != 0) {
            input_reset_pad(*DAT_80611d74);
        }
        player_main_tick();
        entity_interaction_find_target();
        object_weapon_recoil_render_tick();
        return;
    }

    viewport_set_active(DAT_80611d54 + 0x224);
    if (DAT_80612614 == 0x69) {
        return;
    }
    if (DAT_80612614 < 0x69) {
        if (DAT_80612614 == 0x66) {
            return;
        }
        if (0x65 < DAT_80612614) {
            camera_per_frame_update();
            return;
        }
        if (100 < DAT_80612614) {
            camera_per_frame_update();
            return;
        }
    } else if (DAT_80612614 < 0x6c) {
        camera_per_frame_update();
        return;
    }

    session_handle_pause_input();
    FUN_802ef83c();
    FUN_802efde8();
    camera_stub_nop_d();
    player_main_tick();
    player_sync_attached_entities();
    entity_interaction_find_target();
    camera_per_frame_update();
    object_weapon_recoil_render_tick();
    particle_advance_global_time();
    if ((DAT_80611d54 != 0) && ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 2) != 0)) {
        hud_per_frame_tick();
    }
    if (DAT_80611d74[2] != 0) {
        scene_portal_visibility_update();
    }
}

// ===========================================================================
// Address: 0x801c252c | Name: session_render_hud_overlays | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Render the scoring-related HUD overlays (think callbacks, spectator overlay,
// nametags) for levels that support scoring. Sets a full-screen scissor first.
// ===========================================================================
void session_render_hud_overlays(void)
{
    if (game_level_supports_scoring() != 0) {
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        water_stub_shutdown();
        FUN_8035cd34();
        entity_dispatch_think_callbacks();
        hud_overlay_stub_loop();
        spectator_render_overlay();
        character_nametag_render_pass();
        FUN_8035af44();
    }
}

// ===========================================================================
// Address: 0x801c2584 | Name: session_render_scene_by_mode | Date: 2026-09-07
// ---------------------------------------------------------------------------
// The main per-mode scene render dispatcher. Activates the primary viewport and
// GX state, then either the full 3D world render (default in-game path: room
// selection, render lists, glow list, scene visibility, fluid/decal/weather,
// per-mode overlays, fog, particles, characters, lens flare, first-person
// weapon, HUD) or the reduced cutscene-only path (0x20000000 feature bit).
// Menu/special modes take short paths.
// ===========================================================================
void session_render_scene_by_mode(double param_2)
{
    int applyFog;
    bool splitScene;

    if (*(int *)(DAT_80611d54 + 0x214) == 0) {
        sprite_update_pulse_alpha();
    }

    if (DAT_80612614 == 0x68) {
        viewport_set_active(DAT_80611d54 + 0x224);
        viewport_set_gx_active();
        gx_material_state_nop();
        FUN_80354908();
        return;
    }
    if (DAT_80612614 < 0x68) {
        if (DAT_80612614 == 0x66) {
            viewport_set_active(DAT_80611d54 + 0x224);
            viewport_set_gx_active();
            gx_material_state_nop();
            FUN_8031c4cc();
            return;
        }
        if (0x65 < DAT_80612614) {
            boot_splash_render();
            return;
        }
        if (100 < DAT_80612614) {
            viewport_set_active(DAT_80611d54 + 0x224);
            viewport_set_gx_active();
            gx_material_state_nop();
            FUN_802e181c();
            sprite_draw_viewport_quad();
            return;
        }
    } else if (DAT_80612614 < 0x6c) {
        if (DAT_80612614 < 0x6a) {
            return;
        }
        viewport_set_active(DAT_80611d54 + 0x224);
        viewport_set_gx_active();
        gx_material_state_nop();
        FUN_802e181c();
        return;
    }

    entity_select_best_room();
    entity_build_render_lists();
    viewport_set_active(DAT_80611d54 + 0x224);
    viewport_set_gx_active();
    gx_material_state_nop();
    mtx4_transpose(*(float **)(DAT_80611d54 + 0x208), (float *)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;

    if ((DAT_805015c0 & 0x20000000) == 0) {
        if (((*(unsigned int *)(DAT_80611d54 + 0x21c) & 2) == 0) ||
            (FUN_802ef7f4(*(int *)(DAT_80611d74 + 4)) != 0)) {
            double fog = (double)FLOAT_80616eb0;

            camera_stub_nop_a();
            gx_glow_list_alloc();
            FUN_801c5404();
            hud_render_special_reticle_gate();
            splitScene = (FUN_80357a48() != 0);
            if (!splitScene) {
                scene_visibility_and_render();
            } else {
                FUN_80357a58();
            }
            camera_stub_nop_b();
            fluid_render();
            decal_render_pass_base();
            if (FUN_8024786c() != 0) {
                FUN_80244b5c();
            }
            if (DAT_80612614 == 0xb) {
                FUN_801e2c10();
            } else if (DAT_80612614 == 1) {
                FUN_8036deb0();
                player_ai_draw_nametag_hud();
                FUN_801c8600();
                FUN_8028e0cc();
            } else if (DAT_80612614 == 0x11) {
                player_ai_draw_nametag_hud();
                FUN_8028e0cc();
            } else if (DAT_80612614 == 0xe) {
                proj_ai_render_trails();
                FUN_802963d8();
                FUN_8029ad44();
            } else if (DAT_80612614 == 0x12) {
                FUN_801ec7f8();
            }
            gx_set_fog_params_frame(fog, fog, fog, fog, fog, fog, 0);
            player_ai_render_tick();
            particle_system_render_dispatch();
            particle_render_all_wrapper();
            character_render_all();
            if (DAT_80612614 == 0x51) {
                FUN_802a460c();
            }
            if ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 8) == 0) {
                lens_flare_occlusion_render();
            }
            weather_system_render();
            debug_line_render_all();
            FUN_8035b0c0();
            if ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 8) == 0) {
                FUN_8035c448();
                weather_render_pre_pass();
            }
            if (object_weapon_first_person_flag() != 0) {
                hud_reticle_render_gate();
                particle_update_weather();
                object_weapon_first_person_render();
            }
            if (DAT_80611d74 != 0) {
                FUN_801c2dc8();
            }
            weather_post_render_and_camera();
            if ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 2) != 0) {
                FUN_802f0d88();
            }
            if ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 8) == 0) {
                weather_post_process_composite();
            }
            hud_render_scoreboard_overlay();
            if ((DAT_805015bd == '\n') && (DAT_80612614 == 0x38)) {
                FUN_80354908();
            }
            gx_glow_list_free();
        }
    } else {
        FUN_8035af44();
        splitScene = (FUN_80357a48() != 0);
        if (!splitScene) {
            scene_visibility_and_render();
        } else {
            FUN_80357a58();
        }
        applyFog = water_surface_should_apply_fog();
        if (applyFog != 0) {
            character_fx_stub_nop();
        }
        if ((DAT_80612614 != 0x25) && (DAT_80612614 != 0x24)) {
            FUN_8035b0c0();
        }
        lens_flare_occlusion_render();
        particle_system_render_dispatch();
        if ((DAT_80612614 == 0x25) || (DAT_80612614 == 0x24)) {
            FUN_8035b0c0();
        }
        particle_render_all_wrapper();
        weather_system_render();
        debug_line_render_all();
        weather_post_render_and_camera();
        anim_stub_noop();
        weather_post_process_composite();
        cutscene_apply_visual_effects();
        FUN_801c2dc8();
    }
    (void)param_2;
}

// ===========================================================================
// Address: 0x801c2934 | Name: session_render_post_hud | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Post-scene HUD render for the in-game modes (< 0x65): weather post pass,
// full-screen flash, per-player HUD view (unless paused / view disabled), and
// cutscene subtitles when the cutscene feature bit (0x200) is set.
// ===========================================================================
void session_render_post_hud(void)
{
    if (DAT_80612614 < 0x65) {
        weather_render_post_pass();
        hud_draw_fullscreen_flash();
        FUN_80358634();
        if (((DAT_805015c0 & 0x20000000) == 0) &&
            ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 2) != 0)) {
            hud_render_player_view_wrapper(0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        }
        if ((DAT_805015c0 & 0x200) != 0) {
            cutscene_render_subtitles();
        }
    }
}

// ===========================================================================
// Address: 0x801c29cc | Name: session_render_stub_empty | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Empty render hook (no-op).
// ===========================================================================
void session_render_stub_empty(void)
{
}

// ===========================================================================
// Address: 0x801c29d0 | Name: session_render_fullscreen_fade | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Render the full-screen fade/results overlay. Draws the notification message,
// runs the autosave countdown (mode 0x65) or restart countdown (phase 8), and
// for the results phases fills a full-screen alpha quad whose opacity is driven
// by the match/arcade timer. Also renders the debug frame-pool HUD when
// enabled.
// ===========================================================================
void session_render_fullscreen_fade(void)
{
    int isArcade;
    unsigned int alpha;
    unsigned int half;
    double fade;

    if (DAT_80611934 == 3) {
        charselect_draw_inverse_look();
    }
    notify_message_render();
    if (DAT_80612614 == 0x65) {
        autosave_countdown_tick();
        autosave_countdown_render(0.0, 0.0, 0.0, 0.0, 0, 0);
        goto debug_hud;
    }
    if ((DAT_80612614 < 0x65) && (DAT_80611934 == 8)) {
        hud_draw_restart_countdown();
    }

    fade = (double)FLOAT_80616eb4;
    switch (DAT_80611934) {
    default:
        hud_results_panel_update();
        break;
    case 4:
    case 6:
    case 10:
        break;
    case 5:
        hud_results_panel_update();
        break;
    case 8:
        hud_results_panel_update();
        if (((DAT_805015c4 & 0x8000) != 0) && (game_is_standard_arcade() != 0)) {
            fade = game_get_difficulty_speed();
            fade = (double)(float)((double)FLOAT_8061192c / fade);
            goto draw_quad;
        }
        break;
    case 9:
    case 0xb:
    case 0xc:
    draw_quad:
        if (((DAT_805015c4 & 0x8000) == 0) || (game_is_standard_arcade() == 0)) {
            float f = FLOAT_80616eb4;
            fade = FUN_802ee824();
            if ((double)FLOAT_80616eb0 != fade) {
                fade = FUN_802ee824();
                f = (float)((double)FLOAT_8061192c / fade);
            }
            fade = (double)f;
        }
        /* fallthrough */
    case 2:
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        gx_set_blend_mode(1);
        gx_set_sub_material_id(1);
        gx_set_color_update_mode(0);
        alpha = FUN_803825c0((double)(float)((double)FLOAT_80616eb8 * fade));
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, alpha & 0xff);
        break;
    }

    isArcade = game_is_standard_arcade();
    half = ((unsigned int)DAT_806109c0 >> 1) +
           (unsigned int)((DAT_806109c0 < 0) && ((DAT_806109c0 & 1) != 0));
    if ((isArcade != 0) && ((int)DAT_80612028 < (int)half)) {
        double top = (double)FLOAT_80616eb4;
        double num = (double)DAT_80612028 - DOUBLE_80616ec0;
        double den = (double)half - DOUBLE_80616ec0;
        fade = (double)(float)(top - (double)(float)(num / den));
        double clamped = fade;
        if (top < fade) {
            clamped = top;
        }
        if ((double)FLOAT_80616eb0 <= clamped) {
            if ((double)FLOAT_80616eb4 < fade) {
                fade = (double)FLOAT_80616eb4;
            }
        } else {
            fade = (double)FLOAT_80616eb0;
        }
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        gx_set_blend_mode(1);
        gx_set_sub_material_id(1);
        gx_set_color_update_mode(0);
        alpha = FUN_803825c0((double)(float)((double)FLOAT_80616eb8 * fade));
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, alpha & 0xff);
    }

debug_hud:
    if (DAT_80612608 != 0) {
        mem_alloc_dispatch();
        debug_frame_pool_hud();
    }
}

// ===========================================================================
// Address: 0x801c2c48 | Name: session_cutscene_skip_check | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-mode cutscene-skip / event-trigger check. Fires the level event hook
// (FUN_801dd408 x3) for specific game modes and cutscene id ranges when the
// caller reports the trigger condition (param==0).
// ===========================================================================
void session_cutscene_skip_check(int trigger)
{
    int cutscene;
    int cutId;

    if (DAT_80612614 == 0x11) {
        FUN_80209004();
    } else if (DAT_80612614 < 0x11) {
        if ((DAT_80612614 == 0xb) && (trigger == 0)) {
            FUN_801dd408();
            FUN_801dd408();
            FUN_801dd408();
        }
    } else if (DAT_80612614 == 0x22) {
        cutscene = cutscene_get_data();
        if ((cutscene != 0) && (trigger == 0)) {
            cutId = *(int *)(cutscene + 0xc);
            if (((-1 < cutId && cutId < 0x1a6) || (0x39d < cutId && cutId < 0x416)) ||
                ((0x463 < cutId && cutId < 0x529) ||
                 ((0x6d8 < cutId && cutId < 0x70c) || (0x9de < cutId && cutId < 0xa96)))) {
                FUN_801dd408();
                FUN_801dd408();
                FUN_801dd408();
            }
        }
    } else if ((DAT_80612614 < 0x22) && (0x20 < DAT_80612614) &&
               ((cutscene = cutscene_get_data()) != 0) && (trigger == 0)) {
        cutId = *(int *)(cutscene + 0xc);
        if ((0x14a < cutId && cutId < 0x1a5) || (0x2a8 < cutId && cutId < 0x849)) {
            FUN_801dd408();
            FUN_801dd408();
            FUN_801dd408();
        }
    }
}
