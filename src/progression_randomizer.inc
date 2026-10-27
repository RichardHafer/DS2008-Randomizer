    // Seed-manifest progression randomizer.
    constexpr uintptr_t kSchematicSpawnInit = 0x00515E90;
    // The route that delivers the reward. With a Chapter 4 Force Gun schematic in
    // game, SpawnInit never ran and no diagnostic line appeared while the pickup
    // still read FORCE GUN. The attribute decoder writes the reward GUID to
    // this+0x630 (one stack argument, this in ECX, like the store decoder), so the
    // replacement goes behind it.
    constexpr uintptr_t kSchematicAttributeDecode = 0x00515BB0;
    constexpr uintptr_t kItemStoreListDecode = 0x0050FB00;
    constexpr uintptr_t kPlayerStoreInsertEntry = 0x00596A40;
    constexpr uintptr_t kPlayerStoreInsertEntryDone = 0x00596D04;
    // Native UniformManager apply path. Only observed here; starting suit
    // selection stays off until New Game and save restore can be told apart.
    constexpr uintptr_t kUniformApply = 0x005A2A40;
    // Retail StartNewGame_Menu: parses the embedded CH01 checkpoint and starts a
    // fresh run. Save loading and checkpoint restore take other paths.
    constexpr uintptr_t kStartNewGame = 0x00615580;
    constexpr unsigned kUniformCurrentIndexOffset = 0x954;
    constexpr unsigned kUniformPlayerOffset = 0x4C;
    constexpr unsigned kUniformStateByte0Offset = 0x958;
    constexpr unsigned kUniformStateByte1Offset = 0x959;
    constexpr unsigned kUniformStateWordOffset = 0x95C;
    constexpr unsigned kUniformTableOffset = 0x6C;
    constexpr unsigned kUniformEntryStride = 0x4C;
    constexpr unsigned kUniformSlotsOffset = 0x30;
    constexpr unsigned kUniformDamageFactorOffset = 0x34;
    constexpr unsigned kUniformColourOffset = 0x38;
    constexpr unsigned kSchematicTargetOffset = 0x630;
    constexpr unsigned kItemStoreEntriesOffset = 0x3C;
    // The installed manifest has 34 rows (all suits included); 32 silently dropped the last two.
    constexpr unsigned kMaxSchematicMappings = 64;
    constexpr unsigned kMaxStoreMappings = 8;

    struct SchematicMapping
    {
        Guid128 entity{};
        Guid128 source{};
        Guid128 target{};
        char sourceName[48]{};
        char targetName[48]{};
        char kind[16]{};
    };

    struct StoreMapping
    {
        unsigned slot = 0;
        Guid128 source{};
        Guid128 target{};
        char sourceName[48]{};
        char targetName[48]{};
    };

    bool gProgressionRandomizer = false;
    // Diagnostic: also swap the pair inside the ShipStore's own 26-entry
    // catalogue. A full dump showed the rewritten list is Player+0x1610 while the
    // shop's catalogue is ShipStore+0x794, and the shelf still showed the
    // authored weapons.
    bool gProgressionStoreCatalog = false;
    bool gProgressionDiagnostics = true;
    // Declared here because the settings are read long before the
    // world-weapon code further down.
    bool gFirstWeaponRandomizer = false;
    // Cheat: 0 off, otherwise 1-3 for the three weapons that exist as a world
    // pickup. Loading a checkpoint for a test hands out no weapon.
    int gCheatPickupWeapon = 0;
    bool gUpgradeBenchRandomizer = false;
    bool gLootRandomizer = false;
    bool gLootWeightRandomizer = false;
    bool gLootGlobalPool = false;
    bool gLootScarcityRandom = false;
    int gLootScarcity = 0;
    int gLootScarcityApplied = 0;
    // Earlier store attempts worked on an object the shop does not read, which
    // the insert hook could not show because it only reports calls it acts on.
    // This reports every call, so one shop visit shows who fills the store and when.
    bool gStoreDiagnostics = false;
    unsigned gStoreDiagnosticLines = 0;
    constexpr unsigned kStoreDiagnosticLimit = 200;
    bool gStartingSuitDiagnostics = false;
    bool gStartingSuitRandomizer = false;
    bool gStartingSuitIncludeDefault = false;
    uint32_t gStartingSuitSeed = 0x00D5A11u;
    volatile LONG gStartingSuitNewGameArmed = 0;
    volatile LONG gStartingSuitSelectionConsumed = 0;
    volatile LONG gStartingSuitPending = 0;
    uintptr_t gStartingSuitPendingManager = 0;
    unsigned gStartingSuitPendingIndex = 0;
    // Suit changer cheat (F9 menu, TAB page). The overlay thread only files
    // a request; the change is made at a game-thread boundary, the same way
    // the starting suit is, through the native UniformManager apply. The
    // manager pointer is the one the game last used itself.
    bool gSuitChanger = false;
    uintptr_t gUniformLastManager = 0;
    volatile LONG gSuitChangePending = 0;
    unsigned gSuitChangeIndex = 0;
    unsigned gSuitMenuIndex = 0;
    char gSuitChangeFeedback[96]{};
    // The first forced-index test crashed at 0x0054613B: the game dereferenced
    // this singleton + 0x210 while it was still null. A non-default uniform
    // starts asynchronous work the stock index 0 never needs, so the deferred
    // apply waits for this dependency.
    constexpr uintptr_t kUniformRuntimeDependency = 0x00F64A88;
    constexpr uintptr_t kUniformManagerVtable = 0x00C74C04;
    bool gProgressionManifestLoaded = false;
    char gProgressionManifestPath[MAX_PATH]{};
    char gProgressionStorePath[MAX_PATH]{};
    SchematicMapping gSchematicMappings[kMaxSchematicMappings]{};
    unsigned gSchematicMappingCount = 0;
    StoreMapping gStoreMappings[kMaxStoreMappings]{};
    unsigned gStoreMappingCount = 0;
    safetyhook::InlineHook gSchematicSpawnInitHook;
    safetyhook::InlineHook gSchematicAttributeDecodeHook;
    safetyhook::InlineHook gItemStoreListDecodeHook;
    safetyhook::InlineHook gUniformApplyHook;
    safetyhook::MidHook gStartNewGameSuitGateHook;
    safetyhook::MidHook gPlayerStoreInsertEntryHook;

    uint32_t MixStartingSuitSeed(uint32_t value)
    {
        // Small deterministic avalanche so neighbouring seeds do not pick the
        // same obvious sequence of suit indices. Not for security.
        value ^= 0x53554954u; // "SUIT"
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    uint32_t MixStartingStoreSeed(uint32_t value)
    {
        value ^= 0x504F4853u; // "SHOP"
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    struct StartingWeaponPair
    {
        Guid128 weapon;
        Guid128 ammo;
        const char* weaponName;
        const char* ammoName;
    };

    constexpr StartingWeaponPair kStartingWeaponPool[] = {
        {{{0x36C8BC40u, 0x0C67B69Au, 0x44455257u, 0x34303245u}},
         {{0x30C8692Du, 0x6DBBF822u, 0x4E484F4Au, 0x324E4F53u}},
         "Flamethrower", "Flamethrower Fuel"},
        {{{0x36C8BC48u, 0xED477CBAu, 0x44455257u, 0x34303245u}},
         {{0x30C87D90u, 0xE7570C3Eu, 0x4C454D50u, 0x4B534544u}},
         "Force Gun", "Force Gun Ammo"},
        {{{0x36C8BC49u, 0x5F163CD0u, 0x44455257u, 0x34303245u}},
         {{0x4BC78CECu, 0xDFF02CCFu, 0x544E4543u, 0x53574F4Bu}},
         "Line Gun", "Line Gun Rack"},
        {{{0x36C8BC4Fu, 0x67BB5F38u, 0x44455257u, 0x34303245u}},
         {{0x4BC78C36u, 0x9D67757Cu, 0x544E4543u, 0x53574F4Bu}},
         "Pulse Rifle", "Pulse Rifle Bullet"},
        {{{0x36C8BC4Fu, 0x6F9078E2u, 0x44455257u, 0x34303245u}},
         {{0x30C8692Du, 0x79E101DCu, 0x4E484F4Au, 0x324E4F53u}},
         "Ripper", "Ripper Ammo"},
        {{{0x36C8BC3Fu, 0xF55615C8u, 0x44455257u, 0x34303245u}},
         {{0x30C87377u, 0xE3BC55EAu, 0x4C454D50u, 0x4B534544u}},
         "Contact Beam", "Contact Beam Ammo"},
    };

    uint32_t LoadStartingSuitSeed()
    {
        char text[64]{};
        GetPrivateProfileStringA("DeadSpace2008Randomizer", "EnemyRandomizerSeed",
                                 "875025", text,
                                 static_cast<DWORD>(sizeof(text)), gIniPath);
        uint32_t seed = 0;
        return ParseRandomizerSeed(text, seed) ? seed : 0x00D5A11u;
    }

    template <typename T>
    bool ReadUniformValue(const void* address, T* value)
    {
        if (!address || !value)
            return false;
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(), address, value,
                                 sizeof(*value), &read) != FALSE &&
               read == sizeof(*value);
    }

    struct UniformDiagnosticSnapshot
    {
        uintptr_t manager = 0;
        uintptr_t player = 0;
        unsigned currentIndex = 0xFFFFFFFFu;
        unsigned char state0 = 0;
        unsigned char state1 = 0;
        unsigned stateWord = 0;
        Guid128 entryGuid{};
        unsigned slots = 0;
        float damageFactor = 0.0f;
        unsigned colour = 0;
        bool managerFieldsValid = false;
        bool entryValid = false;
    };

    UniformDiagnosticSnapshot ReadUniformSnapshot(void* manager,
                                                   unsigned requestedIndex)
    {
        UniformDiagnosticSnapshot result{};
        result.manager = reinterpret_cast<uintptr_t>(manager);
        if (!manager)
            return result;

        const uintptr_t base = result.manager;
        result.managerFieldsValid =
            ReadUniformValue(reinterpret_cast<const void*>(
                base + kUniformPlayerOffset), &result.player) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + UniformTailOffset(kUniformCurrentIndexOffset)), &result.currentIndex) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + UniformTailOffset(kUniformStateByte0Offset)), &result.state0) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + UniformTailOffset(kUniformStateByte1Offset)), &result.state1) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + UniformTailOffset(kUniformStateWordOffset)), &result.stateWord);

        if (requestedIndex >= UniformSlotCount())
            return result;
        const uintptr_t entry = base + kUniformTableOffset +
                                requestedIndex * kUniformEntryStride;
        result.entryValid =
            ReadUniformValue(reinterpret_cast<const void*>(entry),
                             &result.entryGuid) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                entry + kUniformSlotsOffset), &result.slots) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                entry + kUniformDamageFactorOffset), &result.damageFactor) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                entry + kUniformColourOffset), &result.colour);
        return result;
    }

    void LogUniformSnapshot(const char* stage, LONG sequence,
                            unsigned requestedIndex,
                            const UniformDiagnosticSnapshot& value)
    {
        if (!value.managerFieldsValid)
        {
            Log("starting suit diagnostic #%ld %s: manager=%08X request=%u "
                "(manager fields unreadable), thread=%lu", sequence, stage,
                static_cast<unsigned>(value.manager), requestedIndex,
                GetCurrentThreadId());
            return;
        }
        if (!value.entryValid)
        {
            Log("starting suit diagnostic #%ld %s: manager=%08X player=%08X "
                "request=%u current=%u state=%02X/%02X/%08X "
                "(entry unavailable), thread=%lu", sequence, stage,
                static_cast<unsigned>(value.manager),
                static_cast<unsigned>(value.player), requestedIndex,
                value.currentIndex, value.state0, value.state1,
                value.stateWord, GetCurrentThreadId());
            return;
        }
        Log("starting suit diagnostic #%ld %s: manager=%08X player=%08X "
            "request=%u current=%u entry=%08X-%08X-%08X-%08X slots=%u "
            "damage=%.3f colour=%08X state=%02X/%02X/%08X thread=%lu",
            sequence, stage, static_cast<unsigned>(value.manager),
            static_cast<unsigned>(value.player), requestedIndex,
            value.currentIndex, value.entryGuid.value[0],
            value.entryGuid.value[1], value.entryGuid.value[2],
            value.entryGuid.value[3], value.slots, value.damageFactor,
            value.colour, value.state0, value.state1, value.stateWord,
            GetCurrentThreadId());
    }

    void SAFETYHOOK_STDCALL UniformApply_Hook(void* manager,
                                              unsigned requestedIndex)
    {
        static volatile LONG callCount = 0;
        const LONG sequence = InterlockedIncrement(&callCount);
        gUniformLastManager = reinterpret_cast<uintptr_t>(manager);
        // Uniform changes are rare; the cap keeps a looping bad state from
        // flooding the log. Logged only with StartingSuitDiagnostics (the suit
        // changer installs this hook on every run); the randomizer's gate reads
        // the snapshot on every call.
        const bool report = gStartingSuitDiagnostics && sequence <= 256;
        UniformDiagnosticSnapshot before{};
        const bool startingSuit = gStartingSuitRandomizer || ExtensionWantsStore();
        if (report || startingSuit)
            before = ReadUniformSnapshot(manager, requestedIndex);
        if (report)
            LogUniformSnapshot("before", sequence, requestedIndex, before);

        unsigned effectiveIndex = requestedIndex;
        // Only the StartNewGame_Menu path can arm this. Save loading, checkpoint
        // restore, death and the kiosk can all request index zero, but none of
        // them may open the gate.
        if (startingSuit && requestedIndex == 0 &&
            before.managerFieldsValid && before.entryValid &&
            before.currentIndex == 0 && before.player &&
            InterlockedCompareExchange(&gStartingSuitNewGameArmed,
                                       0, 1) == 1 &&
            InterlockedCompareExchange(&gStartingSuitSelectionConsumed,
                                       1, 0) == 0)
        {
            // A suit path's first suit (the extension) comes before the host's own pick.
            const int path = gExtension && gExtension->startingUniform ? gExtension->startingUniform() : -1;
            const unsigned first = gStartingSuitIncludeDefault ? 0u : 1u;
            const unsigned count = gStartingSuitIncludeDefault ? 6u : 5u;
            const bool chosen = path >= 0 || gStartingSuitRandomizer;
            const unsigned selected = path >= 0 ? static_cast<unsigned>(path)
                                                : first + MixStartingSuitSeed(gStartingSuitSeed) % count;
            if (chosen && selected < UniformSlotCount())
            {
                gStartingSuitPendingManager =
                    reinterpret_cast<uintptr_t>(manager);
                gStartingSuitPendingIndex = selected;
                InterlockedExchange(&gStartingSuitPending, 1);
                Log("starting suit %s: seed=0x%08X selected index %u "
                    "(level %u); stock request %u retained and native change "
                    "deferred until runtime dependency is ready",
                    path >= 0 ? "of the suit path" : "randomizer", gStartingSuitSeed, selected, selected + 1,
                    requestedIndex);
            }
        }

        gUniformApplyHook.stdcall<void>(manager, effectiveIndex);

        if (report)
        {
            const UniformDiagnosticSnapshot after =
                ReadUniformSnapshot(manager, effectiveIndex);
            LogUniformSnapshot("after", sequence, effectiveIndex, after);
            if (before.managerFieldsValid && after.managerFieldsValid)
                Log("starting suit diagnostic #%ld result: current %u -> %u, "
                    "player %08X -> %08X", sequence, before.currentIndex,
                    after.currentIndex, static_cast<unsigned>(before.player),
                    static_cast<unsigned>(after.player));
        }
    }

    void StartNewGameSuitGate_Hook(safetyhook::Context&)
    {
        // A new call supersedes any abandoned earlier request. The arm is used up
        // before a deferred uniform change is queued, so it cannot leak into a
        // later save load.
        InterlockedExchange(&gStartingSuitNewGameArmed, 0);
        InterlockedExchange(&gStartingSuitPending, 0);
        InterlockedExchange(&gStartingSuitSelectionConsumed, 0);
        gStartingSuitPendingManager = 0;
        gStartingSuitPendingIndex = 0;
        if (gExtension && gExtension->newGame)
            gExtension->newGame();
        if (gStartingSuitRandomizer || ExtensionWantsStore())
            InterlockedExchange(&gStartingSuitNewGameArmed, 1);
        Log("New Game randomizer gate: suit=%s", gStartingSuitRandomizer ? "armed" : "off");
    }

    void SuitDebtBegin(unsigned suitIndex);   // suit_debt.inc

    void TryApplyPendingStartingSuit()
    {
        if (InterlockedCompareExchange(&gStartingSuitPending, 0, 0) != 1 ||
            !gUniformApplyHook)
            return;

        uintptr_t dependency = 0;
        if (!ReadUniformValue(reinterpret_cast<const void*>(
                kUniformRuntimeDependency), &dependency) || !dependency)
            return;

        void* manager = reinterpret_cast<void*>(gStartingSuitPendingManager);
        const unsigned target = gStartingSuitPendingIndex;
        uintptr_t vtable = 0;
        const UniformDiagnosticSnapshot before =
            ReadUniformSnapshot(manager, target);
        if (!ReadUniformValue(manager, &vtable) ||
            vtable != kUniformManagerVtable ||
            !before.managerFieldsValid || !before.entryValid ||
            !before.player || before.currentIndex != 0 ||
            target >= UniformSlotCount())
        {
            if (InterlockedCompareExchange(&gStartingSuitPending, 3, 1) == 1)
                Log("starting suit randomizer: deferred request cancelled; "
                    "manager/player changed before safe apply "
                    "(manager=%08X vtable=%08X player=%08X current=%u target=%u)",
                    static_cast<unsigned>(gStartingSuitPendingManager),
                    static_cast<unsigned>(vtable),
                    static_cast<unsigned>(before.player),
                    before.currentIndex, target);
            return;
        }

        if (InterlockedCompareExchange(&gStartingSuitPending, 2, 1) != 1)
            return;
        Log("starting suit randomizer: deferred native apply on game thread; "
            "manager=%08X player=%08X dependency=%08X index=%u level=%u",
            static_cast<unsigned>(gStartingSuitPendingManager),
            static_cast<unsigned>(before.player),
            static_cast<unsigned>(dependency), target, target + 1);
        gUniformApplyHook.stdcall<void>(manager, target);
        const UniformDiagnosticSnapshot after =
            ReadUniformSnapshot(manager, target);
        LogUniformSnapshot("deferred-after", 0, target, after);
        InterlockedExchange(&gStartingSuitPending, 3);
        if (after.managerFieldsValid && after.currentIndex == target)
            SuitDebtBegin(target);
        else
            Log("starting suit randomizer: the change did not take (current %u, wanted %u); no suit debt",
                after.currentIndex, target);
    }

    // Suits are table entries 0..5 (levels 1..6) of the UniformManager.
    constexpr unsigned kSuitChangerCount = 6;

    bool CurrentSuitIndex(unsigned* index)
    {
        return gUniformLastManager != 0 &&
               ReadUniformValue(reinterpret_cast<const void*>(
                   gUniformLastManager + UniformTailOffset(kUniformCurrentIndexOffset)), index);
    }

    // Called from the overlay thread: files the request only.
    void RequestSuitChange(unsigned index, bool random)
    {
        if (!gSuitChanger || !gUniformApplyHook)
        {
            std::snprintf(gSuitChangeFeedback, sizeof(gSuitChangeFeedback),
                          "Suit changer off (SuitChanger=0 or no uniform hook)");
            return;
        }
        unsigned current = 0;
        if (!CurrentSuitIndex(&current))
        {
            std::snprintf(gSuitChangeFeedback, sizeof(gSuitChangeFeedback),
                          "No suit manager seen yet; load a level first");
            return;
        }
        if (random)
        {
            const uint32_t roll = MixStartingSuitSeed(
                static_cast<uint32_t>(GetTickCount()) ^ 0x5A17C0DEu);
            index = roll % (kSuitChangerCount - 1);
            if (index >= current)
                ++index;   // never the suit already worn
        }
        gSuitChangeIndex = index % kSuitChangerCount;
        InterlockedExchange(&gSuitChangePending, 1);
        std::snprintf(gSuitChangeFeedback, sizeof(gSuitChangeFeedback),
                      "Suit level %u requested; applied on the next frame",
                      gSuitChangeIndex + 1);
        Log("suit changer: level %u requested (current %u, %s)",
            gSuitChangeIndex + 1, current + 1, random ? "random" : "chosen");
    }

    // Called on the game thread (air check, FuncSpawn): applies a request.
    void TryApplyPendingSuitChange()
    {
        if (InterlockedCompareExchange(&gSuitChangePending, 0, 0) != 1 ||
            !gUniformApplyHook || !gUniformLastManager)
            return;
        uintptr_t dependency = 0;
        if (!ReadUniformValue(reinterpret_cast<const void*>(
                kUniformRuntimeDependency), &dependency) || !dependency)
            return;
        void* manager = reinterpret_cast<void*>(gUniformLastManager);
        const unsigned target = gSuitChangeIndex;
        uintptr_t vtable = 0;
        const UniformDiagnosticSnapshot before =
            ReadUniformSnapshot(manager, target);
        if (!ReadUniformValue(manager, &vtable) ||
            vtable != kUniformManagerVtable || !before.managerFieldsValid ||
            !before.entryValid || !before.player || target >= kSuitChangerCount)
        {
            if (InterlockedCompareExchange(&gSuitChangePending, 3, 1) == 1)
            {
                Log("suit changer: request cancelled; manager=%08X vtable=%08X "
                    "player=%08X target=%u", static_cast<unsigned>(gUniformLastManager),
                    static_cast<unsigned>(vtable), static_cast<unsigned>(before.player), target);
                std::snprintf(gSuitChangeFeedback, sizeof(gSuitChangeFeedback),
                              "Suit change cancelled: no live player suit manager");
            }
            return;
        }
        if (before.currentIndex == target)
        {
            InterlockedExchange(&gSuitChangePending, 3);
            std::snprintf(gSuitChangeFeedback, sizeof(gSuitChangeFeedback),
                          "Already wearing suit level %u", target + 1);
            return;
        }
        if (InterlockedCompareExchange(&gSuitChangePending, 2, 1) != 1)
            return;
        Log("suit changer: native apply on game thread; manager=%08X player=%08X "
            "current=%u -> level %u", static_cast<unsigned>(gUniformLastManager),
            static_cast<unsigned>(before.player), before.currentIndex + 1, target + 1);
        gUniformApplyHook.stdcall<void>(manager, target);
        const UniformDiagnosticSnapshot after = ReadUniformSnapshot(manager, target);
        LogUniformSnapshot("suit-changer-after", 0, target, after);
        std::snprintf(gSuitChangeFeedback, sizeof(gSuitChangeFeedback),
                      "Suit level %u -> %u applied", before.currentIndex + 1,
                      after.managerFieldsValid ? after.currentIndex + 1 : target + 1);
        InterlockedExchange(&gSuitChangePending, 3);
    }

    bool UniformApplySignatureMatches()
    {
        static constexpr unsigned char expected[] = {
            0x83, 0xEC, 0x1C, 0x53, 0x8B, 0x5C, 0x24, 0x24
        };
        unsigned char actual[sizeof(expected)]{};
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kUniformApply), actual,
                   sizeof(actual), &read) != FALSE &&
               read == sizeof(actual) &&
               std::memcmp(actual, expected, sizeof(expected)) == 0;
    }

    bool StartNewGameSignatureMatches()
    {
        static constexpr unsigned char expected[] = {
            0x83, 0xEC, 0x30, 0x8B, 0x88, 0x88, 0x08, 0x00, 0x00
        };
        unsigned char actual[sizeof(expected)]{};
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kStartNewGame), actual,
                   sizeof(actual), &read) != FALSE &&
               read == sizeof(actual) &&
               std::memcmp(actual, expected, sizeof(expected)) == 0;
    }

    bool PlayerStoreInsertEntrySignatureMatches()
    {
        static constexpr unsigned char expected[] = {
            0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x53, 0x55,
            0x56, 0x57, 0x8B, 0xF8, 0x8B, 0x37, 0x85, 0xF6
        };
        static constexpr unsigned char doneExpected[] = {
            0x5F, 0x5E, 0x5D, 0x5B, 0x81, 0xC4, 0x80, 0x00,
            0x00, 0x00, 0xC2, 0x04, 0x00
        };
        unsigned char actual[sizeof(expected)]{};
        unsigned char doneActual[sizeof(doneExpected)]{};
        SIZE_T read = 0;
        SIZE_T doneRead = 0;
        return ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kPlayerStoreInsertEntry),
                   actual, sizeof(actual), &read) &&
               ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kPlayerStoreInsertEntryDone),
                   doneActual, sizeof(doneActual), &doneRead) &&
               read == sizeof(actual) && doneRead == sizeof(doneActual) &&
               std::memcmp(actual, expected, sizeof(expected)) == 0 &&
               std::memcmp(doneActual, doneExpected,
                           sizeof(doneExpected)) == 0;
    }

    bool ParseProgressionGuid(const char* text, Guid128* result)
    {
        if (!text || !result)
            return false;
        unsigned words[4]{};
        if (std::sscanf(text, "%8x-%8x-%8x-%8x", &words[0], &words[1],
                        &words[2], &words[3]) != 4)
            return false;
        for (unsigned i = 0; i < 4; ++i)
            result->value[i] = static_cast<uint32_t>(words[i]);
        return true;
    }

    unsigned SplitProgressionTsv(char* line, char** fields, unsigned capacity)
    {
        if (!line || !fields || !capacity)
            return 0;
        unsigned count = 1;
        fields[0] = line;
        for (char* cursor = line; *cursor; ++cursor)
        {
            if (*cursor == '\r' || *cursor == '\n')
            {
                *cursor = '\0';
                break;
            }
            if (*cursor == '\t' && count < capacity)
            {
                *cursor = '\0';
                fields[count++] = cursor + 1;
            }
        }
        return count;
    }

    void ResolveProgressionPaths()
    {
        char configured[MAX_PATH]{};
        GetPrivateProfileStringA("DeadSpace2008Randomizer", "ProgressionManifest",
                                 "DeadSpace1Progression.tsv", configured,
                                 sizeof(configured), gIniPath);
        if (std::strchr(configured, ':') || configured[0] == '\\' ||
            configured[0] == '/')
            strncpy_s(gProgressionManifestPath, configured, _TRUNCATE);
        else
        {
            strncpy_s(gProgressionManifestPath, gGameRoot, _TRUNCATE);
            strncat_s(gProgressionManifestPath, configured, _TRUNCATE);
        }
        strncpy_s(gProgressionStorePath, gProgressionManifestPath, _TRUNCATE);
        if (char* extension = std::strrchr(gProgressionStorePath, '.'))
            strcpy_s(extension, MAX_PATH - static_cast<size_t>(extension - gProgressionStorePath),
                     ".store.tsv");
        else
            strncat_s(gProgressionStorePath, ".store.tsv", _TRUNCATE);
    }

    bool LoadSchematicProgressionManifest()
    {
        FILE* file = nullptr;
        if (fopen_s(&file, gProgressionManifestPath, "r") || !file)
        {
            Log("progression: could not open schematic manifest %s",
                gProgressionManifestPath);
            return false;
        }
        char line[1024]{};
        std::fgets(line, sizeof(line), file); // header
        while (gSchematicMappingCount < kMaxSchematicMappings &&
               std::fgets(line, sizeof(line), file))
        {
            char* fields[16]{};
            if (SplitProgressionTsv(line, fields, 16) < 11)
                continue;
            SchematicMapping mapping{};
            if (!ParseProgressionGuid(fields[3], &mapping.entity) ||
                !ParseProgressionGuid(fields[7], &mapping.source) ||
                !ParseProgressionGuid(fields[9], &mapping.target))
                continue;
            strncpy_s(mapping.sourceName, fields[8], _TRUNCATE);
            strncpy_s(mapping.targetName, fields[10], _TRUNCATE);
            strncpy_s(mapping.kind, fields[6], _TRUNCATE);
            gSchematicMappings[gSchematicMappingCount++] = mapping;
        }
        unsigned dropped = 0;
        while (std::fgets(line, sizeof(line), file))
            ++dropped;
        std::fclose(file);
        if (dropped)
            Log("progression: %u schematic row(s) beyond the limit of %u were IGNORED",
                dropped, kMaxSchematicMappings);
        return gSchematicMappingCount != 0;
    }

    // ProgressionSeeded=1: the manifest's rows stay the catalogue (which schematic holds which
    // reward), but the reward each one gives is drawn from EnemyRandomizerSeed instead of the
    // manifest's fixed rotation. Within one kind (weapon, ammo, suit, ...) the rewards are permuted,
    // so every reward of the game is still given exactly as often as before. A kind whose rows are
    // not a permutation of themselves keeps the manifest's targets.
    bool gProgressionSeeded = false;
    // store_gate.inc: the kiosk follows the moved rewards, and optionally the first kiosk's weapons.
    bool gProgressionStoreGate = false;
    bool gProgressionStartingWeapons = false;

    void ReseedSchematicMappings()
    {
        struct Item { Guid128 guid; char name[48]; };
        constexpr unsigned kMaxItems = 32;
        char kinds[16][16]{};
        unsigned kindCount = 0;
        for (unsigned i = 0; i < gSchematicMappingCount && kindCount < 16; ++i)
        {
            bool known = false;
            for (unsigned k = 0; k < kindCount; ++k)
                known = known || std::strcmp(kinds[k], gSchematicMappings[i].kind) == 0;
            if (!known)
                strncpy_s(kinds[kindCount++], gSchematicMappings[i].kind, _TRUNCATE);
        }
        unsigned changed = 0;
        for (unsigned k = 0; k < kindCount; ++k)
        {
            Item sources[kMaxItems]{}, targets[kMaxItems]{};
            unsigned sourceCount = 0, targetCount = 0;
            for (unsigned i = 0; i < gSchematicMappingCount; ++i)
            {
                const SchematicMapping& m = gSchematicMappings[i];
                if (std::strcmp(m.kind, kinds[k]) != 0)
                    continue;
                bool haveSource = false, haveTarget = false;
                for (unsigned j = 0; j < sourceCount; ++j)
                    haveSource = haveSource || SameGuid(sources[j].guid, m.source);
                for (unsigned j = 0; j < targetCount; ++j)
                    haveTarget = haveTarget || SameGuid(targets[j].guid, m.target);
                if (!haveSource && sourceCount < kMaxItems)
                {
                    sources[sourceCount].guid = m.source;
                    strncpy_s(sources[sourceCount++].name, m.sourceName, _TRUNCATE);
                }
                if (!haveTarget && targetCount < kMaxItems)
                {
                    targets[targetCount].guid = m.target;
                    strncpy_s(targets[targetCount++].name, m.targetName, _TRUNCATE);
                }
            }
            bool permutation = sourceCount == targetCount && sourceCount > 1;
            for (unsigned a = 0; permutation && a < sourceCount; ++a)
            {
                bool found = false;
                for (unsigned b = 0; b < targetCount; ++b)
                    found = found || SameGuid(sources[a].guid, targets[b].guid);
                permutation = found;
            }
            if (!permutation)
            {
                Log("progression seeded: kind '%s' keeps the manifest's targets (%u sources, %u targets, not a permutation)",
                    kinds[k], sourceCount, targetCount);
                continue;
            }
            // Sources in GUID order, so the draw depends on the seed alone, not on the row order.
            for (unsigned a = 1; a < sourceCount; ++a)
                for (unsigned b = a; b > 0 && std::memcmp(&sources[b - 1].guid, &sources[b].guid, sizeof(Guid128)) > 0; --b)
                {
                    const Item swap = sources[b - 1];
                    sources[b - 1] = sources[b];
                    sources[b] = swap;
                }
            Item drawn[kMaxItems]{};
            for (unsigned a = 0; a < sourceCount; ++a)
                drawn[a] = sources[a];
            uint32_t state = gStartingSuitSeed ^ (0x9E3779B9u * (k + 1));
            for (const char* c = kinds[k]; *c; ++c)
                state = state * 65599u + static_cast<unsigned char>(*c);
            for (unsigned a = sourceCount - 1; a > 0; --a)
            {
                state = MixStartingSuitSeed(state + a);
                const unsigned b = state % (a + 1);
                const Item swap = drawn[a];
                drawn[a] = drawn[b];
                drawn[b] = swap;
            }
            for (unsigned i = 0; i < gSchematicMappingCount; ++i)
            {
                SchematicMapping& m = gSchematicMappings[i];
                if (std::strcmp(m.kind, kinds[k]) != 0)
                    continue;
                for (unsigned a = 0; a < sourceCount; ++a)
                {
                    if (!SameGuid(sources[a].guid, m.source))
                        continue;
                    if (!SameGuid(m.target, drawn[a].guid))
                        ++changed;
                    m.target = drawn[a].guid;
                    strncpy_s(m.targetName, drawn[a].name, _TRUNCATE);
                    break;
                }
            }
            for (unsigned a = 0; a < sourceCount; ++a)
                Log("progression seeded: %s '%s' -> '%s'", kinds[k], sources[a].name, drawn[a].name);
        }
        Log("progression seeded: seed 0x%08X, %u kind(s), %u of %u row(s) changed from the manifest",
            gStartingSuitSeed, kindCount, changed, gSchematicMappingCount);
    }

    bool LoadStoreProgressionManifest()
    {
        FILE* file = nullptr;
        if (fopen_s(&file, gProgressionStorePath, "r") || !file)
        {
            Log("progression: could not open Chapter 1 store manifest %s",
                gProgressionStorePath);
            return false;
        }
        char line[1024]{};
        std::fgets(line, sizeof(line), file); // header
        while (gStoreMappingCount < kMaxStoreMappings &&
               std::fgets(line, sizeof(line), file))
        {
            char* fields[10]{};
            if (SplitProgressionTsv(line, fields, 10) < 6)
                continue;
            StoreMapping mapping{};
            mapping.slot = static_cast<unsigned>(std::strtoul(fields[1], nullptr, 10));
            if (mapping.slot >= 32 ||
                !ParseProgressionGuid(fields[2], &mapping.source) ||
                !ParseProgressionGuid(fields[4], &mapping.target))
                continue;
            strncpy_s(mapping.sourceName, fields[3], _TRUNCATE);
            strncpy_s(mapping.targetName, fields[5], _TRUNCATE);
            gStoreMappings[gStoreMappingCount++] = mapping;
        }
        std::fclose(file);
        return gStoreMappingCount == 4;
    }

    void LoadProgressionSettings()
    {
        gProgressionRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "ProgressionRandomizer", 0, gIniPath) != 0;
        gProgressionDiagnostics = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "ProgressionRandomizerDiagnostics", 0,
            gIniPath) != 0;
        gProgressionStoreCatalog = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "ProgressionStoreCatalog", 0, gIniPath) != 0;
        gStoreDiagnostics = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "StoreDiagnostics", 0, gIniPath) != 0;
        gFirstWeaponRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "FirstWeaponRandomizer", 0, gIniPath) != 0;
        gCheatPickupWeapon = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "CheatPickupWeapon", 0, gIniPath);
        // Clamped against the literal count because the weapon table is
        // declared further down this file than the settings are read.
        if (gCheatPickupWeapon < 0)
            gCheatPickupWeapon = 0;
        if (gCheatPickupWeapon > 3)
            gCheatPickupWeapon = 3;
        gUpgradeBenchRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "UpgradeBenchRandomizer", 0, gIniPath) != 0;
        gLootRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "LootRandomizer", 0, gIniPath) != 0;
        gLootWeightRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "LootWeightRandomizer", 0, gIniPath) != 0;
        gLootGlobalPool = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "LootGlobalPool", 0, gIniPath) != 0;
        gLootScarcity = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "LootScarcity", 0, gIniPath);
        gLootScarcityRandom = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "LootScarcityRandom", 0, gIniPath) != 0;
        if (gLootScarcity < -100)
            gLootScarcity = -100;
        if (gLootScarcity > 100)
            gLootScarcity = 100;
        gLootScarcityApplied = gLootScarcity;
        if (gLootScarcityRandom)
        {
            // One value for the whole run rather than per table, so a lean run is
            // lean everywhere. The band is lopsided (a little more generous at
            // best, a lot leaner at worst) because scarcity keeps the game tense.
            uint32_t mixed = gStartingSuitSeed ^ 0x53434143u;
            mixed ^= mixed >> 16;
            mixed *= 0x7FEB352Du;
            mixed ^= mixed >> 15;
            gLootScarcityApplied = -15 + static_cast<int>(mixed % 51u);
        }
        if (gLootScarcityApplied != 0)
            Log("loot scarcity: %+d percentage points on the chance that "
                "nothing drops (%s)", gLootScarcityApplied,
                gLootScarcityRandom ? "from the seed" : "from the ini");
        gStartingSuitDiagnostics = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "StartingSuitDiagnostics", 0, gIniPath) != 0;
        gStartingSuitRandomizer = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "StartingSuitRandomizer", 0, gIniPath) != 0;
        gStartingSuitIncludeDefault = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "StartingSuitIncludeDefault", 0, gIniPath) != 0;
        gSuitChanger = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "SuitChanger", 0, gIniPath) != 0;
        gStartingSuitSeed = LoadStartingSuitSeed();
        ResolveProgressionPaths();
        bool schematics = false;
        bool store = false;
        gProgressionSeeded = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "ProgressionSeeded", 0, gIniPath) != 0;
        gProgressionStoreGate = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "ProgressionStoreGate", 0, gIniPath) != 0;
        gProgressionStartingWeapons = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "ProgressionStartingWeapons", 0, gIniPath) != 0;
        if (gProgressionRandomizer)
            schematics = LoadSchematicProgressionManifest();
        if (schematics && gProgressionSeeded)
            ReseedSchematicMappings();
        if (gProgressionRandomizer)
            store = LoadStoreProgressionManifest();
        gProgressionManifestLoaded =
            gProgressionRandomizer && schematics && (store || gProgressionStoreGate);
        Log("progression randomizer: %s, manifest=%s, schematics=%u, store=%u",
            gProgressionRandomizer ?
                (gProgressionManifestLoaded ? "enabled" : "DISABLED (manifest invalid)") :
                "disabled",
            gProgressionManifestPath, gSchematicMappingCount, gStoreMappingCount);
        Log("starting suit diagnostic: %s (observational only)",
            gStartingSuitDiagnostics ? "enabled" : "disabled");
        Log("starting suit randomizer: %s, seed=0x%08X, pool=%s",
            gStartingSuitRandomizer ? "EXPERIMENTAL" : "disabled",
            gStartingSuitSeed,
            gStartingSuitIncludeDefault ? "levels 1-6" : "levels 2-6");
    }

    bool ReadProgressionGuid(const void* address, Guid128* value)
    {
        if (!address || !value)
            return false;
        return ReadProcessMemory(GetCurrentProcess(), address, value,
                                 sizeof(*value), nullptr) != FALSE;
    }

    // The manifest keys every reward by the placed schematic's entity GUID, which
    // is not in this hook's argument: the game queries that argument with the
    // Schematic class id and copies only the reward GUID, so it is the authored
    // attribute object, not a SpawnInfo. Entities carry their GUID at +0x20
    // (checked on FuncSpawn and UniformManager), so it is read from the object.
    constexpr unsigned kEntityGuidOffset = 0x20;

    const SchematicMapping* SchematicMappingForEntity(void* self)
    {
        if (!self)
            return nullptr;
        Guid128 uid{};
        if (!ReadProgressionGuid(reinterpret_cast<const void*>(
                reinterpret_cast<uintptr_t>(self) + kEntityGuidOffset), &uid))
            return nullptr;
        for (unsigned i = 0; i < gSchematicMappingCount; ++i)
            if (SameGuid(uid, gSchematicMappings[i].entity))
                return &gSchematicMappings[i];
        if (gProgressionDiagnostics)
            Log("progression schematic: entity %08X-%08X-%08X-%08X is not in "
                "the manifest; authored reward retained",
                uid.value[0], uid.value[1], uid.value[2], uid.value[3]);
        return nullptr;
    }

    // What a schematic shows comes with its reward: Item[24] (description,
    // +0x52C) and Item[25] (name, +0x53C; Schematic[1] at +0x640 holds the
    // same key); its Item[22]/[23] are the same generic keys on every
    // schematic.  A moved reward takes the keys of the schematic that authored
    // it, or else the reward item's own: [25]/[24] where it has them, else
    // [22]/[23] (+0x50C/+0x51C), which name a GunProxy's weapon.
    // The pickup prompt is a VFX with its text baked in ("SCHEMATIC" and the reward, all 16 in global_assets):
    // PickupItem[13] (+0x454, copied into the HUD at 0x005601CC on pickup) and its small version [12] (+0x444).
    // Without them a moved reward floated and announced the old one.
    struct SchematicText
    {
        Guid128 reward;
        Guid128 description;
        Guid128 name;
        uint32_t smallPrompt;
        uint32_t prompt;
    };
    constexpr unsigned kSchematicSmallPrompt = 0x444;
    constexpr unsigned kSchematicPrompt = 0x454;
    constexpr SchematicText kSchematicTexts[] = {
#include "schematic_texts_generated.inc"
    };
    constexpr unsigned kItemDescriptionText = 0x52C;
    constexpr unsigned kItemNameText = 0x53C;
    constexpr unsigned kItemShortNameText = 0x50C;
    constexpr unsigned kItemShortDescriptionText = 0x51C;
    constexpr unsigned kSchematicNameText = 0x640;
    uintptr_t TargetKeyFor(const Guid128& target);   // dllmain.cpp

    // A text key is one hash repeated four times.
    bool PlausibleTextKey(const Guid128& key)
    {
        return key.value[0] && key.value[0] == key.value[1] && key.value[1] == key.value[2] &&
               key.value[2] == key.value[3];
    }

    const char* ApplySchematicTexts(void* self, const Guid128& reward)
    {
        Guid128 description{}, name{};
        uint32_t smallPrompt = 0, prompt = 0;
        const char* from = nullptr;
        for (const SchematicText& text : kSchematicTexts)
            if (SameGuid(text.reward, reward))
            {
                description = text.description;
                name = text.name;
                smallPrompt = text.smallPrompt;
                prompt = text.prompt;
                from = prompt ? "its own schematic, prompt too" : "its own schematic";
            }
        if (!from)
        {
            const uintptr_t key = TargetKeyFor(reward);
            uintptr_t item = 0;
            if (key)
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(key + 0x14), &item,
                                  sizeof(item), nullptr);
            if (item &&
                ReadProgressionGuid(reinterpret_cast<const void*>(item + kItemDescriptionText), &description) &&
                ReadProgressionGuid(reinterpret_cast<const void*>(item + kItemNameText), &name) &&
                PlausibleTextKey(description) && PlausibleTextKey(name))
                from = "the item itself";
            else if (item &&
                     ReadProgressionGuid(reinterpret_cast<const void*>(item + kItemShortDescriptionText),
                                         &description) &&
                     ReadProgressionGuid(reinterpret_cast<const void*>(item + kItemShortNameText), &name) &&
                     PlausibleTextKey(description) && PlausibleTextKey(name))
                from = "the item's own name";
        }
        if (!from)
            return "unchanged (no text for that reward)";
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kItemDescriptionText), &description,
                           sizeof(description), nullptr);
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kItemNameText), &name, sizeof(name),
                           nullptr);
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kSchematicNameText), &name,
                           sizeof(name), nullptr);
        if (prompt)
        {
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kSchematicPrompt), &prompt,
                               sizeof(prompt), nullptr);
            if (smallPrompt)
                WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kSchematicSmallPrompt),
                                   &smallPrompt, sizeof(smallPrompt), nullptr);
        }
        return from;
    }

    // A world schematic that only names a global definition in PickupItem[17]
    // (+0x344) grants that definition, so the reward moves with the definition's
    // manifest row while the world object keeps its own texts (ch02
    // securitysta_z01 33C85D5B -> 30C8C10A showed "Flamethrower"). It takes the
    // texts of what its definition now gives.
    constexpr unsigned kSchematicGrantedDefinition = 0x344;
    volatile LONG gSchematicGrantTexts = 0;

    bool ApplyGrantedSchematicTexts(void* self)
    {
        Guid128 own{}, granted{};
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        if (!ReadProgressionGuid(reinterpret_cast<const void*>(object + kEntityGuidOffset), &own) ||
            !ReadProgressionGuid(reinterpret_cast<const void*>(object + kSchematicGrantedDefinition), &granted) ||
            SameGuid(own, granted))
            return false;
        const SchematicMapping* definition = nullptr;
        for (unsigned i = 0; i < gSchematicMappingCount && !definition; ++i)
            if (SameGuid(granted, gSchematicMappings[i].entity))
                definition = &gSchematicMappings[i];
        if (!definition || SameGuid(definition->source, definition->target))
            return false;
        const char* texts = ApplySchematicTexts(self, definition->target);
        if (InterlockedIncrement(&gSchematicGrantTexts) <= 16)
            Log("progression schematic %08X-%08X grants %08X-%08X (%s -> %s); name and description from %s",
                own.value[0], own.value[1], granted.value[0], granted.value[1], definition->sourceName,
                definition->targetName, texts);
        return true;
    }

    void ApplySchematicProgression(void* self)
    {
        if (!gProgressionManifestLoaded || !self)
            return;
        const SchematicMapping* mapping = SchematicMappingForEntity(self);
        if (!mapping)
        {
            ApplyGrantedSchematicTexts(self);
            return;
        }
        auto* current = reinterpret_cast<Guid128*>(
            reinterpret_cast<uintptr_t>(self) + kSchematicTargetOffset);
        if (SameGuid(*current, mapping->target))
        {
            // Idempotent on repeated initialization; the Item record may have
            // decoded after the Schematic record and put the authored texts back.
            if (!SameGuid(mapping->source, mapping->target))
                ApplySchematicTexts(self, mapping->target);
            return;
        }
        if (!SameGuid(*current, mapping->source))
            return;
        *current = mapping->target;
        const char* texts = ApplySchematicTexts(self, mapping->target);
        if (gProgressionDiagnostics)
            Log("progression schematic: %s -> %s (object=%08X), name and description from %s",
                mapping->sourceName, mapping->targetName,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)), texts);
    }

    // The decoder writes entries at this+0x3C, indexed up to 32, but the only
    // registered class with a matching name (DLCItemStoreList) is 0x60 bytes and
    // could not hold slot 4 or 5. Since the object that arrives here is unknown,
    // every entry is read guarded, so a foreign or short object fails the match.
    bool ReadStoreEntry(void* self, unsigned slot, Guid128* value)
    {
        const uintptr_t at = reinterpret_cast<uintptr_t>(self) +
                             kItemStoreEntriesOffset + slot * sizeof(Guid128);
        return ReadProgressionGuid(reinterpret_cast<const void*>(at), value);
    }

    bool IsExactChapterOneStore(void* self, bool* needsApply)
    {
        if (needsApply)
            *needsApply = false;
        if (!self || gStoreMappingCount != 4)
            return false;
        // Four exact authored slots identify the six-entry Chapter 1 list.
        // This deliberately refuses the 26/32-entry global catalogues.
        bool sawSource = false;
        for (unsigned i = 0; i < gStoreMappingCount; ++i)
        {
            Guid128 current{};
            if (!ReadStoreEntry(self, gStoreMappings[i].slot, &current))
                return false;
            if (SameGuid(current, gStoreMappings[i].source))
                sawSource = true;
            else if (!SameGuid(current, gStoreMappings[i].target))
                return false;
        }
        if (needsApply)
            *needsApply = sawSource;
        return true;
    }

    bool ApplyStoreProgression(void* self)
    {
        bool needsApply = false;
        if (gStoreMappingCount != 4 ||
            !IsExactChapterOneStore(self, &needsApply) || !needsApply)
            return false;
        for (unsigned i = 0; i < gStoreMappingCount; ++i)
        {
            const uintptr_t at = reinterpret_cast<uintptr_t>(self) +
                                 kItemStoreEntriesOffset +
                                 gStoreMappings[i].slot * sizeof(Guid128);
            SIZE_T written = 0;
            WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(at),
                               &gStoreMappings[i].target,
                               sizeof(Guid128), &written);
        }
        if (gProgressionDiagnostics)
            Log("progression store: randomized Chapter 1 weapon/ammo slots (object=%08X)",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)));
        return true;
    }

    void PlayerStoreInsertEntry_Hook(safetyhook::Context& ctx)
    {
        if (gStoreDiagnostics && gStoreDiagnosticLines <
                                     kStoreDiagnosticLimit &&
            ctx.eax && ctx.esp)
        {
            __try
            {
                const auto* seen = reinterpret_cast<const Guid128*>(ctx.eax);
                const uintptr_t from =
                    *reinterpret_cast<const uintptr_t*>(ctx.esp);
                const uintptr_t above =
                    *reinterpret_cast<const uintptr_t*>(ctx.esp + 0x3C);
                const uintptr_t owner =
                    *reinterpret_cast<const uintptr_t*>(ctx.esp + 4);
                ++gStoreDiagnosticLines;
                Log("store watch #%u: item %08X-%08X-%08X-%08X into %08X "
                    "from %08X (parent %08X)", gStoreDiagnosticLines,
                    seen->value[0], seen->value[1], seen->value[2],
                    seen->value[3], static_cast<unsigned>(owner),
                    static_cast<unsigned>(from),
                    static_cast<unsigned>(above));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("store watch: unreadable context");
            }
        }
    }

    // The shop catalogue is the long list: at least twenty entries, including the
    // weapons the six-entry list never had. The two positions are exchanged rather
    // than overwritten, so the catalogue stays a permutation and every item
    // remains buyable somewhere.
    void ApplyCatalogProgression(void* self)
    {
        if (!gProgressionStoreCatalog || !gProgressionManifestLoaded || !self)
            return;
        Guid128 entries[32]{};
        unsigned count = 0;
        for (unsigned slot = 0; slot < 32; ++slot)
        {
            if (!ReadStoreEntry(self, slot, &entries[slot]))
                return;
            const Guid128& value = entries[slot];
            if (value.value[0] || value.value[1] || value.value[2] ||
                value.value[3])
                count = slot + 1;
        }
        if (count < 20)
            return;   // this is the six-entry list, not the catalogue

        unsigned swaps = 0;
        for (unsigned i = 0; i < gStoreMappingCount; ++i)
        {
            const StoreMapping& mapping = gStoreMappings[i];
            if (SameGuid(mapping.source, mapping.target))
                continue;
            unsigned from = 32, to = 32;
            for (unsigned slot = 0; slot < count; ++slot)
            {
                if (SameGuid(entries[slot], mapping.source))
                    from = slot;
                else if (SameGuid(entries[slot], mapping.target))
                    to = slot;
            }
            if (from >= count || to >= count)
                continue;
            const uintptr_t base = reinterpret_cast<uintptr_t>(self) +
                                   kItemStoreEntriesOffset;
            SIZE_T written = 0;
            WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(base + from * sizeof(Guid128)),
                               &mapping.target, sizeof(Guid128), &written);
            WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(base + to * sizeof(Guid128)),
                               &mapping.source, sizeof(Guid128), &written);
            const Guid128 moved = entries[from];
            entries[from] = entries[to];
            entries[to] = moved;
            ++swaps;
            if (gProgressionDiagnostics)
                Log("progression catalog: swapped %s (slot %u) with %s (slot %u)",
                    mapping.sourceName, from, mapping.targetName, to);
        }
        if (swaps && gProgressionDiagnostics)
            Log("progression catalog: %u swap(s) in a %u-entry catalogue "
                "(object=%08X)", swaps, count,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)));
    }

    // The list the shelf is drawn from. Entries are 40 bytes: three fields, an id,
    // then the item GUID. Patching the authored data, the player's list and the
    // store catalogue all left the shelf unchanged, so this is the last step
    // between data and screen. It is rebuilt, so the address is remembered and
    // re-checked rather than patched once.
    constexpr unsigned kShelfStride = 0x28;
    constexpr unsigned kShelfGuidOffset = 0x10;
    uintptr_t gShelfEntry = 0;
    ULONGLONG gShelfLastScan = 0;

    bool ShelfEntryLooksRight(uintptr_t entry, const Guid128& expected)
    {
        Guid128 value{};
        return ReadProgressionGuid(
                   reinterpret_cast<const void*>(entry + kShelfGuidOffset),
                   &value) && SameGuid(value, expected);
    }

    void PatchShelfList()
    {
        if (!gProgressionStoreCatalog || !gProgressionManifestLoaded ||
            gStoreMappingCount != 4)
            return;
        // Mapping 1 is the weapon slot: Pulse Rifle in, Force Gun out.
        const StoreMapping* weapon = nullptr;
        for (unsigned i = 0; i < gStoreMappingCount; ++i)
            if (!SameGuid(gStoreMappings[i].source, gStoreMappings[i].target))
            {
                weapon = &gStoreMappings[i];
                break;
            }
        if (!weapon)
            return;

        auto applyAt = [&](uintptr_t entry) {
            SIZE_T written = 0;
            if (!WriteProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(entry + kShelfGuidOffset),
                    &weapon->target, sizeof(Guid128), &written))
                return false;
            Log("progression shelf: %s -> %s at %08X", weapon->sourceName,
                weapon->targetName, static_cast<unsigned>(entry));
            return true;
        };

        if (gShelfEntry && ShelfEntryLooksRight(gShelfEntry, weapon->source))
        {
            applyAt(gShelfEntry);
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (now - gShelfLastScan < 2000)
            return;
        gShelfLastScan = now;

        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        uintptr_t cursor =
            reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const uintptr_t maximum =
            reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        while (cursor < maximum)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region,
                              sizeof(region)) || !region.RegionSize)
                break;
            const uintptr_t base =
                reinterpret_cast<uintptr_t>(region.BaseAddress);
            const uintptr_t end = base + region.RegionSize;
            const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                                   PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE &&
                !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (region.Protect & writable) && region.RegionSize <= 0x10000000)
            {
                __try
                {
                    for (uintptr_t at = base; at + kShelfStride * 2 < end;
                         at += 4)
                    {
                        // Two consecutive entries at the shelf spacing: the
                        // wanted slot and the one after it. A lone GUID also
                        // appears in half a dozen other structures.
                        const Guid128* here = reinterpret_cast<const Guid128*>(
                            at + kShelfGuidOffset);
                        if (!SameGuid(*here, weapon->source))
                            continue;
                        const Guid128* next = reinterpret_cast<const Guid128*>(
                            at + kShelfStride + kShelfGuidOffset);
                        bool plausible = false;
                        for (unsigned i = 0; i < gStoreMappingCount; ++i)
                            if (SameGuid(*next, gStoreMappings[i].source) ||
                                SameGuid(*next, gStoreMappings[i].target))
                                plausible = true;
                        if (!plausible)
                            continue;
                        gShelfEntry = at;
                        applyAt(at);
                        return;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            if (end <= cursor)
                break;
            cursor = end;
        }
    }

    void __fastcall SchematicSpawnInit_Hook(void* self, void*, void* attributes)
    {
        gSchematicSpawnInitHook.thiscall<void>(self, attributes);
        ApplySchematicProgression(self);
    }

    // Both routes end in the same idempotent replacement: it only writes while the
    // value is still the authored source, so whichever runs first wins.

    // Pickup decoder.
    //
    // A pickup grants exactly one thing, written by PickupItem's decoder: property
    // 17 checks for a null id and otherwise copies sixteen bytes to object+0x344.
    // Ammo and key items honour that field; a Gun never reads it (Gun::OnPickup
    // takes the world object itself), so the first weapon is swapped in
    // first_weapon.inc and this hook only retargets the chapter 1 batteries.
    //
    // CheatPickupWeapon is the exception: every pickup in the world names the
    // chosen weapon. Off by default.
    constexpr uintptr_t kPickupItemDecode = 0x00513260;
    constexpr unsigned kPickupGrantedItem = 0x344;
    safetyhook::InlineHook gPickupItemDecodeHook;
    void ApplyFirstWeaponAmmo(void* pickup);   // first_weapon.inc
    void ApplyFirstWeaponLabels(void* pickup);   // first_weapon.inc

    struct WorldWeapon
    {
        Guid128 id;
        const char* name;
    };
    constexpr WorldWeapon kWorldWeapons[] = {
        {{{0x4BC78C36u, 0x9DB15E65u, 0x544E4543u, 0x53574F4Bu}}, "Plasma Cutter"},
        {{{0x4BC78C36u, 0x9DBAE7CDu, 0x544E4543u, 0x53574F4Bu}}, "Line Gun"},
        {{{0x30C87CE1u, 0x0FCEC34Au, 0x4C454D50u, 0x4B534544u}}, "Force Gun"},
    };
    constexpr unsigned kWorldWeaponCount =
        sizeof(kWorldWeapons) / sizeof(kWorldWeapons[0]);

    void ApplyWorldWeapon(void* self)
    {
        if (gCheatPickupWeapon <= 0 ||
            gCheatPickupWeapon > static_cast<int>(kWorldWeaponCount) || !self)
            return;
        const uintptr_t slot =
            reinterpret_cast<uintptr_t>(self) + kPickupGrantedItem;
        Guid128 granted{};
        __try
        {
            std::memcpy(&granted, reinterpret_cast<const void*>(slot),
                        sizeof(granted));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        // It also overwrites schematics and key items, so a run played with
        // it on is not a run worth keeping.
        const WorldWeapon& give = kWorldWeapons[gCheatPickupWeapon - 1];
        if (SameGuid(granted, give.id))
            return;
        SIZE_T cheatWritten = 0;
        if (WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(slot),
                               &give.id, sizeof(give.id), &cheatWritten))
        {
            static unsigned reported = 0;
            if (reported < 6)
            {
                ++reported;
                Log("cheat: pickup %08X now grants %s",
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)),
                    give.name);
            }
        }
    }

    void __fastcall PickupItemDecode_Hook(void* self, void*, void* attributes)
    {
        gPickupItemDecodeHook.thiscall<void>(self, attributes);
        ApplyWorldWeapon(self);
        ApplyFirstWeaponAmmo(self);
        ApplyFirstWeaponLabels(self);
    }


    // Upgrade bench.
    //
    // The bench describes its whole node catalogue: twelve blocks of nine
    // properties, each naming a node type in plain text (upg_nodedesc_dmg2, _spd2,
    // _cap2, _wid2, _rel2, _min2, _hp2, _air2, _eng2, _dur2, _rng2, _chr2) with
    // UI_UPG_Points beside it and an id at block+0x08. The blocks start at +0x5A0
    // and repeat every 0x78, checked against all twelve decoder cases.
    //
    // Rotating the ids only randomizes the bench if the id carries the effect and
    // the string is just a label. The id resolves to a lone dword in no name
    // table, so the hook logs all twelve before touching them; twelve identical
    // values would mean there is nothing to rotate.
    constexpr uintptr_t kUpgradeBenchDecode = 0x005DEE70;
    constexpr unsigned kBenchFirstNodeId = 0x5A8;
    constexpr unsigned kBenchNodeStride = 0x78;
    constexpr unsigned kBenchNodeCount = 12;
    safetyhook::InlineHook gUpgradeBenchDecodeHook;
    bool gBenchReported = false;

    void ApplyUpgradeBench(void* self)
    {
        if (!gUpgradeBenchRandomizer || !self)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        Guid128 nodes[kBenchNodeCount]{};
        __try
        {
            for (unsigned i = 0; i < kBenchNodeCount; ++i)
                std::memcpy(&nodes[i],
                            reinterpret_cast<const void*>(
                                object + kBenchFirstNodeId +
                                i * kBenchNodeStride),
                            sizeof(Guid128));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("upgrade bench: %08X unreadable", static_cast<unsigned>(object));
            return;
        }

        if (!gBenchReported)
        {
            gBenchReported = true;
            unsigned distinct = 0;
            for (unsigned i = 0; i < kBenchNodeCount; ++i)
            {
                bool seen = false;
                for (unsigned j = 0; j < i; ++j)
                    seen = seen || SameGuid(nodes[i], nodes[j]);
                if (!seen)
                    ++distinct;
                Log("upgrade bench node %2u: %08X-%08X-%08X-%08X", i,
                    nodes[i].value[0], nodes[i].value[1], nodes[i].value[2],
                    nodes[i].value[3]);
            }
            Log("upgrade bench: %u of %u node ids are distinct%s", distinct,
                kBenchNodeCount,
                distinct <= 1 ? " - nothing to rotate" : "");
        }

        uint32_t mixed = gStartingSuitSeed ^ 0x55504752u;
        mixed ^= mixed >> 16;
        mixed *= 0x7FEB352Du;
        mixed ^= mixed >> 15;
        const unsigned step = 1 + (mixed % (kBenchNodeCount - 1));
        for (unsigned i = 0; i < kBenchNodeCount; ++i)
        {
            const Guid128& source = nodes[(i + step) % kBenchNodeCount];
            SIZE_T written = 0;
            WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(
                                   object + kBenchFirstNodeId +
                                   i * kBenchNodeStride),
                               &source, sizeof(source), &written);
        }
        if (gProgressionDiagnostics)
            Log("upgrade bench %08X: node ids rotated by %u",
                static_cast<unsigned>(object), step);
    }

    void __fastcall UpgradeBenchDecode_Hook(void* self, void*, void* attributes)
    {
        gUpgradeBenchDecodeHook.thiscall<void>(self, attributes);
        ApplyUpgradeBench(self);
    }


    // Loot tables.
    //
    // ItemDropList is the game's weighted drop table, roughly three hundred of
    // them, one per combat area; it decides what necromorphs and crates drop.
    // Layout from the decoder at 0x0050E190, confirmed against authored records:
    // sixteen entries of twenty bytes at +0x50, each a sixteen-byte item id and a
    // float weight. Case 6 stores the first id to this+0x50, case 7 the first
    // weight to this+0x60, case 37 the last weight to this+0x18C.
    //
    // The weights are relative (measured tables run 75/25, 5/1/23/1 and
    // 10/20/1/19/10), so rotating them changes which item is common and which
    // rare. Only values move; the entry count never changes.
    constexpr uintptr_t kItemDropListDecode = 0x0050E190;
    constexpr unsigned kDropFirstEntry = 0x50;
    constexpr unsigned kDropEntryStride = 0x14;
    constexpr unsigned kDropEntryCount = 16;
    constexpr unsigned kDropWeightOffset = 0x10;
    // The weight of nothing. 0x0050EBA0 adds up the populated entry weights plus
    // this field, and its caller at 0x0050EF9D multiplies that total by a random
    // number and compares it with this field: a roll below it drops nothing. So
    // the empty chance is field / (entry weights + field). The shipped tables
    // always add up to a hundred (5+1+23+1 beside 70, 10+20+1+19+10 beside 40),
    // which makes the field a plain percentage.
    constexpr unsigned kDropNothingWeight = 0x19C;
    // A table that never gives anything would read as a broken randomizer
    // rather than as a hard run, so the drought has a ceiling.
    constexpr float kDropNothingCeiling = 0.90f;
    // A table with three hundred siblings would drown the log, so only the
    // first few are described in full.
    constexpr unsigned kDropLogLimit = 12;
    safetyhook::InlineHook gItemDropListDecodeHook;
    unsigned gDropTablesSeen = 0;

    // The whole game draws its drops from eighteen item ids across 608 tables.
    // Twelve appear in fourteen tables or more, spread over every chapter, so
    // they are streamed wherever a drop table exists. The counts below are how
    // many tables each id appears in.
    //
    // The other six stay out. An id used in one to five tables may live in a
    // level that never streams it, and elsewhere would make a pickup the game
    // cannot resolve. Every id here is a PowerUp or SpareAmmo; one of the six is
    // the Captain's RIG from ch02_med, a KeyItem, and progression items in random
    // loot tables would be far worse than a missing pickup.
    //
    // "None" is a real authored PowerUp used by 329 tables and very likely the
    // entry for no drop. It stays in the pool on purpose: moving it makes some
    // areas generous and others barren.
    struct LootPoolItem
    {
        Guid128 id;
        const char* name;
    };
    constexpr LootPoolItem kLootPool[] = {
        {{{0x4BC78C36u, 0x9D5DEC14u, 0x544E4543u, 0x53574F4Bu}}, "Medium Health Kit"},  // 597 tables
        {{{0x4BC78C36u, 0x9D5B89BAu, 0x544E4543u, 0x53574F4Bu}}, "Small Health Kit"},  // 540 tables
        {{{0x4BC806EAu, 0x40D9D6F7u, 0x544E4543u, 0x53574F4Bu}}, "None (empty drop)"},  // 329 tables
        {{{0x36C836DBu, 0x314DD30Eu, 0x44455257u, 0x34303245u}}, "Currency 1"},  // 258 tables
        {{{0x4BC78C36u, 0x9D604E6Eu, 0x544E4543u, 0x53574F4Bu}}, "Large Health Kit"},  // 57 tables
        {{{0x36C836DBu, 0x337E1BAEu, 0x44455257u, 0x34303245u}}, "Currency 2"},  // 57 tables
        {{{0x36C836ACu, 0xF8A08E76u, 0x44455257u, 0x34303245u}}, "Currency 3"},  // 57 tables
        {{{0x4BC78C36u, 0x9D651322u, 0x544E4543u, 0x53574F4Bu}}, "Plasma Cutter Battery"},  // 14 tables
        {{{0x4BC78C36u, 0x9D67757Cu, 0x544E4543u, 0x53574F4Bu}}, "Pulse Rifle Bullet"},  // 14 tables
        {{{0x4BC78CECu, 0xDFF02CCFu, 0x544E4543u, 0x53574F4Bu}}, "Line Gun Rack"},  // 14 tables
        {{{0x35C7C97Fu, 0xCCF3A350u, 0x4854414Du, 0x37375355u}}, "Air Cannister"},  // 14 tables
        {{{0x4BC7F007u, 0x713FFAEEu, 0x544E4543u, 0x53574F4Bu}}, "Double Air Cannister"},  // 14 tables
    };
    constexpr unsigned kLootPoolCount =
        sizeof(kLootPool) / sizeof(kLootPool[0]);

    // Nearly every id a drop table holds is one of the pool's, including in
    // tables the pool never touches, so this names both sides of a swap.
    const char* LootItemName(const Guid128& id)
    {
        for (unsigned i = 0; i < kLootPoolCount; ++i)
            if (SameGuid(id, kLootPool[i].id))
                return kLootPool[i].name;
        return "rare item";
    }
    // Eight is the largest table measured, so the pool can always give every
    // entry of a table a different item.
    static_assert(kLootPoolCount >= 8, "pool must outnumber the largest table");

    // StableRandomizerHash is defined further down in dllmain.cpp than this
    // include, so the key is folded here. It only has to be stable across reloads.
    uint32_t MixLootKey(const Guid128& first, const Guid128& second,
                        uint32_t salt)
    {
        uint32_t mixed = gStartingSuitSeed ^ salt;
        for (unsigned i = 0; i < 4; ++i)
        {
            mixed = (mixed ^ first.value[i]) * 0x01000193u;
            mixed = (mixed ^ second.value[i]) * 0x01000193u;
        }
        mixed ^= mixed >> 16;
        mixed *= 0x7FEB352Du;
        mixed ^= mixed >> 15;
        return mixed;
    }

    void ApplyItemDropList(void* self)
    {
        if ((!gLootRandomizer && !gLootWeightRandomizer &&
             !gLootGlobalPool && gLootScarcityApplied == 0) || !self)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);

        Guid128 ids[kDropEntryCount]{};
        uint32_t weights[kDropEntryCount]{};
        unsigned populated[kDropEntryCount]{};
        unsigned count = 0;
        __try
        {
            for (unsigned i = 0; i < kDropEntryCount; ++i)
            {
                const uintptr_t entry =
                    object + kDropFirstEntry + i * kDropEntryStride;
                std::memcpy(&ids[i], reinterpret_cast<const void*>(entry),
                            sizeof(Guid128));
                std::memcpy(&weights[i],
                            reinterpret_cast<const void*>(
                                entry + kDropWeightOffset),
                            sizeof(uint32_t));
                const bool empty =
                    ids[i].value[0] == 0 && ids[i].value[1] == 0 &&
                    ids[i].value[2] == 0 && ids[i].value[3] == 0;
                if (!empty)
                    populated[count++] = i;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("loot table: %08X unreadable", static_cast<unsigned>(object));
            return;
        }

        // An empty table means the hook fired on something that is not a drop
        // list, which is worth logging. A single-entry table has nothing to
        // rotate, but the pool can still replace its one item.
        if (count == 0 ||
            (count < 2 && !gLootGlobalPool && gLootScarcityApplied == 0))
        {
            if (gProgressionDiagnostics && gDropTablesSeen < kDropLogLimit)
                Log("loot table %08X: %u entries, left alone",
                    static_cast<unsigned>(object), count);
            ++gDropTablesSeen;
            return;
        }

        // Keyed on the table's first two items so a table randomizes the same
        // way every time it streams in; a checkpoint reload must not reshuffle.
        const Guid128 key = ids[populated[0]];
        const Guid128 second = ids[populated[count > 1 ? 1 : 0]];
        // count == 1 only reaches here with the pool on, and then no rotation
        // happens, so the division below is never by zero.
        const unsigned span = count > 1 ? count - 1 : 1;
        const unsigned idStep = 1 + (MixLootKey(key, second, 0x4C4F4F54u) % span);
        const unsigned weightStep =
            1 + (MixLootKey(key, second, 0x57474854u) % span);

        Guid128 newIds[kDropEntryCount]{};
        uint32_t newWeights[kDropEntryCount]{};
        uint32_t poolUsed = 0;
        for (unsigned n = 0; n < count; ++n)
        {
            if (gLootGlobalPool)
            {
                // Drawn from the whole game's item set rather than this table, so
                // an area can drop something it never held. A pick already taken
                // steps forward to a free one, so no two entries grant one item.
                unsigned pick =
                    MixLootKey(ids[populated[n]], key, 0x504F4F4Cu + n) %
                    kLootPoolCount;
                for (unsigned tries = 0; tries < kLootPoolCount; ++tries)
                {
                    if (!(poolUsed & (1u << pick)))
                        break;
                    pick = (pick + 1) % kLootPoolCount;
                }
                poolUsed |= 1u << pick;
                newIds[n] = kLootPool[pick].id;
            }
            else
            {
                newIds[n] = ids[populated[(n + idStep) % count]];
            }
            newWeights[n] = weights[populated[(n + weightStep) % count]];
        }

        // Scarcity: shift how often the table yields nothing. The entry weights
        // stay as decided above; only how often anything drops moves.
        if (gLootScarcityApplied != 0)
        {
            float total = 0.0f;
            for (unsigned n = 0; n < count; ++n)
            {
                float weight = 0.0f;
                std::memcpy(&weight, &weights[populated[n]], sizeof(float));
                if (weight > 0.0f)
                    total += weight;
            }
            float nothing = 0.0f;
            __try
            {
                std::memcpy(&nothing, reinterpret_cast<const void*>(
                                object + kDropNothingWeight), sizeof(float));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                total = 0.0f;
            }
            // Without any entry weight there is no distribution to shift.
            if (total > 0.0f && nothing >= 0.0f)
            {
                const float authored = nothing / (total + nothing);
                float wanted = authored +
                    static_cast<float>(gLootScarcityApplied) / 100.0f;
                if (wanted < 0.0f)
                    wanted = 0.0f;
                if (wanted > kDropNothingCeiling)
                    wanted = kDropNothingCeiling;
                const float replacement = total * wanted / (1.0f - wanted);
                SIZE_T scarcityWritten = 0;
                WriteProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(
                                       object + kDropNothingWeight),
                                   &replacement, sizeof(replacement),
                                   &scarcityWritten);
                if (gProgressionDiagnostics && gDropTablesSeen < kDropLogLimit)
                    Log("loot table %08X: empty drop %.0f%% -> %.0f%%",
                        static_cast<unsigned>(object), authored * 100.0f,
                        wanted * 100.0f);
            }
        }

        const bool describe =
            gProgressionDiagnostics && gDropTablesSeen < kDropLogLimit;
        for (unsigned n = 0; n < count; ++n)
        {
            const uintptr_t entry =
                object + kDropFirstEntry + populated[n] * kDropEntryStride;
            SIZE_T written = 0;
            if (gLootRandomizer || gLootGlobalPool)
                WriteProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(entry),
                                   &newIds[n], sizeof(Guid128), &written);
            if (gLootWeightRandomizer)
                WriteProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(
                                       entry + kDropWeightOffset),
                                   &newWeights[n], sizeof(uint32_t), &written);
            if (describe)
            {
                float before = 0.0f;
                float after = 0.0f;
                std::memcpy(&before, &weights[populated[n]], sizeof(float));
                std::memcpy(&after, &newWeights[n], sizeof(float));
                Log("loot table %08X entry %u: %s w%.0f -> %s w%.0f",
                    static_cast<unsigned>(object), n,
                    LootItemName(ids[populated[n]]), before,
                    LootItemName(newIds[n]), after);
            }
        }
        if (describe)
            Log("loot table %08X: %u entries, items %s, weights %s",
                static_cast<unsigned>(object), count,
                gLootGlobalPool ? "drawn from the global pool" :
                    (gLootRandomizer ? "rotated" : "untouched"),
                gLootWeightRandomizer ? "rotated" : "untouched");
        ++gDropTablesSeen;
        if (gDropTablesSeen == kDropLogLimit)
            Log("loot table: further tables randomized without logging");
    }

    void __fastcall ItemDropListDecode_Hook(void* self, void*, void* attributes)
    {
        gItemDropListDecodeHook.thiscall<void>(self, attributes);
        ApplyItemDropList(self);
    }

    void __fastcall SchematicAttributeDecode_Hook(void* self, void*,
                                                  void* attributes)
    {
        gSchematicAttributeDecodeHook.thiscall<void>(self, attributes);
        ApplySchematicProgression(self);
    }

    void __fastcall ItemStoreListDecode_Hook(void* self, void*, void* attributes)
    {
        gItemStoreListDecodeHook.thiscall<void>(self, attributes);
        if (gProgressionRandomizer)
        {
            ApplyStoreProgression(self);
            ApplyCatalogProgression(self);
        }
    }

    void InstallProgressionHooks()
    {
        if (gStartingSuitDiagnostics || gStartingSuitRandomizer || gSuitChanger || ExtensionWantsStore())
        {
            if (!UniformApplySignatureMatches())
                Log("starting suit diagnostic: NOT installed; retail "
                    "UniformManager signature mismatch at %08X",
                    static_cast<unsigned>(kUniformApply));
            else
            {
                gUniformApplyHook = VerifiedInline(
                    reinterpret_cast<void*>(kUniformApply),
                    reinterpret_cast<void*>(&UniformApply_Hook));
                Log("starting suit diagnostic hook %s at %08X",
                    gUniformApplyHook ? "active" : "NOT installed",
                    static_cast<unsigned>(kUniformApply));
            }

            if (gStartingSuitRandomizer || ExtensionWantsStore())
            {
                if (!StartNewGameSignatureMatches())
                    Log("starting suit randomizer: DISABLED; retail "
                        "StartNewGame_Menu signature mismatch at %08X",
                        static_cast<unsigned>(kStartNewGame));
                else
                {
                    gStartNewGameSuitGateHook = VerifiedMid(
                        reinterpret_cast<void*>(kStartNewGame),
                        &StartNewGameSuitGate_Hook);
                    Log("starting suit New Game gate %s at %08X",
                        gStartNewGameSuitGateHook ? "active" : "NOT installed",
                        static_cast<unsigned>(kStartNewGame));
                }
            }
        }
        const bool fullProgression =
            gProgressionRandomizer && gProgressionManifestLoaded;
        // With the store gate the old Chapter 1 list rewrites stay out: the gate
        // rejects what they put in (analysis/DeadSpace1_Store_Source_2026-10-21.md).
        const bool storePatch = gStoreMappingCount == 4 && fullProgression && !gProgressionStoreGate;
        // The schematic hooks belong to the manifest randomizer alone. Under an
        // earlier braceless if, the attribute decoder was installed whenever
        // anything else was, and an early return above skipped the bench and
        // world weapon hooks unless the store or schematic randomizer was on.
        if (fullProgression)
        {
            gSchematicSpawnInitHook = VerifiedInline(
                reinterpret_cast<void*>(kSchematicSpawnInit),
                reinterpret_cast<void*>(&SchematicSpawnInit_Hook));
            gSchematicAttributeDecodeHook = VerifiedInline(
                reinterpret_cast<void*>(kSchematicAttributeDecode),
                reinterpret_cast<void*>(&SchematicAttributeDecode_Hook));
            Log("progression schematic: attribute decoder %s at %08X",
                gSchematicAttributeDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kSchematicAttributeDecode));
        }
        if (gLootRandomizer || gLootWeightRandomizer || gLootGlobalPool ||
            gLootScarcityApplied != 0)
        {
            gItemDropListDecodeHook = VerifiedInline(
                reinterpret_cast<void*>(kItemDropListDecode),
                reinterpret_cast<void*>(&ItemDropListDecode_Hook));
            Log("loot table: decoder %s at %08X (items %s, weights %s, "
                "pool of %u)",
                gItemDropListDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kItemDropListDecode),
                gLootGlobalPool ? "global pool" :
                    (gLootRandomizer ? "rotated" : "off"),
                gLootWeightRandomizer ? "on" : "off", kLootPoolCount);
        }
        if (gUpgradeBenchRandomizer)
        {
            gUpgradeBenchDecodeHook = VerifiedInline(
                reinterpret_cast<void*>(kUpgradeBenchDecode),
                reinterpret_cast<void*>(&UpgradeBenchDecode_Hook));
            Log("upgrade bench: decoder %s at %08X",
                gUpgradeBenchDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kUpgradeBenchDecode));
        }
        if (gFirstWeaponRandomizer || gCheatPickupWeapon > 0)
        {
            static_assert(kWorldWeaponCount == 3,
                          "the cheat clamps against a literal 3");
            if (gCheatPickupWeapon > 0)
                Log("cheat: EVERY pickup grants the %s - schematics and key "
                    "items are overwritten too, turn this off when done",
                    kWorldWeapons[gCheatPickupWeapon - 1].name);
            gPickupItemDecodeHook = VerifiedInline(
                reinterpret_cast<void*>(kPickupItemDecode),
                reinterpret_cast<void*>(&PickupItemDecode_Hook));
            Log("pickup decoder %s at %08X (first weapon batteries, pickup cheat)",
                gPickupItemDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kPickupItemDecode));
        }
        if (storePatch)
        {
            gItemStoreListDecodeHook = VerifiedInline(
                reinterpret_cast<void*>(kItemStoreListDecode),
                reinterpret_cast<void*>(&ItemStoreListDecode_Hook));
            if (gStoreDiagnostics)
            {
                if (!PlayerStoreInsertEntrySignatureMatches())
                    Log("store watch: NOT installed; retail signature mismatch at %08X",
                        static_cast<unsigned>(kPlayerStoreInsertEntry));
                else
                    gPlayerStoreInsertEntryHook = VerifiedMid(
                        reinterpret_cast<void*>(kPlayerStoreInsertEntry),
                        &PlayerStoreInsertEntry_Hook);
            }
        }
        Log("progression hooks: schematic=%s at %08X, Chapter1 store=%s at %08X",
            gSchematicSpawnInitHook ? "active" : "NOT installed",
            static_cast<unsigned>(kSchematicSpawnInit),
            gItemStoreListDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kItemStoreListDecode));
        if (gStoreDiagnostics && storePatch)
            Log("store watch %s at %08X", gPlayerStoreInsertEntryHook ? "active" : "NOT installed",
                static_cast<unsigned>(kPlayerStoreInsertEntry));
    }
