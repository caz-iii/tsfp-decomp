// Address: 0x8000c024 | Name: scene_transition_begin | Date: 2026-07-23
// Address: 0x8000c064 | Name: scene_transition_update | Date: 2026-07-23
// Description: Scene/level transition state machine.
// DAT_806116a4 = transition state (0=none, 1-3=loading, 4-5=clamped to 3)
// DAT_806116a8, DAT_806116a0 = transition parameters
// DAT_8061169c = saved state pointer
// DAT_806116ac = transition progress counter
// DAT_806116b4 = transition target state
// State 3: copies 0x7C bytes from DAT_805cc61c (level descriptor?) to state buffer

#include "common.h"

extern s32 DAT_806116a4;
extern s32 DAT_806116a8;
extern s32 DAT_806116a0;
extern s32 DAT_8061169c;
extern s32 DAT_806116ac;
extern s32 DAT_806116b4;
