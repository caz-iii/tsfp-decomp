// Address: 0x8002952c-0x80029884 | Name: level_actors | Date: 2026-08-05
// Level actor management: slot table, resource pool sizing, registration,
// initialization, and teardown.

#include "common.h"
#include "level.h"
#include "resource_pool.h"
#include "mem_alloc.h"

// Globals
// DAT_804e7400 - actor system initialized flag
// DAT_804e7404 - actor table base pointer (bank 0)
// DAT_804e7408 - actor table base pointer (bank 1)
// DAT_804e740c - actor count (bank 0)
// DAT_804e7410 - actor count (bank 1)
// DAT_804e741c - main level handle
// DAT_804e7420 - bank handles [2]
// DAT_804e7428 - bank resource handles [2]
// DAT_804e742c - current actor index

#define MAX_ACTOR_BANKS 2
#define ACTOR_ENTRY_SIZE 0x18
#define ACTOR_MAGIC_EMPTY 0x454d5059  // 'EMPY'

// ============================================================================
// Address: 0x8002952c | level_find_actor_slot
// Finds the slot index for an actor ID in the level's actor table.
// Returns -1 if not found.
// ============================================================================
int level_find_actor_slot(int nActorId, int nBank) {
    int nCount;
    int iOffset;

    nCount = g_ActorCounts[nBank];
    iOffset = 0;
    for (int i = 0; i < nCount; i++) {
        if (nActorId == *(int*)(g_ActorTables[nBank] + iOffset)) {
            return i;
        }
        iOffset += ACTOR_ENTRY_SIZE;
    }
    return -1;
}

// ============================================================================
// Address: 0x80029588 | level_actors_init_wrapper
// Wrapper that calls level_actors_init.
// ============================================================================
void level_actors_init_wrapper(void) {
    level_actors_init();
}

// ============================================================================
// Address: 0x800295a8 | level_actors_init
// Initializes the level actor system. Clears actor tables, resets slot
// tracking state, and invalidates all handles.
// ============================================================================
void level_actors_init(void) {
    memset_wrapper((int)&g_ActorState, 0, 0x30);
    g_ActorSystemReady = 1;
    g_LevelHandle = -1;

    for (int i = 0; i < MAX_ACTOR_BANKS; i++) {
        g_BankResourceHandles[i] = -1;
        g_BankHandles[i] = -1;
    }

    g_ActorLoadState1 = 0;
    g_ActorLoadState2 = 0;
}

// ============================================================================
// Address: 0x8002961c | level_actors_register
// Registers actor entries into the level actor table.
// For non-empty entries (not 'EMPY' magic), resolves animation resource.
// ============================================================================
void level_actors_register(int nBank, int nCount) {
    int* pActorTable;
    int iOffset;

    // Prologue / context from registers
    pActorTable = (int*)FUN_803826e4_lo();

    iOffset = 0;
    for (int i = 0; i < nCount; i++) {
        int* pEntry = (int*)((int)pActorTable + iOffset);
        if (*pEntry != ACTOR_MAGIC_EMPTY) {
            // Resolve animation resource for this actor
            pEntry[5] = FUN_8002ac3c();
        }
        iOffset += ACTOR_ENTRY_SIZE;
    }

    g_ActorTables[nBank] = (int)pActorTable;
    g_ActorCounts[nBank] = nCount;
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x800296a8 | level_resource_pool_setup
// Calculates and allocates the resource pool for level actors.
// Scans actor entries across all banks to find max resource requirement,
// then initializes the pool with that size (32-byte aligned).
// ============================================================================
void level_resource_pool_setup(void) {
    int iMaxSize;
    int nBank;
    int i;
    int iOffset;
    int iEntrySize;
    char* pName;

    // Prologue
    int* pBankA = (int*)FUN_803826d4_hi();
    int* pBankB = (int*)FUN_803826d4_lo();

    iMaxSize = -1;

    for (nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
        int nCount = g_ActorCounts[nBank];
        iOffset = 0;
        for (i = 0; i < nCount; i++) {
            // Sum sizes from both banks for each actor slot
            int iSizeA = (pBankA != NULL) ? *(int*)(pBankA + iOffset + 0x14) : 0;
            int iSizeB = (pBankB != NULL) ? *(int*)(pBankB + iOffset + 0x14) : 0;
            int iTotal = iSizeA + iSizeB;

            // Check if actor has associated file (adds 512KB for streaming)
            if (pBankA != NULL) {
                pName = *(char**)(pBankA + iOffset + 4);
                if (pName != NULL) {
                    if (strstr(pName, &g_StreamExt) != NULL) {
                        iTotal += 0x7d000;
                    }
                }
            }
            if (pBankB != NULL) {
                pName = *(char**)(pBankB + iOffset + 4);
                if (pName != NULL) {
                    if (strstr(pName, &g_StreamExt) != NULL) {
                        iTotal += 0x7d000;
                    }
                }
            }

            if (iMaxSize < iTotal) {
                iMaxSize = iTotal;
            }
            iOffset += ACTOR_ENTRY_SIZE;
        }
    }

    if (iMaxSize == -1) {
        get_mode_resource_size();
    } else {
        // Align to 32 bytes and create pool
        resource_pool_init_default((iMaxSize + 0x1F) & ~0x1F);
    }
    FUN_80382720(); // epilogue
}

// ============================================================================
// Address: 0x800297dc | level_actors_teardown
// Tears down level actors. Destroys loaded actor resources,
// invalidates all slot handles, and resets state.
// ============================================================================
void level_actors_teardown(void) {
    int bShouldClean;

    FUN_803826dc(); // prologue, bShouldClean in r6

    if (bShouldClean != 0) {
        // Destroy main level resource
        if (g_LevelHandle != -1) {
            FUN_80187260(g_LevelHandle);
        }

        // Destroy per-bank resources
        for (int i = 0; i < MAX_ACTOR_BANKS; i++) {
            if (g_BankHandles[i] != -1) {
                FUN_8002a948(); // destroy bank resource
            }
            g_BankHandles[i] = -1;
            g_BankResourceHandles[i] = -1;
        }

        g_LevelHandle = -1;
        g_ActorSystemReady = 1;
    }
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x80029884 | level_get_current_actor_flags
// Gets flags word from current actor's entry in the level actor table.
// ============================================================================
u32 level_get_current_actor_flags(void) {
    return *(u32*)(g_ActorTables[1] + g_CurrentActorIndex * ACTOR_ENTRY_SIZE + 0xc);
}

// ============================================================================
// Address: 0x800298a4 | level_get_player_actor_id
// Gets the loadable actor ID for a player slot.
// Checks game state flags and player data for validity.
// Returns -1 if not available.
// ============================================================================
int level_get_player_actor_id(int nPlayerSlot) {
    // Check global state flags - must not be in cutscene/pause
    if ((g_GameStateFlags & 0x20000000) != 0 || (g_GameStateFlags & 0x200) != 0) {
        return -1;
    }

    // Check player has a valid character assignment
    if (*(int*)(g_PlayerDataB + nPlayerSlot * 0xea0 + 0x678) == -1) {
        return -1;
    }

    // Look up actor by player's character flag
    int nActorId = level_find_actor_by_flag(
        *(int*)(g_PlayerDataA + nPlayerSlot * 0x141c + 0x1400));
    return nActorId;
}

// ============================================================================
// Address: 0x80029920 | level_find_actor_by_flag
// Searches actor tables for an entry matching a specific bit flag.
// Returns the actor's ID, or -1 if not found.
// ============================================================================
int level_find_actor_by_flag(int nFlagBit) {
    for (int nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
        int nCount = g_ActorCounts[nBank];
        int iOffset = 0;
        for (int i = 0; i < nCount; i++) {
            int* pEntry = (int*)(g_ActorTables[nBank] + iOffset);
            if ((1 << nFlagBit & pEntry[3]) != 0) {
                return *pEntry;  // actor ID
            }
            iOffset += ACTOR_ENTRY_SIZE;
        }
    }
    return -1;
}

// ============================================================================
// Address: 0x8002998c | level_actor_load_for_players
// Triggers actor loading for both player slots (multiplayer support).
// Checks player count and triggers second slot load if applicable.
// ============================================================================
void level_actor_load_for_players(void) {
    int nActorId;

    // Player 1
    nActorId = level_get_player_actor_id(0);
    if (nActorId != -1) {
        int result = FUN_8002a0f4(nActorId);
        if (result == 1 && g_GameMode == '\n' && g_NumPlayers > 1) {
            FUN_8025b098();  // Signal P1 ready
        }
    }

    // Player 2 (if multiplayer)
    if (g_GameMode == '\n' && g_NumPlayers > 1) {
        nActorId = level_get_player_actor_id(1);
        if (nActorId != -1) {
            int result = FUN_8002a0f4(nActorId);
            if (result == 1) {
                FUN_8025b098();  // Signal P2 ready
            }
        }
    }
}

// ============================================================================
// Address: 0x80029d9c | level_actor_begin_load
// Initiates actor loading sequence. Finds actor in table, sets state,
// and kicks off the load state machine.
// State transitions: 1 -> 2 (async) or 1 -> 0x20 (immediate if found).
// ============================================================================
void level_actor_begin_load(int nActorId, int bImmediate) {
    int iSlot;

    if (g_ActorLoadState != 1) {
        FUN_80382730();
        return;
    }

    FUN_80037b74();  // Pre-load preparation

    int iFoundSlot = -1;
    for (int nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
        iSlot = level_find_actor_slot(nActorId, nBank);
        if (iSlot != -1) {
            iFoundSlot = 0;
            g_BankResourceHandles[nBank] = iSlot;
        }
    }

    if (bImmediate == 0) {
        g_ActorLoadState = 2;  // Async streaming path
    } else if (iFoundSlot == 0) {
        g_ActorLoadState = 0x20;  // Immediate load path
        level_actor_load_state_machine();
    }
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x80029e60 | level_actor_is_loaded
// Checks if actor loading is complete (state has LOADED flag set).
// ============================================================================
BOOL level_actor_is_loaded(void) {
    return (g_ActorLoadState & 0x10) != 0;
}

// ============================================================================
// Address: 0x80029e70 | level_actor_check_active
// Checks if a specific actor ID matches the currently active/loaded one.
// ============================================================================
void level_actor_check_active(int nActorId) {
    if ((g_ActorLoadState & 0x1e) == 0) {
        FUN_80382734();
        return;
    }

    for (int nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
        int iSlot = level_find_actor_slot(nActorId, nBank);
        if (iSlot == g_BankResourceHandles[nBank] && g_ActorLoadState == 0x10) {
            break;  // Found: this actor is the active one
        }
    }
    FUN_80382734(); // epilogue
}

// ============================================================================
// Address: 0x80029f08 | level_actor_get_load_status
// Gets load status for a specific actor ID.
// Returns: -1 (not relevant), 0 (fully loaded), -3 (loading in progress)
// ============================================================================
int level_actor_get_load_status(int nActorId) {
    if ((g_ActorLoadState & 0x1e) == 0) {
        return -1;
    }

    int iSlot = level_find_actor_slot(nActorId, 1);
    if (iSlot != g_CurrentActorIndex) {
        return -1;
    }

    if (g_ActorLoadState == 0x10) {
        return 0;   // Fully loaded
    }
    return -3;      // Loading in progress
}

// ============================================================================
// Address: 0x80029f7c | level_actor_unload
// Unloads current actor from a specific bank. Releases resource slot,
// destroys DVD file handle, and resets bank state.
// ============================================================================
void level_actor_unload(int nActorId) {
    if ((g_ActorLoadState & 0x1e) == 0) {
        FUN_8038272c();
        return;
    }

    for (int nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
        int iSlot = level_find_actor_slot(nActorId, nBank);
        if (iSlot == g_BankResourceHandles[nBank]) {
            if (g_ActorLoadState == 0x10) {
                // Fully loaded - full teardown
                if (g_ActorResourceSlot != 0) {
                    resource_slot_release(g_ActorResourceSlot);
                }
                if (g_LevelHandle != -1) {
                    FUN_80187260(g_LevelHandle);
                }
                if (g_BankHandles[nBank] != -1) {
                    FUN_8002a948();
                }
                g_BankHandles[nBank] = -1;
                g_LevelHandle = -1;
                g_BankResourceHandles[nBank] = -1;
                g_ActorResourceSlot = 0;
                g_ActorLoadOffset = 0;
            } else if (g_BankResourceHandles[nBank] != -1) {
                goto done;  // Still loading - can't unload yet
            }
        }
    }
    g_ActorLoadState = 1;

done:
    FUN_8038272c(); // epilogue
}

// ============================================================================
// Address: 0x8002a080 | level_actor_unload_all
// Unloads all active actors across all banks.
// ============================================================================
void level_actor_unload_all(void) {
    FUN_803826f0(); // prologue

    for (int nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
        if (g_BankResourceHandles[nBank] != -1) {
            level_actor_unload(/* actor from bank */);
        }
    }
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x80029a30 | level_actor_load_state_machine
// Main actor loading state machine. Handles async DVD streaming of actor
// data files into the resource pool.
//
// States:
//   1  = IDLE (waiting for begin_load)
//   2  = ACQUIRE_SLOT (allocate resource pool slot)
//   4  = START_LOAD (initiate DVD file read for current bank)
//   8  = WAIT_LOAD (polling DVD read completion)
//   16 = LOADED (actor ready for use)
//   32 = IMMEDIATE (synchronous load path)
//
// Per bank, loads the actor's data file from DVD, processes it through
// a post-load callback if one exists, advances to next bank, then
// transitions to LOADED state and signals completion.
// ============================================================================
void level_actor_load_state_machine(void) {
    int iActorEntry;
    int iResult;
    int iResourceBase;

    switch (g_ActorLoadState) {
    case 2: // ACQUIRE_SLOT
        FUN_801851d8();  // Prepare streaming context
        iResult = resource_slot_acquire(&g_ActorLoadOffset, level_actors_teardown, 0);
        if (iResult < 0) {
            g_ActorLoadOffset = g_ActorResourceSlot;
        } else {
            g_ActorLoadOffset = g_ActorLoadOffset;
        }
        g_ActorLoadState = 4;
        g_CurrentLoadBank = 0;
        g_ActorResourceSlot = g_ActorLoadOffset;
        break;

    case 4: // START_LOAD
        if (g_ActorTables[g_CurrentLoadBank] == 0) {
            g_CurrentLoadBank++;
        } else {
            iActorEntry = g_ActorTables[g_CurrentLoadBank] +
                          g_BankResourceHandles[g_CurrentLoadBank] * ACTOR_ENTRY_SIZE;
            // Start async DVD read
            g_LevelHandle = dvd_load_file(
                *(char**)(iActorEntry + 4), 0, g_ActorTables[g_CurrentLoadBank]);
            if (g_LevelHandle != -1) {
                // Configure resource target
                FUN_801873f0(g_LevelHandle, g_ActorLoadOffset, 0,
                    (*(int*)(iActorEntry + 0x14) + 0x1F) & ~0x1F);
                g_ActorLoadState = 8;
                FUN_801859a4();
            }
        }
        break;

    case 8: // WAIT_LOAD
        if (FUN_80187544(g_LevelHandle) == 0) break;  // Still loading

        // Load complete - process data
        iActorEntry = g_ActorTables[g_CurrentLoadBank] +
                      g_BankResourceHandles[g_CurrentLoadBank] * ACTOR_ENTRY_SIZE;
        g_ActorAnimData = FUN_8002a264(g_ActorLoadOffset, *(int*)(iActorEntry + 0x14), 6);
        g_BankHandles[g_CurrentLoadBank] = g_ActorAnimData;
        FUN_80187260(g_LevelHandle);  // Release DVD handle

        // Call post-load callback if present
        if (*(void**)(iActorEntry + 8) != NULL) {
            (*(void(*)(void))(*(int*)(iActorEntry + 8)))();
        }
        g_LevelHandle = -1;

        // Advance to next bank
        do {
            g_CurrentLoadBank++;
            if (g_CurrentLoadBank < MAX_ACTOR_BANKS) {
                if (g_BankResourceHandles[g_CurrentLoadBank] != -1 &&
                    *(int*)(g_ActorTables[g_CurrentLoadBank] +
                        g_BankResourceHandles[g_CurrentLoadBank] * ACTOR_ENTRY_SIZE)
                        != ACTOR_MAGIC_EMPTY) {
                    g_ActorLoadState = 4;  // Load next bank
                    g_ActorLoadOffset += (*(int*)(iActorEntry + 0x14) + 0x1F) & ~0x1F;
                    break;
                }
            } else {
                // All banks loaded
                g_ActorLoadState = 0x10;
                FUN_802330a0();  // Signal load complete
                break;
            }
        } while (1);
        break;

    case 0x20: // IMMEDIATE (synchronous load)
        iResult = resource_slot_acquire(&iResourceBase, level_actors_teardown, 0);
        if (iResult < 0) break;

        g_ActorResourceSlot = iResourceBase;
        g_ActorLoadOffset = iResourceBase;
        g_CurrentLoadBank = 0;

        for (int nBank = 0; nBank < MAX_ACTOR_BANKS; nBank++) {
            if (g_BankResourceHandles[nBank] != -1) {
                int* pEntry = (int*)(g_ActorTables[nBank] +
                    g_BankResourceHandles[nBank] * ACTOR_ENTRY_SIZE);
                if (*pEntry != ACTOR_MAGIC_EMPTY) {
                    // Synchronous copy
                    memcpy(auBuf, (void*)pEntry[1], 0x100);
                    g_ActorLoadOffset = iResourceBase;
                    FUN_8002b1c8(auBuf, iResourceBase, pEntry[5]);
                    FUN_801859a4();
                    g_ActorAnimData = FUN_8002a264(g_ActorLoadOffset, pEntry[5], 6);
                    g_BankHandles[nBank] = g_ActorAnimData;

                    // Post-load callback
                    if ((void*)pEntry[2] != NULL) {
                        (*(void(*)(void))(pEntry[2]))();
                    }
                    iResourceBase += (pEntry[5] + 0x1F) & ~0x1F;
                }
            }
        }
        g_LevelHandle = -1;
        g_ActorLoadState = 0x10;
        FUN_802330a0();  // Signal load complete
        break;
    }
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x8002a0f4 | level_actor_switch
// Orchestrates actor switch: checks if already loaded, unloads current,
// begins new load. Returns 1 on success, 0 on failure/no-op.
// ============================================================================
int level_actor_switch(int nTargetActorId) {
    int nCurrentActor = FUN_80033bbc();

    if (nCurrentActor == nTargetActorId) {
        return 0;  // Already the active actor
    }

    if ((g_GameStateFlags & 0x200) != 0) {
        return 0;  // Can't switch during this state
    }

    if (nCurrentActor != -1) {
        int result = level_actor_unload();
        if (result == -5) {
            return 0;  // Unload blocked
        }
    }

    FUN_80026648();  // Prepare for new actor
    level_actor_begin_load(nTargetActorId);
    FUN_80033bcc(nTargetActorId);  // Set current actor tracking
    return 1;
}

// ============================================================================
// Address: 0x8002a190 | game_is_in_cutscene_mode
// Returns bit 20 of global game state flags (cutscene active).
// ============================================================================
BOOL game_is_in_cutscene_mode(void) {
    return (g_GameStateFlags >> 20) & 1;
}
