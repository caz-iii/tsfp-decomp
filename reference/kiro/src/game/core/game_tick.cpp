// Address: 0x80005690 | Name: dispatch_5690 | Date: 2026-07-23
// Description: Main game tick/update. Called each frame from the game loop.
// Calls boot sequence, then updates all major game subsystems:
// - Physics/collision (FUN_80131124)
// - Entity manager (FUN_8012ef38)
// - Heap management (FUN_80006b04)
// - Camera (FUN_8016c8fc)
// - Rendering (FUN_8017e034)
// - AI (FUN_8002b238)
// - Input (FUN_8001c554)
// - Player logic (FUN_8000cbf0)
// - Audio (FUN_8000ef84 → FUN_8000f4f0)
// - HUD/UI (FUN_8004fb9c → FUN_8017c35c)
// - Animation (FUN_800cdae4)
// - Scene graph (FUN_8017d130)
// - Network (FUN_801c0998)
// - Scripting (func_text_8710)
// - Effects (FUN_800346b8)
// - Particles (FUN_802e0efc)
// ... and many more subsystem updates

#include "common.h"

extern void* DAT_80611540;
extern s32 DAT_8060f944;

extern void func_text_566c(/* params */);
extern void stub_6184(void);
extern void FUN_80131124(void);
extern void FUN_8012ef38(s32 param);
extern void FUN_80006b04(void* heap, s32 size);
extern void FUN_8016c8fc(void);
extern void FUN_8017e034(void);
extern void FUN_8002b238(void);
extern void FUN_8001c554(void);
extern void FUN_8000cbf0(/* params */);
extern void FUN_8000ef84(void);
extern void FUN_8000f4f0(/* params */);
extern void FUN_8004fb9c(void);
extern void FUN_8017c35c(/* params */);
extern void FUN_800cdae4(void);
extern void FUN_8017d130(void);
extern void FUN_801c0998(void);
extern void func_text_8710(void);
extern void FUN_800346b8(void);
extern void FUN_802e0efc(void);
extern void FUN_80070364(void);
extern void FUN_800842cc(void);
extern void FUN_80087f24(void);
extern void FUN_802428e4(void);
extern void FUN_800988dc(void);
extern void FUN_8007c3c8(void);
extern void FUN_8008241c(void);
extern void FUN_800ab51c(void);
extern void FUN_80086fa4(void);
extern void FUN_8009344c(void);
extern void FUN_801a6e90(void);
extern void FUN_8015ce58(void);
extern void FUN_801aabec(void);
extern void FUN_8013381c(void);
extern void FUN_80131078(s32 param);

void dispatch_5690(/* params */) {
    func_text_566c(/* forward params */);
    stub_6184();
    FUN_80131124();
    FUN_8012ef38(-0x7FA7CE58);  // likely an address cast
    FUN_80006b04(DAT_80611540, DAT_8060f944);
    FUN_8016c8fc();
    FUN_8017e034();
    FUN_8002b238();
    FUN_8001c554();
    FUN_8000cbf0(/* params */);
    FUN_8000ef84();
    FUN_8000f4f0(/* params */);
    FUN_8004fb9c();
    FUN_8017c35c(/* params */);
    FUN_800cdae4();
    FUN_8017d130();
    FUN_801c0998();
    func_text_8710();
    FUN_800346b8();
    FUN_802e0efc();
    FUN_80070364();
    FUN_800842cc();
    FUN_80087f24();
    FUN_802428e4();
    FUN_800988dc();
    FUN_8007c3c8();
    FUN_8008241c();
    FUN_800ab51c();
    FUN_80086fa4();
    FUN_8009344c();
    FUN_801a6e90();
    FUN_8015ce58();
    FUN_801aabec();
    FUN_8013381c();
    FUN_80131078(1);
}
