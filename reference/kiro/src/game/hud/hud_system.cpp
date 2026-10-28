// =============================================================================
// hud_system.cpp - HUD/viewport system initialization and element management
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 062 | Date: 2026-08-05
// =============================================================================
//
// The HUD system manages:
// - 4 viewports for split-screen (stride 0x1E0 each, base DAT_8051a814)
// - A pool of 20 HUD element slots (stride 0x238 each, base DAT_8051af94)
// - A dynamic element pool (stride 0x10C, capacity varies by game mode)
// - Menu sound playback state
//
// HUD elements are linked to parent entities via a linked list at entity+0x168.
// Each element has a viewport mask, position/scale, animation slots, and sort depth.
//
// =============================================================================

#include "common.h"
#include "sound.h"
#include "memory.h"

// Forward declarations
extern void FUN_803826b4(void);
extern void FUN_803826e0(void);
extern void FUN_803826ec(void);
extern void FUN_80382700(void);
extern void FUN_8038272c(void);
extern void FUN_80382738(void);
extern void FUN_803826f0(void);
extern void FUN_8003d248(int element);
extern void FUN_8003d3fc(int viewport_idx, int p2, int p3);
extern void FUN_8003d4b8(float x, float y, float w, float h);
extern void FUN_8003d4e0(float x, float y, float w, float h);
extern void FUN_8003d548(u32* viewport_data);
extern void FUN_8003e124(float* rect);
extern void FUN_8004cdf0(void* pool, int mode, ...);
extern void FUN_8004d81c(float x, float y, float w, float h);
extern void FUN_80045f08(int element, int* params);
extern void FUN_80046908(int parent, u32 data);
extern void FUN_800469ac(void);
extern void FUN_80047694(int element);
extern void FUN_80132348(float value, u32 property);
extern void FUN_8017bc4c(int w, int h, int* handle);

// State
static int   g_menu_sound_handle;      // DAT_806101d0
static int   g_menu_sound_fading;      // DAT_806119bc
static int   g_current_viewport;       // DAT_806119ec
static int   g_hud_element_pool_ptr;   // DAT_806119c4
static int   g_hud_element_pool_size;  // DAT_806119c8
static int   g_hud_secondary_pool;     // DAT_806119f0
static int   g_hud_entity_context;     // DAT_806119e4
static int   g_hud_active_count;       // DAT_806119e0

// Per-viewport data (4 viewports, stride 0x1E0)
extern u8    g_viewport_data[];        // DAT_8051a814

// HUD element slots (20 slots, stride 0x238)
extern int   g_hud_element_pool[];     // DAT_8051af94

// Display dimensions
extern int   DAT_806109c4;  // screen width
extern int   DAT_806109c8;  // screen height

// Constants
extern float FLOAT_80613bb0;  // 0.0f
extern float FLOAT_80613bb4;  // viewport scale X
extern float FLOAT_80613bb8;  // viewport scale Y
extern float FLOAT_80613bc8;  // 1.0f (fade duration / default scale)
extern float FLOAT_80613bcc;  // fade target volume

// Game mode
extern int   DAT_80612614;    // game mode ID

// -----------------------------------------------------------------------------
// Address: 0x8003c8f8 | Name: hud_element_set_anim_value
// -----------------------------------------------------------------------------
// Sets an animation value on a HUD element. Reads the element's scale
// (offset 0xF0) and a property from its table (offset 0x20 + param_2*4 + 0x3C).
// Passes to FUN_80132348 for interpolation application.
// -----------------------------------------------------------------------------
void hud_element_set_anim_value(int element, int property_index) {
    float scale = *(float*)(element + 0xF0);
    u32 property = *(u32*)(*(int*)(element + 0x20) + property_index * 4 + 0x3C);
    FUN_80132348(scale, property);
}

// -----------------------------------------------------------------------------
// Address: 0x8003c92c | Name: hud_init_viewports
// -----------------------------------------------------------------------------
// Initializes all 4 viewports for split-screen rendering. Per viewport:
// sets bounds to full screen, configures camera region, applies scale constants,
// initializes viewport rect and data structures.
// Viewport data: DAT_8051a814 + viewport * 0x1E0.
// -----------------------------------------------------------------------------
void hud_init_viewports(void) {
    FUN_803826f0();  // enter viewport section

    float scale_x = FLOAT_80613bb4;
    float scale_y = FLOAT_80613bb8;
    float zero = FLOAT_80613bb0;
    float screen_w = (float)DAT_806109c4;
    float screen_h = (float)DAT_806109c8;

    float viewport_rect[4] = { zero, zero, screen_w, screen_h };

    for (int i = 0; i < 4; i++) {
        g_current_viewport = i;

        // Setup viewport bounds
        FUN_8003d3fc(i, 0, 0);
        FUN_8003d4b8(zero, zero, screen_w, screen_h);
        FUN_8003d4e0(zero, zero, screen_w, screen_h);

        // Setup camera region from per-viewport data
        int vp_offset = g_current_viewport * 0x1E0;
        float cam_x = *(float*)(&g_viewport_data[vp_offset + 0x68]);  // DAT_8051a87c
        float cam_y = *(float*)(&g_viewport_data[vp_offset + 0x6C]);  // DAT_8051a880
        float cam_w = *(float*)(&g_viewport_data[vp_offset + 0x70]);  // DAT_8051a884
        float cam_h = *(float*)(&g_viewport_data[vp_offset + 0x74]);  // DAT_8051a888
        FUN_8004d81c(cam_x, cam_y, cam_w, cam_h);

        // Set viewport scale constants
        *(float*)(&g_viewport_data[vp_offset + 0xE8]) = scale_x;  // DAT_8051a8fc
        *(float*)(&g_viewport_data[vp_offset + 0x18]) = scale_y;  // DAT_8051a82c
        *(float*)(&g_viewport_data[vp_offset + 0x1C]) = scale_y;  // DAT_8051a830

        FUN_8003e124(viewport_rect);
        FUN_8003d548((u32*)&g_viewport_data[g_current_viewport * 0x1E0]);
    }

    g_current_viewport = 0;

    // Restore camera to viewport 0's region
    FUN_8004d81c(
        *(float*)(&g_viewport_data[0x68]),
        *(float*)(&g_viewport_data[0x6C]),
        *(float*)(&g_viewport_data[0x70]),
        *(float*)(&g_viewport_data[0x74])
    );

    FUN_8038273c();  // leave viewport section
}

// -----------------------------------------------------------------------------
// Address: 0x8003caf4 | Name: menu_sound_fade_out
// -----------------------------------------------------------------------------
// Fades out the current menu sound effect. Uses guard flag to prevent
// double-fade. Duration and target volume from FLOAT constants.
// -----------------------------------------------------------------------------
void menu_sound_fade_out(void) {
    if (g_menu_sound_fading == 0) {
        int handles[2];
        handles[0] = g_menu_sound_handle;

        sound_register_fade_out(FLOAT_80613bc8, FLOAT_80613bcc, handles, NULL);
        g_menu_sound_fading = 1;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003cb40 | Name: menu_sound_is_finished
// -----------------------------------------------------------------------------
// Returns 1 if the menu sound has finished playing (invalid handle or
// sound_is_playing returns 0). Returns 0 if still active.
// -----------------------------------------------------------------------------
int menu_sound_is_finished(void) {
    if (g_menu_sound_handle < 0) {
        return 1;
    }

    if (sound_is_playing(g_menu_sound_handle) == 0) {
        return 1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003cb88 | Name: menu_sound_play
// -----------------------------------------------------------------------------
// Plays a menu sound effect by ID. On success, fades out any previous sound
// and stores the new handle.
// -----------------------------------------------------------------------------
void menu_sound_play(int sound_id) {
    int params[13];

    sound_init_play_params((int)params);
    params[0] = sound_id;

    u32 handle = sound_play_request(params);
    if ((int)handle >= 0) {
        menu_sound_fade_out();
        g_menu_sound_fading = 0;
        g_menu_sound_handle = handle;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003cbdc | Name: hud_system_init
// -----------------------------------------------------------------------------
// Full HUD/viewport system initialization. Clears all state, resets 4 viewport
// data blocks (stride 0x1E0), initializes viewport cameras, configures
// split-screen button masks and viewport handles.
// -----------------------------------------------------------------------------
void hud_system_init(void) {
    FUN_803826e0();  // enter HUD critical section

    // Clear all state variables
    DAT_806119dc = 0;
    DAT_806119d8 = 0;
    DAT_806119d4 = 0;
    DAT_806119d0 = 0;
    g_menu_sound_handle = -1;
    g_menu_sound_fading = 0;

    // Clear main HUD buffer (0x2C60 bytes at DAT_8051af94 area)
    memset_wrapper((void*)0x8051AF94, 0, 0x2C60);

    // Clear element pool
    memset_wrapper((void*)g_hud_element_pool_ptr, 0, g_hud_element_pool_size * 0x10C);

    // Clear secondary buffer (30000 bytes)
    memset_wrapper((void*)0x8051DBF4, 0, 30000);

    // Reset per-viewport state
    g_hud_entity_context = 0;
    g_hud_active_count = 0;
    DAT_806119cc = 0;
    DAT_806119e8 = -1;

    for (int i = 0; i < 4; i++) {
        g_current_viewport = i;
        // Clear viewport active flag
        // DAT_80525124[i] = -1
        // Clear viewport data block (0x1E0 bytes)
        memset_wrapper((void*)&g_viewport_data[i * 0x1E0], 0, 0x1E0);
    }

    // Initialize viewport cameras and bounds
    hud_init_viewports();

    // Configure split-screen button masks
    // DAT_80525134 = 4 (player 1 mask)
    // DAT_80525138 = 8
    // DAT_8052513c = 1
    // DAT_80525140 = 2
    // DAT_80525154 = 0x8100 (start + special)
    // DAT_80525158 = 0x8000
    // DAT_8052515c = 0x200
    // DAT_80525160 = 0x800
    // DAT_80525164 = 0x400

    // Configure per-viewport resolution from table
    // DAT_80525144-80525150 = DAT_803e9948/9954/9960/996c

    // Initialize viewport render handles
    DAT_806119f4 = -1;
    for (int i = 4; i >= 0; i--) {
        // DAT_8051a800[i] = -1
    }
    for (int i = 4; i >= 0; i--) {
        FUN_8017bc4c(DAT_806109c4, DAT_806109c8, /* &viewport_handles[i] */);
    }

    FUN_8038272c();  // leave HUD critical section
}

// -----------------------------------------------------------------------------
// Address: 0x8003cd80 | Name: hud_alloc_element_pool
// -----------------------------------------------------------------------------
// Allocates the HUD element pool based on game mode:
//   Mode 0x66 (full game): 920 elements
//   Mode 0x65 (medium):    650 elements
//   Default:               200 elements
// Each element is 0x10C bytes. Also allocates secondary pool (stride 0x18).
// -----------------------------------------------------------------------------
void hud_alloc_element_pool(int callback) {
    if (DAT_80612614 == 0x66) {
        g_hud_element_pool_size = 0x398;  // 920
    } else if (DAT_80612614 == 0x65) {
        g_hud_element_pool_size = 0x28A;  // 650
    } else {
        g_hud_element_pool_size = 200;
    }

    DAT_806119fc = callback;

    // Allocate main pool: pool_size * 0x10C bytes, aligned to 16
    int alloc_size = (g_hud_element_pool_size * 0x10C + 0x0F) & ~0x0F;
    g_hud_element_pool_ptr = (int)mem_alloc(alloc_size, 1);

    // Allocate secondary pool: pool_size * 0x18 bytes, aligned to 16
    int secondary_size = (g_hud_element_pool_size * 0x18 + 0x0F) & ~0x0F;
    g_hud_secondary_pool = (int)mem_alloc(secondary_size, 1);

    FUN_8004cdf0((void*)g_hud_secondary_pool, 1 /* mode */);
}

// -----------------------------------------------------------------------------
// Address: 0x8003ce08 | Name: hud_element_alloc_slot
// -----------------------------------------------------------------------------
// Allocates a HUD element from a fixed pool of 20 slots (stride 0x238 bytes,
// starting at DAT_8051af94). Finds the first slot where the first int == 0,
// zeroes it, and returns the pointer. Returns NULL if all slots are full.
// -----------------------------------------------------------------------------
int* hud_element_alloc_slot(void) {
    for (int i = 0; i < 20; i++) {
        int* slot = (int*)((char*)g_hud_element_pool + i * 0x238);
        if (*slot == 0) {
            memset_wrapper((int)slot, 0, 0x238);
            return slot;
        }
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Address: 0x8003ce74 | Name: hud_element_create
// -----------------------------------------------------------------------------
// Creates a new HUD element. Allocates from pool, links to parent entity
// (via linked list at parent+0x168), copies initial data (16 bytes),
// initializes state. If entity cannot support more elements, uses fallback.
// -----------------------------------------------------------------------------
void hud_element_create(u32 type_a, u32 type_b, int script_id) {
    u64 result = FUN_803826ec();  // get parent entity + data
    int parent = (int)(result >> 32);
    u32 data = (u32)result;

    if (g_hud_entity_context == 0 || *(int*)(g_hud_entity_context + 4) != 0) {
        // Normal path: allocate and link element
        int* element = hud_element_alloc_slot();

        element[0] = script_id;
        element[0x0B] = parent;          // parent entity
        element[0x0E] = g_current_viewport;
        element[0x6B] = 0;
        *(float*)(element + 0x7E) = FLOAT_80613bc8;  // default scale

        // Link to parent entity's HUD list
        if (element[0x0B] != 0) {
            element[0x81] = *(int*)(parent + 0x204);
            element[0x82] = *(int*)(parent + 0x208);

            int* list_tail = (int*)(parent + 0x168);
            if (*list_tail == 0) {
                *list_tail = (int)element;
            } else {
                // Walk to end of linked list (next at +0x30)
                int* node = (int*)*list_tail;
                while (*(int*)((char*)node + 0x30) != 0) {
                    node = (int*)*(int*)((char*)node + 0x30);
                }
                *(int*)((char*)node + 0x30) = (int)element;
            }
        }

        // Copy initial data if provided
        if (data != 0) {
            element[1] = data;
            leaf_31e8((u32)(element + 2), data, 0x10);  // memcpy 16 bytes
        }

        hud_element_init_state((int)element, 1);
        FUN_8003d248((int)element);
    } else {
        // Fallback path
        int result2 = FUN_80046908(parent, data);
        if (result2 != 0) {
            FUN_800469ac();
        }
    }

    FUN_80382738();
}

// -----------------------------------------------------------------------------
// Address: 0x8003cf8c | Name: hud_element_init_state
// -----------------------------------------------------------------------------
// Initializes HUD element fields after allocation. Sets viewport mask,
// zeros position/scale/rotation, configures flags and sort depth.
// Optionally clears animation slots (4 entries at +0x1C8).
// -----------------------------------------------------------------------------
void hud_element_init_state(int element, int full_reset) {
    float zero = FLOAT_80613bb0;
    float one = FLOAT_80613bc8;

    // Viewport mask: which viewports render this element
    *(int*)(element + 0x3C) = 1 << g_current_viewport;

    // Zero out transform values
    *(float*)(element + 0x58) = zero;   // pos X
    *(float*)(element + 0x68) = zero;   // pos Y
    *(float*)(element + 0x6C) = zero;   // pos Z
    *(float*)(element + 0x64) = zero;   // rotation
    *(float*)(element + 0x80) = zero;   // scale X
    *(float*)(element + 0x84) = zero;   // scale Y

    // Flags and state
    *(int*)(element + 0x40) = 0x02000002;  // render flags
    *(int*)(element + 0x44) = 0;           // state flags
    *(int*)(element + 0x184) = 0;          // user data

    FUN_80045f08(element, NULL);

    *(int*)(element + 0x1B8) = 0;
    *(int*)(element + 0x1BC) = 0;
    *(int*)(element + 0x1C0) = 0;
    *(int*)(element + 0x1C4) = 0;
    *(int*)(element + 0x190) = -1;     // sort group
    *(int*)(element + 0x48) = -6;      // sort depth (negative = front)
    *(float*)(element + 0x20C) = one;  // alpha/opacity

    if (full_reset) {
        // Clear animation slots
        *(int*)(element + 0x214) = 0;
        *(int*)(element + 0x1E4) = -1;  // current anim = none
        for (int i = 0; i < 4; i++) {
            *(int*)(element + 0x1C8 + i * 4) = 0;
        }
    }

    FUN_80047694(element);
}


// =============================================================================
// HUD Element Stack / Dispatch System (Batch 063)
// =============================================================================
//
// The HUD uses a processing stack (max depth 5) to handle nested element
// updates. Each stack frame is 0x80 bytes at DAT_80525168 + depth*0x80.
// Stack frame layout:
//   [0] = element pointer
//   [1] = active flag
//   [2] = state
//   [0x1B] = pool base index (for element pool relative addressing)
//
// DAT_806119e8 = current stack depth (-1 = empty)
// DAT_806119e4 = pointer to current stack frame
// DAT_806119e0 = current element being processed
//
// =============================================================================

extern void FUN_8004125c(void);
extern void FUN_80040e50(int element);
extern void FUN_80040f14(int a, int b, int c, int d);

// Stack frames (5 frames, 0x80 bytes each)
extern u8 g_hud_stack_frames[];  // DAT_80525168

// -----------------------------------------------------------------------------
// Address: 0x8003d06c | Name: hud_context_is_active
// -----------------------------------------------------------------------------
// Returns the current HUD context's active flag. Non-zero means context
// is valid and accepting element operations.
// -----------------------------------------------------------------------------
int hud_context_is_active(void) {
    return *(int*)((char*)DAT_806119e4 + 4);  // frame[1] = active flag
}

// -----------------------------------------------------------------------------
// Address: 0x8003d078 | Name: hud_element_push
// -----------------------------------------------------------------------------
// Pushes a HUD element onto the processing stack (max depth 5).
// Sets up the viewport camera for the current viewport, allocates context,
// links element. Calculates pool base from previous frame for relative indexing.
// -----------------------------------------------------------------------------
void hud_element_push(int element) {
    if (DAT_806119e8 + 1 >= 5) {
        return;  // stack full
    }

    DAT_806119e8++;

    // Set camera to current viewport's region
    int vp = g_current_viewport;
    FUN_8004d81c(
        *(float*)(&g_viewport_data[vp * 0x1E0 + 0x68]),
        *(float*)(&g_viewport_data[vp * 0x1E0 + 0x6C]),
        *(float*)(&g_viewport_data[vp * 0x1E0 + 0x70]),
        *(float*)(&g_viewport_data[vp * 0x1E0 + 0x74])
    );

    // Initialize stack frame
    int* frame = (int*)(&g_hud_stack_frames[DAT_806119e8 * 0x80]);
    DAT_806119e4 = frame;
    memset_wrapper((int)frame, 0, 0x80);

    frame[0] = element;   // element pointer
    frame[2] = 0;         // state = 0
    frame[1] = 1;         // active = true
    DAT_806119e0 = element;

    // Calculate pool base index from previous frame
    if (DAT_806119e8 > 0) {
        int prev_offset = (DAT_806119e8 - 1) * 0x80;
        int* prev_frame = (int*)(&g_hud_stack_frames[prev_offset]);
        int combined = prev_frame[0x1B] + prev_frame[0x1C];  // offsets 0x6C + 0x70
        frame[0x1B] = combined - (combined / g_hud_element_pool_size) * g_hud_element_pool_size;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003d174 | Name: hud_element_pop
// -----------------------------------------------------------------------------
// Pops a HUD element from the processing stack. Finalizes the element
// (visibility check, layout update, render submission), then restores
// the previous stack frame or NULLs if empty.
// -----------------------------------------------------------------------------
void hud_element_pop(void) {
    int element = DAT_806119e0;

    FUN_8004125c();  // finalize current element

    // Check if context is valid and active
    if (*(int*)((char*)DAT_806119e4 + 8) == 0 &&
        *(int*)((char*)DAT_806119e4 + 4) != 0) {
        // Check viewport visibility
        int viewport_idx = *(int*)(element + 0x38);
        int vp_active = *(int*)(&g_viewport_data[viewport_idx * 0x1E0 + 0x10]);

        if (vp_active == 0 || (*(u32*)(element + 0x40) & 0x40000) != 0) {
            // Update layout with cached params
            FUN_80045f08(element, *(int**)(element + 0x174));
        }
    }

    // Submit for rendering if context is active
    if (*(int*)((char*)DAT_806119e4 + 4) != 0) {
        FUN_80040e50(element);
    }

    // Pop stack
    DAT_806119e8--;
    if (DAT_806119e8 < 0) {
        DAT_806119e4 = NULL;
        DAT_806119e0 = 0;
    } else {
        DAT_806119e4 = (int*)(&g_hud_stack_frames[DAT_806119e8 * 0x80]);
        DAT_806119e0 = *DAT_806119e4;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003d248 | Name: hud_element_dispatch
// -----------------------------------------------------------------------------
// Dispatches a HUD element's per-frame update. Pushes element onto stack,
// calls its registered callback (function pointer at element+8), or if NULL
// and element+0x2C is 0, calls default update. Pops after completion.
// -----------------------------------------------------------------------------
void hud_element_dispatch(int element) {
    hud_element_push(element);

    void (*callback)(int) = *(void (**)(int))(element + 8);

    if (callback != NULL) {
        callback(element);
    } else {
        if (*(int*)(element + 0x2C) == 0) {
            FUN_80040f14(0, 0, 0, 0);
        }
    }

    hud_element_pop();
}

// -----------------------------------------------------------------------------
// Address: 0x8003d2b8 | Name: hud_viewport_create_root
// -----------------------------------------------------------------------------
// Creates a root HUD element for the current viewport. Wrapper for
// hud_viewport_create_element with type=0.
// -----------------------------------------------------------------------------
void hud_viewport_create_root(int callback, int param_b, int param_c) {
    hud_viewport_create_element(0, callback, param_b, param_c);
}

// -----------------------------------------------------------------------------
// Address: 0x8003d2f0 | Name: hud_viewport_create_element
// -----------------------------------------------------------------------------
// Creates a HUD element bound to the current viewport. Allocates via
// hud_element_create, stores pointer in viewport data, sets viewport
// index on element, and stores callback parameters.
// -----------------------------------------------------------------------------
void hud_viewport_create_element(int type, int callback, int param_b, int param_c) {
    u64 result = FUN_803826f0();  // get viewport context
    int viewport_idx = (int)(result >> 32);

    int* element = (int*)hud_element_create(0, (int)result, 1);

    // Link element to viewport
    *(int*)(element + 0x38 / 4) = viewport_idx;
    *(int*)(&g_viewport_data[viewport_idx * 0x1E0 + 0x0C]) = (int)element;  // DAT_8051a820

    // Store parameters in viewport data
    *(int*)(&g_viewport_data[viewport_idx * 0x1E0 + 0x04]) = param_b;  // DAT_8051a818
    *(int*)(&g_viewport_data[viewport_idx * 0x1E0 + 0x08]) = param_c;  // DAT_8051a81c

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003d354 | Name: hud_element_advance_time
// -----------------------------------------------------------------------------
// Advances a HUD element's animation time by delta. Adds to either the
// element's rotation (offset 0x64) or X position (offset 0x58) depending
// on flag 0x40 in the animation state (param_3+0x28). Always accumulates
// into param_3+0x70 (total elapsed time).
// -----------------------------------------------------------------------------
void hud_element_advance_time(float delta, int element, int anim_state) {
    if ((*(u32*)(anim_state + 0x28) & 0x40) == 0) {
        *(float*)(element + 0x64) += delta;  // rotation
    } else {
        *(float*)(element + 0x58) += delta;  // X position
    }
    *(float*)(anim_state + 0x70) += delta;   // accumulated time
}

// -----------------------------------------------------------------------------
// Address: 0x8003d38c | Name: hud_element_get_dimension
// -----------------------------------------------------------------------------
// Gets a dimension value from a HUD element's layout descriptor.
// Flag 0x100 = width (layout+0x4C), 0x200 = X pos (layout+0x44),
// default = height (layout+0x48). Layout pointer at element+0x1C.
// -----------------------------------------------------------------------------
int hud_element_get_dimension(int element, u32 flags) {
    int* layout = *(int**)(element + 0x1C);

    if (flags & 0x100) {
        return *(int*)((char*)layout + 0x4C);  // width
    }
    if (flags & 0x200) {
        return *(int*)((char*)layout + 0x44);  // X position
    }
    return *(int*)((char*)layout + 0x48);      // height (default)
}

// -----------------------------------------------------------------------------
// Address: 0x8003d3c0 | Name: hud_element_set_user_data
// -----------------------------------------------------------------------------
// Sets a user data value at element offset 0x1D8. Used for attaching
// custom callback context or auxiliary data to an element.
// -----------------------------------------------------------------------------
void hud_element_set_user_data(int element, int value) {
    *(int*)(element + 0x1D8) = value;
}

// -----------------------------------------------------------------------------
// Address: 0x8003d3c8 | Name: hud_element_clear_dirty_flag
// -----------------------------------------------------------------------------
// Clears bit 0x02000000 from element flags (offset 0x40). This marks the
// element as 'clean' after a layout/render update pass.
// -----------------------------------------------------------------------------
void hud_element_clear_dirty_flag(int element) {
    *(u32*)(element + 0x40) &= ~0x02000000;
}
