    // Crate contents.
    //
    // StandardContainer (class hash 0xD8507464, decoder 0x005F3670, dispatch table
    // 0x005F41F8) names its contents directly: property 0 is a guaranteed item at
    // +0x2E4, property 1 the drop table an ordinary crate rolls instead, at +0x2F4.
    //
    // Across all 794 archives: 585 crates, 509 roll only a table and 76 carry a
    // guaranteed item, 37 of them a Power Node. Those 37 are the fixed upgrade node
    // locations, so moving them changes the route and when upgrades are affordable.
    //
    // The shuffle is closed: the 74 participating crates trade items among
    // themselves, so nothing new appears and no crate can hold something its level
    // does not stream. Two crates are left out, one holding an audio log (story
    // order matters) and one holding an unidentified item. As in the loot pool,
    // only items measured to be PowerUp or SpareAmmo move.
    //
    // A crate has no unique authored value at a fixed offset: properties 2, 10 and
    // 11 are shared templates (8 distinct across all 76), and 27 to 33 go to stack
    // locals. So the hook searches the object for any of the 74 known crate ids, and
    // logs it and leaves the crate alone when none is found.
    constexpr uintptr_t kStandardContainerDecode = 0x005F3670;
    constexpr unsigned kCrateFixedItem = 0x2E4;
    constexpr unsigned kCrateObjectSize = 0x580;

    struct CrateItem
    {
        Guid128 id;
        const char* name;
    };

    struct FixedCrate
    {
        Guid128 guid;
        unsigned item;
    };

    constexpr CrateItem kCrateItems[] = {
        {{{0x30C8A723u, 0xCBB05BE2u, 0x4E484F4Au, 0x324E4F53u}}, "Ruby Treasure"},
        {{{0x30C8A727u, 0x8E8CAB31u, 0x4E484F4Au, 0x324E4F53u}}, "Gold Treasure"},
        {{{0x35C7C97Fu, 0xCCF3A350u, 0x4854414Du, 0x37375355u}}, "Air Cannister"},
        {{{0x36C836ACu, 0xE6FCE89Au, 0x44455257u, 0x34303245u}}, "Currency 4"},
        {{{0x36C836ACu, 0xF8A08E76u, 0x44455257u, 0x34303245u}}, "Currency 3"},
        {{{0x36C836DBu, 0x337E1BAEu, 0x44455257u, 0x34303245u}}, "Currency 2"},
        {{{0x37C7B444u, 0x8607BF9Du, 0x4E59414Du, 0x36445241u}}, "Power Node"},
        {{{0x4BC78C36u, 0x9D5B89BAu, 0x544E4543u, 0x53574F4Bu}}, "Small Health Kit"},
        {{{0x4BC78C36u, 0x9D5DEC14u, 0x544E4543u, 0x53574F4Bu}}, "Medium Health Kit"},
        {{{0x4BC78C36u, 0x9D604E6Eu, 0x544E4543u, 0x53574F4Bu}}, "Large Health Kit"},
        {{{0x4BC7F007u, 0x713FFAEEu, 0x544E4543u, 0x53574F4Bu}}, "Double Air Cannister"},
        {{{0x4BC806EAu, 0x40D9D6F7u, 0x544E4543u, 0x53574F4Bu}}, "None (empty)"},
    };

    constexpr FixedCrate kFixedCrates[] = {
        {{{0x2EE0EEFAu, 0x2EE0EEFAu, 0x2EE0EEFAu, 0x2EE0EEFAu}}, 6},  // Power Node, cryogenics_z01.str
        {{{0x30C83B6Bu, 0xB0CD84FEu, 0x4E484F4Au, 0x324E4F53u}}, 6},  // Power Node, res_messhall_z02.str
        {{{0x30C83B6Cu, 0x2016B3D7u, 0x4E484F4Au, 0x324E4F53u}}, 4},  // Currency 3, res_hallway_z02.str
        {{{0x30C842C0u, 0xAC1D3291u, 0x4E484F4Au, 0x324E4F53u}}, 9},  // Large Health Kit, res_sleepblocka_z02.str
        {{{0x30C842C1u, 0xF9A35618u, 0x4E484F4Au, 0x324E4F53u}}, 6},  // Power Node, res_sleepblockb_z03.str
        {{{0x32C8C200u, 0xF7D444C8u, 0x41485350u, 0x4B534544u}}, 0},  // Ruby Treasure, rec_lockerroom_z01.str
        {{{0x32C8C200u, 0xF7D444CBu, 0x41485350u, 0x4B534544u}}, 4},  // Currency 3, rec_lockerroom_z01.str
        {{{0x32C8C200u, 0xF7D444CDu, 0x41485350u, 0x4B534544u}}, 11},  // None (empty), rec_lockerroom_z01.str
        {{{0x32C8C200u, 0xF7D444D0u, 0x41485350u, 0x4B534544u}}, 8},  // Medium Health Kit, rec_lockerroom_z01.str
        {{{0x32C8C200u, 0xF7D444D1u, 0x41485350u, 0x4B534544u}}, 6},  // Power Node, rec_lockerroom_z01.str
        {{{0x33C85C92u, 0xF9EF70B2u, 0x45544150u, 0x3233374Cu}}, 9},  // Large Health Kit, medicalstorage_z01.str
        {{{0x33C85C92u, 0xF9EF70B2u, 0x45544150u, 0x3233374Cu}}, 9},  // Large Health Kit, medicalstorage_z01.str
        {{{0x33C878E5u, 0x587DD1DAu, 0x45544150u, 0x3336304Cu}}, 6},  // Power Node, medicalstorage_z01.str
        {{{0x35C8BDE2u, 0xA44A9470u, 0x4854414Du, 0x37375355u}}, 6},  // Power Node, biolab_z01.str
        {{{0x36C87669u, 0x72A777FFu, 0x44455257u, 0x34303245u}}, 8},  // Medium Health Kit, cargoHall_z01.str
        {{{0x36C878F1u, 0x540FCEF8u, 0x44455257u, 0x34303245u}}, 1},  // Gold Treasure, torpedo_z01.str
        {{{0x36C8796Bu, 0x17574106u, 0x44455257u, 0x34303245u}}, 6},  // Power Node, armory_z01.str
        {{{0x36C87A93u, 0xFBBF5768u, 0x44455257u, 0x34303245u}}, 8},  // Medium Health Kit, engineHall_z01.str
        {{{0x36C89AA9u, 0x34E9245Du, 0x44455257u, 0x34303245u}}, 10},  // Double Air Cannister, tramstation_z02.str
        {{{0x36C89B5Cu, 0x95A53C1Cu, 0x44455257u, 0x34303245u}}, 6},  // Power Node, barracksHall_z01.str
        {{{0x36C8ABCEu, 0x875520C1u, 0x45544150u, 0x3230314Cu}}, 6},  // Power Node, chemistry_lab_z01.str
        {{{0x36C8B6A9u, 0x2302529Fu, 0x494C4F4Du, 0x3736414Eu}}, 6},  // Power Node, medicalstorage_z01.str
        {{{0x36C8C1C0u, 0xEAA3D225u, 0x4E455546u, 0x31534554u}}, 10},  // Double Air Cannister, destroyedhall_z01.str
        {{{0x38C85254u, 0x04EF0FA7u, 0x54544950u, 0x32303253u}}, 11},  // None (empty), extractionroom_z01.str
        {{{0x38C86D0Eu, 0xEF8B2158u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, tramhall_z01.str
        {{{0x38C86D12u, 0xC307FC16u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, proccontrol_z02.str
        {{{0x38C86D14u, 0x7D573B00u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, mainannex_z02.str
        {{{0x38C86D17u, 0xBF63BF71u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, miningcontrol_z01.str
        {{{0x38C86FF2u, 0xDD179482u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, storagechamber_z01.str
        {{{0x38C86FF4u, 0x11ACD488u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, emergencytunnel_z01.str
        {{{0x38C86FF4u, 0x3FCB9ED8u, 0x54544950u, 0x32303253u}}, 6},  // Power Node, landingpad_z02.str
        {{{0x38C86FF4u, 0xBDD0DD42u, 0x54544950u, 0x32303253u}}, 9},  // Large Health Kit, zerogtunnel_z02.str
        {{{0x45C86446u, 0xC7CF4056u, 0x41455753u, 0x474E4952u}}, 5},  // Currency 2, atmocontrol_z01.str
        {{{0x45C89B43u, 0xEEBBBF5Du, 0x41455753u, 0x474E4952u}}, 5},  // Currency 2, easthall_z04.str
        {{{0x45C8AC02u, 0xA070A652u, 0x41455753u, 0x474E4952u}}, 6},  // Power Node, cargoArea_z01.str
        {{{0x45C8AC02u, 0xB3706B7Au, 0x41455753u, 0x474E4952u}}, 6},  // Power Node, hangar_z07.str
        {{{0x48C8D0C7u, 0x9C040EDEu, 0x5543434Du, 0x434F4C4Cu}}, 2},  // Air Cannister, decon_z01.str
        {{{0x48C8D0C7u, 0xB013A08Cu, 0x5543434Du, 0x434F4C4Cu}}, 2},  // Air Cannister, decon_z01.str
        {{{0x49C86947u, 0x722FDDB8u, 0x4B544E45u, 0x4B53574Fu}}, 7},  // Small Health Kit, workshop_z01.str
        {{{0x49C86947u, 0x722FDDB8u, 0x4B544E45u, 0x4B53574Fu}}, 7},  // Small Health Kit, workshop_z01.str
        {{{0x49C86947u, 0x96D0FD24u, 0x4B544E45u, 0x4B53574Fu}}, 3},  // Currency 4, workshop_z01.str
        {{{0x49C86947u, 0x96D0FD24u, 0x4B544E45u, 0x4B53574Fu}}, 3},  // Currency 4, workshop_z01.str
        {{{0x49C86948u, 0xA3C4632Cu, 0x4B544E45u, 0x4B53574Fu}}, 7},  // Small Health Kit, tramCtrl_z01.str
        {{{0x49C86948u, 0xA3C4632Cu, 0x4B544E45u, 0x4B53574Fu}}, 7},  // Small Health Kit, tramCtrl_z01.str
        {{{0x49C86CE4u, 0xD1ECC0E8u, 0x4B544E45u, 0x4B53574Fu}}, 8},  // Medium Health Kit, mainBay_z03.str
        {{{0x49C86CE4u, 0xD1ECC0E8u, 0x4B544E45u, 0x4B53574Fu}}, 8},  // Medium Health Kit, mainBay_z03.str
        {{{0x49C86CE5u, 0x1C00B04Eu, 0x4B544E45u, 0x4B53574Fu}}, 7},  // Small Health Kit, mainBay_z01.str
        {{{0x49C86CE5u, 0x1C00B04Eu, 0x4B544E45u, 0x4B53574Fu}}, 7},  // Small Health Kit, mainBay_z01.str
        {{{0x49C86CE8u, 0x8AFDE2F5u, 0x4B544E45u, 0x4B53574Fu}}, 5},  // Currency 2, tramRepair_z02.str
        {{{0x49C86CE8u, 0x8AFDE2F5u, 0x4B544E45u, 0x4B53574Fu}}, 5},  // Currency 2, tramRepair_z02.str
        {{{0x49C86EC1u, 0x8FE34937u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, mainBay_z03.str
        {{{0x49C86EC1u, 0x8FE34937u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, mainBay_z03.str
        {{{0x49C86EC1u, 0xE422E81Fu, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, tramRepair_z02.str
        {{{0x49C86EC1u, 0xE422E81Fu, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, tramRepair_z02.str
        {{{0x49C875C9u, 0x04A0E558u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, huntgrnd_z05.str
        {{{0x49C875CDu, 0x6AECCEBFu, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, fuelstorage_z05.str
        {{{0x49C894F8u, 0x09377812u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, arena_z03.str
        {{{0x49C894FAu, 0x22484FA0u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, centrifugeHall_z01.str
        {{{0x49C8BC48u, 0xB393FD56u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, filtration_z01.str
        {{{0x49C8BC4Cu, 0xE099DFD6u, 0x4B544E45u, 0x4B53574Fu}}, 6},  // Power Node, eastzeroghall_z04.str
        {{{0x4BC828A1u, 0x9E0AE921u, 0x544E4543u, 0x53574F4Bu}}, 5},  // Currency 2, tramCtrl_z01.str
        {{{0x4BC828A1u, 0x9E0AE921u, 0x544E4543u, 0x53574F4Bu}}, 5},  // Currency 2, tramCtrl_z01.str
        {{{0x4BC8D110u, 0x91B22011u, 0x544E4543u, 0x53574F4Bu}}, 7},  // Small Health Kit, decon_z01.str
        {{{0x5AC85EF5u, 0x06C15A69u, 0x52444F52u, 0x45554749u}}, 5},  // Currency 2, miningadmin_z05.str
        {{{0x5AC85EF9u, 0x0131217Eu, 0x52444F52u, 0x45554749u}}, 9},  // Large Health Kit, atrium_z02.str
        {{{0x5AC85EF9u, 0x0131217Eu, 0x52444F52u, 0x45554749u}}, 9},  // Large Health Kit, atrium_z02.str
        {{{0x5AC8A572u, 0x84D302E4u, 0x52444F52u, 0x45554749u}}, 6},  // Power Node, securityroom_z01.str
        {{{0x5AC8A572u, 0x84D302E4u, 0x52444F52u, 0x45554749u}}, 6},  // Power Node, securityroom_z01.str
        {{{0x5AC8A572u, 0xE81091E4u, 0x52444F52u, 0x45554749u}}, 6},  // Power Node, miningadmin_z03.str
        {{{0x5AC8AC02u, 0x193CB841u, 0x52444F52u, 0x45554749u}}, 6},  // Power Node, gondolatunnel_z01.str
        {{{0x5AC8AC09u, 0x37F4DF9Bu, 0x52444F52u, 0x45554749u}}, 6},  // Power Node, mdscannonb_z01.str
        {{{0x5AC8C8D7u, 0x96B3808Fu, 0x52444F52u, 0x45554749u}}, 6},  // Power Node, airlockarray_z01.str
        {{{0xD5110002u, 0xE00000F3u, 0x50315344u, 0x314C4F4Fu}}, 6},  // Power Node, ds1_enemy_pool_ordinary.str
        {{{0xD5110002u, 0xE00000F3u, 0x50315344u, 0x314C4F4Fu}}, 6},  // Power Node, ds1_enemy_pool_ordinary_2.str
    };

    constexpr unsigned kCrateItemCount =
        sizeof(kCrateItems) / sizeof(kCrateItems[0]);
    constexpr unsigned kFixedCrateCount =
        sizeof(kFixedCrates) / sizeof(kFixedCrates[0]);
    static_assert(kFixedCrateCount > 1, "nothing to trade between");

    safetyhook::InlineHook gContainerDecodeHook;
    bool gCrateRandomizer = false;
    unsigned gCrateAssignment[kFixedCrateCount]{};
    unsigned gCratesSeen = 0;
    unsigned gCratesIdentified = 0;
    bool gCrateOffsetReported = false;

    // Power Nodes stay out of the opened crates: picking a node off the floor of
    // one is fiddly and reads as a broken pickup. They may drop from breakable
    // crates instead (ApplyBreakablePowerNode).
    constexpr unsigned kCratePowerNode = 6;
    // Share of the retail node count that moves to breakable crates, per chapter.
    unsigned gCratePowerNodePercent = 100;

    void BuildCrateAssignment()
    {
        for (unsigned i = 0; i < kFixedCrateCount; ++i)
            gCrateAssignment[i] = kFixedCrates[i].item;
        // Fisher-Yates from the shared seed, computed once, so the layout is
        // fixed for the whole run and a checkpoint reload cannot reroll it.
        // Only the crates without a node trade; a node crate then takes one of
        // their items, drawn from the same seed.
        unsigned others[kFixedCrateCount]{};
        unsigned otherCount = 0;
        for (unsigned i = 0; i < kFixedCrateCount; ++i)
            if (kFixedCrates[i].item != kCratePowerNode)
                others[otherCount++] = i;
        uint32_t state = gStartingSuitSeed ^ 0x43524154u;
        auto next = [&state]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        for (unsigned i = otherCount - 1; i > 0; --i)
        {
            const unsigned j = next() % (i + 1);
            const unsigned swap = gCrateAssignment[others[i]];
            gCrateAssignment[others[i]] = gCrateAssignment[others[j]];
            gCrateAssignment[others[j]] = swap;
        }
        for (unsigned i = 0; i < kFixedCrateCount; ++i)
            if (kFixedCrates[i].item == kCratePowerNode)
                gCrateAssignment[i] = kFixedCrates[others[next() % otherCount]].item;
        unsigned moved = 0;
        for (unsigned i = 0; i < kFixedCrateCount; ++i)
            if (gCrateAssignment[i] != kFixedCrates[i].item)
                ++moved;
        Log("crate randomizer: %u of %u crates changed contents",
            moved, kFixedCrateCount);
    }

    // Which of the known crates is this object?  Returns its index, or -1.
    int IdentifyCrate(uintptr_t object, unsigned& foundAt)
    {
        for (unsigned off = 0; off + sizeof(Guid128) <= kCrateObjectSize;
             off += 4)
        {
            uint32_t first = 0;
            __try
            {
                std::memcpy(&first, reinterpret_cast<const void*>(object + off),
                            sizeof(first));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
            if (first == 0)
                continue;
            for (unsigned c = 0; c < kFixedCrateCount; ++c)
            {
                if (kFixedCrates[c].guid.value[0] != first)
                    continue;
                Guid128 candidate{};
                __try
                {
                    std::memcpy(&candidate,
                                reinterpret_cast<const void*>(object + off),
                                sizeof(candidate));
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return -1;
                }
                if (SameGuid(candidate, kFixedCrates[c].guid))
                {
                    foundAt = off;
                    return static_cast<int>(c);
                }
            }
        }
        return -1;
    }

    void ApplyCrateContents(void* self)
    {
        if (!gCrateRandomizer || !self)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        const uintptr_t slot = object + kCrateFixedItem;

        Guid128 current{};
        __try
        {
            std::memcpy(&current, reinterpret_cast<const void*>(slot),
                        sizeof(current));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        // Only crates with a guaranteed item are in the table at all; the 509
        // that merely roll a table have nothing here to move.
        if (current.value[0] == 0 && current.value[1] == 0 &&
            current.value[2] == 0 && current.value[3] == 0)
            return;

        ++gCratesSeen;
        unsigned foundAt = 0;
        const int crate = IdentifyCrate(object, foundAt);
        if (crate < 0)
        {
            if (gCratesSeen <= 4)
                Log("crate %08X: not identified - its id is not stored in the "
                    "object, so the shuffle cannot run",
                    static_cast<unsigned>(object));
            return;
        }
        if (!gCrateOffsetReported)
        {
            gCrateOffsetReported = true;
            Log("crate randomizer: entity ids live at +0x%X in the object",
                foundAt);
        }
        ++gCratesIdentified;

        // The authored content must be what the sweep recorded, otherwise
        // this is not the crate the table thinks it is.
        const Guid128& authored = kCrateItems[kFixedCrates[crate].item].id;
        if (!SameGuid(current, authored))
        {
            Log("crate %08X: holds something the table did not expect, "
                "left alone", static_cast<unsigned>(object));
            return;
        }

        const CrateItem& give = kCrateItems[gCrateAssignment[crate]];
        if (SameGuid(current, give.id))
            return;
        SIZE_T written = 0;
        if (!WriteProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<void*>(slot),
                                &give.id, sizeof(give.id), &written))
            return;
        if (gCratesIdentified <= 12)
            Log("crate %08X: %s -> %s", static_cast<unsigned>(object),
                kCrateItems[kFixedCrates[crate].item].name, give.name);
    }

    void __fastcall ContainerDecode_Hook(void* self, void*, void* attributes)
    {
        gContainerDecodeHook.thiscall<void>(self, attributes);
        ApplyCrateContents(self);
        ApplyLogCrate(self, kCrateFixedItem);
    }

    // A breakable crate spawns the item at +0x3D0 when that is set and rolls its
    // drop table otherwise (0x005F215E). Every chapter gets as many nodes as its
    // opened crates held in retail, in breakable crates of that chapter the seed
    // picks; a flat 11% per crate left chapter 1 with none (seed 0x6690023F).
    // Drop tables are shared by crates, opened boxes and necromorphs, so they
    // are left alone. Tables: breakable_nodes.py.
    struct BreakableChapter
    {
        uint32_t chapter, quota, first, count;
    };
    constexpr BreakableChapter kBreakableChapters[] = {
#define BREAKABLE_CHAPTERS
#include "breakable_nodes_generated.inc"
#undef BREAKABLE_CHAPTERS
    };
    constexpr Guid128 kBreakableCrates[] = {
#include "breakable_nodes_generated.inc"
    };
    constexpr unsigned kBreakableCrateCount = sizeof(kBreakableCrates) / sizeof(kBreakableCrates[0]);
    bool gBreakableNode[kBreakableCrateCount]{};
    constexpr uintptr_t kBreakableContainerDecode = 0x005F1E80;
    constexpr unsigned kBreakableFixedItem = 0x3D0;
    safetyhook::InlineHook gBreakableDecodeHook;
    volatile LONG gBreakablesSeen = 0, gBreakablePowerNodes = 0;

    // Fixed for the run, so a reload finds the nodes where they were.
    void BuildBreakableNodes()
    {
        unsigned total = 0;
        for (const BreakableChapter& chapter : kBreakableChapters)
        {
            unsigned order[64]{};
            const unsigned count = chapter.count < 64 ? chapter.count : 64;
            for (unsigned i = 0; i < count; ++i)
                order[i] = chapter.first + i;
            uint32_t state = (gStartingSuitSeed ^ 0x504E4F44u) + chapter.chapter * 0x9E3779B9u;
            if (!state)
                state = 1;
            for (unsigned i = count; i > 1; --i)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                const unsigned j = state % i;
                const unsigned swap = order[i - 1];
                order[i - 1] = order[j];
                order[j] = swap;
            }
            unsigned nodes = (chapter.quota * gCratePowerNodePercent + 50) / 100;
            if (nodes > count)
                nodes = count;
            for (unsigned i = 0; i < nodes; ++i)
                gBreakableNode[order[i]] = true;
            total += nodes;
            Log("crate randomizer: chapter %u, %u Power Node(s) in %u breakable crate(s)", chapter.chapter, nodes,
                count);
        }
        Log("crate randomizer: %u Power Nodes in breakable crates in all", total);
    }

    void ApplyBreakablePowerNode(void* self)
    {
        if (!gCrateRandomizer || !self || !gCratePowerNodePercent)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        Guid128 id{}, fixed{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + 0x20), &id, sizeof(id),
                               nullptr) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kBreakableFixedItem), &fixed,
                               sizeof(fixed), nullptr))
            return;
        InterlockedIncrement(&gBreakablesSeen);
        if (fixed.value[0] | fixed.value[1] | fixed.value[2] | fixed.value[3])
            return;   // an authored item stays
        bool chosen = false;
        for (unsigned i = 0; i < kBreakableCrateCount && !chosen; ++i)
            chosen = gBreakableNode[i] && SameGuid(kBreakableCrates[i], id);
        if (!chosen)
            return;
        const Guid128& node = kCrateItems[kCratePowerNode].id;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kBreakableFixedItem), &node,
                                sizeof(node), nullptr))
            return;
        if (InterlockedIncrement(&gBreakablePowerNodes) <= 12)
            Log("crate %08X (%08X-%08X): this breakable crate holds a Power Node",
                static_cast<unsigned>(object), id.value[0], id.value[1]);
    }

    void __fastcall BreakableDecode_Hook(void* self, void*, void* attributes)
    {
        gBreakableDecodeHook.thiscall<void>(self, attributes);
        ApplyBreakablePowerNode(self);
    }

    void LoadCrateSettings()
    {
        gCrateRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "CrateRandomizer", 0, gIniPath) != 0;
        gCratePowerNodePercent = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "CratePowerNodePercent", 100, gIniPath);
        if (gCratePowerNodePercent > 200)
            gCratePowerNodePercent = 200;
        if (gCrateRandomizer)
        {
            BuildCrateAssignment();
            if (gCratePowerNodePercent)
                BuildBreakableNodes();
        }
    }

    void InstallCrateHooks()
    {
        // The log randomizer moves the one log crate through the same decoder.
        if (!gCrateRandomizer && !gLogRandomizer)
            return;
        gContainerDecodeHook = VerifiedInline(
            reinterpret_cast<void*>(kStandardContainerDecode),
            reinterpret_cast<void*>(&ContainerDecode_Hook));
        Log("crate randomizer: decoder %s at %08X (%u crates, %u item kinds)",
            gContainerDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kStandardContainerDecode),
            kFixedCrateCount, kCrateItemCount);
        if (gCrateRandomizer && gCratePowerNodePercent)
        {
            gBreakableDecodeHook = VerifiedInline(reinterpret_cast<void*>(kBreakableContainerDecode),
                                                  reinterpret_cast<void*>(&BreakableDecode_Hook));
            Log("crate randomizer: Power Nodes only from breakable crates, %u%% of the retail count; decoder %s at %08X",
                gCratePowerNodePercent, gBreakableDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kBreakableContainerDecode));
        }
    }
