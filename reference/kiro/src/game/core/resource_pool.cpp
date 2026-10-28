// Address: 0x80007a94 | Name: resource_slot_index | Date: 2026-07-23
// Address: 0x80007ab4 | Name: resource_table_clear | Date: 2026-07-23
// Address: 0x80007ae4 | Name: resource_slots_teardown | Date: 2026-07-23
// Address: 0x80007b94 | Name: resource_slots_teardown_all | Date: 2026-07-23
// Address: 0x80007bd0 | Name: resource_pool_clear_ptrs | Date: 2026-07-23
// Address: 0x80007be8 | Name: resource_pool_init_default | Date: 2026-07-23
// Address: 0x80007c34 | Name: get_mode_resource_size | Date: 2026-07-23
// Address: 0x80007c84 | Name: resource_pool_create | Date: 2026-07-23
// Address: 0x80007d68 | Name: resource_pool_create_wrapper | Date: 2026-07-23
// Address: 0x80007d94 | Name: resource_slot_acquire | Date: 2026-07-23
// Address: 0x80007eb0 | Name: resource_slot_release | Date: 2026-07-23
// Address: 0x80007f3c | Name: get_resource_slot_size | Date: 2026-07-23
// Address: 0x80007f4c | Name: get_resource_data_base | Date: 2026-07-23
// Address: 0x80007f5c | Name: resource_get_data_ptr | Date: 2026-07-23
// Address: 0x80007f80 | Name: resource_pool_shutdown | Date: 2026-07-23
// Address: 0x80007fa4 | Name: resource_pool_flush_dvd | Date: 2026-07-23
//
// Description: Resource/asset pool manager. Manages slots for loaded game assets.
// Two pools exist (iterated as indices 0 and 1).
// Each pool has:
//   DAT_804b0140[pool*4] = slot count
//   DAT_804b0144[pool*4] = slot data size
//   DAT_804b0148[pool*4] = data buffer base
//   DAT_804b014c[pool*4] = descriptor table base
//
// Each slot descriptor (0x30 bytes):
//   [0x00] data pointer
//   [0x04] userdata
//   [0x08] active flag (0=free, 1=in-use)
//   [0x0C] destructor callback
//   [0x10-0x2C] extended fields (asset ID, load state, stream handle, etc.)
//
// resource_slot_acquire: finds free slot or calls destructor to evict oldest
// resource_slot_release: calls destructor and marks slot free

#include "common.h"

// Compute descriptor address for pool[poolIdx].slot[slotIdx]
s32 resource_slot_index(s32 poolIdx, s32 slotIdx) {
    extern s32 DAT_804b014c[];
    return DAT_804b014c[poolIdx * 4] + slotIdx * 0x30;
}
