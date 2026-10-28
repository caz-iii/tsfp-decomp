// Address: 0x80008034 | Name: texture_stream_update | Date: 2026-07-23
// Address: 0x800083bc | Name: texture_slot_unload | Date: 2026-07-23
// Address: 0x80008498 | Name: resource_get_asset_id | Date: 2026-07-23
// Address: 0x800084bc | Name: model_begin_load | Date: 2026-07-23
// Address: 0x80008598 | Name: resource_get_load_state | Date: 2026-07-23
// Address: 0x800085bc | Name: resource_is_loaded | Date: 2026-07-23
// Address: 0x80008600 | Name: wait_resources_loaded | Date: 2026-07-23
// Address: 0x80008668 | Name: wait_resource_by_id | Date: 2026-07-23
// Address: 0x800086dc | Name: resource_get_context | Date: 2026-07-23
// Address: 0x80008700 | Name: set_stream_priority | Date: 2026-07-23
//
// Description: Texture streaming and model loading system.
// texture_stream_update: Per-frame update for async texture loads.
//   - Processes pending texture load requests
//   - Resolves loaded data: fixups vertex buffers, registers textures
//   - Path format: "textures\\%06x" (texture ID as 6-digit hex)
//   - Uses "memstreamblock_c" for streaming read operations
//
// model_begin_load: Initiates async model file load.
//   - Path format: "assets\\ts\\models\\<name>\\gamecube.out"
//   - Allocates stream block for async DVD read
//
// DAT_806125e8 = global asset lookup table base
// DAT_805c9858 = asset ID to table offset (short array)
// DAT_806108e0 = frame delta time (for timeout countdown)
// DAT_80611630 = current texture list pointer
// DAT_80611634 = current model context pointer

#include "common.h"

// resource_is_loaded: check if slot has valid asset and is fully loaded
u32 resource_is_loaded(s32 poolIdx, s32 slotIdx) {
    extern s32 DAT_804b014c[];
    s32 desc = DAT_804b014c[poolIdx * 4] + slotIdx * 0x30;
    if (*(s32*)(desc + 0x10) != -1 && *(s32*)(desc + 0x14) == 0) {
        return 1;
    }
    return 0;
}
