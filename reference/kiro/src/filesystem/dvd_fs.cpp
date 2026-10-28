// TSFP GameCube DVD file-system API layer (Dolphin SDK DVD, from main.dol).
//
// Address: 0x803a2504 | Name: DVDConvertPathToEntrynum   | Date: 2026-09-15
// Address: 0x803a27f8 | Name: dvd_open_entrynum          | Date: 2026-09-15
// Address: 0x803a286c | Name: DVDOpen                    | Date: 2026-09-15
// Address: 0x803a2b7c | Name: DVDReadAsync               | Date: 2026-09-15
// Address: 0x803a2cdc | Name: DVDInit                    | Date: 2026-09-15
// Address: 0x803a2db4 | Name: DVDChangeDisk              | Date: 2026-09-15
// Address: 0x803a5aec | Name: dvd_read_bootinfo_blocking | Date: 2026-09-15
//
// This is the public file-system face of the DVD subsystem, sitting on top of
// the command engine in dvd_driver.cpp. It walks the on-disc File System Table
// (FST) to turn a path into a file entry, fills DVDFileInfo blocks, submits
// async reads, and performs one-time DVD/DSP initialisation.
//
// FST layout (Dolphin standard): the entry array begins at DAT_80613164, each
// entry is 0xC bytes: word +0 packs an is-dir flag in the top byte with the
// name offset into the string table (DAT_80613168) in the low 24 bits; for a
// file, word +4 is the disc start offset and word +8 is the length; for a
// directory, word +4 is the parent index and word +8 is the one-past-last
// child index. DAT_8061316c is the total entry count.
//
// DVDFileInfo (the caller command block): the resolved file lives at +0x30
// (disc start), +0x34 (length), +0x38 (position), and +0xC (command-block
// state) is reset to 0. The file length is also read as word index [0xd] and
// the callback stored at word index [0xe].
//
// NON-MATCHING: OSReport/OSPanic/DVDOpen/DVDReadAsync keep their clean SDK
// signatures (the decompiler's r3..r10 pass-through args are variadic-ABI
// artifacts and are dropped); low-memory globals, the FST, and the DVD state
// block are reached through per-file externs; control flow is preserved.

#include "common.h"

extern "C" {

// OSReport / DVDOpen / DVDReadAsync are already declared in sdk_stubs.h
// (pulled in via common.h); do NOT re-declare them here. OSPanic is not, so
// declare it with its clean variadic signature.
extern void OSPanic(const char *file, int line, const char *fmt, ...);

// ---- low-memory boot-info globals (0x8000000x) ----
extern unsigned char DAT_80000000;   // game code byte 0
extern unsigned char DAT_80000001;   // game code byte 1
extern unsigned char DAT_80000002;   // game code byte 2
extern unsigned char DAT_80000003;   // game code byte 3
extern unsigned char DAT_80000004;   // company code byte 0
extern unsigned char DAT_80000005;   // company code byte 1
extern unsigned char DAT_80000006;   // disk number
extern unsigned char DAT_80000007;   // game version
extern char          DAT_80000008;   // streaming enable flag
extern int           DAT_80000038;   // boot-info: FST pointer (published here)
extern int           DAT_8000003c;   // boot-info: FST max length (published here)

// ---- FST cache (DVD driver state block) ----
extern int          DAT_80613164;   // FST entry array base
extern int          DAT_80613168;   // FST string table base
extern unsigned int DAT_8061316c;   // FST entry count
extern unsigned int DAT_80613170;   // current working entry index (root=0)
extern int          DAT_80613174;   // "strict path validation disabled" flag

// ---- DVD subsystem state ----
extern int          DAT_806131c4;   // "DVDInit already ran" latch
extern int          DAT_806131c0;   // pending sub-state latch
extern int          DAT_806131bc;   // "non-standard disc magic" flag
extern int          DAT_80613188;   // boot-info scratch pointer
extern int          DAT_80613184;   // FST valid sentinel copy
extern int          DAT_806131dc;   // dvd_read_bootinfo_blocking: boot-info dest
extern void        *DAT_806131e0;   // dvd_read_bootinfo_blocking: id-read dest
extern void        *DAT_806131c8;   // change-disk in-progress callback marker
extern int          DAT_80613178;   // DVD id-read wait thread queue

// ---- change-disk FST parameters ----
extern unsigned int DAT_805f3568;   // new-disc FST length
extern int          DAT_805f3564;   // new-disc FST disc offset
extern int          DAT_805f3658;   // disc-id read scratch buffer
extern char         DAT_805f3620;   // boot-info dest block base

// ---- AI/DI control registers ----
extern volatile unsigned int DAT_cc006000;  // AI main control register
extern volatile unsigned int DAT_cc006004;  // AI secondary control register

// ---- raw string/format literals ----
extern char DAT_80611408;                                  // panic file name
extern char DAT_8061141c;                                  // panic file name (changedisk)
extern char DAT_80611410;                                  // OSRegisterVersion string
extern char DAT_80611430;                                  // OSReport separator line
extern char DAT_80611434;                                  // "no" literal
extern char DAT_80611438;                                  // "yes" literal
extern char s_ODVDConvertEntrynumToPath_possib_80499e77;   // panic message
extern char s_Warning__DVDOpen____file___s__wa_80499f40;   // DVDOpen warning fmt
extern char s_DVDReadAsync____specified_area_i_80499f78;   // DVDReadAsync panic fmt
extern char s_load_fst_80499ff8;                           // OSReport "load_fst"
extern char s_DVDChangeDisk____FST_in_the_new_d_8049a004;  // changedisk panic fmt
extern char s_Game_Name______c_c_c_c_8049a530;             // OSReport game name fmt
extern char s_Company________c_c_8049a54c;                 // OSReport company fmt
extern char s_Disk___________d_8049a564;                   // OSReport disk fmt
extern char s_Game_ver_______d_8049a578;                   // OSReport version fmt
extern char s_Streaming______s_8049a58c;                   // OSReport streaming fmt

// ---- helpers from other translation units (resolve via generated decls) ----
extern int  msl_tolower_map(int c);
extern void dvd_fst_entry_to_path(int buf, int bufLen);
extern void dvd_submit_command(unsigned int *fileInfo, int dest, int length,
                               int discOffset, void *cancelCb, int userCb);
extern void dvd_cmd_invoke_cancel_cb(void);
extern void os_register_version(char *ver);
extern void dvd_fst_setup(void);
extern void dvd_queue_init(void);
extern void dsp_init(void);
extern void dsp_reset_handler(void);
extern void os_set_interrupt_handler(int irq, void *handler);
extern void os_unmask_interrupts(unsigned int mask);
extern int  os_init_thread_queue(int *queue);
extern void dvd_reset_di(void);
extern void dvd_submit_read_diskid(unsigned int *dest, void *buf, void *cb);
extern void dvd_bootinfo_read_step(void);
extern int  dvd_get_drive_status(void);
extern void os_get_arena_hi(void);
extern void os_set_arena_hi(int hi);
extern int  leaf_31e8(int a, unsigned int b, int c);
extern void aram_dma_dispatch(int src, unsigned int mainDest, int discOffset,
                              void *completionCb);
extern void dvd_state_advance(void);   // DVD interrupt dispatcher (dvd_driver.cpp)

// Forward decl: DVDInit's debug-fst fallback path calls this, defined below.
void dvd_read_bootinfo_blocking(void);

// ============================================================================
// DVDConvertPathToEntrynum (0x803a2504): resolve a slash-delimited path against
// the FST, returning the file/dir entry index, or 0xffffffff if not found.
//
// Starts from the cached working entry (DAT_80613170, normally the root=0).
// Leading '/' resets to root; "." stays put; ".." follows the parent link at
// entry+4; a plain name scans the current directory's child range doing a
// case-insensitive compare through msl_tolower_map. When DAT_80613174 is 0 the
// stock validation runs (rejecting names > 8 chars, > 3-char extensions, and
// embedded spaces) and panics on a malformed component.
// ============================================================================
unsigned int DVDConvertPathToEntrynum(char *path)
{
    char cVar1;
    char cVar2;
    bool matched;
    bool malformed;
    unsigned int uVar5;
    unsigned int *puVar6;
    unsigned int uVar7;
    char *pcVar8;
    char *pcVar9;
    char *nameStart = 0;
    unsigned int entry;
    char *comp;
    int iVar13;

    char *cur = path;
    entry = DAT_80613170;
LAB_803a2528:
    while (true) {
        while (true) {
            cVar1 = *cur;
            if (cVar1 == '\0') {
                return entry;
            }
            if (cVar1 != '/') break;
            entry = 0;
            cur = cur + 1;
        }
        if (cVar1 != '.') break;
        cVar1 = cur[1];
        if (cVar1 == '.') {
            if (cur[2] != '/') {
                if (cur[2] == '\0') {
                    return *(unsigned int *)(DAT_80613164 + entry * 0xc + 4);
                }
                break;
            }
            entry = *(unsigned int *)(DAT_80613164 + entry * 0xc + 4);
            cur = cur + 3;
        }
        else {
            if (cVar1 != '/') {
                if (cVar1 == '\0') {
                    return entry;
                }
                break;
            }
            cur = cur + 2;
        }
    }

    comp = cur;
    if (DAT_80613174 == 0) {
        matched = false;
        malformed = false;
LAB_803a262c:
        cVar1 = *comp;
        if ((cVar1 == '\0') || (cVar1 == '/')) goto LAB_803a2644;
        if (cVar1 == '.') {
            if ((8 < (int)(comp - cur)) || (matched)) {
                malformed = true;
LAB_803a2644:
                if ((matched) && (3 < (int)(comp - nameStart))) {
                    malformed = true;
                }
                if (malformed) {
                    OSPanic(&DAT_80611408, 0x183,
                            &s_ODVDConvertEntrynumToPath_possib_80499e77 + 1,
                            path);
                }
                goto LAB_803a26a4;
            }
            nameStart = comp + 1;
            matched = true;
        }
        else if (cVar1 == ' ') {
            malformed = true;
        }
        comp = comp + 1;
        goto LAB_803a262c;
    }
    for (; (*comp != '\0' && (*comp != '/')); comp = comp + 1) {
    }

LAB_803a26a4:
    cVar1 = *comp;
    iVar13 = entry * 0xc;
    entry = entry + 1;
LAB_803a27ac:
    if (*(unsigned int *)(iVar13 + DAT_80613164 + 8) <= entry) {
        return 0xffffffff;
    }
    uVar7 = *(unsigned int *)(DAT_80613164 + entry * 0xc);
    if (((uVar7 & 0xff000000) != 0) || (cVar1 == '\0')) {
        pcVar8 = (char *)(DAT_80613168 + (uVar7 & 0xffffff));
        pcVar9 = cur;
        do {
            if (*pcVar8 == '\0') {
                matched = ((*pcVar9 == '/') || (*pcVar9 == '\0'));
                goto LAB_803a276c;
            }
            cVar2 = *pcVar8;
            pcVar8 = pcVar8 + 1;
            uVar7 = (unsigned int)msl_tolower_map((int)cVar2);
            cVar2 = *pcVar9;
            pcVar9 = pcVar9 + 1;
            uVar5 = (unsigned int)msl_tolower_map((int)cVar2);
        } while (uVar5 == uVar7);
        matched = false;
LAB_803a276c:
        if (matched) goto LAB_803a27c8;
    }
    puVar6 = (unsigned int *)(DAT_80613164 + entry * 0xc);
    if ((*puVar6 & 0xff000000) == 0) {
        entry = entry + 1;
    }
    else {
        entry = puVar6[2];
    }
    goto LAB_803a27ac;
LAB_803a27c8:
    if (cVar1 == '\0') {
        return entry;
    }
    cur = comp + 1;
    goto LAB_803a2528;
}

// ============================================================================
// dvd_open_entrynum (0x803a27f8): fill a DVDFileInfo from an FST file entry
// index (start/length at entry+4/+8), rejecting out-of-range or directory
// entries. Core of DVDFastOpen. Returns 1 on success, 0 otherwise.
// ============================================================================
int dvd_open_entrynum(unsigned int entrynum, int fileInfo)
{
    int iVar1;

    if (((-1 < (int)entrynum) && (entrynum < DAT_8061316c)) &&
        (iVar1 = entrynum * 0xc,
         (*(unsigned int *)(DAT_80613164 + iVar1) & 0xff000000) == 0)) {
        *(int *)(fileInfo + 0x30) = *(int *)(DAT_80613164 + iVar1 + 4);
        *(int *)(fileInfo + 0x34) = *(int *)(DAT_80613164 + iVar1 + 8);
        *(int *)(fileInfo + 0x38) = 0;
        *(int *)(fileInfo + 0xc) = 0;
        return 1;
    }
    return 0;
}

// ============================================================================
// DVDOpen (0x803a286c): resolve a path to an FST entry then fill the caller's
// DVDFileInfo; warns via OSReport and returns 0 on a miss or a directory.
// (Clean SDK signature; the raw block pointer is taken as int here.)
// ============================================================================
BOOL DVDOpen(const char *fileName, DVDFileInfo fileInfoPtr)
{
    unsigned int entry;
    int result;
    int iVar3;
    int fileInfo = (int)(intptr_t)fileInfoPtr;
    unsigned char pathBuf[128];

    entry = DVDConvertPathToEntrynum((char *)fileName);
    if ((int)entry < 0) {
        dvd_fst_entry_to_path((int)(intptr_t)pathBuf, 0x80);
        OSReport(&s_Warning__DVDOpen____file___s__wa_80499f40, fileName, pathBuf);
        result = 0;
    }
    else {
        iVar3 = entry * 0xc;
        if ((*(unsigned int *)(DAT_80613164 + iVar3) & 0xff000000) == 0) {
            result = 1;
            *(int *)(fileInfo + 0x30) = *(int *)(DAT_80613164 + iVar3 + 4);
            *(int *)(fileInfo + 0x34) = *(int *)(DAT_80613164 + iVar3 + 8);
            *(int *)(fileInfo + 0x38) = 0;
            *(int *)(fileInfo + 0xc) = 0;
        }
        else {
            result = 0;
        }
    }
    return result;
}

// ============================================================================
// DVDReadAsync (0x803a2b7c): bounds-check the requested [offset,offset+len)
// against the file length (word [0xd]) then submit an async read command with
// the caller callback stored at word [0xe]. Panics on an out-of-range region.
// Always returns 1. (Clean SDK signature.)
// ============================================================================
BOOL DVDReadAsync(DVDFileInfo fileInfoPtr, void *addr, s32 length, s32 offset,
                  DVDCallback callback)
{
    unsigned int *fileInfo = (unsigned int *)fileInfoPtr;
    unsigned int uoff = (unsigned int)offset;
    int userCb = (int)(intptr_t)callback;

    if (-1 < offset) {
        if (uoff <= (unsigned int)fileInfo[0xd]) goto LAB_803a2bcc;
    }
    OSPanic(&DAT_80611408, 0x2ee,
            &s_DVDReadAsync____specified_area_i_80499f78, uoff, userCb);
LAB_803a2bcc:
    if (((int)(uoff + length) < 0) ||
        (fileInfo[0xd] + 0x20 <= uoff + length)) {
        OSPanic(&DAT_80611408, 0x2f4,
                &s_DVDReadAsync____specified_area_i_80499f78, uoff, userCb);
    }
    fileInfo[0xe] = (unsigned int)userCb;
    dvd_submit_command(fileInfo, (int)(intptr_t)addr, length,
                       fileInfo[0xc] + uoff, (void *)dvd_cmd_invoke_cancel_cb,
                       userCb);
    return 1;
}

// ============================================================================
// DVDInit (0x803a2cdc): one-time DVD subsystem init. Registers the SDK version,
// caches the FST, initialises the command queue and DSP, installs the DSP reset
// interrupt handler, arms the AI control registers, and validates the boot-info
// magic word - reloading boot-info the slow way on a debug-fst image, or
// flagging a non-standard disc otherwise.
// ============================================================================
void DVDInit(void)
{
    if (DAT_806131c4 == 0) {
        os_register_version(&DAT_80611410);
        DAT_806131c4 = 1;
        dvd_fst_setup();
        dvd_queue_init();
        dsp_init();
        DAT_80613188 = (int)0x80000000;
        DAT_806131c0 = 0;
        DAT_80613184 = (int)0x80000000;
        os_set_interrupt_handler(0x15, (void *)dsp_reset_handler);
        os_unmask_interrupts(0x400);
        os_init_thread_queue(&DAT_80613178);
        DAT_cc006000 = 0x2a;
        DAT_cc006004 = 0;
        if (*(int *)(DAT_80613188 + 0x20) == -0x1adf83de) {
            OSReport(&s_load_fst_80499ff8);
            dvd_read_bootinfo_blocking();
        }
        else if (*(int *)(DAT_80613188 + 0x20) != 0xd15ea5e) {
            DAT_806131bc = 1;
        }
    }
}

// ============================================================================
// DVDChangeDisk (0x803a2db4): begin a disc-change by DMAing the new disc's FST
// image (at DAT_805f3564, length DAT_805f3568) into the reserved FST region in
// main memory, panicking if the new FST is larger than the reserved space.
// ============================================================================
void DVDChangeDisk(void)
{
    DAT_806131c8 = (void *)DVDChangeDisk;
    if (*(unsigned int *)(DAT_80613188 + 0x3c) < DAT_805f3568) {
        OSPanic(&DAT_8061141c, 0x295,
                &s_DVDChangeDisk____FST_in_the_new_d_8049a004);
    }
    aram_dma_dispatch(*(int *)(DAT_80613188 + 0x38),
                      (DAT_805f3568 + 0x1f) & 0xffffffe0, DAT_805f3564,
                      (void *)dvd_state_advance);
}

// ============================================================================
// dvd_read_bootinfo_blocking (0x803a5aec): the slow-path boot-info reload used
// by DVDInit when the debug FST magic is present. Resets the DI, kicks a disc-id
// read into an arena-hi scratch buffer, spins until the drive is idle, publishes
// the FST pointer/length to low memory (0x80000038/0x8000003c), and OSReports
// the game name, company, disk number, version, and streaming flag.
// ============================================================================
void dvd_read_bootinfo_blocking(void)
{
    int status;
    char *streamStr;
    unsigned char idScratch[52];

    os_get_arena_hi();
    DAT_806131e0 = idScratch;
    DAT_806131dc = (int)(intptr_t)&DAT_805f3620;
    dvd_reset_di();
    dvd_submit_read_diskid((unsigned int *)&DAT_805f3658, DAT_806131e0,
                           (void *)dvd_bootinfo_read_step);
    do {
        status = dvd_get_drive_status();
    } while (status != 0);

    DAT_80000038 = *(int *)(DAT_806131dc + 0x10);
    DAT_8000003c = *(int *)(DAT_806131dc + 0xc);
    leaf_31e8((int)0x80000000, (unsigned int)(uintptr_t)DAT_806131e0, 0x20);
    OSReport(&DAT_80611430);
    OSReport(&s_Game_Name______c_c_c_c_8049a530, (int)DAT_80000000,
             (int)DAT_80000001, (int)DAT_80000002, (int)DAT_80000003);
    OSReport(&s_Company________c_c_8049a54c, (int)DAT_80000004,
             (int)DAT_80000005);
    OSReport(&s_Disk___________d_8049a564, (unsigned int)DAT_80000006);
    OSReport(&s_Game_ver_______d_8049a578, (unsigned int)DAT_80000007);
    if (DAT_80000008 == '\0') {
        streamStr = &DAT_80611434;
    }
    else {
        streamStr = &DAT_80611438;
    }
    OSReport(&s_Streaming______s_8049a58c, streamStr);
    OSReport(&DAT_80611430);
    os_set_arena_hi(*(int *)(DAT_806131dc + 0x10));
}

} // extern "C"
