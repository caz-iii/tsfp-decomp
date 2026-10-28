// Address: 0x8005fd98 - 0x800601e8 | Name: render_entity | Date: 2026-08-10
// Render entity pool system: manages the pool of renderable entity descriptors.
// Each entity is 0x1F8 bytes. Pool holds 0x226 (550) entries in normal mode,
// or 0x14 (20) in mode 0x65 (cutscene/minimal).
// Handles pointer relocation when model data (.gcr) is loaded into memory.
//
// Globals:
//   DAT_80611b30 = pool capacity (entry count)
//   DAT_80611b34 = pool base address
//   DAT_80611b4c, DAT_80611b50 = active indices

#include "common.h"
#include "mem_alloc.h"

// ============================================================================
// Constants
// ============================================================================

#define RENDER_ENTITY_STRIDE    0x1F8   // 504 bytes per entity
#define RENDER_ENTITY_MAX       0x226   // 550 max in normal mode
#define RENDER_ENTITY_MIN       0x14    // 20 in cutscene mode
#define RENDER_ENTITY_NODE_FLAG 0x200000 // visibility flag on scene nodes

// ============================================================================
// Globals (extern)
// ============================================================================

extern int g_render_entity_capacity;    // DAT_80611b30
extern int g_render_entity_pool_base;   // DAT_80611b34
extern int g_render_entity_index_a;     // DAT_80611b4c
extern int g_render_entity_index_b;     // DAT_80611b50
extern int g_render_entity_no_lighting; // DAT_80610420
extern int g_game_mode_id;              // DAT_80612614
extern int g_has_lighting;              // DAT_80611970

// ============================================================================
// 0x8005fd98 | render_entity_pool_reset
// Zeros all entity pool memory and resets active indices to 0.
// Also sets g_render_entity_no_lighting based on whether lighting is available.
// ============================================================================

void render_entity_pool_reset(void)
{
    g_render_entity_index_b = 0;
    g_render_entity_index_a = 0;

    int size = g_render_entity_capacity * RENDER_ENTITY_STRIDE;
    size = (size + 0xF) & ~0xF;  // 16-byte align
    memset(g_render_entity_pool_base, 0, size);

    // countLeadingZeros(g_has_lighting) >> 5: yields 1 if zero, 0 if non-zero
    g_render_entity_no_lighting = (__builtin_clz(g_has_lighting) >> 5);
}

// ============================================================================
// 0x8005fdec | render_entity_pool_init
// Allocates the render entity pool. Capacity depends on game mode:
// mode 0x65 (cutscene) = 20 entities, otherwise 550 entities.
// Calls render_entity_pool_reset after allocation.
// ============================================================================

void render_entity_pool_init(void)
{
    g_render_entity_capacity = RENDER_ENTITY_MAX;
    if (g_game_mode_id == 0x65) {
        g_render_entity_capacity = RENDER_ENTITY_MIN;
    }

    g_render_entity_pool_base = mem_alloc(
        g_render_entity_capacity * RENDER_ENTITY_STRIDE, 1);

    render_entity_pool_reset();
}

// ============================================================================
// 0x8005fe3c | render_entity_stub_empty
// Empty stub function - no operation.
// ============================================================================

void render_entity_stub_empty(void)
{
    // intentionally empty
}

// ============================================================================
// 0x8005fe40 | render_entity_mark_nodes_visible
// Marks scene nodes as visible by setting flag 0x200000 in their flags field.
// param_1: scene node array base
// param_2: pointer to { count, ..., node_indices[count] } (indices at stride 8,
//          offset +0x14 from list base per entry; each index * 0x10 + 0x0C = flag offset)
// ============================================================================

void render_entity_mark_nodes_visible(int scene_nodes, int *node_list)
{
    int count = *node_list;
    int offset = 0;

    for (int i = 0; i < count; i++) {
        int node_index = *(int *)((int)node_list + offset + 0x14);
        int flag_offset = node_index * 0x10 + 0x0C;
        *(unsigned int *)(scene_nodes + flag_offset) |= RENDER_ENTITY_NODE_FLAG;
        offset += 8;
    }
}

// ============================================================================
// 0x8005fe80 | render_entity_fixup_pointers
// Adds a base address offset to up to 5 pointer fields in a structure.
// Only adds offset if the field is non-zero (NULL check via & -(field != 0)).
// Returns 1 if any field remains non-null after fixup, 0 if all are null.
// ============================================================================

int render_entity_fixup_pointers(unsigned int *fields, unsigned int base_offset)
{
    for (int i = 0; i < 5; i++) {
        if (fields[i] < base_offset) {
            // Fixup: add offset only if non-null
            fields[i] = (fields[i] + base_offset) & -(unsigned int)(fields[i] != 0);
        }
    }

    // Return 1 if any field is non-null
    if (fields[0] != 0 || fields[1] != 0 || fields[2] != 0 ||
        fields[3] != 0 || fields[4] != 0) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x8005ff6c | render_entity_link_data
// Links render entity data after loading: relocates pointer fields per entry
// in the entity's sub-object array (terminated by entry[4] == -1).
// Per entry (stride 0x30 = 12 words):
//   [0]: node index or node list ptr (flag 0x08 = list)
//   [1]: secondary node ptr (flag 0x10)
//   [2]: third node / list ptr (flag 0x20 or 0x100)
//   [3]: fourth node / list ptr (flag 0x40)
//   [4]: terminator (-1 = end)
//   [7]: flags
//   [8..11]: 4 pointer fields → fixup via render_entity_fixup_pointers
// Also marks scene nodes visible and sets flag 0x20000 on type 0x13 entries.
// ============================================================================

void render_entity_link_data(void *entity_ctx, int scene_nodes_base, int base_offset)
{
    FUN_803826f0();  // save_ps_registers (context setup)

    unsigned int *entry = /* from register context: high word */;
    unsigned int offset = /* from register context: low word (base_offset) */;

    while (entry[4] != 0xFFFFFFFF) {
        // Relocate field [8] and fixup its sub-pointers
        unsigned int val = entry[8] + offset;
        if (entry[8] != 0) val = entry[8] + offset;
        entry[8] = val & -(unsigned int)(entry[8] != 0);
        if (entry[8] != 0) {
            if (render_entity_fixup_pointers((unsigned int *)entry[8], offset)) {
                entry[7] |= 4;
            }
        }

        // Fields [9], [10], [11] - same pattern
        for (int f = 9; f <= 11; f++) {
            val = entry[f] + offset;
            entry[f] = val & -(unsigned int)(entry[f] != 0);
            if (entry[f] != 0) {
                if (render_entity_fixup_pointers((unsigned int *)entry[f], offset)) {
                    entry[7] |= 4;
                }
            }
        }

        // Mark visibility based on flags
        if ((entry[7] & 8) == 0) {
            // Single node index
            if ((int)entry[0] >= 0) {
                int flag_off = entry[0] * 0x10 + 0x0C;
                *(unsigned int *)(scene_nodes_base + flag_off) |= RENDER_ENTITY_NODE_FLAG;
            }
        } else {
            // Node list pointer
            entry[0] = (entry[0] + offset) & -(unsigned int)(entry[0] != 0);
            render_entity_mark_nodes_visible(scene_nodes_base, (int *)entry[0]);
        }

        // Secondary node (flag 0x10)
        if ((entry[7] & 0x10) != 0) {
            entry[1] = (entry[1] + offset) & -(unsigned int)(entry[1] != 0);
        }

        // Third node (flag 0x20 = list, else single, flag 0x100 = skip)
        if ((entry[7] & 0x20) != 0) {
            entry[2] = (entry[2] + offset) & -(unsigned int)(entry[2] != 0);
            render_entity_mark_nodes_visible(scene_nodes_base, (int *)entry[2]);
        } else if ((entry[7] & 0x100) == 0) {
            if ((int)entry[2] >= 0) {
                int flag_off = entry[2] * 0x10 + 0x0C;
                *(unsigned int *)(scene_nodes_base + flag_off) |= RENDER_ENTITY_NODE_FLAG;
            }
        }

        // Fourth node (flag 0x40 = list, else single)
        if ((entry[7] & 0x40) != 0) {
            entry[3] = (entry[3] + offset) & -(unsigned int)(entry[3] != 0);
            render_entity_mark_nodes_visible(scene_nodes_base, (int *)entry[3]);
        } else {
            if ((int)entry[3] >= 0) {
                int flag_off = entry[3] * 0x10 + 0x0C;
                *(unsigned int *)(scene_nodes_base + flag_off) |= RENDER_ENTITY_NODE_FLAG;
            }
        }

        // Type 0x13: set special flag
        if (*(char *)(entry + 6) == 0x13) {
            entry[7] |= 0x20000;
        }

        entry += 12;  // stride 0x30
    }

    FUN_8038273c();  // restore_ps_registers
}

// ============================================================================
// 0x800601e8 | render_entity_relocate_all
// MASSIVE pointer relocation for all render entities in a loaded model.
// Called after .gcr model data is loaded to convert relative offsets to
// absolute pointers. Iterates param_1[0] entries (stride 0xC0 = 192 bytes).
//
// Per entity (at negative offset from array end):
// - Relocates param_1[0xE] (global pointer field)
// - Per LOD (3 LODs, stride 0x14 per LOD within entity):
//   * 5 pointer fields at offsets 0x00..0x10 (vertex/index/material/bone/weight)
//   * 1 pointer at +0x54 (per LOD: sub-mesh table)
// - Additional pointers at fixed offsets:
//   +0x60: animation data ptr
//   +0x6C: bone hierarchy ptr
//   +0x70: bone name table ptr (+ relocate each entry)
//   +0x74: material array ptr (+ relocate each material's 3 texture ptrs)
//   +0x78: morph target ptr (+ relocate each entry)
//   +0x7C: collision mesh ptr (+ relocate each entry's sub-ptr at +0x4C)
//   +0x80: bounding box ptr
//   +0x9C: physics data ptr (+ relocate 4 sub-ptrs at +0x1C, +0x24, +0x2C, +0x34)
//   +0xA0: LOD distance ptr
//   +0xA4: shadow data ptr
//   +0xA8: unknown ptr
//   +0xB0..0xB8: 3 display list ptrs (per LOD, walk sub-mesh linked list)
//   +0xBC: special field (negative = negate+subtract 1, else relocate)
//
// Pattern: ptr = (ptr + base) & -(ptr != 0)  [only relocate non-null]
// ============================================================================

void render_entity_relocate_all(int *entity_array, int base_offset)
{
    // Relocate global field
    if (entity_array[0xE] == 0) {
        // already null
    } else {
        entity_array[0xE] = entity_array[0xE] + base_offset;
    }

    int entry_count = *entity_array;
    int entry_offset = 0;

    for (int i = 0; i < entry_count; i++) {
        int base = entry_offset + entry_count * -0xC0;  // negative stride from end

        // Per-LOD (3 LODs)
        int lod_off = 0;
        int sub_off = 0;
        for (int lod = 0; lod < 3; lod++) {
            // Sub-mesh table pointer at +0x54
            int sub_ptr_off = sub_off + 0x54;
            int val = *(int *)((int)entity_array + sub_ptr_off + base);
            if (val != 0) {
                *(unsigned int *)((int)entity_array + sub_ptr_off + base) =
                    (base_offset + val) & -(unsigned int)(val != 0);
            }

            // 5 vertex/material pointers (stride 4, at lod_off within entity)
            unsigned int *ptrs = (unsigned int *)((int)entity_array + base + lod_off);
            ptrs[0] = (ptrs[0] + base_offset) & -(unsigned int)(ptrs[0] != 0);
            ptrs[1] = (ptrs[1] + base_offset) & -(unsigned int)(ptrs[1] != 0);
            ptrs[2] = (ptrs[2] + base_offset) & -(unsigned int)(ptrs[2] != 0);
            ptrs[3] = (ptrs[3] + base_offset) & -(unsigned int)(ptrs[3] != 0);
            ptrs[4] = (ptrs[4] + base_offset) & -(unsigned int)(ptrs[4] != 0);

            // If sub-mesh table exists, mark visibility bits
            if (*(int *)((int)entity_array + sub_ptr_off + base) != 0) {
                entity_array[3] |= (1 << lod);
            }

            lod_off += 0x14;
            sub_off += 4;
        }

        // Fixed-offset pointer relocations (+0x60 through +0xBC)
        // [See raw decompilation for the full list of 15+ pointer fields]
        // Each follows the pattern: val = *(int*)(base+off);
        //   *(uint*)(base+off) = (val + base_offset) & -(uint)(val != 0);
        // Some fields have sub-pointer arrays that also need relocation.

        // +0x70: bone name table (relocate each entry in array)
        // +0x74: material array (relocate + each material's 3 texture ptrs at +0x30,+0x34,+0x38)
        // +0x78: morph targets (relocate each entry)
        // +0x7C: collision mesh (relocate + each entry's ptr at +0x4C)
        // +0x9C: physics data (relocate + 4 sub-ptrs)
        // +0xB0..0xB8: display list ptrs (walk linked list while byte[3] >= 0)
        // +0xBC: special (if negative: negate-1, else relocate)

        entry_offset += 0xC0;
    }
}

// ============================================================================
// 0x800606fc | render_entity_load_from_stream
// Loads a model from the stream queue callback. Resolves relative data pointers
// to absolute, calls render_entity_link_data for sub-object setup, then walks
// the material table (stride 0x10, terminated by -1) to queue textures.
// Texture paths: 'textures/VT_%06x.gct' (virtual textures) or stream_queue_texture.
// Skips entries marked 0x40000000 (unloaded) or without flag 0x200000.
// Flag 0x10000000 entries use FUN_8017bffc (embedded texture).
// Flag 0x04 on stream entry enables secondary texture path 'textures/%06x.gct'.
// Called from stream_queue_process.
// ============================================================================

void render_entity_load_from_stream(void)
{
    char *name = FUN_803826ec();  // get_model_name from context
    int stream_entry = stream_find_model_by_name();
    if (stream_entry == 0) return;

    int *model_data = *(int **)(stream_entry + 0x10);
    if (model_data == NULL) return;

    // Resolve node table pointer (relative → absolute)
    int node_table = (*model_data != 0) ? *model_data + (int)model_data : 0;

    // Check for tileset or alternate path
    char *tileset_match = strstr(name, "tileset");
    int material_table;
    if (tileset_match == NULL || strstr(name + 8, /* secondary pattern */) == NULL) {
        material_table = (model_data[2] != 0) ? model_data[2] + (int)model_data : 0;
    } else {
        material_table = (model_data[10] != 0) ? model_data[10] + (int)model_data : 0;
    }

    // Fixup embedded resource pointers
    render_entity_fixup_embedded_ptrs(node_table, (int)model_data);

    // Link sub-object data (visibility, pointer relocation)
    render_entity_link_data(material_table, model_data, node_table);

    // Walk material table, queue textures
    int offset = 0;
    while (true) {
        int *entry = (int *)(node_table + offset);
        int tex_id = *entry;
        if (tex_id == -1) break;

        if (tex_id != 0x40000000 && (entry[3] & 0x200000) != 0) {
            if ((entry[3] & 0x10000000) == 0) {
                if ((*(unsigned short *)(stream_entry + 2) & 0x20) == 0) {
                    stream_queue_texture(tex_id, entry);
                } else {
                    // Load from pack: 'textures/VT_%06x.gct'
                    FUN_8012e878("textures/VT_%06x.gct", tex_id, 0x80400000);
                }

                // Secondary texture if flag 0x04
                if ((*(unsigned short *)(stream_entry + 2) & 4) != 0) {
                    DAT_80611870 = 1;
                    FUN_8012e878("textures/%06x.gct", *entry, entry);
                    DAT_80611870 = 0;
                }
            } else {
                // Embedded texture resource
                FUN_8017bffc(tex_id, entry, entry);
            }
        }
        offset += 0x10;
    }

    FUN_80382738();  // restore context
}

// ============================================================================
// 0x8006089c | render_entity_resolve_texture_from_pack
// Resolves a texture from pack files using multiple path formats:
// 1. 'textures/CT_%06x.s' (compressed texture)
// 2. 'textures/%06x.s' (fallback)
// 3. 'textures/VT_%06x.s' (virtual texture variant)
// If texture has multiple pages (FUN_8017b034 > 0), stores page ID directly.
// Otherwise delegates to FUN_8017bffc for single-page load + optional
// FUN_801852f0 for variant texture association.
// ============================================================================

void render_entity_resolve_texture_from_pack(void)
{
    FUN_803826e4();  // save context

    int tex_offset = /* high_word */ * 0x10;
    int *entry = (int *)( /* low_word */ + tex_offset);

    // Try 'textures/CT_%06x.s' first
    char path[280];
    sprintf(path, "textures/CT_%06x.s", *entry);
    int asset = pack_get_asset_ptr_from_slot(path, DAT_8061185c);

    int is_fallback = (asset == 0);
    if (is_fallback) {
        // Fallback: 'textures/%06x.s'
        sprintf(path, "textures/%06x.s", *entry);
        asset = pack_get_asset_ptr_from_slot(path, DAT_8061185c);
    }

    int page_count = FUN_8017b034(asset);
    if (page_count < 1) {
        // Single page: load via standard path
        FUN_8017bffc(asset, entry, entry);
        if (!is_fallback) {
            // Associate variant texture
            sprintf(path, "textures/VT_%06x.s", entry[1]);
            FUN_801852f0(path, *entry);
        }
    } else {
        // Multi-page: store page ID directly
        *entry = page_count;
    }

    FUN_80382730();  // restore context
}

// ============================================================================
// 0x80060994 | render_entity_unload_textures
// Unloads all textures referenced by an entity's material table.
// Walks the table (stride 0x10, terminated by -1), calls FUN_8017c454 to
// release each texture, marks the slot as 0x40000000 (unloaded sentinel).
// Finally calls FUN_8017c358 to flush the texture system.
// Called from entity_process_despawns and entity unload paths.
// ============================================================================

void render_entity_unload_textures(void)
{
    int stream_entry = FUN_803826ec();  // get_context
    if (stream_entry == 0) goto done;

    int *data = *(int **)(stream_entry + 0x10);
    if (data == NULL) goto done;

    int offset = 0;
    while (*(int *)((int)data + offset) != -1) {
        int tex_id = *(int *)((int)data + offset);
        FUN_8017c454(tex_id);                        // release texture
        *(int *)((int)data + offset) = 0x40000000;   // mark unloaded
        offset += 0x10;
    }
    FUN_8017c358();  // flush texture system

done:
    FUN_80382738();  // restore context
}

// ============================================================================
// 0x80060a08 | render_entity_load_from_data
// Loads a model from a raw data pointer (not stream queue).
// Same pattern as render_entity_load_from_stream but without stream_find:
// 1. Resolves node table and material table from relative pointers
// 2. Calls render_entity_fixup_embedded_ptrs + render_entity_link_data
// 3. Walks material table, queues textures:
//    - If context flag == 0: standard stream_queue_texture
//    - Else: render_entity_resolve_texture_from_pack
//    - Flag 0x10000000: FUN_8017bffc (embedded)
// Called from scene_block_load_and_link, scene_load_level_model, etc.
// ============================================================================

void render_entity_load_from_data(void)
{
    int ctx = FUN_803826e8();  // get_context pair
    int *model_data = (int *)(ctx >> 32);

    int node_table = (*model_data != 0) ? *model_data + (int)model_data : 0;
    int material_table = (model_data[2] != 0) ? model_data[2] + (int)model_data : 0;

    render_entity_fixup_embedded_ptrs(node_table, (int)model_data);
    render_entity_link_data(material_table, model_data, node_table);

    int offset = 0;
    while (true) {
        int *entry = (int *)(node_table + offset);
        int tex_id = *entry;
        if (tex_id == -1) break;

        if (tex_id != 0x40000000 && (entry[3] & 0x200000) != 0) {
            if ((entry[3] & 0x10000000) == 0) {
                if ((int)ctx == 0) {  // low word = mode flag
                    stream_queue_texture(tex_id, entry);
                } else {
                    render_entity_resolve_texture_from_pack();
                }
            } else {
                FUN_8017bffc(tex_id, entry, entry);
            }
        }
        offset += 0x10;
    }

    FUN_80382734();  // restore context
}

// ============================================================================
// 0x80060b20 | render_entity_load_if_callback_ready
// Wrapper: checks if a stream callback is ready (stream_find_callback_by_name),
// and if so delegates to render_entity_load_from_data.
// ============================================================================

void render_entity_load_if_callback_ready(void)
{
    int callback = stream_find_callback_by_name();
    if (callback != 0) {
        render_entity_load_from_data();
    }
}

// ============================================================================
// 0x80060b54 | render_entity_set_global_flag
// Simple setter: stores a value to the global render entity flag (DAT_8061043c).
// ============================================================================

void render_entity_set_global_flag(int value)
{
    DAT_8061043c = value;
}

// ============================================================================
// 0x80060b5c | render_entity_fixup_embedded_ptrs
// Walks the material/node table (stride 0x10, terminated by -1).
// For entries with flag 0x10000000 in word[3], relocates word[0] by adding
// the model base address. Skips entries marked 0x40000000 (unloaded).
// param_1: node table base (absolute)
// param_2: model data base address (for relocation)
// ============================================================================

void render_entity_fixup_embedded_ptrs(int node_table, int model_base)
{
    int offset = 0;
    while (true) {
        unsigned int *entry = (unsigned int *)(node_table + offset);
        unsigned int val = *entry;
        if (val == 0xFFFFFFFF) break;  // terminator

        if (val != 0x40000000 && (entry[3] & 0x10000000) != 0) {
            // Relocate: add model base (only if non-null)
            *entry = (model_base + val) & -(unsigned int)(val != 0);
        }
        offset += 0x10;
    }
}

// ============================================================================
// 0x80060ba8 | render_entity_resolve_model_path
// Builds a model asset path using the format:
//   'assets/ts/models/%s/gamecube_out/%s'
// where first %s = full path, second %s = filename only (via path_get_filename).
// Resolves the asset from the current pack slot (DAT_8061185c).
// param_9: model name/path string
// ============================================================================

void render_entity_resolve_model_path(char *model_name)
{
    char path[212];
    char *filename = path_get_filename(model_name);

    sprintf(path, "assets/ts/models/%s/gamecube_out/%s", model_name, filename);
    pack_get_asset_ptr_from_slot(path, DAT_8061185c);
}

// ============================================================================
// 0x80060bfc | render_entity_load_model
// MAIN model loading entry point. Resolves model data from:
// 1. Stream cache (stream_find_model_by_name)
// 2. Pack file (pack_get_asset_ptr or render_entity_resolve_model_path)
// 3. DVD fallback (asset_load_from_dvd)
// After loading, checks if already relocated (flag 0x800000). If not:
// - Optionally calls render_entity_load_from_data for texture setup
// - Resolves node table + material table pointers
// - Calls render_entity_relocate_all for full pointer fixup
// - Sets loaded flag (0x800000)
// - Marks stream entry as linked (flag 0x01)
// Mode-dependent: checks DAT_80612614 for cutscene (100/0x38) vs gameplay (0x66).
// Path format decision uses DAT_805c9858 lookup table (model ID → flags).
// ============================================================================

void render_entity_load_model(void)
{
    FUN_803826e4();  // save context

    byte *model_name = /* from register context: high word */;
    int stream_entry = stream_find_model_by_name();

    int *model_data;
    if (stream_entry == 0) {
        // Not in stream cache - determine load path
        int model_index = (int)/* context low */ * 2;
        unsigned int use_pack_path = 0;

        if ((DAT_805015c0 & 0x40) == 0) {
            if (DAT_80612614 != 100 && DAT_80612614 != 0x38) {
                if (DAT_805015bd == 0x0A || DAT_80612614 == 0x66) {
                    // Check model flags table for pack-load flag
                    int entry_idx = *(short *)(&DAT_805c9858 + model_index);
                    use_pack_path = *(unsigned int *)(DAT_806125e8 + entry_idx * 0x30 + 0x28)
                                    & 0x2000000;
                }
            }
        }

        if (use_pack_path == 0) {
            model_data = (int *)pack_get_asset_ptr(model_name, NULL);
        } else {
            model_data = (int *)render_entity_resolve_model_path((char *)model_name);
            if (model_data == NULL) {
                // Error: "Could not load '%s'"
                goto done;
            }
            entity_add_to_despawn_list(*(short *)(&DAT_805c9858 + model_index));
        }

        if (model_data == NULL) {
            // Final fallback: load from DVD
            unsigned int file_size;
            model_data = (int *)asset_load_from_dvd(model_name, 1, &file_size);
        }
    } else {
        model_data = *(int **)(stream_entry + 0x10);
    }

    // Get sub-object header
    int *sub_header = (model_data[1] != 0) ? (int *)(model_data[1] + (int)model_data) : NULL;

    // Check if already relocated
    if ((stream_entry == 0 || (*(unsigned short *)(stream_entry + 2) & 1) == 0) &&
        (sub_header[3] & 0x800000) == 0) {

        // Check if model needs texture loading via alternate path
        if (use_pack_path != 0) {
            render_entity_load_from_data();
        }

        // Resolve pointers
        int node_table = (*model_data != 0) ? *model_data + (int)model_data : 0;
        int material_table = (model_data[2] != 0) ? model_data[2] + (int)model_data : 0;

        sub_header[4] = node_table;
        sub_header[5] = material_table;

        // Full pointer relocation
        render_entity_relocate_all(sub_header, (int)model_data);

        // Mark as loaded
        sub_header[3] |= 0x800000;

        // Mark stream entry as linked
        if (stream_entry != 0) {
            *(unsigned short *)(stream_entry + 2) |= 1;
        }
    }

done:
    FUN_80382730();  // restore context
}

// ============================================================================
// 0x80060e60 | render_entity_load_lod
// Loads a specific LOD (Level of Detail) variant from model data.
// param_3: LOD index (0 = base, 1+ = lower detail)
// Resolves the sub-object at the LOD's offset within the model data array
// (stride 0xB4 per LOD entry, pointer at first word of each).
// Sets sub_header[1] = 1 (loaded flag), then:
// - If not already linked (stream flag 0x01 absent, or LOD > 1):
//   * Resolves node table + material table pointers
//   * Calls render_entity_relocate_all + render_entity_link_data
//   * Marks stream entry as linked (flag 0x01)
// - If already linked: skips relocation (shared data)
// ============================================================================

void render_entity_load_lod(int *model_data, int stream_entry, int lod_index)
{
    FUN_803826f0();  // save_ps_registers

    if (lod_index == 0) goto done;  // LOD 0 = already loaded as base

    // Resolve sub-object at LOD offset
    int sub_offset;
    if (model_data[1] == 0) {
        sub_offset = 0;
    } else {
        sub_offset = model_data[1] + (int)model_data;
    }

    int lod_entry_ptr = *(int *)(sub_offset + lod_index * 0xB4);
    int *sub_header;
    if (lod_entry_ptr == 0) {
        sub_header = NULL;
    } else {
        sub_header = (int *)((int)model_data + lod_entry_ptr);
    }

    // Mark as loaded
    sub_header[1] = 1;

    // Check if relocation needed
    if ((stream_entry == 0 || (*(unsigned short *)(stream_entry + 2) & 1) == 0) ||
        (lod_index > 1)) {

        // Resolve node table
        int node_table = (*model_data != 0) ? *model_data + (int)model_data : 0;

        // Resolve material table (from field [10] for LOD variants)
        int material_table;
        if (model_data[10] == 0) {
            material_table = 0;
        } else {
            material_table = model_data[10] + (int)model_data;
        }

        sub_header[4] = node_table;
        sub_header[5] = material_table;

        // Full pointer relocation + link
        render_entity_relocate_all(sub_header, (int)model_data);
        render_entity_link_data(sub_header[5], model_data, sub_header[4]);

        // Mark stream entry as linked
        if (stream_entry != 0) {
            *(unsigned short *)(stream_entry + 2) |= 1;
        }
    }

done:
    FUN_8038273c();  // restore_ps_registers
}
