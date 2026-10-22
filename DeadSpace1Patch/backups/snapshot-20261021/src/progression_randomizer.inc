    // Seed-manifest progression randomizer.
    constexpr uintptr_t kSchematicSpawnInit = 0x00515E90;
    // The route that actually delivers the reward.  A Chapter 4 Force Gun
    // schematic was watched in game: SpawnInit never ran and not a single
    // diagnostic line appeared, while the pickup label still read FORCE GUN.
    // The attribute decoder is what writes the reward GUID to this+0x630 - it
    // takes one stack argument with this in ECX, the same shape as the store
    // decoder - so the replacement belongs behind it.
    constexpr uintptr_t kSchematicAttributeDecode = 0x00515BB0;
    constexpr uintptr_t kItemStoreListDecode = 0x0050FB00;
    constexpr uintptr_t kPlayerStoreAddStoreList = 0x00596910;
    constexpr uintptr_t kPlayerStoreInsertEntry = 0x00596A40;
    constexpr uintptr_t kPlayerStoreInsertEntryDone = 0x00596D04;
    constexpr uintptr_t kPlayerStoreAddStoreListReturn = 0x00596949;
    constexpr uintptr_t kPlayerStoreRestoreInsertReturn = 0x005972EC;
    constexpr uintptr_t kPlayerStoreRestoreItem = 0x005972E2;
    constexpr uintptr_t kPlayerStoreRestoreDone = 0x00597301;
    constexpr uintptr_t kPlayerStoreVtable = 0x00C7496C;
    // Native UniformManager application path.  This build only observes it;
    // starting-suit selection remains deliberately disabled until New Game
    // and save restoration can be distinguished from the trace.
    constexpr uintptr_t kUniformApply = 0x005A2A40;
    // Retail StartNewGame_Menu path.  This is the function that parses the
    // embedded CH01 checkpoint and starts a fresh run; save loading and
    // checkpoint restoration use different paths.
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
    constexpr unsigned kUniformEntryLimit = 30;
    constexpr unsigned kSchematicTargetOffset = 0x630;
    constexpr unsigned kItemStoreEntriesOffset = 0x3C;
    constexpr unsigned kMaxSchematicMappings = 32;
    constexpr unsigned kMaxStoreMappings = 8;
    constexpr Guid128 kAuthoredStoreAnchor0{{
        0x36C8BC4Fu, 0x07EADE56u, 0x44455257u, 0x34303245u}};
    constexpr Guid128 kAuthoredStoreAnchor1{{
        0x36C8BC49u, 0x5F163CD0u, 0x44455257u, 0x34303245u}};
    constexpr Guid128 kAuthoredStoreAnchor2{{
        0x36C8BC4Fu, 0x67BB5F38u, 0x44455257u, 0x34303245u}};

    struct SchematicMapping
    {
        Guid128 entity{};
        Guid128 source{};
        Guid128 target{};
        char sourceName[48]{};
        char targetName[48]{};
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
    // catalogue.  Measured in a full dump, the list the patch has been
    // rewriting is Player+0x1610, while the shop's catalogue is
    // ShipStore+0x794 - and the shelf still showed the authored weapons.
    // This settles which of the two the display is built from.
    bool gProgressionStoreCatalog = false;
    bool gProgressionDiagnostics = true;
    // Declared here because the settings are read long before the
    // world-weapon code further down.
    bool gFirstWeaponRandomizer = false;
    // Cheat: 0 off, otherwise 1-3 for the three weapons that exist as a
    // world pickup at all.  Testing needs a weapon in hand, and loading a
    // checkpoint hands you none.
    int gCheatPickupWeapon = 0;
    bool gUpgradeBenchRandomizer = false;
    bool gLootRandomizer = false;
    bool gLootWeightRandomizer = false;
    bool gLootGlobalPool = false;
    bool gLootScarcityRandom = false;
    int gLootScarcity = 0;
    int gLootScarcityApplied = 0;
    // Every earlier store attempt worked on an object the shop does not
    // read, and we could not tell because the insert hook only reports
    // calls it also acts on.  This reports every call, unfiltered, so one
    // visit to a shop shows who fills the store and when.
    bool gStoreDiagnostics = false;
    unsigned gStoreDiagnosticLines = 0;
    constexpr unsigned kStoreDiagnosticLimit = 200;
    bool gStartingStoreRandomizer = false;
    bool gExistingSaveStoreMigration = false;
    bool gStartingSuitDiagnostics = false;
    bool gStartingSuitRandomizer = false;
    bool gStartingSuitIncludeDefault = false;
    uint32_t gStartingSuitSeed = 0x00D5A11u;
    volatile LONG gStartingSuitNewGameArmed = 0;
    volatile LONG gStartingStoreNewGameArmed = 0;
    volatile LONG gStartingSuitSelectionConsumed = 0;
    volatile LONG gStartingSuitPending = 0;
    uintptr_t gStartingSuitPendingManager = 0;
    unsigned gStartingSuitPendingIndex = 0;
    // Suit changer cheat (F9 menu, TAB page). The overlay thread only files
    // a request; the change is made at a game-thread boundary, the same way
    // the starting suit is, through the native UniformManager apply. The
    // manager pointer is the one the game last used itself.
    bool gSuitChanger = true;
    uintptr_t gUniformLastManager = 0;
    volatile LONG gSuitChangePending = 0;
    unsigned gSuitChangeIndex = 0;
    unsigned gSuitMenuIndex = 0;
    char gSuitChangeFeedback[96]{};
    bool gAuthoredStorePatched = false;
    unsigned gAuthoredStoreAttempts = 0;
    unsigned gStoreHandoffCalls = 0;
    unsigned gStoreRestoreItems = 0;
    unsigned gStoreInsertItems = 0;
    unsigned gStoreInsertReplacements = 0;
    unsigned gStoreInsertMappingMask = 0;
    bool gStoreInsertCompleteLogged = false;
    ULONGLONG gAuthoredStoreLastScan = 0;
    // The crash dump from the first forced-index test stopped at 0x0054613B:
    // the game dereferenced this singleton plus 0x210 while it was still
    // null.  A non-default uniform starts asynchronous work that the stock
    // index-0 initialization never needs, so readiness of this dependency is
    // a mandatory part of the deferred apply gate.
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
    safetyhook::MidHook gPlayerStoreAddStoreListHook;
    safetyhook::MidHook gAddStoreListDoneHook;
    safetyhook::MidHook gPlayerStoreInsertEntryHook;
    safetyhook::MidHook gPlayerStoreInsertEntryDoneHook;
    safetyhook::MidHook gPlayerStoreRestoreItemHook;
    safetyhook::MidHook gPlayerStoreRestoreDoneHook;

    struct PendingStoreInsert
    {
        uintptr_t store = 0;
        uintptr_t caller = 0;
        unsigned mapping = 0;
        bool active = false;
    };

    thread_local PendingStoreInsert gPendingStoreInsert;

    uint32_t MixStartingSuitSeed(uint32_t value)
    {
        // A small deterministic avalanche.  This is not used for security;
        // it merely keeps neighbouring user seeds from selecting the same
        // obvious sequence of suit indices.
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

    bool BuildStartingStoreMappingsFromSeed()
    {
        static constexpr Guid128 sourceLineGun{{
            0x36C8BC49u, 0x5F163CD0u, 0x44455257u, 0x34303245u}};
        static constexpr Guid128 sourcePulseRifle{{
            0x36C8BC4Fu, 0x67BB5F38u, 0x44455257u, 0x34303245u}};
        static constexpr Guid128 sourcePulseAmmo{{
            0x4BC78C36u, 0x9D67757Cu, 0x544E4543u, 0x53574F4Bu}};
        static constexpr Guid128 sourceLineAmmo{{
            0x4BC78CECu, 0xDFF02CCFu, 0x544E4543u, 0x53574F4Bu}};

        constexpr unsigned count = static_cast<unsigned>(
            sizeof(kStartingWeaponPool) / sizeof(kStartingWeaponPool[0]));
        const unsigned first = MixStartingStoreSeed(gStartingSuitSeed) % count;
        unsigned second = MixStartingStoreSeed(
            gStartingSuitSeed ^ 0x9E3779B9u) % (count - 1);
        if (second >= first)
            ++second;

        const StartingWeaponPair& a = kStartingWeaponPool[first];
        const StartingWeaponPair& b = kStartingWeaponPool[second];
        gStoreMappingCount = 4;
        gStoreMappings[0].slot = 1;
        gStoreMappings[0].source = sourceLineGun;
        gStoreMappings[0].target = a.weapon;
        strcpy_s(gStoreMappings[0].sourceName, "Line Gun");
        strcpy_s(gStoreMappings[0].targetName, a.weaponName);
        gStoreMappings[1].slot = 2;
        gStoreMappings[1].source = sourcePulseRifle;
        gStoreMappings[1].target = b.weapon;
        strcpy_s(gStoreMappings[1].sourceName, "Pulse Rifle");
        strcpy_s(gStoreMappings[1].targetName, b.weaponName);
        gStoreMappings[2].slot = 4;
        gStoreMappings[2].source = sourcePulseAmmo;
        gStoreMappings[2].target = b.ammo;
        strcpy_s(gStoreMappings[2].sourceName, "Pulse Rifle Bullet");
        strcpy_s(gStoreMappings[2].targetName, b.ammoName);
        gStoreMappings[3].slot = 5;
        gStoreMappings[3].source = sourceLineAmmo;
        gStoreMappings[3].target = a.ammo;
        strcpy_s(gStoreMappings[3].sourceName, "Line Gun Rack");
        strcpy_s(gStoreMappings[3].targetName, a.ammoName);
        Log("starting store seed 0x%08X: Line Gun -> %s, Pulse Rifle -> %s",
            gStartingSuitSeed, a.weaponName, b.weaponName);
        return true;
    }

    uint32_t LoadStartingSuitSeed()
    {
        char text[64]{};
        GetPrivateProfileStringA("DeadSpace1Patch", "EnemyRandomizerSeed",
                                 "0x00D5A11", text,
                                 static_cast<DWORD>(sizeof(text)), gIniPath);
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 0);
        if (!end || end == text || *end != '\0')
            return 0x00D5A11u;
        return static_cast<uint32_t>(parsed);
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
                base + kUniformCurrentIndexOffset), &result.currentIndex) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + kUniformStateByte0Offset), &result.state0) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + kUniformStateByte1Offset), &result.state1) &&
            ReadUniformValue(reinterpret_cast<const void*>(
                base + kUniformStateWordOffset), &result.stateWord);

        if (requestedIndex >= kUniformEntryLimit)
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
        // Uniform changes are rare, but retain a hard ceiling in case a bad
        // state repeatedly calls this path and would otherwise flood the log.
        const bool report = sequence <= 256;
        UniformDiagnosticSnapshot before{};
        if (report)
        {
            before = ReadUniformSnapshot(manager, requestedIndex);
            LogUniformSnapshot("before", sequence, requestedIndex, before);
        }

        unsigned effectiveIndex = requestedIndex;
        // Only the exact StartNewGame_Menu path can arm this operation.  Save
        // loading, checkpoint restore, death and the kiosk can all issue a
        // request for index zero, but none of them may open this gate.
        if (gStartingSuitRandomizer && requestedIndex == 0 &&
            before.managerFieldsValid && before.entryValid &&
            before.currentIndex == 0 && before.player &&
            InterlockedCompareExchange(&gStartingSuitNewGameArmed,
                                       0, 1) == 1 &&
            InterlockedCompareExchange(&gStartingSuitSelectionConsumed,
                                       1, 0) == 0)
        {
            const unsigned first = gStartingSuitIncludeDefault ? 0u : 1u;
            const unsigned count = gStartingSuitIncludeDefault ? 6u : 5u;
            const unsigned selected =
                first + MixStartingSuitSeed(gStartingSuitSeed) % count;
            gStartingSuitPendingManager =
                reinterpret_cast<uintptr_t>(manager);
            gStartingSuitPendingIndex = selected;
            InterlockedExchange(&gStartingSuitPending, 1);
            Log("starting suit randomizer: seed=0x%08X selected index %u "
                "(level %u); stock request %u retained and native change "
                "deferred until runtime dependency is ready",
                gStartingSuitSeed, selected, selected + 1, requestedIndex);
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
        // A new invocation supersedes any abandoned request from an earlier
        // attempt.  The arm is consumed before a deferred uniform change is
        // queued, so it can never leak into a later save load.
        InterlockedExchange(&gStartingSuitNewGameArmed, 0);
        InterlockedExchange(&gStartingStoreNewGameArmed, 0);
        InterlockedExchange(&gStartingSuitPending, 0);
        InterlockedExchange(&gStartingSuitSelectionConsumed, 0);
        gStartingSuitPendingManager = 0;
        gStartingSuitPendingIndex = 0;
        gAuthoredStorePatched = false;
        gAuthoredStoreAttempts = 0;
        gStoreHandoffCalls = 0;
        gStoreRestoreItems = 0;
        gStoreInsertItems = 0;
        gStoreInsertReplacements = 0;
        gStoreInsertMappingMask = 0;
        gStoreInsertCompleteLogged = false;
        gAuthoredStoreLastScan = 0;
        if (gStartingSuitRandomizer)
            InterlockedExchange(&gStartingSuitNewGameArmed, 1);
        if (gStartingStoreRandomizer)
            InterlockedExchange(&gStartingStoreNewGameArmed, 1);
        Log("New Game randomizer gate: suit=%s, starting store=%s",
            gStartingSuitRandomizer ? "armed" : "off",
            gStartingStoreRandomizer ? "armed" : "off");
    }

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
            target >= kUniformEntryLimit)
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
    }

    // Suits are table entries 0..5 (levels 1..6) of the UniformManager.
    constexpr unsigned kSuitChangerCount = 6;

    bool CurrentSuitIndex(unsigned* index)
    {
        return gUniformLastManager != 0 &&
               ReadUniformValue(reinterpret_cast<const void*>(
                   gUniformLastManager + kUniformCurrentIndexOffset), index);
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

    bool PlayerStoreAddStoreListSignatureMatches()
    {
        static constexpr unsigned char expected[] = {
            0x83, 0xEC, 0x18, 0x53, 0x56, 0x57, 0x8B, 0xF9,
            0x8B, 0xF0, 0x83, 0xC7, 0x44, 0xBB, 0x20, 0x00
        };
        unsigned char actual[sizeof(expected)]{};
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kPlayerStoreAddStoreList),
                   actual, sizeof(actual), &read) &&
               read == sizeof(actual) &&
               std::memcmp(actual, expected, sizeof(expected)) == 0;
    }

    bool PlayerStoreRestoreSignaturesMatch()
    {
        static constexpr unsigned char itemExpected[] = {
            0xC6, 0x44, 0x24, 0x3D, 0x01, 0xE8, 0x54, 0xF7,
            0xFF, 0xFF
        };
        static constexpr unsigned char doneExpected[] = {
            0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x40, 0xC3
        };
        unsigned char itemActual[sizeof(itemExpected)]{};
        unsigned char doneActual[sizeof(doneExpected)]{};
        SIZE_T itemRead = 0;
        SIZE_T doneRead = 0;
        return ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kPlayerStoreRestoreItem),
                   itemActual, sizeof(itemActual), &itemRead) &&
               ReadProcessMemory(GetCurrentProcess(),
                   reinterpret_cast<const void*>(kPlayerStoreRestoreDone),
                   doneActual, sizeof(doneActual), &doneRead) &&
               itemRead == sizeof(itemActual) && doneRead == sizeof(doneActual) &&
               std::memcmp(itemActual, itemExpected, sizeof(itemExpected)) == 0 &&
               std::memcmp(doneActual, doneExpected, sizeof(doneExpected)) == 0;
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
        GetPrivateProfileStringA("DeadSpace1Patch", "ProgressionManifest",
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
            gSchematicMappings[gSchematicMappingCount++] = mapping;
        }
        std::fclose(file);
        return gSchematicMappingCount != 0;
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
            "DeadSpace1Patch", "ProgressionRandomizer", 0, gIniPath) != 0;
        gProgressionDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ProgressionRandomizerDiagnostics", 1,
            gIniPath) != 0;
        gProgressionStoreCatalog = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ProgressionStoreCatalog", 0, gIniPath) != 0;
        gStoreDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "StoreDiagnostics", 0, gIniPath) != 0;
        gFirstWeaponRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "FirstWeaponRandomizer", 0, gIniPath) != 0;
        gCheatPickupWeapon = GetPrivateProfileIntA(
            "DeadSpace1Patch", "CheatPickupWeapon", 0, gIniPath);
        // Clamped against the literal count because the weapon table is
        // declared further down this file than the settings are read.
        if (gCheatPickupWeapon < 0)
            gCheatPickupWeapon = 0;
        if (gCheatPickupWeapon > 3)
            gCheatPickupWeapon = 3;
        gUpgradeBenchRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "UpgradeBenchRandomizer", 0, gIniPath) != 0;
        gLootRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LootRandomizer", 0, gIniPath) != 0;
        gLootWeightRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LootWeightRandomizer", 0, gIniPath) != 0;
        gLootGlobalPool = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LootGlobalPool", 0, gIniPath) != 0;
        gLootScarcity = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LootScarcity", 0, gIniPath);
        gLootScarcityRandom = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LootScarcityRandom", 0, gIniPath) != 0;
        if (gLootScarcity < -100)
            gLootScarcity = -100;
        if (gLootScarcity > 100)
            gLootScarcity = 100;
        gLootScarcityApplied = gLootScarcity;
        if (gLootScarcityRandom)
        {
            // One character for the whole run rather than a value per
            // table: a lean run should feel lean everywhere.  The band is
            // deliberately lopsided - a little more generous at best, a
            // lot leaner at worst - because scarcity is what makes Dead
            // Space tense and abundance mostly removes the game.
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
        gStartingStoreRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "StartingStoreRandomizer", 0, gIniPath) != 0;
        gExistingSaveStoreMigration = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ExistingSaveStoreMigration", 0, gIniPath) != 0;
        gStartingSuitDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "StartingSuitDiagnostics", 0, gIniPath) != 0;
        gStartingSuitRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "StartingSuitRandomizer", 0, gIniPath) != 0;
        gStartingSuitIncludeDefault = GetPrivateProfileIntA(
            "DeadSpace1Patch", "StartingSuitIncludeDefault", 0, gIniPath) != 0;
        gSuitChanger = GetPrivateProfileIntA(
            "DeadSpace1Patch", "SuitChanger", 1, gIniPath) != 0;
        gStartingSuitSeed = LoadStartingSuitSeed();
        ResolveProgressionPaths();
        bool schematics = false;
        bool store = false;
        if (gProgressionRandomizer)
            schematics = LoadSchematicProgressionManifest();
        if (gStartingStoreRandomizer)
            store = BuildStartingStoreMappingsFromSeed();
        else if (gProgressionRandomizer)
            store = LoadStoreProgressionManifest();
        gProgressionManifestLoaded =
            gProgressionRandomizer && schematics && store;
        Log("progression randomizer: %s, manifest=%s, schematics=%u, store=%u",
            gProgressionRandomizer ?
                (gProgressionManifestLoaded ? "enabled" : "DISABLED (manifest invalid)") :
                "disabled",
            gProgressionManifestPath, gSchematicMappingCount, gStoreMappingCount);
        Log("starting store randomizer: %s (New Game only, %u mappings)",
            gStartingStoreRandomizer ?
                (store ? "enabled" : "DISABLED (catalog invalid)") :
                "disabled",
            gStoreMappingCount);
        Log("existing-save store migration: %s (explicit one-shot test mode)",
            gExistingSaveStoreMigration ? "enabled" : "disabled");
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

    // The manifest keys every reward by the placed schematic's own entity
    // GUID.  That GUID is not in the argument this hook receives: the game
    // queries that argument with the Schematic class id and copies only the
    // reward GUID out of it, so it is the authored attribute object, not a
    // SpawnInfo.  Entities in this engine carry their GUID at +0x20 - checked
    // against two live classes, FuncSpawn and UniformManager - so read it off
    // the object being initialised instead.
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

    void ApplySchematicProgression(void* self)
    {
        if (!gProgressionManifestLoaded || !self)
            return;
        const SchematicMapping* mapping = SchematicMappingForEntity(self);
        if (!mapping)
            return;
        auto* current = reinterpret_cast<Guid128*>(
            reinterpret_cast<uintptr_t>(self) + kSchematicTargetOffset);
        if (SameGuid(*current, mapping->target))
            return; // idempotent on repeated initialization
        if (!SameGuid(*current, mapping->source))
            return;
        *current = mapping->target;
        if (gProgressionDiagnostics)
            Log("progression schematic: %s -> %s (object=%08X)",
                mapping->sourceName, mapping->targetName,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)));
    }

    // The decoder writes entries at this+0x3C, indexed up to 32, but the only
    // registered class with a matching name (DLCItemStoreList) is 0x60 bytes
    // and could not hold slot 4 or 5.  Which object actually arrives here is
    // therefore not settled, so every entry is read through a guarded read:
    // a foreign or short object fails the match instead of faulting.
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

    // Defined further down, next to the retail reasoning for the route.
    void SwapStoreListEntries(uintptr_t list, uintptr_t store);

    void PlayerStoreAddStoreList_Hook(safetyhook::Context& ctx)
    {
        if (!gStartingStoreRandomizer || !ctx.ecx || !ctx.eax ||
            InterlockedCompareExchange(&gStartingStoreNewGameArmed,
                                       1, 1) != 1)
            return;

        void* list = reinterpret_cast<void*>(ctx.ecx);
        Guid128 first{};
        ++gStoreHandoffCalls;
        if (!ReadStoreEntry(list, 0, &first))
            return;
        if (!SameGuid(first, kAuthoredStoreAnchor0))
        {
            if (gStoreHandoffCalls <= 4)
                Log("starting store handoff: skipped candidate %u with first "
                    "GUID %08X-%08X-%08X-%08X",
                    gStoreHandoffCalls, first.value[0], first.value[1],
                    first.value[2], first.value[3]);
            return;
        }

        bool needsApply = false;
        if (!IsExactChapterOneStore(list, &needsApply))
            return;

        gAuthoredStorePatched = true;
        Log("starting store handoff: Chapter 1 list at %08X, PlayerStore %08X, "
            "authored sources present: %s",
            static_cast<unsigned>(ctx.ecx), static_cast<unsigned>(ctx.eax),
            needsApply ? "yes" : "no");
        if (needsApply)
            SwapStoreListEntries(ctx.ecx, ctx.eax);
    }

    void PlayerStoreInsertEntry_Hook(safetyhook::Context& ctx)
    {
        gPendingStoreInsert = {};
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
        if (!gStartingStoreRandomizer || !ctx.eax || !ctx.esp ||
            InterlockedCompareExchange(&gStartingStoreNewGameArmed,
                                       1, 1) != 1)
            return;

        __try
        {
            const uintptr_t caller =
                *reinterpret_cast<const uintptr_t*>(ctx.esp);
            // Measured with the unfiltered watch: the Chapter 1 shop is
            // filled in two waves when the level loads.  The restore
            // path at 005972EC brings the four items the shop actually
            // shows - the Line Gun and the Pulse Rifle among them - and
            // the authored list follows from 0059688F.  Skipping the
            // restore wave is why three attempts at this changed
            // nothing on screen.
            const bool fromRestore =
                caller == kPlayerStoreRestoreInsertReturn;
            if (!fromRestore && caller != 0x0059688F)
                return;
            // Only the list wave has a meaningful parent frame; the
            // restore wave is entered directly and reads 0 there.
            const uintptr_t parentCaller = fromRestore ? caller :
                *reinterpret_cast<const uintptr_t*>(ctx.esp + 0x3C);
            if (!fromRestore &&
                parentCaller == kPlayerStoreAddStoreListReturn)
                return;
            const uintptr_t store =
                *reinterpret_cast<const uintptr_t*>(ctx.esp + 4);
            if (!store ||
                *reinterpret_cast<const uintptr_t*>(store) !=
                    kPlayerStoreVtable)
            {
                if (gProgressionDiagnostics && gStoreInsertItems < 4)
                    Log("starting store insert: rejected call from %08X with "
                        "PlayerStore=%08X", static_cast<unsigned>(caller),
                        static_cast<unsigned>(store));
                return;
            }

            ++gStoreInsertItems;
            const auto* item = reinterpret_cast<const Guid128*>(ctx.eax);
            for (unsigned i = 0; i < gStoreMappingCount; ++i)
            {
                const StoreMapping& mapping = gStoreMappings[i];
                if (!SameGuid(*item, mapping.source))
                    continue;
                gPendingStoreInsert.store = store;
                gPendingStoreInsert.caller = parentCaller;
                gPendingStoreInsert.mapping = i;
                gPendingStoreInsert.active = true;
                Log("starting store insert: pending post-insert replacement "
                    "%s -> %s in PlayerStore %08X from %08X",
                    mapping.sourceName, mapping.targetName,
                    static_cast<unsigned>(store),
                    static_cast<unsigned>(parentCaller));
                break;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("starting store insert: unreadable call context "
                "(entry=%08X stack=%08X)", static_cast<unsigned>(ctx.eax),
                static_cast<unsigned>(ctx.esp));
        }
    }

    void PlayerStoreInsertEntryDone_Hook(safetyhook::Context&)
    {
        if (!gPendingStoreInsert.active)
            return;
        const PendingStoreInsert pending = gPendingStoreInsert;
        gPendingStoreInsert = {};
        if (pending.mapping >= gStoreMappingCount)
            return;

        const StoreMapping& mapping = gStoreMappings[pending.mapping];
        bool sourceFound = false;
        bool targetFound = false;
        __try
        {
            const uintptr_t entries = *reinterpret_cast<const uintptr_t*>(
                pending.store + 4);
            const unsigned count = *reinterpret_cast<const unsigned*>(
                pending.store + 8);
            if (!entries || count > 256)
                return;
            Guid128* sourceEntry = nullptr;
            for (unsigned i = 0; i < count; ++i)
            {
                auto* guid = reinterpret_cast<Guid128*>(entries + i * 0x28);
                if (SameGuid(*guid, mapping.source))
                {
                    sourceEntry = guid;
                    sourceFound = true;
                }
                if (SameGuid(*guid, mapping.target))
                    targetFound = true;
            }
            if (sourceEntry && !targetFound)
            {
                *sourceEntry = mapping.target;
                ++gStoreInsertReplacements;
                Log("starting store insert: committed %s -> %s after native "
                    "validation (PlayerStore=%08X, count=%u)",
                    mapping.sourceName, mapping.targetName,
                    static_cast<unsigned>(pending.store), count);
            }
            else
            {
                Log("starting store insert: post-insert %s -> %s not written "
                    "(source=%s, target=%s, PlayerStore=%08X, count=%u)",
                    mapping.sourceName, mapping.targetName,
                    sourceFound ? "present" : "missing",
                    targetFound ? "present" : "missing",
                    static_cast<unsigned>(pending.store), count);
            }

            if ((sourceFound && !targetFound) || pending.mapping >= 2)
                gStoreInsertMappingMask |= 1u << pending.mapping;
            const unsigned completeMask =
                (1u << gStoreMappingCount) - 1u;
            if (gStoreMappingCount &&
                (gStoreInsertMappingMask & completeMask) == completeMask &&
                !gStoreInsertCompleteLogged)
            {
                gStoreInsertCompleteLogged = true;
                Log("starting store insert: completed %u mapped unlocks with "
                    "%u committed replacement(s)", gStoreMappingCount,
                    gStoreInsertReplacements);
                InterlockedExchange(&gStartingStoreNewGameArmed, 0);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("starting store insert: post-insert inspection failed for "
                "PlayerStore %08X", static_cast<unsigned>(pending.store));
        }
    }

    void PlayerStoreRestoreItem_Hook(safetyhook::Context& ctx)
    {
        if (!gStartingStoreRandomizer || !ctx.eax ||
            InterlockedCompareExchange(&gStartingStoreNewGameArmed,
                                       1, 1) != 1)
            return;

        ++gStoreRestoreItems;
        __try
        {
            const auto* item = reinterpret_cast<const Guid128*>(ctx.eax);
            for (unsigned i = 0; i < gStoreMappingCount; ++i)
            {
                const StoreMapping& mapping = gStoreMappings[i];
                if (!SameGuid(*item, mapping.source))
                    continue;
                Log("starting store restore: retained %s while checking "
                    "whether this is an existing save", mapping.sourceName);
                break;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("starting store restore: unreadable entry %08X",
                static_cast<unsigned>(ctx.eax));
        }
    }

    bool LogRestoredPlayerStore(uintptr_t store, unsigned* entryCount,
                                unsigned* sourceMatches,
                                unsigned* targetMatches)
    {
        if (entryCount)
            *entryCount = 0;
        if (sourceMatches)
            *sourceMatches = 0;
        if (targetMatches)
            *targetMatches = 0;
        if (!store)
            return false;

        __try
        {
            const uintptr_t entries =
                *reinterpret_cast<const uintptr_t*>(store + 4);
            const unsigned count =
                *reinterpret_cast<const unsigned*>(store + 8);
            if (!entries || count > 256)
            {
                Log("starting store restore: invalid final PlayerStore "
                    "header (store=%08X entries=%08X count=%u)",
                    static_cast<unsigned>(store),
                    static_cast<unsigned>(entries), count);
                return false;
            }

            unsigned sources = 0;
            unsigned targets = 0;
            for (unsigned entry = 0; entry < count; ++entry)
            {
                const auto* guid = reinterpret_cast<const Guid128*>(
                    entries + entry * 0x28);
                for (unsigned mappingIndex = 0;
                     mappingIndex < gStoreMappingCount; ++mappingIndex)
                {
                    const StoreMapping& mapping =
                        gStoreMappings[mappingIndex];
                    if (SameGuid(*guid, mapping.source))
                        ++sources;
                    if (SameGuid(*guid, mapping.target))
                        ++targets;
                }
            }
            if (entryCount)
                *entryCount = count;
            if (sourceMatches)
                *sourceMatches = sources;
            if (targetMatches)
                *targetMatches = targets;
            if (gProgressionDiagnostics)
                Log("starting store restore: final PlayerStore has %u "
                    "entry/ies, %u source match(es), %u target match(es)",
                    count, sources, targets);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("starting store restore: could not inspect final PlayerStore "
                "%08X", static_cast<unsigned>(store));
            return false;
        }
    }

    void PlayerStoreRestoreDone_Hook(safetyhook::Context& ctx)
    {
        if (InterlockedCompareExchange(&gStartingStoreNewGameArmed,
                                       1, 1) != 1)
            return;
        Log("starting store restore: completed with %u item(s), "
            "PlayerStore=%08X", gStoreRestoreItems,
            static_cast<unsigned>(ctx.edi));
        unsigned count = 0;
        unsigned sources = 0;
        unsigned targets = 0;
        const bool inspected = LogRestoredPlayerStore(
            ctx.edi, &count, &sources, &targets);
        if (gStoreRestoreItems > 0 || (inspected && count > 0))
        {
            Log("starting store restore: non-empty restore cancelled the "
                "pending New Game store randomizer");
            InterlockedExchange(&gStartingStoreNewGameArmed, 0);
        }
        else if (gProgressionDiagnostics)
            Log("starting store restore: gate remains armed for later "
                "PlayerStore population");
    }

    // The shop catalogue is the long list: at least twenty entries, and it
    // still carries the weapons the six-entry list never had.  Rather than
    // overwrite entries - which would duplicate one item and lose another -
    // exchange the two positions, so the catalogue stays a permutation of
    // itself and every item remains buyable somewhere.
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

    // The authored store list, as it sits in the decompressed SimGroup buffer
    // before anything reads it.  Entries there are 20 bytes - a 16-byte GUID
    // plus four bytes the loader fills in - and the chapter-1 list is four
    // long: Plasma Cutter, Line Gun, Pulse Rifle, Plasma Energy.  Patching
    // here rather than in one of its derivations is what makes the change
    // reach the shop and the save: the decoded array, the player's list and
    // the persisted unlock set are all downstream of these bytes.
    // The first three entries of the authored chapter-1 list, read out of a
    // full memory dump and confirmed byte for byte against a save file:
    // Plasma Cutter, Line Gun, Pulse Rifle.
    constexpr unsigned kAuthoredStoreStride = 0x14;
    constexpr unsigned kAuthoredStoreLength = 4;
    bool AuthoredStoreAnchorAt(uintptr_t at)
    {
        // Three exact GUIDs at the authored spacing. Specific enough that a
        // chance match anywhere else in the address space is not a concern.
        const Guid128* first = reinterpret_cast<const Guid128*>(at);
        const Guid128* second =
            reinterpret_cast<const Guid128*>(at + kAuthoredStoreStride);
        const Guid128* third =
            reinterpret_cast<const Guid128*>(at + kAuthoredStoreStride * 2);
        return SameGuid(*first, kAuthoredStoreAnchor0) &&
               SameGuid(*second, kAuthoredStoreAnchor1) &&
               SameGuid(*third, kAuthoredStoreAnchor2);
    }

    // ---------------------------------------------------------------------
    // The route that actually feeds the starting unlocks into the PlayerStore.
    //
    // AddStoreList at 0x00596910 walks an ItemStoreList: edi = ecx + 0x44, then
    // 32 iterations reading 16 bytes from [edi-8], each handed to public
    // AddItem with the PlayerStore in eax.  The first read is ecx + 0x3C, the
    // entry array of the list.
    //
    // Replacing the GUIDs in that array before the native loop runs means the
    // whole insert - including SimManager::FindOrCreateTargetKey at 0x00596AC4,
    // whose null return silently drops an entry - executes on the target item.
    // Replacing later, at the InsertEntry boundary, is what made every entry
    // disappear: the key lookup ran on an item whose resources were not
    // registered.
    //
    // The array is restored on the way out, so the authored list is left as it
    // was and a second ingestion cannot produce duplicates.
    // ---------------------------------------------------------------------
    // Was 0x0059695C, AddStoreList's lone ret.  A mid-hook needs five
    // bytes and there are only four there - ret plus three int3 - because
    // the next function starts at 0x00596960, so the detour overwrote its
    // first byte.  The epilogue begins seven bytes earlier and the restore
    // reads none of the registers being popped, so it can sit there
    // instead: 5F 5E 5B 83 C4 18 C3.
    constexpr uintptr_t kPlayerStoreAddStoreListReturnSite = 0x00596956;
    constexpr unsigned char kAddStoreListEpilogue[] = {
        0x5F, 0x5E, 0x5B, 0x83, 0xC4, 0x18, 0xC3};
    constexpr unsigned kStoreListEntryOffset = 0x3C;
    constexpr unsigned kStoreListEntryCount = 32;

    struct StoreListSwap
    {
        uintptr_t list = 0;
        unsigned slots[8]{};
        unsigned mappings[8]{};
        unsigned count = 0;
    };
    StoreListSwap gStoreListSwap;

    void SwapStoreListEntries(uintptr_t list, uintptr_t store)
    {
        gStoreListSwap = {};
        if (!gStoreMappingCount || !list)
            return;
        __try
        {
            if (!store || *reinterpret_cast<const uintptr_t*>(store) !=
                              kPlayerStoreVtable)
                return;
            const uintptr_t entries = list + kStoreListEntryOffset;
            for (unsigned i = 0;
                 i < gStoreMappingCount && gStoreListSwap.count < 8; ++i)
            {
                const StoreMapping& mapping = gStoreMappings[i];
                if (SameGuid(mapping.source, mapping.target))
                    continue;
                for (unsigned slot = 0; slot < kStoreListEntryCount; ++slot)
                {
                    auto* guid = reinterpret_cast<Guid128*>(
                        entries + slot * sizeof(Guid128));
                    if (!SameGuid(*guid, mapping.source))
                        continue;
                    SIZE_T written = 0;
                    if (!WriteProcessMemory(GetCurrentProcess(), guid,
                                            &mapping.target, sizeof(Guid128),
                                            &written))
                        break;
                    const unsigned n = gStoreListSwap.count++;
                    gStoreListSwap.slots[n] = slot;
                    gStoreListSwap.mappings[n] = i;
                    Log("starting store list: %s -> %s in slot %u "
                        "(list=%08X, PlayerStore=%08X)",
                        mapping.sourceName, mapping.targetName, slot,
                        static_cast<unsigned>(list),
                        static_cast<unsigned>(store));
                    break;
                }
            }
            if (gStoreListSwap.count)
                gStoreListSwap.list = list;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gStoreListSwap = {};
            Log("starting store list: unreadable list at %08X",
                static_cast<unsigned>(list));
        }
    }

    void AddStoreListDone_Hook(safetyhook::Context&)
    {
        if (!gStoreListSwap.list || !gStoreListSwap.count)
            return;
        const StoreListSwap swap = gStoreListSwap;
        gStoreListSwap = {};
        __try
        {
            const uintptr_t entries = swap.list + kStoreListEntryOffset;
            for (unsigned i = 0; i < swap.count; ++i)
            {
                if (swap.mappings[i] >= gStoreMappingCount)
                    continue;
                SIZE_T written = 0;
                WriteProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(entries +
                        swap.slots[i] * sizeof(Guid128)),
                    &gStoreMappings[swap.mappings[i]].source, sizeof(Guid128),
                    &written);
            }
            Log("starting store list: %u slot(s) restored after native "
                "ingestion", swap.count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("starting store list: could not restore the authored list");
        }
    }

    void PatchAuthoredStoreList()
    {
        if (!gStartingStoreRandomizer || gStoreMappingCount != 4 ||
            InterlockedCompareExchange(&gStartingStoreNewGameArmed,
                                       1, 1) != 1)
            return;
        const ULONGLONG now = GetTickCount64();
        if (gAuthoredStorePatched)
            return;
        if (gAuthoredStoreLastScan && now - gAuthoredStoreLastScan < 500)
            return;
        gAuthoredStoreLastScan = now;
        ++gAuthoredStoreAttempts;

        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        uintptr_t cursor =
            reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const uintptr_t maximum =
            reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        unsigned blocks = 0, writes = 0;
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
                    const uintptr_t limit =
                        end - kAuthoredStoreStride * kAuthoredStoreLength;
                    for (uintptr_t at = base; at < limit; at += 4)
                    {
                        if (!AuthoredStoreAnchorAt(at))
                            continue;
                        ++blocks;
                        for (unsigned slot = 0; slot < kAuthoredStoreLength;
                             ++slot)
                        {
                            const uintptr_t field =
                                at + slot * kAuthoredStoreStride;
                            const Guid128 current =
                                *reinterpret_cast<const Guid128*>(field);
                            for (unsigned i = 0; i < gStoreMappingCount; ++i)
                            {
                                const StoreMapping& mapping = gStoreMappings[i];
                                if (SameGuid(mapping.source, mapping.target) ||
                                    !SameGuid(current, mapping.source))
                                    continue;
                                SIZE_T written = 0;
                                if (WriteProcessMemory(GetCurrentProcess(),
                                        reinterpret_cast<void*>(field),
                                        &mapping.target, sizeof(Guid128),
                                        &written))
                                {
                                    ++writes;
                                    if (gProgressionDiagnostics)
                                        Log("progression authored store: %s -> "
                                            "%s at %08X (entry %u)",
                                            mapping.sourceName,
                                            mapping.targetName,
                                            static_cast<unsigned>(field), slot);
                                }
                                break;
                            }
                        }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // Streaming may release a region between query and read.
                }
            }
            if (end <= cursor)
                break;
            cursor = end;
        }
        if (blocks)
        {
            gAuthoredStorePatched = true;
            Log("progression authored store: %u block(s) found, %u entry/ies "
                "rewritten on attempt %u", blocks, writes,
                gAuthoredStoreAttempts);
        }
        else if (gProgressionDiagnostics && gAuthoredStoreAttempts <= 2)
            Log("progression authored store: no authored list found yet "
                "(attempt %u)", gAuthoredStoreAttempts);
    }

    // The list the shelf is drawn from.  Entries are 40 bytes: three fields,
    // an id, then the item GUID.  Patching the authored data, the player's
    // list and the store catalogue all left the shelf unchanged, so this is
    // the last thing between the data and the screen - if rewriting it here
    // does not move, nothing about the item set is decided by a GUID list at
    // all.  Unlike the authored buffer this one is rebuilt, so the address is
    // remembered and re-checked rather than patched once.
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
                        // slot we want, and the one after it.  A lone GUID is
                        // not enough, the same value sits in half a dozen
                        // other structures.
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

    // Both routes end in the same idempotent replacement: it only writes when
    // the current value is still the authored source, so whichever of the two
    // runs first wins and the other one does nothing.

    // ---------------------------------------------------------------------
    // World weapons.
    //
    // A pickup grants exactly one thing, and PickupItem's decoder writes it:
    // property 17 checks for a null id and otherwise copies sixteen bytes to
    // object+0x344.  The chapter 1 plasma cutter is an ordinary pickup whose
    // property 17 names the global weapon definition, so swapping that id is
    // all it takes to find something else in the maintenance bay.
    //
    // Only three weapons exist as a world entity, each with its own archive
    // under global_assets\...\weapn_str.  Everything else - Ripper,
    // Flamethrower, Pulse Rifle, Contact Beam - exists only as a GunProxy,
    // which is a store entry, and can never be picked up off the floor.
    // ---------------------------------------------------------------------
    constexpr uintptr_t kPickupItemDecode = 0x00513260;
    constexpr unsigned kPickupGrantedItem = 0x344;
    safetyhook::InlineHook gPickupItemDecodeHook;

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
    unsigned gWorldWeaponMap[kWorldWeaponCount]{};
    bool gWorldWeaponMapReady = false;

    void BuildWorldWeaponMap()
    {
        if (gWorldWeaponMapReady)
            return;
        gWorldWeaponMapReady = true;
        for (unsigned i = 0; i < kWorldWeaponCount; ++i)
            gWorldWeaponMap[i] = i;
        // A rotation, not a shuffle: with three entries a plain shuffle can
        // leave one weapon on itself, and finding the plasma cutter again
        // reads as a broken randomizer rather than as chance.
        // A small mix of the shared seed; the rotation only needs to be
        // stable across reloads, not statistically strong.
        uint32_t mixed = gStartingSuitSeed ^ 0x57504E31u;
        mixed ^= mixed >> 16;
        mixed *= 0x7FEB352Du;
        mixed ^= mixed >> 15;
        const unsigned step = 1 + (mixed % (kWorldWeaponCount - 1));
        for (unsigned i = 0; i < kWorldWeaponCount; ++i)
            gWorldWeaponMap[i] = (i + step) % kWorldWeaponCount;
        for (unsigned i = 0; i < kWorldWeaponCount; ++i)
            Log("world weapon: %s -> %s", kWorldWeapons[i].name,
                kWorldWeapons[gWorldWeaponMap[i]].name);
    }

    void ApplyWorldWeapon(void* self)
    {
        if ((!gFirstWeaponRandomizer && gCheatPickupWeapon <= 0) || !self)
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
        // The cheat comes first and ignores what the pickup was: every
        // pickup in the world becomes the chosen weapon, which is the only
        // way to arm a checkpoint start without knowing the inventory
        // layout.  Deliberately blunt, and deliberately off by default -
        // it also overwrites schematics and key items, so a run played with
        // it on is not a run worth keeping.
        if (gCheatPickupWeapon > 0 &&
            gCheatPickupWeapon <= static_cast<int>(kWorldWeaponCount))
        {
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
            return;
        }

        for (unsigned i = 0; i < kWorldWeaponCount; ++i)
        {
            if (!SameGuid(granted, kWorldWeapons[i].id))
                continue;
            const WorldWeapon& target = kWorldWeapons[gWorldWeaponMap[i]];
            if (SameGuid(granted, target.id))
                return;
            SIZE_T written = 0;
            if (!WriteProcessMemory(GetCurrentProcess(),
                                    reinterpret_cast<void*>(slot),
                                    &target.id, sizeof(target.id), &written))
                return;
            Log("world weapon: pickup %08X now grants %s instead of %s",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)),
                target.name, kWorldWeapons[i].name);
            return;
        }
    }

    void __fastcall PickupItemDecode_Hook(void* self, void*, void* attributes)
    {
        gPickupItemDecodeHook.thiscall<void>(self, attributes);
        ApplyWorldWeapon(self);
    }


    // ---------------------------------------------------------------------
    // Upgrade bench.
    //
    // The bench describes its whole node catalogue: twelve blocks of nine
    // properties, each block naming a node type in plain text
    // (upg_nodedesc_dmg2, _spd2, _cap2, _wid2, _rel2, _min2, _hp2, _air2,
    // _eng2, _dur2, _rng2, _chr2) with UI_UPG_Points beside it, and carrying
    // an id at block+0x08.  The blocks sit at +0x5A0 and repeat every 0x78,
    // verified against all twelve decoder cases.
    //
    // Rotating those ids is the randomization *if* the id carries the effect
    // and the string only labels it.  Static reading could not settle that -
    // the id resolves to a lone dword that is in no name table - so the hook
    // logs all twelve before touching them.  Twelve identical values would
    // mean there is nothing to rotate, and the log says so without anyone
    // having to look at a bench in game.
    // ---------------------------------------------------------------------
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


    // ---------------------------------------------------------------------
    // Loot tables.
    //
    // ItemDropList is the game's weighted drop table and there are roughly
    // three hundred of them, one per combat area, so this is what actually
    // decides what falls out of necromorphs and crates.  Layout, read out of
    // the decoder at 0x0050E190 and confirmed against authored records:
    // sixteen entries of twenty bytes at +0x50, each a sixteen-byte item id
    // followed by a float weight.  Case 6 stores the first id to this+0x50,
    // case 7 the first weight to this+0x60, case 37 the last weight to
    // this+0x18C, which pins base, stride and count.
    //
    // The weights are relative, not percentages - measured tables run 75/25,
    // 5/1/23/1 and 10/20/1/19/10 - so rotating them changes which item in a
    // table is the common one and which is the rare one.
    //
    // Only the values move; the number of entries never changes, so the
    // authored record keeps the shape the game expects.
    // ---------------------------------------------------------------------
    constexpr uintptr_t kItemDropListDecode = 0x0050E190;
    constexpr unsigned kDropFirstEntry = 0x50;
    constexpr unsigned kDropEntryStride = 0x14;
    constexpr unsigned kDropEntryCount = 16;
    constexpr unsigned kDropWeightOffset = 0x10;
    // The weight of nothing.  0x0050EBA0 adds up the weights of the populated
    // entries and then adds this field on top, and its caller at 0x0050EF9D
    // multiplies that total by a random number and compares the result with
    // this same field: a roll below it means the table yields nothing at all.
    // So the chance of an empty drop is field / (entry weights + field), and
    // the shipped tables bear that out - 5+1+23+1 entries beside a field of
    // 70, and 10+20+1+19+10 beside a field of 40.  The designers always made
    // it add up to a hundred, which makes the field a plain percentage.
    constexpr unsigned kDropNothingWeight = 0x19C;
    // A table that never gives anything would read as a broken randomizer
    // rather than as a hard run, so the drought has a ceiling.
    constexpr float kDropNothingCeiling = 0.90f;
    // A table with three hundred siblings would drown the log, so only the
    // first few are described in full.
    constexpr unsigned kDropLogLimit = 12;
    safetyhook::InlineHook gItemDropListDecodeHook;
    unsigned gDropTablesSeen = 0;

    // The whole game draws its drops from eighteen item ids across 608
    // tables.  Twelve of them appear in fourteen tables or more, spread over
    // every chapter, so they are demonstrably streamed wherever a drop table
    // exists.  Counts below are the measured number of tables each id
    // appears in.
    //
    // Two rules keep the six others out, and both matter.  Rarity: an id used
    // once to five times may well live in a level that never streams it, and
    // handing it to another area would produce a pickup the game cannot
    // resolve.  Class: every id here resolves to a PowerUp or SpareAmmo, and
    // nothing else is allowed in.  One of the excluded six is the Captain's
    // RIG from ch02_med, which is a KeyItem - dropping progression items into
    // random loot tables across the whole game would be a far worse bug than
    // a missing pickup.
    //
    // "None" is a real authored PowerUp used by 329 tables and is very
    // probably the entry that means nothing drops.  It stays in the pool on
    // purpose: moving it around is what makes some areas generous and others
    // barren.
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

    // The shared StableRandomizerHash lives further down in dllmain.cpp than
    // this include, so fold the key here.  The rotation only has to be stable
    // across reloads, not statistically strong.
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

        // An empty table means the hook fired on something that is not a
        // drop list, which is worth seeing.  A single-entry table has nothing
        // to rotate but the pool can still replace its one item.
        if (count == 0 ||
            (count < 2 && !gLootGlobalPool && gLootScarcityApplied == 0))
        {
            if (gProgressionDiagnostics && gDropTablesSeen < kDropLogLimit)
                Log("loot table %08X: %u entries, left alone",
                    static_cast<unsigned>(object), count);
            ++gDropTablesSeen;
            return;
        }

        // Keyed on the table's own first two items so one table randomizes
        // the same way every time it streams in - reloading a checkpoint must
        // not reshuffle the loot.
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
                // Drawn from the whole game's item set rather than from this
                // table, so an area can drop something it never held.  Two
                // entries granting the same item would waste a slot, so a
                // taken pick steps forward until it finds a free one.
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

        // Scarcity: shift how often the table yields nothing at all.  The
        // entry weights are left alone, so what an area can drop and in what
        // proportion stays whatever the rest of this function decided; only
        // how often it gives anything moves.
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
        else if (gExistingSaveStoreMigration)
        {
            // Explicit rescue path for a save that already reached the first
            // store before StartingStoreRandomizer was installed.  The exact
            // six-entry Chapter 1 list guard prevents touching other stores.
            ApplyStoreProgression(self);
        }
    }

    void InstallProgressionHooks()
    {
        if (gStartingSuitDiagnostics || gStartingSuitRandomizer ||
            gStartingStoreRandomizer || gSuitChanger)
        {
            if (!UniformApplySignatureMatches())
                Log("starting suit diagnostic: NOT installed; retail "
                    "UniformManager signature mismatch at %08X",
                    static_cast<unsigned>(kUniformApply));
            else
            {
                gUniformApplyHook = safetyhook::create_inline(
                    reinterpret_cast<void*>(kUniformApply),
                    reinterpret_cast<void*>(&UniformApply_Hook));
                Log("starting suit diagnostic hook %s at %08X",
                    gUniformApplyHook ? "active" : "NOT installed",
                    static_cast<unsigned>(kUniformApply));
            }

            if (gStartingSuitRandomizer || gStartingStoreRandomizer)
            {
                if (!StartNewGameSignatureMatches())
                    Log("starting suit randomizer: DISABLED; retail "
                        "StartNewGame_Menu signature mismatch at %08X",
                        static_cast<unsigned>(kStartNewGame));
                else
                {
                    gStartNewGameSuitGateHook = safetyhook::create_mid(
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
        const bool storePatch = gStoreMappingCount == 4 &&
            (gStartingStoreRandomizer || gExistingSaveStoreMigration ||
             fullProgression);
        // The schematic hooks belong to the manifest randomizer alone.  They
        // used to sit under a braceless if, so the attribute decoder was
        // installed whenever anything else was, and an early return above
        // this point meant the bench and world-weapon hooks below were
        // skipped unless the store or schematic randomizer happened to be on.
        if (fullProgression)
        {
            gSchematicSpawnInitHook = safetyhook::create_inline(
                reinterpret_cast<void*>(kSchematicSpawnInit),
                reinterpret_cast<void*>(&SchematicSpawnInit_Hook));
            gSchematicAttributeDecodeHook = safetyhook::create_inline(
                reinterpret_cast<void*>(kSchematicAttributeDecode),
                reinterpret_cast<void*>(&SchematicAttributeDecode_Hook));
            Log("progression schematic: attribute decoder %s at %08X",
                gSchematicAttributeDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kSchematicAttributeDecode));
        }
        if (gLootRandomizer || gLootWeightRandomizer || gLootGlobalPool ||
            gLootScarcityApplied != 0)
        {
            gItemDropListDecodeHook = safetyhook::create_inline(
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
            gUpgradeBenchDecodeHook = safetyhook::create_inline(
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
            if (gFirstWeaponRandomizer)
                BuildWorldWeaponMap();
            if (gCheatPickupWeapon > 0)
                Log("cheat: EVERY pickup grants the %s - schematics and key "
                    "items are overwritten too, turn this off when done",
                    kWorldWeapons[gCheatPickupWeapon - 1].name);
            gPickupItemDecodeHook = safetyhook::create_inline(
                reinterpret_cast<void*>(kPickupItemDecode),
                reinterpret_cast<void*>(&PickupItemDecode_Hook));
            Log("world weapon: pickup decoder %s at %08X",
                gPickupItemDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kPickupItemDecode));
        }
        if (storePatch)
        {
            gItemStoreListDecodeHook = safetyhook::create_inline(
                reinterpret_cast<void*>(kItemStoreListDecode),
                reinterpret_cast<void*>(&ItemStoreListDecode_Hook));
            if (!PlayerStoreAddStoreListSignatureMatches())
                Log("starting store handoff: NOT installed; retail signature "
                    "mismatch at %08X",
                    static_cast<unsigned>(kPlayerStoreAddStoreList));
            else
            {
                gPlayerStoreAddStoreListHook = safetyhook::create_mid(
                    reinterpret_cast<void*>(kPlayerStoreAddStoreList),
                    &PlayerStoreAddStoreList_Hook);
                // The authored list is put back on the way out.  Refuse to
                // hook anything but the exact epilogue this was measured on.
                if (std::memcmp(reinterpret_cast<const void*>(
                                    kPlayerStoreAddStoreListReturnSite),
                                kAddStoreListEpilogue,
                                sizeof(kAddStoreListEpilogue)) == 0)
                    gAddStoreListDoneHook = safetyhook::create_mid(
                        reinterpret_cast<void*>(
                            kPlayerStoreAddStoreListReturnSite),
                        &AddStoreListDone_Hook);
                else
                    Log("starting store list: restore NOT installed; %08X is "
                        "not the measured epilogue",
                        static_cast<unsigned>(
                            kPlayerStoreAddStoreListReturnSite));
                Log("starting store list: swap %s, restore %s",
                    gPlayerStoreAddStoreListHook ? "active" : "NOT installed",
                    gAddStoreListDoneHook ? "active" : "NOT installed");
            }
            if (!PlayerStoreInsertEntrySignatureMatches())
                Log("starting store insert: NOT installed; retail signature "
                    "mismatch at %08X",
                    static_cast<unsigned>(kPlayerStoreInsertEntry));
            else
            {
                gPlayerStoreInsertEntryHook = safetyhook::create_mid(
                    reinterpret_cast<void*>(kPlayerStoreInsertEntry),
                    &PlayerStoreInsertEntry_Hook);
                gPlayerStoreInsertEntryDoneHook = safetyhook::create_mid(
                    reinterpret_cast<void*>(kPlayerStoreInsertEntryDone),
                    &PlayerStoreInsertEntryDone_Hook);
            }
            if (!PlayerStoreRestoreSignaturesMatch())
                Log("starting store restore: NOT installed; retail signature "
                    "mismatch at %08X/%08X",
                    static_cast<unsigned>(kPlayerStoreRestoreItem),
                    static_cast<unsigned>(kPlayerStoreRestoreDone));
            else
            {
                gPlayerStoreRestoreItemHook = safetyhook::create_mid(
                    reinterpret_cast<void*>(kPlayerStoreRestoreItem),
                    &PlayerStoreRestoreItem_Hook);
                gPlayerStoreRestoreDoneHook = safetyhook::create_mid(
                    reinterpret_cast<void*>(kPlayerStoreRestoreDone),
                    &PlayerStoreRestoreDone_Hook);
            }
        }
        Log("progression hooks: schematic=%s at %08X, Chapter1 store=%s at %08X",
            gSchematicSpawnInitHook ? "active" : "NOT installed",
            static_cast<unsigned>(kSchematicSpawnInit),
            gItemStoreListDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kItemStoreListDecode));
        if (storePatch)
        {
            Log("starting store PlayerStore handoff=%s at %08X",
                gPlayerStoreAddStoreListHook ? "active" : "NOT installed",
                static_cast<unsigned>(kPlayerStoreAddStoreList));
            Log("starting store PlayerStore insert=%s at %08X/%08X",
                gPlayerStoreInsertEntryHook &&
                        gPlayerStoreInsertEntryDoneHook ?
                    "active" : "NOT installed",
                static_cast<unsigned>(kPlayerStoreInsertEntry),
                static_cast<unsigned>(kPlayerStoreInsertEntryDone));
            Log("starting store PlayerStore restore=%s at %08X/%08X",
                gPlayerStoreRestoreItemHook && gPlayerStoreRestoreDoneHook ?
                    "active" : "NOT installed",
                static_cast<unsigned>(kPlayerStoreRestoreItem),
                static_cast<unsigned>(kPlayerStoreRestoreDone));
        }
    }
