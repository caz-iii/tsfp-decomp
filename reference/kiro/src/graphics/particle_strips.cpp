// Address range: 0x80175f6c - 0x8017781c | batch_457 (part 2/2) | Date: 2026-09-06
// Subsystem: graphics (particle / weather strip renderers)
//
// A family of camera-facing "strip" renderers driven by particle_system_render_dispatch
// (0x800df83c).  Each walks a particle slot's point list and streams textured quad
// strips (GXBegin 0x98) or point/line lists (0xa8) straight into the GX FIFO write
// port at 0xCC008000.  They share the same shape:
//   * fetch the slot record from the spawn frame (FUN_803826xx),
//   * bind a sprite texture (sprite_get_texture_id) + material,
//   * set up the indirect distortion matrix (gx_indirect_matrix_update 0xd),
//   * compute a per-vertex screen-space extrude (sine LUT DAT_80583228 or a normal
//     cross-product) and a distance/age fade, then emit two rows of the strip.
//
//   particle_projected_billboard_render  0x80175f6c  2-pass projected billboards
//   particle_render_nop                  0x801768ac  empty slot renderer
//   particle_ribbon_normal_render        0x801768b0  ribbon extruded along a normal
//   particle_ribbon_render               0x80176e08  ribbon extruded by sine LUT
//   particle_chunked_strip_render        0x80177254  segmented strip (chunks of 0x10)
//   particle_trail_strip_render          0x801774c0  fading trail strip
//   particle_spark_line_render           0x8017781c  spark point/line list
//
// Structurally-faithful non-matching port.  Vertex math preserves the original
// int->double magic-bias / byte-swapped-colour idioms of the decompiler output.

#include "common.h"
#include "generated_decls.h"
#include "particle.h"

// ---- GX FIFO write-gather port (0xCC008000, write-only volatile) ----------
extern volatile float        DAT_cc008000;    // GX FIFO float write port
extern volatile unsigned int DAT_cc008000_u;  // GX FIFO u32 write port
extern volatile unsigned char DAT_cc008000_b; // GX FIFO u8 write port
extern volatile unsigned short DAT_cc008000_s; // GX FIFO u16 write port
#define GXF(v) (DAT_cc008000   = (float)(v))
#define GXU(v) (DAT_cc008000_u = (unsigned int)(v))
#define GXB(v) (DAT_cc008000_b = (unsigned char)(v))
#define GXH(v) (DAT_cc008000_s = (unsigned short)(v))
extern unsigned int gx_bswap32(unsigned int v);

// ---- spawn-frame accessors (hi word = index/count, lo word = record ptr) ---
extern unsigned int FUN_803826b4(void);
extern unsigned int FUN_803826bc(void);
extern unsigned int FUN_803826c0(void);
extern unsigned int FUN_803826d8(void);
extern void FUN_80382700(void);   // pop of 803826b4
extern void FUN_80382708(void);   // pop of 803826bc
extern void FUN_8038270c(void);   // pop of 803826c0
extern void FUN_80382724(void);   // pop of 803826d8

// ---- shared 2D/immediate draw-state helpers --------------------------------
extern void FUN_8017fab4(void);   // set immediate vertex format
extern void FUN_8017c658(void);   // enter 2D draw state
extern void FUN_8017c670(void);   // finish 2D draw state
extern int  FUN_803825c0(double);            // quantise fade -> alpha byte
extern void FUN_803c7624(int, int, int);     // GXSetChanCtrl-ish
extern void FUN_803c75a4(int, int);          // GXSetTevOp-ish
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

// ---- render-state globals --------------------------------------------------
extern int   DAT_80611d54;      // camera / render context pointer
extern int   DAT_80611748;      // clip-rect / screen-projection block
extern int*  DAT_80610a20;      // [0]=material table base, [2]=lo id, [3]=hi id
extern int   DAT_80612254;      // active material pointer (table base + id*0x58)
extern unsigned char DAT_80583228;  // sine/cosine lookup table (0x800 entries)
extern int   DAT_8040709c;      // per-type texture-id table base (stride 0x90)
extern int   DAT_804070a4;      // per-type point count
extern int   DAT_804070a8;      // per-type packed colour
extern int   DAT_804070ac;      // per-type fade-far
extern int   DAT_804070b4;      // per-type fade-near
extern int   DAT_804070d0;      // per-type width scale
extern int   DAT_8040f2a0;      // indirect distortion matrix source
extern float DAT_803ccfc8;      // world up.x
extern float DAT_803ccfcc;      // world up.y
extern float DAT_803ccfd0;      // world up.z

// float / double constants (from the .sdata2 pool)
extern float  FLOAT_806165a0, FLOAT_806165a4, FLOAT_806165a8, FLOAT_806165ac;
extern float  FLOAT_806165b0, FLOAT_806165b4, FLOAT_806165b8, FLOAT_806165bc;
extern float  FLOAT_806165c0, FLOAT_806165d8, FLOAT_806165dc, FLOAT_806165e0;
extern float  FLOAT_806165e4, FLOAT_806165e8, FLOAT_806165ec, FLOAT_806165f0, FLOAT_806165f4;
extern double DOUBLE_806165c8, DOUBLE_806165d0;

// Byte-swap a packed RGBA colour (ABGR<->RGBA) exactly as the decompiler modelled it.
static inline unsigned int strip_bswap_rgb(unsigned int c)
{
    return (c << 0x18) | ((c & 0xff00) << 8) | ((c & 0xff0000) >> 8);
}

// particle_render_nop (0x801768ac) - a registered-but-empty renderer slot.
void particle_render_nop(void)
{
}

// particle_trail_strip_render (0x801774c0)
// Fading camera-facing trail strip.  Texture 0x33a, additive-ish blend (mode 2).
// For each segment after the first, computes an age/length fade and emits two
// rows of a quad strip offset along two screen-space basis vectors taken from the
// slot's cached view rows (piVar15[0x25d..0x262]).
void particle_trail_strip_render(void)
{
    int slot = (int)FUN_803826bc();
    int* rec = *(int**)(slot + 0x18);
    int tex = (int)sprite_get_texture_id(0x33a);
    gx_set_blend_mode(2);
    gx_set_sub_material_id(2);
    FUN_8017fab4();

    double kE8 = (double)FLOAT_806165e8;
    double kB8 = (double)FLOAT_806165b8;
    double kAC = (double)FLOAT_806165ac;
    double kA8 = (double)FLOAT_806165a8;
    int rowStride = 0x1e0;
    double kC8 = DOUBLE_806165c8;

    for (int seg = 1; seg < *rec; seg = seg + 1) {
        int* pseg = (int*)((int)rec + rowStride + 4);
        int count = *pseg;
        double fade = (double)((*(float*)(slot + 0xc) - (float)pseg[0x75]) / FLOAT_806165e4);
        if (fade < (double)FLOAT_806165a4) fade = (double)FLOAT_806165a4;
        if ((double)FLOAT_806165a8 < fade) fade = (double)FLOAT_806165a8;

        if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *DAT_80610a20 + tex * 0x58;
        }

        fade = (double)(float)(kA8 - fade);
        for (int row = 0; row < 2; row = row + 1) {
            float sel = (row == 0) ? FLOAT_806165a4 : FLOAT_806165d8;
            unsigned int a = (unsigned int)(kB8 * (double)sel + kAC);
            double sinv = (double)(float)(kE8 * (double)(float)(&DAT_80583228)[a & 0x7ff]);
            double cosv = (double)(float)(kE8 * (double)*(float*)((int)&DAT_80583228 + ((a + 0x200) * 4 & 0x1ffc)));
            gx_begin_primitive(0x98, 0, (short)((count << 2) >> 1));

            int off = 0;
            for (int i = 0; i < *pseg; i = i + 1) {
                double zbias = (double)CONCAT44(0x43300000, *(unsigned int*)((int)pseg + off + 0x1c));
                unsigned int alpha = (unsigned int)FUN_803825c0((double)(float)((double)(float)(zbias - kC8) * fade));
                unsigned int col = (i == 0) ? (unsigned int)rec[0x263] : (unsigned int)rec[0x264];
                float bx = *(float*)((int)pseg + off + 0x10);
                float by = *(float*)((int)pseg + off + 0x14);
                float bz = *(float*)((int)pseg + off + 0x18);
                float ex0 = (float)((double)(float)rec[0x25d] * cosv);
                float ey0 = (float)((double)(float)rec[0x25e] * cosv);
                float ez0 = (float)((double)(float)rec[0x25f] * cosv);
                float ex1 = (float)((double)(float)rec[0x260] * sinv);
                float ey1 = (float)((double)(float)rec[0x261] * sinv);
                float ez1 = (float)((double)(float)rec[0x262] * sinv);
                unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
                GXF(bx + ex0 + ex1); GXF(by + ey0 + ey1); GXF(bz + ez0 + ez1);
                GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
                GXF((bx - ex0) - ex1); GXF((by - ey0) - ey1); GXF((bz - ez0) - ez1);
                GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
                off = off + 0x1c;
            }
        }
        rowStride = rowStride + 0x1e0;
    }
    FUN_80382708();
}

// particle_ribbon_render (0x80176e08)
// Camera-facing ribbon strip driven by a per-type parameter table (DAT_804070xx,
// stride 0x90) with indirect distortion.  Extrudes each ring vertex by a sine/
// cosine offset scaled by the type width and an age fade; wraps the point index.
void particle_ribbon_render(void)
{
    int slot = (int)FUN_803826c0();
    int rec = *(int*)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    unsigned int col = *(unsigned int*)(&DAT_804070a8 + t9);
    double topBias = (double)CONCAT44(0x43300000, col >> 0x18);
    double invLen = (double)((float)((double)CONCAT44(0x43300000, *(unsigned int*)(&DAT_804070a4 + t9) ^ 0x80000000)
                                     - DOUBLE_806165d0) / *(float*)(rec + 0x138));
    double fade = (double)FLOAT_806165a8;
    int alpha0 = FUN_803825c0((double)(FLOAT_806165e0 * (float)(topBias - DOUBLE_806165c8)));
    gx_set_indirect_blend_mode(1);
    gx_indirect_matrix_update(0xd, (unsigned int)&DAT_8040f2a0, 8);
    FUN_8017fab4();

    if (FLOAT_806165a4 < *(float*)(&DAT_804070ac + t9)) {
        float near = *(float*)(&DAT_804070b4 + t9);
        if (near < *(float*)(slot + 0xc)) {
            fade = (double)(FLOAT_806165a8 - (*(float*)(slot + 0xc) - near) / (*(float*)(&DAT_804070ac + t9) - near));
        }
    }

    if (0 < *(int*)(slot + 0x1c)) {
        int haveTail = 1;
        int last = (int)*(float*)(rec + 0xc) + -1;
        if (last < 0) last = last + *(int*)(&DAT_804070a4 + t9);
        int lastVtx = rec + last * 0xc;
        if ((*(float*)(slot + 0x20) == *(float*)(lastVtx + 0x10)) &&
            (*(float*)(slot + 0x24) == *(float*)(lastVtx + 0x14)) &&
            (*(float*)(slot + 0x28) == *(float*)(lastVtx + 0x18))) {
            haveTail = 0;
        }
        int total = *(int*)(slot + 0x1c) + haveTail;
        if (1 < total) {
            if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                gx_bind_material_fallback();
            } else {
                DAT_80612254 = *DAT_80610a20 + tex * 0x58;
            }
            double kA8 = (double)FLOAT_806165a8;
            double kB8 = (double)FLOAT_806165b8;
            double kAC = (double)FLOAT_806165ac;
            double kC8 = DOUBLE_806165c8;
            for (int row = 0; row < 2; row = row + 1) {
                float sel = (row == 0) ? FLOAT_806165a4 : FLOAT_806165d8;
                unsigned int a = (unsigned int)(kB8 * (double)sel + kAC);
                double cosv = (double)((float)(&DAT_80583228)[a & 0x7ff] * *(float*)(&DAT_804070d0 + t9));
                double sinv = (double)(*(float*)((int)&DAT_80583228 + ((a + 0x200) * 4 & 0x1ffc)) * *(float*)(&DAT_804070d0 + t9));
                gx_begin_primitive(0x98, 0, (short)(total * 4 >> 1));
                int idx = last;
                for (int i = -haveTail; i < *(int*)(slot + 0x1c); i = i + 1) {
                    int lastTwo = *(int*)(slot + 0x1c) + -2;
                    double edge = kA8;
                    if (lastTwo <= i) edge = (i == lastTwo) ? (double)FLOAT_806165dc : (double)FLOAT_806165a4;
                    double topB = (double)CONCAT44(0x43300000, alpha0);
                    unsigned int alpha = FUN_803825c0((double)(float)(fade * (double)((float)(topB - kC8) *
                                          (float)(edge * (double)(float)(kA8 - (double)(float)((double)(*(float*)(slot + 0xc)
                                          - *(float*)(rec + idx * 4 + 0xdc)) / invLen))))));
                    unsigned int aByte = (col & 0xffffff) >> 0x10;
                    float* p = (i == -1) ? (float*)(slot + 0x20) : (float*)(rec + idx * 0xc + 0x10);
                    float e0x = (float)((double)*(float*)(rec + 0x120) * sinv);
                    float e0y = (float)((double)*(float*)(rec + 0x124) * sinv);
                    float e1x = (float)((double)*(float*)(rec + 300) * cosv);
                    float e0z = (float)((double)*(float*)(rec + 0x128) * sinv);
                    float e1y = (float)((double)*(float*)(rec + 0x130) * cosv);
                    float e1z = (float)((double)*(float*)(rec + 0x134) * cosv);
                    unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
                    GXF(p[0] + e0x + e1x); GXF(p[1] + e0y + e1y); GXF(p[2] + e0z + e1z);
                    GXU(c); GXU((aByte << 0x18) | ((alpha & 0xff) << 0x10)); GXB(0);
                    GXF((p[0] - e0x) - e1x); GXF((p[1] - e0y) - e1y); GXF((p[2] - e0z) - e1z);
                    GXU(c); GXU((aByte << 0x18) | ((alpha & 0xff) << 0x10)); GXB(1);
                    if ((i != -1) && (idx = idx + -1, idx < 0)) idx = idx + *(int*)(&DAT_804070a4 + t9);
                }
            }
        }
    }
    gx_set_indirect_blend_mode(0);
    FUN_8038270c();
}

// particle_chunked_strip_render (0x80177254)
// Segmented strip renderer: walks the point list in chunks of up to 0x10 vertices,
// binding the material stored at rec[0x554] and emitting each chunk as a quad
// strip with a sine/cosine screen-space extrude (offsets 0x66 / 0xcc into rec).
void particle_chunked_strip_render(void)
{
    int slot = (int)FUN_803826d8();
    int rec = *(int*)(slot + 0x18);
    gx_indirect_matrix_update(0xd, (unsigned int)&DAT_8040f2a0, 8);
    FUN_8017fab4();

    for (int start = 0; start < *(int*)(slot + 0x1c) + -1;) {
        int end = start + 0x11;
        if (*(int*)(slot + 0x1c) < end) end = *(int*)(slot + 0x1c);
        int chunk = end - start;
        if (1 < chunk) {
            int tex = *(int*)(rec + 0x554);
            if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                gx_bind_material_fallback();
            } else {
                DAT_80612254 = *DAT_80610a20 + tex * 0x58;
            }
            double kB8 = (double)FLOAT_806165b8;
            double kAC = (double)FLOAT_806165ac;
            for (int row = 0; row < 2; row = row + 1) {
                float sel = (row == 0) ? FLOAT_806165a4 : FLOAT_806165d8;
                unsigned int a = (unsigned int)(kB8 * (double)sel + kAC);
                double cosv = (double)(float)(&DAT_80583228)[a & 0x7ff];
                double sinv = (double)*(float*)((int)&DAT_80583228 + ((a + 0x200) * 4 & 0x1ffc));
                gx_begin_primitive(0x98, 0, (short)(chunk * 4 >> 1));
                int remaining = chunk;
                int i = 0;
                if (0 < chunk) {
                    do {
                        int e = rec + (i + start) * 4;
                        double width = (double)*(float*)(e + 0x558);
                        unsigned int col = *(unsigned int*)(e + 0x4cc);
                        float wCos = (float)(sinv * width);
                        float* p = (float*)(rec + (i + start) * 0xc);
                        float wSin = (float)(cosv * width);
                        unsigned int c = (col << 0x18) | ((col & 0xff00) << 8) | (col >> 8 & 0xff00) | (col >> 0x18);
                        GXF(p[0xcc] * wCos + p[0] + p[0x66] * wSin);
                        GXF(p[0xcd] * wCos + p[1] + p[0x67] * wSin);
                        GXF(p[0xce] * wCos + p[2] + p[0x68] * wSin);
                        GXU(c); GXB(0);
                        GXF((p[0] - p[0x66] * wSin) - p[0xcc] * wCos);
                        GXF((p[1] - p[0x67] * wSin) - p[0xcd] * wCos);
                        GXF((p[2] - p[0x68] * wSin) - p[0xce] * wCos);
                        GXU(c); GXB(1);
                        i = i + 1;
                        remaining = remaining + -1;
                    } while (remaining != 0);
                }
            }
        }
        start = chunk + start + -1;
    }
    FUN_80382724();
}

// particle_spark_line_render (0x8017781c)
// Spark/point list renderer.  Texture 0x32e; walks a linked list of spark records
// (next at +0x64) and emits each as a short line list (GXBegin 0xa8) whose two
// endpoints are offset along the record's velocity by a gravity-integrated span.
void particle_spark_line_render(void)
{
    // The saved-frame pair carries the list head in its hi word and the visibility
    // group mask in its lo word.
    unsigned long long frame = (unsigned long long)__save_gpr_28();
    int head = (int)(frame >> 0x20);
    unsigned int groupMask = (unsigned int)frame;

    int tex = (int)sprite_get_texture_id(0x32e);
    gx_set_active_material(tex);
    gx_indirect_matrix_update(0xd, (unsigned int)&DAT_8040f2a0, 8);
    FUN_8017fab4();
    FUN_803c7624(0, 1, 0);
    FUN_803c75a4(6, 5);

    for (int it = head; it != 0; it = *(int*)(it + 100)) {
        if (particle_slot_is_visible(it, groupMask) != 0) {
            double t0 = (double)*(float*)(it + 0xc);
            double t1 = (double)(float)((double)FLOAT_806165f4 + t0);
            float* pts = *(float**)(it + 0x18);
            double drop0 = (double)(float)((double)(float)((double)FLOAT_806165f0 * t0) * t0);
            unsigned int alpha = (unsigned int)(FLOAT_806165c0 *
                                 (float)((double)(float)((double)FLOAT_806165ec - t0) / (double)FLOAT_806165ec));
            double drop1 = (double)(float)((double)(float)((double)FLOAT_806165f0 * t1) * t1);
            gx_begin_primitive(0xa8, 0, (short)(*(int*)(it + 0x1c) << 1));
            int off = 0;
            for (int i = 0; i < *(int*)(it + 0x1c); i = i + 1) {
                double vx = (double)*(float*)((int)pts + off + 0x10);
                double base = (double)*(float*)(it + 0xc);
                double vz = (double)*(float*)((int)pts + off + 0xc);
                double vy = (double)*(float*)((int)pts + off + 0x14);
                unsigned int c = (alpha & 0xff) | 0xffffff00;
                GXF((float)(vz * base + (double)pts[0]));
                GXF((float)(drop0 + (double)(float)(vx * base + (double)pts[1])));
                GXF((float)(vy * base + (double)pts[2]));
                GXU(c); GXB(0);
                GXF((float)(vz * t1 + (double)pts[0]));
                GXF((float)(drop1 + (double)(float)(vx * t1 + (double)pts[1])));
                GXF((float)(vy * t1 + (double)pts[2]));
                GXU(c); GXB(1);
                off = off + 0xc;
            }
        }
    }
}


// particle_ribbon_normal_render (0x801768b0)
// Ribbon strip whose extrude direction is a true surface normal: for the first
// (tail) segment it builds an orthonormal basis from the segment direction and
// world-up (DAT_803ccfc8..d0) via two cross products, then reuses the ring's
// cached basis rows (rec[..0x45],[..0x78]) for the remaining segments.  Same
// per-type table (stride 0x90) and age fade as particle_ribbon_render.
void particle_ribbon_normal_render(void)
{
    int slot = (int)FUN_803826b4();
    float* rec = *(float**)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    float colf = rec[0xac];
    double negCount = (double)(CONCAT44(0x43300000, *(unsigned int*)(&DAT_804070a4 + t9)) ^ 0x80000000);
    double fade = (double)FLOAT_806165a8;
    double invLen = (double)((float)(negCount - DOUBLE_806165d0) / rec[0xab]);
    if (colf == 0.0f) colf = *(float*)(&DAT_804070a8 + t9);

    gx_indirect_matrix_update(0xd, (unsigned int)&DAT_8040f2a0, 8);
    FUN_8017fab4();

    if (FLOAT_806165a4 < *(float*)(&DAT_804070ac + t9)) {
        float near = *(float*)(&DAT_804070b4 + t9);
        if (near < *(float*)(slot + 0xc)) {
            fade = (double)(FLOAT_806165a8 - (*(float*)(slot + 0xc) - near) / (*(float*)(&DAT_804070ac + t9) - near));
        }
    }

    if (0 < *(int*)(slot + 0x1c)) {
        int haveTail = 1;
        int last = (int)rec[0] + -1;
        if (last < 0) last = last + *(int*)(&DAT_804070a4 + t9);
        if ((*(float*)(slot + 0x20) == rec[last * 3 + 1]) &&
            (*(float*)(slot + 0x24) == rec[last * 3 + 2]) &&
            (*(float*)(slot + 0x28) == rec[last * 3 + 3])) {
            haveTail = 0;
        }
        int total = *(int*)(slot + 0x1c) + haveTail;
        if (1 < total) {
            if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                gx_bind_material_fallback();
            } else {
                DAT_80612254 = *DAT_80610a20 + tex * 0x58;
            }
            double kA8 = (double)FLOAT_806165a8;
            double kB8 = (double)FLOAT_806165b8;
            double kAC = (double)FLOAT_806165ac;
            double kC8 = DOUBLE_806165c8;
            for (int row = 0; row < 2; row = row + 1) {
                float extrudeBasis[3];      // local_100/fc/f8
                float extrudeTangent[3];    // local_10c/108/104
                float sel = (row == 0) ? FLOAT_806165a4 : FLOAT_806165d8;
                unsigned int a = (unsigned int)(kB8 * (double)sel + kAC);
                double cosv = (double)((float)(&DAT_80583228)[a & 0x7ff] * *(float*)(&DAT_804070d0 + t9));
                double sinv = (double)(*(float*)((int)&DAT_80583228 + ((a + 0x200) * 4 & 0x1ffc)) * *(float*)(&DAT_804070d0 + t9));
                gx_begin_primitive(0x98, 0, (short)(total * 4 >> 1));
                int idx = last;
                for (int i = -haveTail; i < *(int*)(slot + 0x1c); i = i + 1) {
                    int cnt = *(int*)(slot + 0x1c);
                    double edge = kA8;
                    if (cnt + -2 <= i) edge = (i == cnt + -2) ? (double)FLOAT_806165dc : (double)FLOAT_806165a4;
                    double topB = (double)CONCAT44(0x43300000, (unsigned int)colf >> 0x18);
                    unsigned int alpha = FUN_803825c0((double)(float)(fade * (double)((float)(topB - kC8) *
                                          (float)(edge * (double)(float)(kA8 - (double)(float)((double)(*(float*)(slot + 0xc)
                                          - rec[idx + 0x34]) / invLen))))));
                    float upx = DAT_803ccfc8, upy = DAT_803ccfcc, upz = DAT_803ccfd0;
                    float* p;
                    float* basis;
                    float* tangent;
                    if (i == -1) {
                        p = (float*)(slot + 0x20);
                        float* nxt = rec + idx * 3 + 1;
                        float dx = nxt[0] - *(float*)(slot + 0x20);
                        float dy = nxt[1] - *(float*)(slot + 0x24);
                        float dz = nxt[2] - *(float*)(slot + 0x28);
                        double len = math_sqrt((double)(float)((double)(dz * dz) + (double)(float)((double)(dx * dx) + (double)(dy * dy))));
                        float ndx = (float)((double)dx * (double)(float)((double)FLOAT_806165a8 / len));
                        float ndz = (float)((double)dz * (double)(float)((double)FLOAT_806165a8 / len));
                        float cx = ndx * upy - FLOAT_806165a4 * upx;
                        float cy = ndz * upx - ndx * upz;
                        float cz = FLOAT_806165a4 * upz - ndz * upy;
                        extrudeBasis[0]  = (float)ABS(cz);
                        extrudeBasis[1]  = (float)ABS(cy);
                        extrudeBasis[2]  = (float)ABS(cx);
                        extrudeTangent[0]= (float)ABS(cy * ndz - cx * FLOAT_806165a4);
                        extrudeTangent[1]= (float)ABS(cx * ndx - cz * ndz);
                        extrudeTangent[2]= (float)ABS(cz * FLOAT_806165a4 - cy * ndx);
                        basis   = extrudeBasis;
                        tangent = extrudeTangent;
                    } else {
                        p = rec + idx * 3 + 1;
                        if (i != cnt + -1) {
                            basis   = rec + idx * 3 + 0x45;
                            tangent = rec + idx * 3 + 0x78;
                        } else {
                            basis   = extrudeBasis;   // reuse last computed
                            tangent = extrudeTangent;
                        }
                    }
                    unsigned int c = ((int)colf << 0x18) | (((unsigned int)colf & 0xff00) << 8) |
                                     (((unsigned int)colf & 0xff0000) >> 8) | (alpha & 0xff);
                    GXF(p[0] + (float)((double)basis[0] * sinv) + (float)((double)tangent[0] * cosv));
                    GXF(p[1] + (float)((double)basis[1] * sinv) + (float)((double)tangent[1] * cosv));
                    GXF(p[2] + (float)((double)basis[2] * sinv) + (float)((double)tangent[2] * cosv));
                    GXU(c); GXB(0);
                    GXF((p[0] - (float)((double)basis[0] * sinv)) - (float)((double)tangent[0] * cosv));
                    GXF((p[1] - (float)((double)basis[1] * sinv)) - (float)((double)tangent[1] * cosv));
                    GXF((p[2] - (float)((double)basis[2] * sinv)) - (float)((double)tangent[2] * cosv));
                    GXU(c); GXB(1);
                    if ((i != -1) && (idx = idx + -1, idx < 0)) idx = idx + *(int*)(&DAT_804070a4 + t9);
                }
            }
        }
    }
    FUN_80382700();
}

// particle_projected_billboard_render (0x80175f6c)
// Two-pass projected-billboard renderer.  Pass 1 (texture 0x330) draws the first
// half of the point list; pass 2 (texture 0x323) the remainder.  Each visible
// point is transformed by the projection matrix (mtx4_transform_point4_alias),
// clip-culled against the frustum, perspective-divided, then splatted as a screen
// aligned quad rotated by the sine LUT, with a distance/age fade -> alpha.
void particle_projected_billboard_render(void)
{
    int slot = (int)FUN_803826d8();
    int rec = *(int*)(slot + 0x18);
    unsigned int col = *(unsigned int*)(rec + 4);
    unsigned int topByte = col >> 0x18;
    FUN_8017c658();
    gx_indirect_matrix_update(0xd, (unsigned int)&DAT_8040f2a0, 8);
    FUN_8017fab4();

    // ---- pass 1: texture 0x330 ----
    int tex = (int)sprite_get_texture_id(0x330);
    gx_set_active_material(tex);
    double invNear = (double)FLOAT_806165a0;
    double kA4 = (double)FLOAT_806165a4;
    int posOff = 0;
    int rgbOff = 0;
    unsigned int colRGB = col & 0xffffff;
    int i = 0;
    int npts = *(int*)(slot + 0x1c);
    for (i = 0; i < ((npts >> 1) + ((npts < 0 && (npts & 1) != 0) ? 1 : 0)); i = i + 1) {
        int pe = rec + posOff;
        double depth = (double)(float)((double)(*(float*)(slot + 0xc) - *(float*)(pe + 0x120)) / invNear);
        if ((kA4 < depth) && (depth < (double)FLOAT_806165a8)) {
            int ce = rec + rgbOff;
            double dnear = (double)(float)((double)FLOAT_806165a8 - depth);
            double dfar  = (double)(float)((double)FLOAT_806165ac + depth);
            float wp[4];
            float sp[4];
            wp[0] = (float)((double)*(float*)(ce + 0x1d8) * depth + (double)*(float*)(ce + 0xc));
            wp[1] = (float)((double)*(float*)(ce + 0x1dc) * depth + (double)*(float*)(ce + 0x10));
            wp[2] = (float)((double)*(float*)(ce + 0x1e0) * depth + (double)*(float*)(ce + 0x14));
            mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c), wp, sp);
            double w = (double)sp[3];
            float wclip = (float)((double)FLOAT_806165b0 * w);
            if ((-wclip <= sp[0]) && (sp[0] <= wclip) && (-wclip <= sp[1]) && (sp[1] <= wclip) &&
                (-w <= (double)sp[2]) && ((double)sp[2] <= w)) {
                double size = (double)(*(float*)(pe + 0x120) * *(float*)(pe + 0x2ec));
                unsigned int alpha = (unsigned int)FUN_803825c0(
                    (double)(float)((double)(float)((double)CONCAT44(0x43300000, topByte) - DOUBLE_806165c8) * dnear));
                unsigned int c = col | (alpha << 0x18);
                float invW = (float)((double)FLOAT_806165a8 / w);
                sp[0] = sp[0] * invW; sp[1] = sp[1] * invW; sp[2] = sp[2] * invW;
                unsigned int ang = (unsigned int)((double)FLOAT_806165b8 * size + (double)FLOAT_806165ac);
                float clip10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
                float clip08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
                float cx = clip10 * sp[0] + clip08;
                float sinE = (float)((double)CONCAT44(0x43300000, (unsigned int)(
                                (double)(*(float*)(pe + 0x17c) * (float)(dfar * (double)*(float*)(DAT_80611d54 + 0x268))) / w))
                                - DOUBLE_806165d0);
                (void)sinE;
                float rot0 = (float)((double)(float)((double)CONCAT44(0x43300000, ang) ) - DOUBLE_806165d0);
                (void)rot0;
                unsigned int sIdx = (unsigned int)((float)((double)CONCAT44(0x43300000, ang) - DOUBLE_806165d0) *
                                     (float)(&DAT_80583228)[ang & 0x7ff]);
                unsigned int cIdx = (unsigned int)((float)((double)CONCAT44(0x43300000, ang) - DOUBLE_806165d0) *
                                     *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc)));
                float ex = (float)((double)CONCAT44(0x43300000, sIdx) - DOUBLE_806165d0);
                float ey = (float)((double)CONCAT44(0x43300000, cIdx) - DOUBLE_806165d0);
                float clip24 = -(float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x24)) - DOUBLE_806165d0);
                float clip1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
                float cy = clip24 * sp[1] + clip1c;
                float z = FLOAT_806165a8 + sp[2];
                unsigned int cc = strip_bswap_rgb(colRGB) | (c >> 0x18);
                gx_begin_primitive(0x98, 0, 4);
                GXF(cx + ey); GXF(cy + ex); GXF(z); GXU(cc); GXB(0);
                GXF(cx + ey); GXF(cy - ex); GXF(z); GXU(cc); GXB(1);
                GXF(cx - ey); GXF(cy + ex); GXF(z); GXU(cc); GXB(2);
                GXF(cx - ey); GXF(cy - ex); GXF(z); GXU(cc); GXB(3);
            }
        }
        rgbOff = rgbOff + 0xc;
        posOff = posOff + 4;
        colRGB = col & 0xffffff;
    }

    // ---- pass 2: texture 0x323 (remaining points) ----
    tex = (int)sprite_get_texture_id(0x323);
    gx_set_active_material(tex);
    int rgbOff2 = i * 0xc;
    double invFar = (double)FLOAT_806165bc;
    int posOff2 = i << 2;
    for (; i < *(int*)(slot + 0x1c); i = i + 1) {
        int pe = rec + posOff2;
        double depth = (double)(float)((double)(*(float*)(slot + 0xc) - *(float*)(pe + 0x120)) / invFar);
        if ((kA4 < depth) && (depth < (double)FLOAT_806165a8)) {
            int ce = rec + rgbOff2;
            double dnear = (double)(float)((double)FLOAT_806165a8 - depth);
            double dfar  = (double)(float)((double)FLOAT_806165ac + depth);
            float wp[4];
            float sp[4];
            wp[0] = (float)((double)*(float*)(ce + 0x1d8) * depth + (double)*(float*)(ce + 0xc));
            wp[1] = (float)((double)*(float*)(ce + 0x1dc) * depth + (double)*(float*)(ce + 0x10));
            wp[2] = (float)((double)*(float*)(ce + 0x1e0) * depth + (double)*(float*)(ce + 0x14));
            mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c), wp, sp);
            double w = (double)sp[3];
            float wclip = (float)((double)FLOAT_806165b0 * w);
            if ((-wclip <= sp[0]) && (sp[0] <= wclip) && (-wclip <= sp[1]) && (sp[1] <= wclip) &&
                (-w <= (double)sp[2]) && ((double)sp[2] <= w)) {
                unsigned int alpha = (unsigned int)FUN_803825c0((double)(float)((double)FLOAT_806165c0 * dnear));
                float invW = (float)((double)FLOAT_806165a8 / w);
                sp[0] = sp[0] * invW; sp[1] = sp[1] * invW; sp[2] = sp[2] * invW;
                unsigned int ang = (unsigned int)(FLOAT_806165b8 * *(float*)(pe + 0x120) * *(float*)(pe + 0x2ec) + FLOAT_806165ac);
                float clip10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
                float clip08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
                float cx = clip10 * sp[0] + clip08;
                unsigned int sIdx = (unsigned int)((float)((double)CONCAT44(0x43300000, ang) - DOUBLE_806165d0) *
                                     (float)(&DAT_80583228)[ang & 0x7ff]);
                unsigned int cIdx = (unsigned int)((float)((double)CONCAT44(0x43300000, ang) - DOUBLE_806165d0) *
                                     *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc)));
                float ex = (float)((double)CONCAT44(0x43300000, sIdx) - DOUBLE_806165d0);
                float ey = (float)((double)CONCAT44(0x43300000, cIdx) - DOUBLE_806165d0);
                float clip24 = -(float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x24)) - DOUBLE_806165d0);
                float clip1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
                float cy = clip24 * sp[1] + clip1c;
                float z = FLOAT_806165a8 + sp[2];
                unsigned char aByte = (unsigned char)alpha;
                gx_begin_primitive(0x98, 0, 4);
                GXF(cx + ey); GXF(cy + ex); GXF(z); GXB(0x7f); GXB(0x7f); GXB(0x7f); GXB(aByte); GXB(0);
                GXF(cx + ey); GXF(cy - ex); GXF(z); GXB(0x7f); GXB(0x7f); GXB(0x7f); GXB(aByte); GXB(1);
                GXF(cx - ey); GXF(cy + ex); GXF(z); GXB(0x7f); GXB(0x7f); GXB(0x7f); GXB(aByte); GXB(2);
                GXF(cx - ey); GXF(cy - ex); GXF(z); GXB(0x7f); GXB(0x7f); GXB(0x7f); GXB(aByte); GXB(3);
                (void)dfar;
            }
        }
        rgbOff2 = rgbOff2 + 0xc;
        posOff2 = posOff2 + 4;
    }
    FUN_8017c670();
    FUN_80382724();
}


// ===========================================================================
// batch_458 : 0x801779e4 - 0x8017a16c
// Ten more particle renderers dispatched by particle_system_render_dispatch
// (0x800df83c): point-sprite splats, projected-sprite strips, camera-facing
// quad ribbons and beam quads.  All stream vertices into the GX FIFO at
// 0xCC008000; most first transform world points through the projection matrix
// (mtx4_transform_point4_alias) and frustum-clip before perspective divide.
//
//   particle_point_sprite_render            0x801779e4  point-sprite splat (0xb8)
//   particle_point_sprite_list_render       0x80177c2c  grouped point-sprite list
//   particle_beam_quad_render               0x80177f10  single beam quad (2 ends)
//   particle_arc_quad_render                0x8017841c  sine-rotated arc quad strip
//   particle_projected_sprite_render        0x801788a4  projected sprite strip
//   particle_projected_sprite_strip_render  0x80178dc8  projected sprite (16-stride)
//   particle_projected_velocity_sprite_render 0x8017926c projected sprite, vel-aligned
//   particle_matrix_sprite_strip_render     0x801797e4  pre-multiplied-matrix strip
//   particle_ribbon_quad_render             0x80179d08  quad ribbon (paired ends)
//   particle_ribbon_quad_group_render       0x8017a16c  quad ribbon (grouped)
// ===========================================================================

// Additional spawn-frame accessors + pops used by this batch.
extern unsigned int FUN_803826dc(void);   // frame ptr (hi=slot, lo=matrix)
extern unsigned int FUN_803826d4(void);   // frame ptr (hi=slot, lo=matrix, group)
extern unsigned int FUN_803826d0(void);   // frame ptr (hi=slot, lo=matrix)
extern unsigned int FUN_803826e4(void);   // frame ptr (hi=slot, lo=x0/y0)
extern void FUN_80382728(void);           // pop of 803826dc
extern void FUN_80382720(void);           // pop of 803826d4
extern void FUN_8038271c(void);           // pop of 803826d0
extern void FUN_80382730(void);           // pop of 803826e4

// Additional GX / math helpers.
extern void FUN_803c75e4(int a, int b);   // GXSetTevKAlphaSel-ish (point size)
extern double math_atan2(double y, double x);

// Per-type parameter table extra columns (base + stride 0x90 like DAT_804070xx).
extern int DAT_804070b0;   // fade-in distance
extern int DAT_804070b8;   // near cull distance
extern int DAT_804070bc;   // screen-size numerator
extern int DAT_804070c0;   // screen-size numerator (height)
extern int DAT_804070c4;   // max screen size
extern int DAT_804070cc;   // depth-bias factor
extern int DAT_804070d4;   // size ramp X
extern int DAT_804070d8;   // size ramp Y
extern int DAT_80407108;   // spread X
extern int DAT_8040710c;   // spread Y
extern int DAT_80407110;   // spread Z

// Point-sprite / arc parameter tables (base, stride 8 or 0x18).
extern int DAT_8040ba4c;   // arc point count
extern int DAT_8040ba50;   // arc texture id
extern int DAT_8040ba54;   // arc size (variant A)
extern int DAT_8040ba58;   // arc size (variant B)
extern int DAT_8040ba5c;   // arc width (variant A)
extern int DAT_8040ba60;   // arc width (variant B)
extern int DAT_8040ba94;   // point-sprite colour (stride 8)
extern int DAT_8040ba98;   // point-sprite texture id (stride 8)

// Extra float pool constants.
extern float FLOAT_806165f8, FLOAT_806165fc;
extern float FLOAT_80616600, FLOAT_80616604, FLOAT_80616608, FLOAT_8061660c, FLOAT_80616610;
extern float FLOAT_80616614, FLOAT_80616618, FLOAT_8061661c, FLOAT_80616620, FLOAT_80616624;
extern float FLOAT_80616628, FLOAT_8061662c, FLOAT_80616630;
extern double DOUBLE_80616600, DOUBLE_80616604;

// per-frame draw-stat counters
extern int DAT_80611570, DAT_80611574, DAT_80611578;

// particle_point_sprite_render (0x801779e4)
// Emits each particle as a single GX point sprite (GXBegin 0xb8, tex 0x28d) whose
// on-screen size is inversely proportional to view-space depth; applies a
// gravity-integrated vertical drop and a linear age fade.  Bumps the per-frame
// vertex/prim draw-stat counters up front.
void particle_point_sprite_render(void)
{
    int slot = (int)FUN_803826d8();
    int* rec = *(int**)(slot + 0x18);
    int tex = (int)sprite_get_texture_id(0x28d);
    float age = *(float*)(slot + 0xc);
    DAT_80611570 = DAT_80611570 + *(int*)(slot + 0x1c) * 4;
    int viewMtx = *(int*)(DAT_80611d54 + 0x20c);
    float fade = FLOAT_806165f8 * ((FLOAT_806165ec - age) / FLOAT_806165ec);
    double drop = (double)(FLOAT_806165f0 * age * age);
    double sizeNum = (double)(FLOAT_806165fc * *(float*)(DAT_80611d54 + 0x268));
    DAT_80611574 = DAT_80611574 + *(int*)(slot + 0x1c) * 2;
    unsigned int col = *(unsigned int*)(&DAT_8040ba94 + *rec * 8);
    DAT_80611578 = DAT_80611578 + *(int*)(slot + 0x1c) * 2;
    gx_set_active_material(tex);
    FUN_8017fab4();
    FUN_803c7624(0, 1, 1);
    double sizeMin = (double)FLOAT_80616604;
    sizeNum = (double)(float)((double)DOUBLE_80616600 * sizeNum);

    int off = 0;
    for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1) {
        float a = *(float*)(slot + 0xc);
        double px = (double)(*(float*)((int)rec + off + 0x10) * a + (float)rec[1]);
        double py = (double)(float)(drop + (double)(*(float*)((int)rec + off + 0x14) * a + (float)rec[2]));
        double pz = (double)(*(float*)((int)rec + off + 0x18) * a + (float)rec[3]);
        double viewZ = (double)(*(float*)(viewMtx + 0x3c) +
                       (float)((double)*(float*)(viewMtx + 0x2c) * pz +
                              (double)(float)((double)*(float*)(viewMtx + 0xc) * px +
                                             (double)(float)((double)*(float*)(viewMtx + 0x1c) * py))));
        if (sizeMin < viewZ) {
            float size = (float)(sizeNum / viewZ);
            if (FLOAT_80616608 < size) size = FLOAT_80616608;
            FUN_803c75e4((int)size, 5);
            gx_begin_primitive(0xb8, 0, 1);
            GXF(px); GXF(py); GXF(pz);
            GXB((char)col); GXB((char)((unsigned int)col >> 8)); GXB((char)((unsigned int)col >> 0x10));
            GXB((char)(int)fade);
            GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        }
        off = off + 0xc;
    }
    FUN_80382724();
}

// particle_point_sprite_list_render (0x80177c2c)
// Grouped point-sprite splat over a linked list of particle slots (next at +0x64,
// tex 0x323).  Per particle the vertical position is offset by a per-index
// gravity term; size is depth-scaled and clamped, alpha ramps in/out by age.
void particle_point_sprite_list_render(void)
{
    unsigned long long frame = (unsigned long long)__save_gpr_25();
    int node = (int)(frame >> 0x20);
    unsigned int groupMask = (unsigned int)frame;
    int tex = (int)sprite_get_texture_id(0x323);
    int viewMtx = *(int*)(DAT_80611d54 + 0x20c);
    if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = *DAT_80610a20 + tex * 0x58;
    }
    FUN_8017fab4();
    FUN_803c7624(0, 1, 1);

    for (; node != 0; node = *(int*)(node + 100)) {
        if (particle_slot_is_visible(node, groupMask) == 0) continue;
        float* rec = *(float**)(node + 0x18);
        double kAC = (double)FLOAT_806165ac;
        int off = 0;
        double kInvLife = (double)FLOAT_8061660c;
        double kA4 = (double)FLOAT_806165a4;
        double kBias = DOUBLE_806165d0;
        for (int i = 0; i < *(int*)(node + 0x1c); i = i + 1) {
            double idxf = (double)(CONCAT44(0x43300000, i) ^ 0x80000000);
            double age = (double)(*(float*)(node + 0xc) -
                         (float)((double)(float)(kAC * (double)(float)(idxf - kBias)) / kInvLife));
            if ((kA4 <= age) && (age < (double)FLOAT_806165e0)) {
                double px = (double)*rec;
                double py = (double)(FLOAT_80616618 * (float)((double)*(float*)((int)rec + off + 0x14) * age) + rec[1]);
                double pz = (double)rec[2];
                float viewZ = *(float*)(viewMtx + 0x3c) +
                              (float)((double)*(float*)(viewMtx + 0x2c) * pz +
                                     (double)(float)((double)*(float*)(viewMtx + 0xc) * px +
                                                    (double)(float)((double)*(float*)(viewMtx + 0x1c) * py)));
                if (viewZ < FLOAT_80616604) viewZ = FLOAT_80616604;
                float size = (FLOAT_80616600 * FLOAT_80616610 * *(float*)(DAT_80611d54 + 0x268) *
                             (float)((double)FLOAT_80616614 * age + (double)FLOAT_806165a8)) / viewZ;
                if (FLOAT_80616608 < size) size = FLOAT_80616608;
                double fade = (double)FLOAT_806165a8;
                double kAC2 = (double)FLOAT_806165ac;
                if (kAC2 <= age) {
                    if ((double)FLOAT_806165a8 < age) fade = (double)(float)((double)(float)((double)FLOAT_806165e0 - age) / kAC2);
                } else {
                    fade = (double)(float)(age / kAC2);
                }
                FUN_803c75e4((int)size, 5);
                gx_begin_primitive(0xb8, 0, 1);
                GXF(px); GXF(py); GXF(pz);
                GXB(0x2f); GXB(0x2f); GXB(0x2f); GXB((char)(int)((double)FLOAT_806165f8 * fade));
                GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
            }
            off = off + 4;
        }
    }
    __restore_gpr_25();
}

// particle_beam_quad_render (0x80177f10)
// Draws a single beam as one camera-facing quad spanning two projected endpoints
// (head at rec[1..3], tail = head + dir*len).  Both ends are projected and
// frustum-clipped; the quad is extruded perpendicular to the screen-space beam
// direction with per-end widths (near/far).
void particle_beam_quad_render(void)
{
    int slot = (int)__save_gpr_28();
    int* rec = *(int**)(slot + 0x18);
    FUN_8017fab4();
    if (!((FLOAT_806165a4 < *(float*)(slot + 0xc)) ||
          ((*(unsigned int*)(slot + 0x34) & (*(unsigned int*)(slot + 0x34) - 1)) == 0))) {
        __restore_gpr_28();
        return;
    }
    unsigned int count = *(unsigned int*)(slot + 0x1c);
    int t = *rec * 0x18;
    if (count == 0) count = *(unsigned int*)(&DAT_8040ba4c + t);
    unsigned int variant = *(unsigned int*)(slot + 0x34) & (*(unsigned int*)(slot + 0x34) - 1);
    float len = (variant == 0) ? *(float*)(&DAT_8040ba54 + t) : *(float*)(&DAT_8040ba58 + t);
    double dLen = (double)len;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040ba50 + t));
    gx_set_active_material(tex);

    float head[4], hs[4], tail[4], ts[4];
    head[0] = (float)rec[1]; head[1] = (float)rec[2]; head[2] = (float)rec[3];
    tail[0] = (float)((double)(float)rec[4] * dLen + (double)head[0]);
    tail[1] = (float)((double)(float)rec[5] * dLen + (double)head[1]);
    tail[2] = (float)((double)(float)rec[6] * dLen + (double)head[2]);
    mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c), head, hs);
    mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c), tail, ts);
    float wH = FLOAT_806165b0 * hs[3];
    float wT = FLOAT_806165b0 * ts[3];
    if (!((-wH <= hs[0]) && (hs[0] <= wH) && (-wH <= hs[1]) && (hs[1] <= wH) &&
          (-hs[3] <= hs[2]) && (hs[2] <= hs[3]) &&
          (-wT <= ts[0]) && (ts[0] <= wT) && (-wT <= ts[1]) && (ts[1] <= wT) &&
          (-ts[3] <= ts[2]) && (ts[2] <= ts[3]))) {
        __restore_gpr_28();
        return;
    }
    float width = (variant == 0) ? *(float*)(&DAT_8040ba5c + t) : *(float*)(&DAT_8040ba60 + t);
    width = *(float*)(DAT_80611d54 + 0x268) * width;
    float invH = FLOAT_806165a8 / hs[3];
    float invT = FLOAT_806165a8 / ts[3];
    hs[0] = hs[0] * invH; hs[1] = hs[1] * invH; hs[2] = hs[2] * invH;
    ts[0] = ts[0] * invT; ts[1] = ts[1] * invT; ts[2] = ts[2] * invT;
    double wHalf = (double)(width / (FLOAT_8061660c + hs[3]));
    double wHalfT = (double)(width / (FLOAT_8061660c + ts[3]));
    float clip24 = (float)((double)CONCAT44(0x43300000, -*(int*)(DAT_80611748 + 0x24)) - DOUBLE_806165d0);
    (void)clip24;
    float sc24_h = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
    float sc1c_h = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
    float sc10_h = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
    float sc08_h = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
    double sy_h = (double)(sc24_h * hs[1] + sc1c_h);
    double sy_t = (double)(sc24_h * ts[1] + sc1c_h);
    double dy = (double)(float)(sy_t - sy_h);
    double sx_h = (double)(sc10_h * hs[0] + sc08_h);
    double sx_t = (double)(sc10_h * ts[0] + sc08_h);
    double dx = (double)(float)(sx_t - sx_h);
    double invMag = (double)(float)((double)FLOAT_806165a8 / math_sqrt((double)(float)(dx * dx + (double)(float)(dy * dy))));
    double nx = (double)(float)(dx * invMag);
    double ny = (double)(float)(dy * invMag);
    unsigned int c = (count << 0x18) | ((count & 0xff00) << 8) | (count >> 8 & 0xff00) | (count >> 0x18);
    FUN_8017c658();
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);
    GXF((float)(wHalf * (double)(float)(-ny + nx) + sx_h)); GXF((float)(wHalf * (double)(float)(-nx - ny) + sy_h)); GXF(FLOAT_806165a8 + hs[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
    GXF((float)(wHalf * (double)(float)(-ny - nx) + sx_h)); GXF((float)(wHalf * (double)(float)(-nx + ny) + sy_h)); GXF(FLOAT_806165a8 + ts[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
    GXF((float)(wHalfT * (double)(float)(ny + nx) + sx_t)); GXF((float)(wHalfT * (double)(float)(nx - ny) + sy_t)); GXF(FLOAT_806165a8 + hs[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
    GXF((float)(wHalfT * (double)(float)(ny - nx) + sx_t)); GXF((float)(wHalfT * (double)(float)(ny + nx) + sy_t)); GXF(FLOAT_806165a8 + ts[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    __restore_gpr_28();
}

// particle_arc_quad_render (0x8017841c)
// Emits an arc/spline of sine-rotated camera-facing quads.  Each sample point is
// interpolated along the particle's basis (rec[4..6]) by a per-index parameter,
// projected + frustum-clipped, then splatted as a screen-aligned quad rotated by
// the sine LUT.  Frame supplied by FUN_803826e4.
void particle_arc_quad_render(void)
{
    int slot = (int)FUN_803826e4();
    int* rec = *(int**)(slot + 0x18);
    unsigned int col = *(unsigned int*)(&DAT_8040ba94 + *rec * 8);
    unsigned int colA = col | 0xff000000;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040ba98 + *rec * 8));
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();

    if ((FLOAT_806165a4 < *(float*)(slot + 0xc)) ||
        ((*(unsigned int*)(slot + 0x34) & (*(unsigned int*)(slot + 0x34) - 1)) == 0)) {
        double kSpan = (double)FLOAT_8061661c;
        int off = 0;
        double kBias = DOUBLE_806165d0;
        for (unsigned int i = 0; (int)i < *(int*)(slot + 0x1c); i = i + 1) {
            double idxf = (double)CONCAT44(0x43300000, i ^ 0x80000000);
            float p[4], sp[4];
            p[0] = (float)rec[1] + (float)((double)((float)rec[4] * (float)(idxf - kBias)) / kSpan);
            p[1] = (float)rec[2] + (float)((double)((float)rec[5] * (float)(idxf - kBias)) / kSpan);
            p[2] = (float)rec[3] + (float)((double)((float)rec[6] * (float)(idxf - kBias)) / kSpan);
            mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c), p, sp);
            if ((-sp[3] <= sp[0]) && (sp[0] <= sp[3]) && (-sp[3] <= sp[1]) &&
                (sp[1] <= sp[3]) && (-sp[3] <= sp[2]) && (sp[2] <= sp[3])) {
                float invW = FLOAT_806165a8 / sp[3];
                sp[1] = sp[1] * invW; sp[2] = sp[2] * invW; sp[0] = sp[0] * invW;
                float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
                float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
                float cx = sc10 * sp[0] + sc08;
                float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
                float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
                float cy = sc24 * sp[1] + sc1c;
                float z = *(float*)(DAT_80611748 + 0x38) * sp[2] + *(float*)(DAT_80611748 + 0x3c);
                unsigned int sizeI = (unsigned int)(((FLOAT_80616620 + (float)((double)CONCAT44(0x43300000, i ^ 0x80000000) - DOUBLE_806165d0)) *
                                     *(float*)(DAT_80611d54 + 0x268)) / sp[3]);
                unsigned int ang = (unsigned int)(FLOAT_806165b8 *
                                   ((FLOAT_80616624 * *(float*)((int)rec + off + 0x1c)) / FLOAT_80616628) + FLOAT_806165ac);
                float ex = (float)((double)CONCAT44(0x43300000, sizeI ^ 0x80000000) - DOUBLE_806165d0) *
                           (float)(&DAT_80583228)[ang & 0x7ff];
                float ey = (float)((double)CONCAT44(0x43300000, sizeI ^ 0x80000000) - DOUBLE_806165d0) *
                           *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc));
                unsigned int c = strip_bswap_rgb(col) | (colA >> 0x18);
                (void)z;
                gx_begin_primitive(0x98, 0, 4);
                GXF(cx + ey); GXF(cy + ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
                GXF(cx + ex); GXF(cy - ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
                GXF(cx - ex); GXF(cy + ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
                GXF(cx - ey); GXF(cy - ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
                DAT_80611570 = DAT_80611570 + 4;
                DAT_80611574 = DAT_80611574 + 2;
                DAT_80611578 = DAT_80611578 + 2;
            }
            off = off + 4;
        }
    }
    FUN_8017c670();
    FUN_80382730();
}

// particle_projected_sprite_render (0x801788a4)
// Projected sprite strip: per particle interpolates a world position along its
// basis by view-space depth, projects + frustum-clips, computes a depth-scaled
// screen size (clamped by DAT_804070c4) and a sine-rotated quad, with a two-zone
// (fade-in / fade-out) age fade.  Frame supplied by FUN_803826dc.
void particle_projected_sprite_render(void)
{
    int slot = (int)FUN_803826dc();
    int rec = *(int*)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    unsigned int col = *(unsigned int*)(&DAT_804070a8 + t9);
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();

    int posOff = 0, recOff = 0;
    for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1, posOff = posOff + 0xc, recOff = recOff + 4) {
        int pe = rec + recOff;
        double depth = (double)(*(float*)(slot + 0xc) - *(float*)(pe + 0xaf0));
        if (!((double)FLOAT_806165a4 <= depth)) continue;
        double far = (double)*(float*)(&DAT_804070ac + t9);
        if (!(depth < far)) continue;
        double curve = (double)(float)((double)*(float*)(pe + 0x4b0) * depth);
        double fade = (double)FLOAT_806165a8;
        if ((double)*(float*)(&DAT_804070b0 + t9) <= depth) {
            if ((double)(float)(far - (double)*(float*)(&DAT_804070b4 + t9)) < depth) {
                fade = (double)(float)((double)(float)(far - depth) / (double)*(float*)(&DAT_804070b4 + t9));
            }
        } else {
            fade = (double)(float)(depth / (double)*(float*)(&DAT_804070b0 + t9));
        }
        double curveAbs = (curve < (double)FLOAT_806165a4) ? -curve : curve;
        float* p = (float*)(rec + posOff);
        float wp[4], sp[4];
        wp[0] = (float)((double)p[400] * depth + (double)(float)((double)*(float*)(&DAT_80407108 + t9) * curve + (double)p[0]));
        wp[1] = (float)((double)p[0x191] * depth + (double)(float)((double)*(float*)(&DAT_8040710c + t9) * curveAbs + (double)p[1]));
        wp[2] = (float)((double)p[0x192] * depth + (double)(float)((double)*(float*)(&DAT_80407110 + t9) * curve + (double)p[2]));
        mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c), wp, sp);
        double w = (double)sp[3];
        if (!((-w <= (double)sp[0]) && ((double)sp[0] <= w) && (-w <= (double)sp[1]) &&
              ((double)sp[1] <= w) && (-w <= (double)sp[2]) && ((double)sp[2] <= w))) continue;
        unsigned int sizeCap = (unsigned int)(*(float*)(&DAT_804070c4 + t9) * *(float*)(DAT_80611d54 + 0x268));
        if (w < (double)FLOAT_806165a8) {
            if (w < (double)FLOAT_806165a4) continue;
            fade = (double)(float)(fade * w);
        }
        float invW = (float)((double)FLOAT_806165a8 / w);
        sp[0] = sp[0] * invW; sp[1] = sp[1] * invW; sp[2] = sp[2] * invW;
        float sizeScale = *(float*)(DAT_80611d54 + 0x268) * (float)((double)*(float*)(&DAT_804070bc + t9) / w);
        unsigned int szX = (unsigned int)(sizeScale * (float)(depth * (double)*(float*)(&DAT_804070d4 + t9) + (double)FLOAT_806165a8));
        unsigned int szY = (unsigned int)(sizeScale * (float)(depth * (double)*(float*)(&DAT_804070d8 + t9) + (double)FLOAT_806165a8));
        float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10)) - DOUBLE_806165d0);
        float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
        float cx = sc10 * sp[0] + sc08;
        float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
        float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
        float cy = sc24 * sp[1] + sc1c;
        float cz = *(float*)(&DAT_804070cc + t9) * *(float*)(DAT_80611748 + 0x40) +
                   *(float*)(DAT_80611748 + 0x38) * sp[2] + *(float*)(DAT_80611748 + 0x3c);
        if ((int)sizeCap < (int)szX) szX = sizeCap;
        if ((int)sizeCap < (int)szY) szY = sizeCap;
        unsigned int ang = (unsigned int)((double)FLOAT_806165b8 * curve + (double)FLOAT_806165ac);
        float ex = (float)((double)CONCAT44(0x43300000, szY ^ 0x80000000) - DOUBLE_806165d0) *
                   (float)(&DAT_80583228)[ang & 0x7ff];
        float ey = (float)((double)CONCAT44(0x43300000, szX ^ 0x80000000) - DOUBLE_806165d0) *
                   *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc));
        double bx = (double)(float)(cx + ey);
        double by = (double)(float)(cy + ex);
        unsigned int alpha = (unsigned int)(fade * (double)(float)((double)CONCAT44(0x43300000, col >> 0x18) - DOUBLE_806165c8));
        unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
        (void)cz;
        gx_begin_primitive(0x98, 0, 4);
        GXF((float)bx); GXF((float)by); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(cx + ex); GXF(cy - ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(cx - ex); GXF(cy + ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(cx - ey); GXF(cy - ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    }
    FUN_8017c670();
    FUN_80382728();
}

// particle_projected_sprite_strip_render (0x80178dc8)
// Projected sprite strip with a 16-word per-entry stride; optionally pre-multiplies
// the slot matrix into the projection.  Each entry is projected, frustum-clipped,
// perspective-divided, and drawn as a sine-rotated quad with a two-zone age fade.
// Frame supplied by FUN_803826d8.
void particle_projected_sprite_strip_render(void)
{
    unsigned long long frame = (unsigned long long)FUN_803826d8();
    int slot = (int)(frame >> 0x20);
    int rec = *(int*)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    unsigned int col = *(unsigned int*)(&DAT_804070a8 + t9);
    double age = (double)*(float*)(slot + 0xc);
    float projBuf[16];
    float* proj;
    if ((float*)(unsigned int)frame == 0) {
        proj = *(float**)(DAT_80611d54 + 0x20c);
    } else {
        mtx4_multiply((float*)(unsigned int)frame, *(float**)(DAT_80611d54 + 0x20c), projBuf);
        proj = projBuf;
    }
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();

    if (((double)FLOAT_806165a4 <= age) && (age < (double)*(float*)(&DAT_804070ac + t9))) {
        double far = (double)*(float*)(&DAT_804070ac + t9);
        double fade = (double)FLOAT_806165a8;
        if ((double)*(float*)(&DAT_804070b0 + t9) <= age) {
            if ((double)(float)(far - (double)*(float*)(&DAT_804070b4 + t9)) < age) {
                fade = (double)(float)((double)(float)(far - age) / (double)*(float*)(&DAT_804070b4 + t9));
            }
        } else {
            fade = (double)(float)(age / (double)*(float*)(&DAT_804070b0 + t9));
        }
        int recOff = 0, posOff = 0;
        for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1, recOff = recOff + 4, posOff = posOff + 0x10) {
            float sp[4];
            mtx4_transform_point4_alias(proj, (float*)(rec + posOff), sp);
            double w = (double)sp[3];
            if (!((-w <= (double)sp[0]) && ((double)sp[0] <= w) && (-w <= (double)sp[1]) &&
                  ((double)sp[1] <= w) && (-w <= (double)sp[2]) && ((double)sp[2] <= w))) continue;
            unsigned int sizeCap = (unsigned int)(*(float*)(&DAT_804070c4 + t9) * *(float*)(DAT_80611d54 + 0x268));
            double localFade = fade;
            if (!((double)FLOAT_806165a8 <= w)) {
                localFade = (double)(float)(fade * w);
                if (!((double)FLOAT_806165a4 <= w)) continue;
            }
            float invW = (float)((double)FLOAT_806165a8 / w);
            sp[0] = sp[0] * invW; sp[1] = sp[1] * invW; sp[2] = sp[2] * invW;
            float sizeScale = *(float*)(DAT_80611d54 + 0x268) * (float)((double)*(float*)(&DAT_804070bc + t9) / w);
            unsigned int szX = (unsigned int)(sizeScale * (float)(age * (double)*(float*)(&DAT_804070d4 + t9) + (double)FLOAT_806165a8));
            unsigned int szY = (unsigned int)(sizeScale * (float)(age * (double)*(float*)(&DAT_804070d8 + t9) + (double)FLOAT_806165a8));
            if ((int)sizeCap < (int)szX) szX = sizeCap;
            if ((int)sizeCap < (int)szY) szY = sizeCap;
            float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
            float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
            float cx = sc10 * sp[0] + sc08;
            float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
            float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
            float cy = sc24 * sp[1] + sc1c;
            unsigned int ang = (unsigned int)(FLOAT_806165b8 *
                               (float)((double)*(float*)(rec + recOff + 0xaf0) * age) + FLOAT_806165ac);
            float ex = (float)((double)CONCAT44(0x43300000, szY ^ 0x80000000) - DOUBLE_806165d0) *
                       (float)(&DAT_80583228)[ang & 0x7ff];
            float ey = (float)((double)CONCAT44(0x43300000, szX ^ 0x80000000) - DOUBLE_806165d0) *
                       *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc));
            unsigned int alpha = (unsigned int)(localFade * (double)(float)((double)CONCAT44(0x43300000, col >> 0x18) - DOUBLE_806165c8));
            unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
            gx_begin_primitive(0x98, 0, 4);
            GXF(cx + ey); GXF(cy + ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
            GXF(cx + ex); GXF(cy - ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
            GXF(cx - ex); GXF(cy + ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
            GXF(cx - ey); GXF(cy - ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
        }
    }
    FUN_8017c670();
    FUN_80382724();
}

// particle_projected_velocity_sprite_render (0x8017926c)
// Like the projected sprite strip but the quad rotation can be aligned to the
// particle's screen-space velocity (when slot flag 0x800000 is set) via
// math_atan2 of the transformed velocity vector.  Frame supplied by FUN_803826d0.
void particle_projected_velocity_sprite_render(void)
{
    unsigned long long frame = (unsigned long long)FUN_803826d0();
    int slot = (int)(frame >> 0x20);
    int rec = *(int*)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    unsigned int col = *(unsigned int*)(rec + 0xfa4);
    float projBuf[16];
    float* proj;
    if ((float*)(unsigned int)frame == 0) {
        proj = *(float**)(DAT_80611d54 + 0x20c);
    } else {
        mtx4_multiply((float*)(unsigned int)frame, *(float**)(DAT_80611d54 + 0x20c), projBuf);
        proj = projBuf;
    }
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();

    int posOff = 0, recOff = 0;
    for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1, posOff = posOff + 0x10, recOff = recOff + 4) {
        double near = (double)*(float*)(&DAT_804070b8 + t9);
        double depth = (double)(*(float*)(slot + 0xc) - *(float*)(rec + recOff + 0xc84));
        if (!((near <= depth) && (depth < (double)*(float*)(&DAT_804070ac + t9)))) continue;
        double far = (double)*(float*)(&DAT_804070ac + t9);
        double fade = (double)FLOAT_806165a8;
        if ((double)*(float*)(&DAT_804070b0 + t9) <= depth) {
            if ((double)(float)(far - (double)*(float*)(&DAT_804070b4 + t9)) < depth) {
                fade = (double)(float)((double)(float)(far - depth) / (double)*(float*)(&DAT_804070b4 + t9));
            }
        } else {
            fade = (double)((float)(depth - near) / (float)((double)*(float*)(&DAT_804070b0 + t9) - near));
        }
        float* p = (float*)(rec + posOff);
        float sp[4];
        mtx4_transform_point4_alias(proj, p, sp);
        float wclip = (float)((double)FLOAT_806165b0 * (double)sp[3]);
        if (!((-wclip <= sp[0]) && (sp[0] <= wclip) && (-wclip <= sp[1]) &&
              (sp[1] <= wclip) && (-(double)sp[3] <= (double)sp[2]) && ((double)sp[2] <= (double)sp[3]))) continue;
        float rot = (float)((double)*(float*)(rec + recOff + 0xe14) * depth);
        unsigned int sizeCap = (unsigned int)(*(float*)(&DAT_804070c4 + t9) * *(float*)(DAT_80611d54 + 0x268));
        double w = (double)sp[3];
        if (!((double)FLOAT_806165a8 <= w)) {
            fade = (double)(float)(fade * w);
            if (!((double)FLOAT_806165a4 <= w)) continue;
        }
        if ((*(unsigned int*)(slot + 8) & 0x800000) != 0) {
            float vv[4];
            vv[0] = p[400];
            vv[1] = FLOAT_8061662c + p[0x191];
            if (vv[1] < FLOAT_806165a8) vv[1] = FLOAT_806165a8;
            vv[2] = p[0x192];
            mtx4_transform_vector_alias(proj, vv);
            rot = FLOAT_80616630 + (float)math_atan2((double)vv[0], (double)vv[1]);
        }
        float invW = (float)((double)FLOAT_806165a8 / w);
        sp[0] = sp[0] * invW; sp[1] = sp[1] * invW; sp[2] = sp[2] * invW;
        unsigned int ang = (unsigned int)(FLOAT_806165b8 * rot + FLOAT_806165ac);
        float sizeScale = *(float*)(DAT_80611d54 + 0x268) * (float)((double)*(float*)(&DAT_804070bc + t9) / w);
        unsigned int szX = (unsigned int)(sizeScale * (float)(depth * (double)*(float*)(&DAT_804070d4 + t9) + (double)FLOAT_806165a8));
        unsigned int szY = (unsigned int)(sizeScale * (float)(depth * (double)*(float*)(&DAT_804070d8 + t9) + (double)FLOAT_806165a8));
        if ((int)sizeCap < (int)szX) szX = sizeCap;
        if ((int)sizeCap < (int)szY) szY = sizeCap;
        float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
        float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
        float cx = sc10 * sp[0] + sc08;
        float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
        float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
        float cy = sc24 * sp[1] + sc1c;
        float sinv = (float)(&DAT_80583228)[ang & 0x7ff];
        float cosv = *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc));
        float exX = (float)((double)CONCAT44(0x43300000, szX ^ 0x80000000) - DOUBLE_806165d0) * cosv;
        float eyX = (float)((double)CONCAT44(0x43300000, szX ^ 0x80000000) - DOUBLE_806165d0) * sinv;
        float exY = (float)((double)CONCAT44(0x43300000, szY ^ 0x80000000) - DOUBLE_806165d0) * cosv;
        float eyY = (float)((double)CONCAT44(0x43300000, szY ^ 0x80000000) - DOUBLE_806165d0) * sinv;
        unsigned int alpha = (unsigned int)(fade * (double)(float)((double)CONCAT44(0x43300000, col >> 0x18) - DOUBLE_806165c8));
        unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
        gx_begin_primitive(0x98, 0, 4);
        GXF(cx + exX); GXF(cy + eyX); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(cx + eyY); GXF(cy - exY); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(cx - eyY); GXF(cy + exY); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(cx - exX); GXF(cy - eyX); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    }
    FUN_8017c670();
    FUN_8038271c();
}

// particle_matrix_sprite_strip_render (0x801797e4)
// Projected sprite strip that first concatenates the slot's animation matrix into
// the projection (two mtx4_multiply passes) before transforming each entry's
// cached position (pfVar11[iVar7 + 0x40..0x48]).  Otherwise identical projection /
// clip / sine-rotated-quad shape.  Frame supplied by __save_gpr_25.
void particle_matrix_sprite_strip_render(void)
{
    unsigned long long frame = (unsigned long long)__save_gpr_25();
    int slot = (int)(frame >> 0x20);
    float* rec = *(float**)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    float projTmp[16];
    float projFull[17];
    float* proj;
    if ((float*)(unsigned int)frame == 0) {
        proj = *(float**)(DAT_80611d54 + 0x20c);
    } else {
        mtx4_multiply((float*)(unsigned int)frame, *(float**)(DAT_80611d54 + 0x20c), projTmp);
        proj = projTmp;
    }
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();
    mtx4_multiply(rec, proj, projFull);

    int recOff = 0, iterOff = 0;
    for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1, iterOff = iterOff + 4, recOff = recOff + 0xc) {
        double age = (double)*(float*)(slot + 0xc);
        double far = (double)*(float*)(&DAT_804070ac + t9);
        if (!(((double)FLOAT_806165a4 <= age) && (((double)FLOAT_806165a4 == far) || (age <= far)))) continue;
        double fade = (double)FLOAT_806165a8;
        if ((double)FLOAT_806165a4 < far) {
            if ((double)*(float*)(&DAT_804070b0 + t9) <= age) {
                if ((double)(float)(far - (double)*(float*)(&DAT_804070b4 + t9)) < age) {
                    fade = (double)(float)((double)(float)(far - age) / (double)*(float*)(&DAT_804070b4 + t9));
                }
            } else {
                fade = (double)(float)(age / (double)*(float*)(&DAT_804070b0 + t9));
            }
        }
        float wp[4], sp[4];
        wp[0] = *(float*)((int)rec + recOff + 0x40);
        wp[1] = *(float*)((int)rec + recOff + 0x44);
        wp[2] = *(float*)((int)rec + recOff + 0x48);
        mtx4_transform_point4_alias(projFull, wp, sp);
        double w = (double)sp[3];
        if (!((-w <= (double)sp[0]) && ((double)sp[0] <= w) && (-w <= (double)sp[1]) &&
              ((double)sp[1] <= w) && (-w <= (double)sp[2]) && ((double)sp[2] <= w))) continue;
        unsigned int col = *(unsigned int*)((int)rec + iterOff + 0x4f0);
        unsigned int sizeCap = (unsigned int)((double)*(float*)(&DAT_804070c4 + t9) * (double)*(float*)(DAT_80611d54 + 0x268));
        if (!((double)FLOAT_806165a8 <= w)) {
            fade = (double)(float)(fade * w);
            if (!((double)FLOAT_806165a4 <= w)) continue;
        }
        float sizeScale = (float)((double)*(float*)(DAT_80611d54 + 0x268) / w);
        unsigned int szX = (unsigned int)(*(float*)(&DAT_804070bc + t9) * sizeScale *
                           (float)(age * (double)*(float*)(&DAT_804070d4 + t9) + (double)FLOAT_806165a8));
        unsigned int szY = (unsigned int)(*(float*)(&DAT_804070c0 + t9) * sizeScale *
                           (float)(age * (double)*(float*)(&DAT_804070d8 + t9) + (double)FLOAT_806165a8));
        unsigned int alpha = (unsigned int)(fade * (double)(float)((double)CONCAT44(0x43300000, col >> 0x18) - DOUBLE_806165c8));
        if ((int)sizeCap < (int)szX) szX = sizeCap;
        if ((int)sizeCap < (int)szY) szY = sizeCap;
        float invW = (float)((double)FLOAT_806165a8 / w);
        sp[0] = sp[0] * invW; sp[1] = sp[1] * invW; sp[2] = sp[2] * invW;
        unsigned int ang = (unsigned int)(FLOAT_806165b8 *
                           (float)((double)*(float*)((int)rec + iterOff + 0x680) * age + (double)rec[0x204]) + FLOAT_806165ac);
        float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
        float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
        float cx = sc10 * sp[0] + sc08;
        float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
        float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
        float cy = sc24 * sp[1] + sc1c;
        float sinv = (float)(&DAT_80583228)[ang & 0x7ff];
        float cosv = *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc));
        float ex = (float)((double)CONCAT44(0x43300000, szX ^ 0x80000000) - DOUBLE_806165d0) * sinv;
        float ey = (float)((double)CONCAT44(0x43300000, szX ^ 0x80000000) - DOUBLE_806165d0) * cosv;
        unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
        gx_begin_primitive(0x98, 0, 4);
        GXF(cx + ey); GXF(cy + ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(cx + ex); GXF(cy - ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(cx - ex); GXF(cy + ey); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(cx - ey); GXF(cy - ex); GXF(FLOAT_806165a8 + sp[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    }
    FUN_8017c670();
    __restore_gpr_25();
}

// particle_ribbon_quad_render (0x80179d08)
// Camera-facing quad ribbon built from paired endpoints (rec[0] and rec[4], stride
// 0x20).  Both endpoints are projected + frustum-clipped, perspective-divided, then
// the quad is extruded perpendicular to the screen-space edge by a width scale
// (DAT_804070d0).  Frame supplied by __save_gpr_25.
void particle_ribbon_quad_render(void)
{
    unsigned long long frame = (unsigned long long)__save_gpr_25();
    int slot = (int)(frame >> 0x20);
    int rec = *(int*)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    unsigned int col = *(unsigned int*)(&DAT_804070a8 + t9);
    double width = (double)*(float*)(&DAT_804070d0 + t9);
    unsigned int alpha = (unsigned int)FUN_803825c0((double)((float)((double)CONCAT44(0x43300000, col >> 0x18) - DOUBLE_806165c8) *
                          ((*(float*)(&DAT_804070ac + t9) - *(float*)(slot + 0xc)) / *(float*)(&DAT_804070ac + t9))));
    float projBuf[16];
    float* proj;
    if ((float*)(unsigned int)frame == 0) {
        proj = *(float**)(DAT_80611d54 + 0x20c);
    } else {
        mtx4_multiply((float*)(unsigned int)frame, *(float**)(DAT_80611d54 + 0x20c), projBuf);
        proj = projBuf;
    }
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();

    int off = 0;
    for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1, off = off + 0x20) {
        float* p = (float*)(rec + off);
        float s0[4], s1[4];
        mtx4_transform_point4_alias(proj, p, s0);
        mtx4_transform_point4_alias(proj, p + 4, s1);
        if (!((-s0[3] <= s0[0]) && (s0[0] <= s0[3]) && (-s0[3] <= s0[1]) && (s0[1] <= s0[3]) &&
              (-s0[3] <= s0[2]) && (s0[2] <= s0[3]) &&
              (-s1[3] <= s1[0]) && (s1[0] <= s1[3]) && (-s1[3] <= s1[1]) && (s1[1] <= s1[3]) &&
              (-s1[3] <= s1[2]) && (s1[2] <= s1[3]))) continue;
        float inv0 = FLOAT_806165a8 / s0[3];
        float inv1 = FLOAT_806165a8 / s1[3];
        s0[0] = s0[0] * inv0; s0[1] = s0[1] * inv0; s0[2] = s0[2] * inv0;
        s1[0] = s1[0] * inv1; s1[1] = s1[1] * inv1; s1[2] = s1[2] * inv1;
        float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
        float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
        float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
        float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
        float x0 = sc10 * s0[0] + sc08;
        float y0 = sc24 * s0[1] + sc1c;
        float x1 = sc10 * s1[0] + sc08;
        float y1 = sc24 * s1[1] + sc1c;
        float hx = FLOAT_806165ac * (float)(width * (double)(x1 - x0));
        float hy = FLOAT_806165ac * (float)(width * (double)(y1 - y0));
        unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
        gx_begin_primitive(0x98, 0, 4);
        GXF(x0 - hy); GXF(y0 + hx); GXF(FLOAT_806165a8 + s0[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(x1 - hy); GXF(y1 + hx); GXF(FLOAT_806165a8 + s1[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(x0 + hy); GXF(y0 - hx); GXF(FLOAT_806165a8 + s0[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(x1 + hy); GXF(y1 - hx); GXF(FLOAT_806165a8 + s1[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    }
    FUN_8017c670();
    __restore_gpr_25();
}

// particle_ribbon_quad_group_render (0x8017a16c)
// Same paired-endpoint quad ribbon as particle_ribbon_quad_render, but driven from
// a group frame (FUN_803826d4) with a per-entry depth cull (rec + 0x1134) and a
// FUN_803825c0 age->alpha fade.  Endpoints at (rec+4) and (rec+0x14), stride 0x20.
void particle_ribbon_quad_group_render(void)
{
    unsigned long long frame = (unsigned long long)FUN_803826d4();
    int slot = (int)(frame >> 0x20);
    int rec = *(int*)(slot + 0x18);
    int t9 = *(int*)(slot + 4) * 0x90;
    int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
    unsigned int col = *(unsigned int*)(&DAT_804070a8 + t9);
    double width = (double)*(float*)(&DAT_804070d0 + t9);
    float projBuf[16];
    float* proj;
    if ((float*)(unsigned int)frame == 0) {
        proj = *(float**)(DAT_80611d54 + 0x20c);
    } else {
        mtx4_multiply((float*)(unsigned int)frame, *(float**)(DAT_80611d54 + 0x20c), projBuf);
        proj = projBuf;
    }
    gx_set_active_material(tex);
    FUN_8017c658();
    FUN_8017fab4();

    int posOff = 0, recOff = 0;
    for (int i = 0; i < *(int*)(slot + 0x1c); i = i + 1, posOff = posOff + 0x20, recOff = recOff + 4) {
        double depth = (double)(*(float*)(slot + 0xc) - *(float*)(rec + recOff + 0x1134));
        if (!(((double)FLOAT_806165a4 <= depth) && (depth < (double)*(float*)(&DAT_804070ac + t9)))) continue;
        int e = rec + posOff;
        float s0[4], s1[4];
        mtx4_transform_point4_alias(proj, (float*)(e + 4), s0);
        mtx4_transform_point4_alias(proj, (float*)(e + 0x14), s1);
        if (!((-s0[3] <= s0[0]) && (s0[0] <= s0[3]) && (-s0[3] <= s0[1]) && (s0[1] <= s0[3]) &&
              (-s0[3] <= s0[2]) && (s0[2] <= s0[3]) &&
              (-s1[3] <= s1[0]) && (s1[0] <= s1[3]) && (-s1[3] <= s1[1]) && (s1[1] <= s1[3]) &&
              (-s1[3] <= s1[2]) && (s1[2] <= s1[3]))) continue;
        unsigned int alpha = (unsigned int)FUN_803825c0((double)((float)((double)CONCAT44(0x43300000, col >> 0x18) - DOUBLE_806165c8) *
                             (float)((double)(float)((double)*(float*)(&DAT_804070ac + t9) - depth) /
                                    (double)*(float*)(&DAT_804070ac + t9))));
        float inv0 = (float)((double)FLOAT_806165a8 / (double)s0[3]);
        float inv1 = (float)((double)FLOAT_806165a8 / (double)s1[3]);
        s0[0] = s0[0] * inv0; s0[1] = s0[1] * inv0; s0[2] = s0[2] * inv0;
        s1[0] = s1[0] * inv1; s1[1] = s1[1] * inv1; s1[2] = s1[2] * inv1;
        float sc10 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_806165d0);
        float sc08 = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_806165d0);
        float sc24 = (float)((double)CONCAT44(0x43300000, (-*(int*)(DAT_80611748 + 0x24)) ^ 0x80000000) - DOUBLE_806165d0);
        float sc1c = (float)((double)CONCAT44(0x43300000, *(unsigned int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_806165d0);
        float x0 = sc10 * s0[0] + sc08;
        float y0 = sc24 * s0[1] + sc1c;
        float x1 = sc10 * s1[0] + sc08;
        float y1 = sc24 * s1[1] + sc1c;
        float hy = FLOAT_806165ac * (float)(width * (double)(x1 - x0));
        float hx = FLOAT_806165ac * (float)(width * (double)(y1 - y0));
        unsigned int c = strip_bswap_rgb(col) | (alpha & 0xff);
        gx_begin_primitive(0x98, 0, 4);
        GXF(x0 - hx); GXF(y0 + hy); GXF(FLOAT_806165a8 + s0[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(x1 - hx); GXF(y1 + hy); GXF(FLOAT_806165a8 + s1[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(x0 + hx); GXF(y0 - hy); GXF(FLOAT_806165a8 + s0[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(x1 + hx); GXF(y1 - hy); GXF(FLOAT_806165a8 + s1[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    }
    FUN_8017c670();
    FUN_80382720();
}


// ===========================================================================
// batch_459 (part 1/2) : particle mesh/fan renderers
//   particle_refraction_mesh_render  0x8017a838  heat-haze distortion mesh
//   particle_rotor_fan_render        0x8017a9f8  rotating 4x billboard fan
// ===========================================================================

extern void FUN_8039955c(int dst, int len);   // GX display-list close/flush
extern void mtx4_set_rotation_y(double angle, float* out);
extern void mtx4_write_translation(double x, double y, double z, int mtx);
// mtx4_set_scale's shared prototype is (float* mtx, f,f,f); this call site passes
// 3 doubles into an implicit current matrix, so route through a typed alias.
typedef void (*mtx4_set_scale3_fn)(double, double, double);
static const mtx4_set_scale3_fn __mtx4_set_scale3 = (mtx4_set_scale3_fn)&mtx4_set_scale;

extern float FLOAT_80616634;
extern int   DAT_80612250, DAT_80612258;
extern unsigned char DAT_8058e3cc;   // transposed model->view matrix scratch
extern int   DAT_804070a0;           // per-type sub-material id

// particle_refraction_mesh_render (0x8017a838)
// Heat-haze / refraction distortion mesh.  Builds three GX display lists (position
// 0x1400, colour 0x5000, texcoord 0x2800) sized by the mesh tri count (rec[0x6190]),
// binds them as indirect matrices, then per tri transforms the model matrix into
// view space (mtx4_multiply + transpose into DAT_8058e3cc) and emits a 4-vertex
// quad with byte-swapped colours read from the colour list.  Frame FUN_803826dc.
void particle_refraction_mesh_render(void)
{
    int slot = (int)FUN_803826dc();
    int rec = *(int*)(slot + 0x18);
    if (*(int*)(rec + 0x6190) != 0) {
        FUN_8017fab4();
        FUN_8039955c(rec + 0x1400, *(int*)(rec + 0x6190) << 6);
        FUN_8039955c(rec + 0x5000, *(int*)(rec + 0x6190) << 4);
        FUN_8039955c(rec + 0x2800, *(int*)(rec + 0x6190) << 6);
        gx_indirect_matrix_update(9, rec + 0x1400, 0x10);
        gx_indirect_matrix_update(0xd, rec + 0x2800, 0x10);
    }
    float mv[26];
    int mtxOff = 0, colOff = 0;
    short idx = 0;
    for (int i = 0; i < *(int*)(rec + 0x6190); i = i + 1, mtxOff = mtxOff + 0x40, colOff = colOff + 0x10, idx = idx + 4) {
        mtx4_multiply((float*)(rec + mtxOff), *(float**)(DAT_80611d54 + 0x208), mv);
        mtx4_transpose(mv, (float*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        if (i < 2) {
            int tex = *(int*)(rec + 0x6188);
            if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                gx_bind_material_fallback();
            } else {
                DAT_80612254 = *DAT_80610a20 + tex * 0x58;
            }
        }
        int ce = rec + colOff;
        gx_begin_primitive(0x98, 0, 4);
        GXH(idx);     GXU(gx_bswap32(*(unsigned int*)(ce + 0x5000)));
        GXH(idx + 1); GXU(gx_bswap32(*(unsigned int*)(ce + 0x5004)));
        GXH(idx + 2); GXU(gx_bswap32(*(unsigned int*)(ce + 0x5008)));
        GXH(idx + 3); GXU(gx_bswap32(*(unsigned int*)(ce + 0x500c)));
    }
    FUN_80382728();
}

// particle_rotor_fan_render (0x8017a9f8)
// Rotating billboard fan: emits four copies of an 8-quad strip, each rotated about
// Y by an evenly-spaced angle (mtx4_set_rotation_y), scaled by the particle's
// stored scale (pfVar12[0x43..0x45]) and translated to the slot position.  Age fade
// drives two alpha bytes (top and mid) via FUN_803825c0.  Frame FUN_803826d4.
void particle_rotor_fan_render(void)
{
    int slot = (int)FUN_803826d4();
    int t9 = *(int*)(slot + 4) * 0x90;
    float* rec = *(float**)(slot + 0x18);
    float far = *(float*)(&DAT_804070ac + t9);
    float fade = FLOAT_806165a8;
    if (FLOAT_806165a4 < far) {
        float age = *(float*)(slot + 0xc);
        if (*(float*)(&DAT_804070b0 + t9) <= age) {
            if (far - *(float*)(&DAT_804070b4 + t9) < age) {
                fade = (far - age) / *(float*)(&DAT_804070b4 + t9);
            }
        } else {
            fade = age / *(float*)(&DAT_804070b0 + t9);
        }
    }
    unsigned int col = *(unsigned int*)(&DAT_804070a8 + t9);
    double fadeA = (double)((float)((double)CONCAT44(0x43300000, col >> 0x18 & 0x7f) - DOUBLE_806165c8) * fade);
    unsigned int alphaTop = (unsigned int)FUN_803825c0(fadeA);
    unsigned int alphaMid = (unsigned int)FUN_803825c0((double)(float)((double)rec[0x4b] * fadeA));
    gx_set_sub_material_id(*(int*)(&DAT_804070a0 + t9));
    FUN_8017fab4();
    __mtx4_set_scale3((double)rec[0x43], (double)rec[0x44], (double)rec[0x45]);

    float rotBuf[16], scaleBuf[16], mvBuf[16];
    double kSpin = (double)FLOAT_80616634;
    double kAC = (double)FLOAT_806165ac;
    double kStep = (double)FLOAT_80616624;
    double kBias = DOUBLE_806165d0;
    for (unsigned int r = 0; (int)r < 4; r = r + 1) {
        mtx4_copy(rec, mvBuf);
        double angle = (double)(float)(kAC * (double)(float)(kStep * (double)(float)((double)CONCAT44(0x43300000, r ^ 0x80000000) - kBias)) + (double)rec[0x49]);
        mtx4_set_rotation_y(angle, rotBuf);
        mtx4_multiply(rotBuf, mvBuf, mvBuf);
        mtx4_multiply(scaleBuf, mvBuf, mvBuf);
        mtx4_write_translation((double)*(float*)(slot + 0x20), (double)*(float*)(slot + 0x24), (double)*(float*)(slot + 0x28), (int)mvBuf);
        float mv[16];
        mtx4_multiply(mvBuf, *(float**)(DAT_80611d54 + 0x208), mv);
        mtx4_transpose(mv, (float*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        int tex = (int)sprite_get_texture_id(*(int*)(&DAT_8040709c + t9));
        if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *DAT_80610a20 + tex * 0x58;
        }
        gx_begin_primitive(0x98, 0, 0x10);
        int vp = 0;
        for (unsigned int q = 0; q < 8; q = q + 1, vp = vp + 2) {
            float v = (float)((double)(float)((double)CONCAT44(0x43300000, q ^ 0x80000000) - kBias) * kSpin);
            GXF(rec[vp * 3 + 0x10]); GXF(rec[vp * 3 + 0x11]); GXF(rec[vp * 3 + 0x12]);
            GXU(strip_bswap_rgb(col) | (alphaTop & 0xff)); GXF(v); GXF(FLOAT_806165a4);
            GXF(rec[vp * 3 + 0x13]); GXF(rec[vp * 3 + 0x14]); GXF(rec[vp * 3 + 0x15]);
            GXU(strip_bswap_rgb(col) | (alphaMid & 0xff)); GXF(v); GXF(FLOAT_806165a8);
        }
    }
    FUN_80382720();
}
