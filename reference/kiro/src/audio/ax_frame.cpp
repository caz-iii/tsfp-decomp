// AX frame driver / DSP-task scheduler.
//
// This is the top of the AX pipeline: it registers the AX DSP task, builds a
// per-frame DSP command list, dispatches it to the DSP, and drives the frame
// cadence off the AI DMA-completion interrupt. The command list is emitted as
// a stream of 16-bit words into a 0x300-byte double buffer (DAT_805f6620),
// with DAT_80613350 accumulating an estimated DSP cycle cost.
//
// Address: 0x803aca90..0x803ad8d8 | Module: audio/ax_frame.cpp | Date: 2026-09-15

#include "common.h"

// ---- OS / cache / thread helpers ----------------------------------------
extern unsigned long long os_read_time_base(void);
extern void os_dc_flush_range(u32 addr, u32 len);
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern int  os_init_thread_queue(u32* q);
extern void os_wakeup_thread(int* q);
extern void os_sleep_thread(int* q);

// ---- AX studio buffer getters (audio/ax_studio.cpp) ----------------------
extern void  ax_studio_get_dsp_out_buffer(void** out);
extern void  ax_studio_get_dsp_out_buffer_prev(void** out);
extern void  ax_studio_get_out_aram_addr(int* out);
extern void  ax_studio_get_out_l_aram_prev(int* out);
extern void  ax_studio_get_out_r_aram_prev(int* out);
extern void  ax_studio_get_out_s_aram_prev(int* out);
extern void  ax_studio_get_aux_out_buffer(void** out);
extern void  ax_studio_get_aux_out_buffer_prev(void** out);
extern void  ax_studio_get_aux_in_buffer(void** out);
extern void  ax_studio_get_aux_in_buffer_prev(void** out);
extern void  ax_studio_run_frame_callbacks(void);
extern void* ax_cmd_list_swap_buffer(void);
extern void  ax_free_stopped_voices(void);

// ---- AI stream (audio/ai_stream.cpp) -------------------------------------
extern void ai_set_stream_sample_rate(void* buf, int rate);
extern void ai_enable_stream_sample_count(void);
extern void ai_disable_stream_sample_count(void);
extern int  pad_set_analog_map_callback(void* cb);

// ---- AX synth internals (defined elsewhere in the AX module) -------------
extern void* FUN_803ad974(void);        // synth param block (SPB) address
extern int   FUN_803ae808(void);        // voice mix-buffer table address
extern void  FUN_803ae590(int arg);     // per-frame VPB update pass
extern void  FUN_803ad980(void);        // rebuild voice state
extern void  FUN_803b113c(u32 mail);    // DSP mailbox write
extern u16   FUN_803b1104(void);        // DSP mailbox status
extern void  FUN_803b12cc(int* task);   // ping DSP task
extern int   FUN_803b1214(void);        // DSP scheduler busy?
extern void  FUN_803b1150(u64 q, u64, u64, u64, u64, u64, u64, u64); // start task
extern void  FUN_803b121c(u32* task);   // add DSP task
extern void  FUN_803b128c(int task);    // cancel DSP task
extern int   FUN_803ade3c(void);        // active voice count
extern void* FUN_803aef4c(void);        // profiling scratch destination

// ---- command-list / studio state -----------------------------------------
extern u16*  DAT_8061334c;              // command-list write cursor (16-bit)
extern int   DAT_80613348;              // command buffer index
extern int   DAT_80613350;              // command-list cycle-cost accumulator
extern int   DAT_80613354;              // Dolby-surround cmd enable
extern int   DAT_80613358;              // output mode (0 stereo, 1, 2 surround)
extern void* DAT_805f6620;              // command-list double buffer base

// ---- frame driver state ---------------------------------------------------
extern int   DAT_80613360;              // AI DMA buffer index (sync path)
extern int   DAT_80613364;              // AI DMA buffer index (free-run path)
extern int   DAT_80613368;              // frame state machine
extern u32   DAT_80613370;              // last frame time-base hi
extern u32   DAT_80613374;              // last frame time-base lo
extern int   DAT_8061337c;              // DSP task ready flag
extern int   DAT_80613380;              // DSP task pending flag
extern int   DAT_80613388;              // DSP task thread queue
extern int   DAT_80613390;              // free-run mode flag
extern void (*DAT_80613378)(void);      // optional user frame callback

// ---- DSP task descriptor + profiling block --------------------------------
extern void* DAT_805fb620;              // DSP task descriptor base
extern void* DAT_805fb624;
extern void* DAT_805fb62c;
extern u32   DAT_805fb630;
extern int   DAT_805fb634;
extern void* DAT_805fb638;
extern int   DAT_805fb63c;
extern int   DAT_805fb640;
extern short DAT_805fb644;
extern short DAT_805fb646;
extern void* DAT_805fb648;
extern void* DAT_805fb64c;
extern void* DAT_805fb650;
extern int   DAT_805fb654;
extern void* DAT_8049c620;              // DSP task image
extern u32   DAT_80611490;              // DSP task image length
extern void* DAT_805f7620;              // DSP task input buffer
extern int   LAB_803ad3dc;              // DSP task init entry
extern int   DAT_80613384;              // frame flag

// ---- AI stream buffers / SPB / profiling -----------------------------------
extern int   DAT_805f6c20;              // AI DMA stream buffer array base
extern int   DAT_805f73a0;              // studio parameter blocks base
extern int   DAT_805f7120;              // free-run run arg block
extern int   DAT_805f6ea0;              // sync run arg block
extern u8    DAT_805fb670;              // profiling timestamp block base
extern u8    DAT_805fb678;              // profiling timestamp (frame-callbacks start)
extern u32   DAT_805fb680, DAT_805fb684, DAT_805fb688, DAT_805fb68c;
extern u32   DAT_805fb690, DAT_805fb694, DAT_805fb698, DAT_805fb69c;
extern int   DAT_805fb6a0;

extern void ax_ai_dma_callback(void);
extern void ax_sync_frame(int arg);
extern void ax_dsp_task_setup(u64, u64, u64, u64, u64, u64, u64, u64);
extern void ax_dsp_task_done_callback(void);
extern void ax_dsp_task_wakeup(void);
extern void ax_cmd_list_build(void* spb, void* run_arg);

// -- helper: emit one 16-bit word into the command list --
static inline void ax_cmd_w(u16 v) { *DAT_8061334c = v; DAT_8061334c = DAT_8061334c + 1; }
static inline void ax_cmd_w32(u32 v) { ax_cmd_w((u16)(v >> 0x10)); ax_cmd_w((u16)v); }

// ax_cmd_list_reset: reset the DSP command-list write state.
void ax_cmd_list_reset(void) {
    DAT_80613358 = 0;
    DAT_80613348 = 0;
    DAT_8061334c = (u16*)&DAT_805f6620;
    DAT_80613354 = 1;
}

// ax_frame_stub_nop: empty frame-driver hook.
void ax_frame_stub_nop(void) {
}

// ax_cmd_list_build: emit the per-frame AX DSP command list.
void ax_cmd_list_build(void* spb, void* run_arg) {
    u16* buf_start = DAT_8061334c;
    void* p;
    int   a;
    u16   run_hi = (u16)((u32)run_arg >> 0x10);
    u16   run_lo = (u16)(u32)run_arg;

    DAT_80613350 = 0x1a9;
    p = FUN_803ad974();                          // cmd 0: studio SPB address
    ax_cmd_w(0);
    ax_cmd_w32((u32)p);
    DAT_80613350 = DAT_80613350 + 0x2e44;

    // sample-rate command varies by output mode
    if (DAT_80613358 == 1) {
        ax_cmd_w(0x11); ax_cmd_w((u16)((u32)spb >> 0x10)); ax_cmd_w((u16)(u32)spb);
        DAT_80613350 = DAT_80613350 + 0x5e6;
    } else if ((DAT_80613358 < 1) && (-1 < DAT_80613358)) {
        ax_cmd_w(7);    ax_cmd_w((u16)((u32)spb >> 0x10)); ax_cmd_w((u16)(u32)spb);
        DAT_80613350 = DAT_80613350 + 0x546;
    }

    a = FUN_803ae808();                          // cmd 2/3: mixer buffer table
    ax_cmd_w(2);
    ax_cmd_w32((u32)a);
    ax_cmd_w(3);

    if (DAT_80613358 == 2) {                      // surround / Dolby Pro Logic
        ax_studio_get_dsp_out_buffer(&p);
        if (p != 0) {
            ax_cmd_w(0x13); ax_cmd_w32((u32)p);
            ax_studio_get_out_aram_addr((int*)&p);       ax_cmd_w32((u32)p);
            ax_studio_get_dsp_out_buffer_prev(&p);       ax_cmd_w32((u32)p);
            ax_studio_get_out_l_aram_prev((int*)&p);     ax_cmd_w32((u32)p);
            ax_studio_get_out_r_aram_prev((int*)&p);     ax_cmd_w32((u32)p);
            ax_studio_get_out_s_aram_prev((int*)&p);     ax_cmd_w32((u32)p);
            DAT_80613350 = DAT_80613350 + 0xded;
        }
        ax_cmd_w(0x10);
        ax_studio_get_aux_in_buffer(&p);             ax_cmd_w32((u32)p);
        ax_studio_get_aux_in_buffer_prev(&p);        ax_cmd_w32((u32)p);
        DAT_80613350 = DAT_80613350 + 0xded;
    } else {                                      // stereo
        ax_studio_get_dsp_out_buffer(&p);
        if (p != 0) {
            ax_cmd_w(4); ax_cmd_w32((u32)p);
            ax_studio_get_dsp_out_buffer_prev(&p);   ax_cmd_w32((u32)p);
            DAT_80613350 = DAT_80613350 + 0xded;
        }
        ax_studio_get_aux_out_buffer(&p);
        if (p != 0) {
            ax_cmd_w(5);
            DAT_80613350 = DAT_80613350 + 0xded;
            ax_cmd_w32((u32)p);
            ax_studio_get_aux_out_buffer_prev(&p);   ax_cmd_w32((u32)p);
        }
    }

    if (DAT_80613354 != 0) {                       // Dolby-surround config cmd
        ax_cmd_w(0x12);
        ax_cmd_w(0x8000);
        ax_cmd_w(10);
        ax_cmd_w(0x8049);
        ax_cmd_w(44000);
        DAT_80613350 = DAT_80613350 + 3000;
    }

    ax_cmd_w(0xe);                                 // run command
    ax_cmd_w(run_hi);
    ax_cmd_w(run_lo);
    ax_cmd_w((u16)((u32)run_arg >> 0x10));         // note: run_arg reused below
    ax_cmd_w((u16)(u32)run_arg);
    DAT_80613350 = DAT_80613350 + 10000;

    ax_cmd_w(0xf);                                 // end command
    DAT_80613350 = DAT_80613350 + 2;

    os_dc_flush_range((u32)buf_start, 0x300);
}

// ax_sync_frame: the AX frame driver body.
void ax_sync_frame(int arg) {
    u8* dst;
    u8* src;
    unsigned long long t0, t1;
    int i;

    *(unsigned long long*)&DAT_805fb670 = os_read_time_base();
    FUN_803ae590(arg);
    FUN_803ad980();
    void* cmd = ax_cmd_list_swap_buffer();

    FUN_803b113c(0xbabe0180);
    while (FUN_803b1104() != 0) { }
    FUN_803b113c((u32)cmd);
    while (FUN_803b1104() != 0) { }

    ax_free_stopped_voices();
    *(unsigned long long*)&DAT_805fb678 = os_read_time_base();
    ax_studio_run_frame_callbacks();

    t1 = os_read_time_base();
    DAT_805fb680 = (u32)(t1 >> 0x20); DAT_805fb684 = (u32)t1;
    t1 = os_read_time_base();
    DAT_805fb688 = (u32)(t1 >> 0x20); DAT_805fb68c = (u32)t1;
    if (DAT_80613378 != 0) {
        DAT_80613378();
        t1 = ((unsigned long long)DAT_805fb688 << 0x20) | DAT_805fb68c;
    }
    DAT_805fb688 = (u32)(t1 >> 0x20); DAT_805fb68c = (u32)t1;
    t1 = os_read_time_base();
    DAT_805fb690 = (u32)(t1 >> 0x20); DAT_805fb694 = (u32)t1;

    ax_cmd_list_build(&DAT_805f73a0, (void*)(&DAT_805f6c20 + DAT_80613360 * 0xa0));
    if (DAT_80613390 == 1) {
        DAT_80613360 = (DAT_80613360 + 1) % 3;
    } else {
        DAT_80613360 = (DAT_80613360 + 1) & 1;
        ai_set_stream_sample_rate(&DAT_805f6c20 + DAT_80613360 * 0xa0, 0x280);
    }

    t1 = os_read_time_base();
    DAT_805fb698 = (u32)(t1 >> 0x20); DAT_805fb69c = (u32)t1;
    DAT_805fb6a0 = FUN_803ade3c();

    dst = (u8*)FUN_803aef4c();                      // copy the 0x40-byte profile out
    if (dst != 0) {
        src = &DAT_805fb670;
        for (i = 0; i < 8; ++i) {
            for (int k = 0; k < 8; ++k) dst[k] = src[k];
            dst += 8;
            src += 8;
        }
    }
    // re-store the frame's timestamp pair at the block head
    t0 = *(unsigned long long*)&DAT_805fb670;
    t1 = *(unsigned long long*)&DAT_805fb678;
    *(unsigned long long*)&DAT_805fb670 = t0;
    *(unsigned long long*)&DAT_805fb678 = t1;
}

// ax_ai_dma_callback: AI DMA-completion callback driving the frame cadence.
void ax_ai_dma_callback(void) {
    unsigned long long t;
    t = ((unsigned long long)DAT_80613370 << 0x20) | DAT_80613374;
    if (DAT_80613368 == 0) {
        t = os_read_time_base();
    }
    DAT_80613370 = (u32)(t >> 0x20);
    DAT_80613374 = (u32)t;
    if (DAT_80613368 == 1) {
        DAT_80613368 = 0;
        ax_sync_frame(0);
    } else {
        DAT_80613368 = 2;
        FUN_803b12cc((int*)&DAT_805fb620);
    }
    if (DAT_80613390 == 1) {
        ai_set_stream_sample_rate(&DAT_805f6c20 + DAT_80613364 * 0xa0, 0x280);
        DAT_80613364 = (DAT_80613364 + 1) % 3;
    }
}

// ax_dsp_task_done_callback: DSP-task completion callback.
void ax_dsp_task_done_callback(void) {
    if (DAT_80613368 == 2) {
        DAT_80613368 = 0;
        int now = (int)os_read_time_base();
        ax_sync_frame((int)((u32)(now - DAT_80613374) >> 2));
    } else {
        DAT_80613368 = 1;
    }
}

// ax_dsp_task_wakeup: request-and-wake the DSP task thread.
void ax_dsp_task_wakeup(void) {
    DAT_80613380 = 1;
    os_wakeup_thread((int*)&DAT_80613388);
}

// ax_dsp_task_setup: register the AX DSP task and start it.
void ax_dsp_task_setup(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8) {
    int busy;
    u64 q;
    DAT_805fb62c = &DAT_8049c620;
    DAT_805fb630 = DAT_80611490;
    DAT_805fb634 = 0;
    DAT_805fb638 = &DAT_805f7620;
    DAT_805fb63c = 0x2000;
    DAT_805fb640 = 0;
    DAT_805fb644 = 0x10;
    DAT_805fb646 = 0x30;
    DAT_805fb648 = &LAB_803ad3dc;
    DAT_805fb64c = (void*)ax_dsp_task_done_callback;
    DAT_805fb650 = (void*)ax_dsp_task_wakeup;
    DAT_805fb654 = 0;
    DAT_805fb624 = 0;
    DAT_8061337c = 0;
    DAT_80613380 = 0;
    q = (u64)os_init_thread_queue((u32*)&DAT_80613388);
    busy = FUN_803b1214();
    if (busy == 0) {
        FUN_803b1150(q, p2, p3, p4, p5, p6, p7, p8);
    }
    FUN_803b121c((u32*)&DAT_805fb620);
    while (DAT_8061337c == 0) { }
}

// ax_frame_init: bring up the AX frame driver.
void ax_frame_init(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8, int mode) {
    u32* q;
    int  i;

    DAT_80613360 = 0;
    DAT_80613364 = 0;
    DAT_80613384 = 0;
    DAT_80613390 = mode;

    q = (u32*)&DAT_805f6c20;                        // AI DMA stream buffers
    for (i = 0; i < 6; ++i) {
        for (int k = 0; k < 0x50; ++k) q[k] = 0;
        q += 0x50;
    }
    os_dc_flush_range(0x805f6c20, 0x780);

    q = (u32*)&DAT_805f73a0;                         // studio parameter blocks
    for (i = 0; i < 2; ++i) {
        for (int k = 0; k < 0x50; ++k) q[k] = 0;
        q += 0x50;
    }
    os_dc_flush_range(0x805f73a0, 0x280);

    ax_dsp_task_setup(p1, p2, p3, p4, p5, p6, p7, p8);
    pad_set_analog_map_callback((void*)ax_ai_dma_callback);

    if (DAT_80613390 == 1) {
        ax_cmd_list_build(&DAT_805f73a0, &DAT_805f7120);
    } else {
        ax_cmd_list_build(&DAT_805f73a0, &DAT_805f6ea0);
    }
    DAT_80613368 = 1;
    DAT_80613378 = 0;

    if (DAT_80613390 == 1) {
        ai_set_stream_sample_rate(&DAT_805f6c20 + DAT_80613364 * 0xa0, 0x280);
        DAT_80613364 = (DAT_80613364 + 1) & 1;
    } else {
        ai_set_stream_sample_rate(&DAT_805f6c20 + DAT_80613360 * 0xa0, 0x280);
    }
    ai_enable_stream_sample_count();
}

// ax_frame_quit: shut down the AX frame driver.
void ax_frame_quit(void) {
    os_disable_interrupts();
    DAT_80613378 = 0;
    FUN_803b128c(-0x7fa049e0);
    os_sleep_thread((int*)&DAT_80613388);
    ai_disable_stream_sample_count();
    os_get_interrupts_enabled();
}
