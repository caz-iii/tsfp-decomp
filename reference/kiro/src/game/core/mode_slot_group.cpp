// mode_slot_group.cpp - Attract-screen flow + weapon-slot group classifiers.
//
// A small game-flow / slot-index cluster that sits alongside mode_progression:
//   - mode_attract_screen_init  (0x802c8c6c) one-shot init callback for the
//     attract/idle screen: raises a screen flag, installs a prompt string, and
//     resets the fade timer. Referenced from a screen-handler table (0x80476380).
//   - mode_attract_screen_tick  (0x802c8d38) per-frame attract tick: advances
//     the fade timer, fades the menu music out, and once the music has stopped
//     either plays the fullscreen attract movie (movies/attract.thp, mode 1) or
//     shows the load screen.
//   - mode_slot_is_high         (0x802c91cc) predicate: slot index >= 0xf.
//   - mode_slot_high_group      (0x802c91dc) -1 for a high slot in mode 9, else -2.
//   - mode_slot_group_id        (0x802c9230) map a slot index to a group id.
//
// Address range: 0x802c8c6c - 0x802c9293

#include "common.h"

extern "C" {

extern int   DAT_805cc614;    // active game mode id
extern int   DAT_80611934;    // load-screen pending flag
extern int   DAT_806109c0;    // frame-rate divisor
extern float FLOAT_80612b0c;  // attract/fade timer
extern const float FLOAT_80619f08;  // 0.0 default
extern const float FLOAT_80619ee8;  // 1.0 / brightness base
extern const float FLOAT_806108e8;  // fade-in rate
extern const float FLOAT_80619f64;  // master volume (music finished)
extern const float FLOAT_80619f68;  // master volume (fading)
extern const double DOUBLE_80619f00;   // int->double bias reduction constant

// attract-movie path string "movies/attract.thp"
extern char  s_movies_attract_thp_8047636c[];

// ---- named cross-module helpers (resolve via generated_decls) ----
extern int   get_string_by_id(int id);
extern int   menu_sound_is_finished(void);
extern void  menu_sound_fade_out(void);
extern unsigned long long sound_set_master_volume(double volume);
extern void  sound_set_voice_volume_scale(unsigned int scale);
extern void  hud_screen_pop_current(void);
extern unsigned long long loadscreen_set_bounds(int x, int y, int w, int h);
// countLeadingZeros is provided as an intrinsic macro by common.h.

// ---- raw frame/screen helpers (per-file) ----
extern void FUN_802e53ec(unsigned long long p1, double p2, int p3, int p4, int p5);
extern void FUN_802e2650(int a, int b, int c, int d, int e, int f, int g, int h,
                         int p9, int p10, int p11, int p12);
extern void thp_movie_play_fullscreen(const char *path, int allowSkip);


// 0x802c8c6c - one-shot init callback for the attract/idle screen: raise flag
// bit 0x40 on the screen object, install prompt string 0xdf2, reset fade timer.
void mode_attract_screen_init(int screen)
{
    *(unsigned int *)(screen + 0x40) |= 0x40;
    *(int *)(screen + 0x200) = get_string_by_id(0xdf2);
    FLOAT_80612b0c = FLOAT_80619f08;
}

// 0x802c8d38 - per-frame attract tick. Advances the fade timer, fades the menu
// music, and once the music has stopped either plays the fullscreen attract
// movie (mode 1, then pops the screen) or displays the load screen.
void mode_attract_screen_tick(void)
{
    FLOAT_80612b0c = (float)((double)FLOAT_806108e8 *
                             (double)(float)((double)FLOAT_80619ee8 / (double)DAT_806109c0) +
                             (double)FLOAT_80612b0c);

    if (menu_sound_is_finished() == 0) {
        menu_sound_fade_out();
        sound_set_master_volume((double)FLOAT_80619f68);
        sound_set_voice_volume_scale(0xffffffff);
    } else {
        sound_set_master_volume((double)FLOAT_80619f64);
        if (DAT_805cc614 == 1) {
            thp_movie_play_fullscreen(s_movies_attract_thp_8047636c, 1);
            hud_screen_pop_current();
        } else if (DAT_80611934 == 1) {
            loadscreen_set_bounds(0x3c, 0x195, 0x188, 0xf);
            FUN_802e2650(0, 0, 0, 0, 0, 0, 0, 0, 0x188, 0xf, 0x3c, 0x195);
        }
    }
}

// 0x802c91cc - predicate: slot index >= 0xf (15).
unsigned int mode_slot_is_high(int slot)
{
    return countLeadingZeros((unsigned int)(0xf - slot)) >> 5;
}

// 0x802c91dc - slot classifier: -1 for a high slot in game mode 9, else -2.
char mode_slot_high_group(int slot)
{
    bool isHigh = false;
    if ((DAT_805cc614 == 9) && (mode_slot_is_high(slot) != 0)) {
        isHigh = true;
    }
    return (char)(-1 - (int)(!isHigh));
}

// 0x802c9230 - map a slot index to a group id: in mode 9, high slots collapse
// to group 0; otherwise -1 for negative indices, 1 for indices > 9, else 0.
int mode_slot_group_id(unsigned int slot)
{
    int group;

    if ((DAT_805cc614 == 9) && (mode_slot_high_group(slot) != '\0')) {
        group = 0;
    } else {
        group = ((int)slot >> 0x1f) + (unsigned int)(9 < slot);
    }
    return group;
}

} // extern "C"
