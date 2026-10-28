// AX studio / AXOUT frame buffer layer.
//
// The AX synthesizer keeps triple-buffered DSP-out and AUX-out mix buffers in
// two studio pools (DAT_805f3920 for the DSP mix, DAT_805f4fa0 for the AUX
// mix), each 0x1e0 (480) bytes per frame slot, plus matching ARAM staging
// buffers (base 0x0055f4a0, 0x780 bytes per frame). DAT_80613330/34/38/3c/40
// are the rotating frame indices; DAT_80613310/14 hold the per-frame DSP-out
// and AUX-out callbacks.
//
// Address: 0x803ac6a8..0x803ac70c | Module: audio/ax_studio.cpp | Date: 2026-09-15

#include "common.h"

extern int   DAT_80613310;         // DSP-out frame callback (0 if none)
extern int   DAT_80613314;         // AUX-out frame callback (0 if none)
extern int   DAT_80613330;         // current DSP-out frame index
extern int   DAT_80613334;         // previous/next DSP-out frame index
extern void* DAT_805f3920;         // DSP-out studio mix pool base
extern void* DAT_805f4fa0;         // AUX-out studio mix pool base

// ax_studio_clear_callbacks: drop the DSP-out and AUX-out frame callbacks.
void ax_studio_clear_callbacks(void) {
    DAT_80613310 = 0;
    DAT_80613314 = 0;
}

// ax_studio_get_dsp_out_buffer: return the current-frame DSP-out mix buffer,
// or NULL when no DSP-out callback is installed.
void ax_studio_get_dsp_out_buffer(void** out) {
    if (DAT_80613310 != 0) {
        *out = (void*)((char*)&DAT_805f3920 + DAT_80613330 * 0x1e0);
        return;
    }
    *out = 0;
}

// ax_studio_get_out_aram_addr: ARAM address of the current-frame DSP-out
// staging buffer (index * 0x780 + base 0x0055f4a0).
void ax_studio_get_out_aram_addr(int* out) {
    *out = DAT_80613330 * 0x780 + -0x7fa0ab60;
}

// ax_studio_get_dsp_out_buffer_prev: DSP-out mix buffer for the alternate
// (DAT_80613334) frame index.
void ax_studio_get_dsp_out_buffer_prev(void** out) {
    *out = (void*)((char*)&DAT_805f3920 + DAT_80613334 * 0x1e0);
}

// ============================================================================
// batch_1134: alternate-frame studio buffer getters, the per-frame studio
// callback pump, and the AX DSP command-list double buffer.
// Address: 0x803ac728..0x803aca54.
// ============================================================================

extern void os_dc_invalidate_range(u32 addr, u32 len);
extern void os_dc_flush_range_no_sync(u32 addr, u32 len);
extern void os_dc_flush_range(u32 addr, u32 len);

extern int   DAT_80613318;         // DSP-out callback user arg
extern int   DAT_8061331c;         // AUX-out callback user arg
extern void* DAT_80613320;         // latched current DSP-out buffer
extern void* DAT_80613324;         // latched alt DSP-out buffer
extern void* DAT_80613328;         // latched current AUX-out buffer
extern void* DAT_8061332c;         // latched alt AUX-out buffer
extern int   DAT_80613338;         // AUX-in current index
extern int   DAT_8061333c;         // AUX-in alt index
extern int   DAT_80613340;         // DSP task working frame index
extern int   DAT_80613348;         // command-list buffer index (0/1)
extern void* DAT_8061334c;         // command-list write cursor
extern int   DAT_80613350;         // command-list byte-size accumulator
extern int   DAT_80613358;         // AX output mode (2 = surround/DPL)
extern void* DAT_805f6620;         // command-list double buffer base

// ax_studio_get_out_l_aram_prev: ARAM addr of alt-frame DSP-out left buffer.
void ax_studio_get_out_l_aram_prev(int* out) {
    *out = DAT_80613334 * 0x780 + -0x7fa0c460;
}

// ax_studio_get_out_r_aram_prev: ARAM addr of alt-frame DSP-out right buffer.
void ax_studio_get_out_r_aram_prev(int* out) {
    *out = DAT_80613334 * 0x780 + -0x7fa0c1e0;
}

// ax_studio_get_out_s_aram_prev: ARAM addr of alt-frame DSP-out surround buf.
void ax_studio_get_out_s_aram_prev(int* out) {
    *out = DAT_80613334 * 0x780 + -0x7fa0ab60;
}

// ax_studio_get_aux_out_buffer: current-frame AUX-out mix buffer, or NULL when
// no AUX-out callback is installed.
void ax_studio_get_aux_out_buffer(void** out) {
    if (DAT_80613314 != 0) {
        *out = (void*)((char*)&DAT_805f4fa0 + DAT_80613330 * 0x1e0);
        return;
    }
    *out = 0;
}

// ax_studio_get_aux_out_buffer_prev: alt-frame AUX-out mix buffer.
void ax_studio_get_aux_out_buffer_prev(void** out) {
    *out = (void*)((char*)&DAT_805f4fa0 + DAT_80613334 * 0x1e0);
}

// ax_studio_get_aux_in_buffer: AUX-in mix buffer for the double-buffered
// AUX-in index DAT_80613338.
void ax_studio_get_aux_in_buffer(void** out) {
    *out = (void*)((char*)&DAT_805f4fa0 + DAT_80613338 * 0x1e0);
}

// ax_studio_get_aux_in_buffer_prev: alternate AUX-in mix buffer.
void ax_studio_get_aux_in_buffer_prev(void** out) {
    *out = (void*)((char*)&DAT_805f4fa0 + DAT_8061333c * 0x1e0);
}

// ax_studio_run_frame_callbacks: AX per-frame studio pump. Latches the mix
// buffer pointers, runs the DSP-out and AUX-out frame callbacks (with cache
// invalidate/flush around each), then rotates all frame indices.
void ax_studio_run_frame_callbacks(void) {
    int   base;
    void* dsp_buf;
    int   dsp_l;
    int   dsp_r;
    u32   dsp_s;
    void* aux_buf;
    int   aux_l;
    int   aux_r;

    DAT_80613320 = (void*)((char*)&DAT_805f3920 + DAT_80613330 * 0x1e0);
    DAT_80613324 = (void*)((char*)&DAT_805f3920 + DAT_80613334 * 0x1e0);
    DAT_80613328 = (void*)((char*)&DAT_805f4fa0 + DAT_80613330 * 0x1e0);
    DAT_8061332c = (void*)((char*)&DAT_805f4fa0 + DAT_80613334 * 0x1e0);

    if (DAT_80613310 != 0) {
        if (DAT_80613358 == 2) {
            base = DAT_80613340 * 0x780;
            dsp_buf = (void*)((char*)&DAT_805f3920 + DAT_80613340 * 0x1e0);
            dsp_l = base + -0x7fa0c460;
            dsp_r = base + -0x7fa0c1e0;
            dsp_s = (u32)(base + 0x805f54a0);
            os_dc_invalidate_range((u32)dsp_buf, 0x780);
            os_dc_invalidate_range(dsp_s, 0x280);
            ((void (*)(void**, int))DAT_80613310)(&dsp_buf, DAT_80613318);
            os_dc_flush_range_no_sync((u32)dsp_buf, 0x780);
            os_dc_flush_range_no_sync(dsp_s, 0x280);
            (void)dsp_l; (void)dsp_r;
        } else {
            dsp_buf = (void*)((char*)&DAT_805f3920 + DAT_80613340 * 0x1e0);
            dsp_l = DAT_80613340 * 0x780 + -0x7fa0c460;
            dsp_r = DAT_80613340 * 0x780 + -0x7fa0c1e0;
            os_dc_invalidate_range((u32)dsp_buf, 0x780);
            ((void (*)(void**, int))DAT_80613310)(&dsp_buf, DAT_80613318);
            os_dc_flush_range_no_sync((u32)dsp_buf, 0x780);
            (void)dsp_l; (void)dsp_r;
        }
    }
    if ((DAT_80613314 != 0) && (DAT_80613358 != 2)) {
        aux_buf = (void*)((char*)&DAT_805f4fa0 + DAT_80613340 * 0x1e0);
        aux_l = DAT_80613340 * 0x780 + -0x7fa0ade0;
        aux_r = DAT_80613340 * 0x780 + -0x7fa0ab60;
        os_dc_invalidate_range((u32)aux_buf, 0x780);
        ((void (*)(void**, int))DAT_80613314)(&aux_buf, DAT_8061331c);
        os_dc_flush_range_no_sync((u32)aux_buf, 0x780);
        (void)aux_l; (void)aux_r;
    }
    DAT_80613330 = (DAT_80613330 + 1) % 3;
    DAT_80613334 = (DAT_80613334 + 1) % 3;
    DAT_80613338 = (DAT_80613338 + 1) & 1;
    DAT_8061333c = (DAT_8061333c + 1) & 1;
    DAT_80613340 = (DAT_80613340 + 1) % 3;
}

// ax_cmd_list_get_size: current DSP command-list byte-size accumulator.
int ax_cmd_list_get_size(void) {
    return DAT_80613350;
}

// ax_cmd_list_swap_buffer: return the completed 0x300-byte command buffer and
// point the write cursor at the other of the two command buffers.
void* ax_cmd_list_swap_buffer(void) {
    int old = DAT_80613348 * 0x300;
    DAT_80613348 = (DAT_80613348 + 1) & 1;
    DAT_8061334c = (void*)((char*)&DAT_805f6620 + DAT_80613348 * 0x300);
    return (void*)((char*)&DAT_805f6620 + old);
}
