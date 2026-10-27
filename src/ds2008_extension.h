// Dead Space 2008 Randomizer extension interface.
//
// DeadSpace2008Randomizer.asi (the host) loads an optional extension after the game code is decrypted
// (DeadSpace2008Randomizer_ConsoleDLC.asi: console DLC suits and weapons, the larger suit table, the suit paths)
// and calls it from its own hooks. An extension never hooks a function the host hooks, so two jumps never
// stack on one site. Without an extension the host behaves like retail there.
#pragma once
#include <cstdint>

namespace ds2008
{
    constexpr uint32_t kExtensionApiVersion = 3;
    constexpr char kExtensionExport[] = "DS2008_GetExtension";

    // What storeAddItem answers.
    enum StoreAnswer : int
    {
        kStoreKeep = 0,
        kStoreReplaced = 1,   // the GUID was rewritten
        kStoreHide = 2,       // the item does not enter the store
    };

    // What the host lends an extension.
    struct HostApi
    {
        uint32_t version;
        void (*log)(const char* line);
        const char* iniPath;    // the host's DeadSpace2008Randomizer.ini
        const char* gameRoot;   // the game folder, with a trailing backslash
        int (*streamState)(void* streamManager, uint32_t streamId);
        uintptr_t (*streamObject)(void* streamManager, uint32_t streamId);
        // The host's retail byte check for a fixed hook site (false: another exe or a site in use).
        bool (*hookSiteOk)(void* target, const char* kind);
        // The run seed (EnemyRandomizerSeed).
        uint32_t (*seed)();
        // The live entity a GUID names (SimManager key +0x14), 0 when none is resident.
        uintptr_t (*objectFor)(const uint32_t* guid);
        // The store requirement the host gives an item: PickupItem[20] chapter and [21] completed games, after its
        // own store gate rules. False for an item it does not know.
        bool (*storeRequirement)(const uint32_t* guid, int32_t* chapter, int32_t* played);
        // The store GUID of the weapon FirstWeaponRandomizer hands out instead of the Plasma Cutter. False when the
        // run starts with the cutter. The player owns it from chapter 1, so the kiosk does not list its row.
        bool (*firstWeaponItem)(uint32_t* guid);
    };

    // What an extension offers. Every function may be null.
    struct Extension
    {
        uint32_t version;
        const char* name;
        // After decryption, before global_assets loads.
        void (*install)();
        // True when the host must hook StreamManager::Load for it.
        bool (*wantsStreamHook)();
        // Before the original load: the last argument (1 opens a path inside a packed archive).
        int (*adjustLoad)(const char* path, int packed);
        // After the original load; globalAssets is true from the load of global_assets on.
        void (*loadSeen)(void* streams, const char* path, uint8_t flags, uint32_t result, uintptr_t caller,
                         bool globalAssets);
        // The host's poll thread, about every 100 ms.
        void (*poll)();
        // The UniformManager layout: where the fields after the suit table start, and how many suits it holds.
        uint32_t (*uniformTail)();
        uint32_t (*uniformSlots)();
        // True when the host must hook the store (AddItem 0x00596830 and its gate) and New Game for it.
        bool (*wantsStoreHooks)();
        // Game thread, before PlayerStore::AddItem copies the GUID. caller is AddItem's return address
        // (0x00596949 a store list, 0x005A0A43 a schematic upload, 0x005A0F30 the DLC content registry).
        int (*storeAddItem)(uint32_t* guid, uintptr_t caller);
        // Game thread, at the store gate after the host's rules: the entry's requirement may be changed.
        void (*storeGate)(const uint32_t* guid, int32_t* chapter, int32_t* played, bool forced);
        // Game thread: StartNewGame_Menu began a fresh run.
        void (*newGame)();
        // Game thread, when a new run puts on its first suit: the UniformManager entry to wear instead, or -1.
        int (*startingUniform)();
        // Game thread, when the chapter 1 Plasma Cutter is picked up or a loadout hands one out: true and the
        // GunProxy GUID to give instead. Not asked while FirstWeaponRandomizer has its own pick.
        bool (*firstWeapon)(uint32_t* proxy);
    };

    typedef const Extension* (__cdecl* GetExtensionFn)(const HostApi* host);
}
