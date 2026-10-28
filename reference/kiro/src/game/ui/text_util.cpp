// Addresses: Multiple | Date: 2026-07-16T00:00:00Z
// Description: Text utility functions for string manipulation, metrics, and color fading.

#include "types.h"
#include "sdk_stubs.h"
#include "forward_decls.h"

extern u8 DAT_804b0160;
extern f32 FLOAT_80613624;
extern f32 FLOAT_8061362c;
extern f32 FLOAT_80611664;
extern f32 FLOAT_80611660;
extern f32 FLOAT_8061165c;
extern u8 DAT_804b0930;

extern "C" void leaf_31e8(u32, u32, s32);
extern "C" s32 FUN_8038b5c8(u32);
extern "C" void func_text_9580(void);
extern "C" void func_text_9010(f64, u8*, s32);
extern "C" void func_text_9d10(f32*, f32*, u8*, s32);
extern "C" void text_set_color_rgb(f64, f64, f64);

// Address: 0x80009474 | Name: text_string_to_upper | Xrefs: 29
// Description: Copies a string into a static buffer and converts all lowercase
//              characters (a-z and extended 0xE0-0xFE) to uppercase by subtracting 0x20.
u8* text_string_to_upper(u32 param_1) {
    u8 bVar1;
    s32 iVar2;
    u8* pbVar3;
    s32 iVar4;

    DAT_804b0160 = 0;
    if (param_1 != 0) {
        iVar2 = FUN_8038b5c8(param_1);
        leaf_31e8((u32)&DAT_804b0160, param_1, iVar2);
        iVar4 = 0;
        pbVar3 = &DAT_804b0160;
        if (0 < iVar2) {
            do {
                bVar1 = *pbVar3;
                if ((0x60 < bVar1 && bVar1 < 0x7b) ||
                    (0xdf < bVar1 && bVar1 != 0xff)) {
                    *pbVar3 = bVar1 - 0x20;
                }
                iVar4 = iVar4 + 1;
                pbVar3 = pbVar3 + 1;
                iVar2 = iVar2 - 1;
            } while (iVar2 != 0);
        }
        (&DAT_804b0160)[iVar4] = 0;
    }
    return &DAT_804b0160;
}

// Address: 0x8000997c | Name: text_get_char_height | Xrefs: 73
// Description: Returns the height of a character glyph (float).
//              Complementary to text_get_char_width.
f32 text_get_char_height(u8* param_1) {
    f32 local_8;
    u8 auStack_4[4];

    func_text_9d10(auStack_4, &local_8, param_1, 0);
    return local_8;
}

// Address: 0x80009084 | Name: text_apply_alpha_fade | Xrefs: 55
// Description: Multiplies the current text color by a fade/alpha factor.
//              If param_4 != 0, applies uniform fade; otherwise scales per-channel.
void text_apply_alpha_fade(f64 param_1, u8* param_2, s32 param_3, s32 param_4) {
    f64 dVar1;
    f64 dVar2;

    dVar2 = (f64)func_text_9010(param_1, param_2, param_3);
    if (dVar2 < (f64)FLOAT_8061362c || (f64)FLOAT_8061362c < dVar2) {
        dVar1 = (f64)FLOAT_80611664 * dVar2;
        if (param_4 != 0) {
            dVar2 = (f64)FLOAT_8061362c;
        }
        text_set_color_rgb(dVar1, (f64)FLOAT_80611660 * dVar2, (f64)FLOAT_8061165c);
    }
    return;
}

// Address: 0x800090f0 | Name: text_apply_alpha | Xrefs: 24
// Description: Applies alpha fade with default mode (param_4 = 0).
void text_apply_alpha(f64 param_1, u8* param_2, s32 param_3) {
    text_apply_alpha_fade(param_1, param_2, param_3, 0);
    return;
}

// Address: 0x80009540 | Name: text_check_scale | Xrefs: 36
// Description: Checks if the given scale differs from the current font scale.
//              If so, recalculates and returns an alternate buffer.
u8* text_check_scale(f64 param_1, u8* param_2) {
    if ((f64)FLOAT_80613624 != param_1) {
        func_text_9580();
        param_2 = &DAT_804b0930;
    }
    return param_2;
}
