    // Enhanced exploders.
    //
    // Exploder (class hash 0x73553751, decoder 0x004CAE70, dispatch table
    // 0x004CB100) has only five floats, and all 59 exploders author the same values:
    //
    //   property 0 -> +0xA68   20
    //   property 1 -> +0xA6C    3
    //   property 2 -> +0xA48    3
    //   property 3 -> +0xA54    3
    //   property 4 -> +0xA60    3
    //
    // The last three are twelve bytes apart and 0x004CAF22 writes them through an
    // index, so they form one array; the pair at +0xA68 is separate. Which is blast
    // radius and which is damage is not clear from the code, so all five are scaled
    // by the same modest factor. There is no authored super exploder to copy from.
    //
    // The decoder takes `this` in EAX ("mov edi, eax" at 0x004CAE73), so a
    // __fastcall inline hook would read the wrong register. The hook sits on the
    // epilogue at 0x004CB0E1 instead, where EDI still holds the object and
    // "mov eax, edi / pop edi / pop esi / pop ebp" is a clean five-byte site.
    constexpr uintptr_t kExploderDecodeEpilogue = 0x004CB0E1;
    constexpr unsigned kExploderStats[] = {0xA48, 0xA54, 0xA60, 0xA68, 0xA6C};
    constexpr unsigned kExploderStatCount =
        sizeof(kExploderStats) / sizeof(kExploderStats[0]);
    // Entity ids live at +0x20; the crate randomizer measured that in game.
    constexpr unsigned kEntityId = 0x20;
    // An entity carries a 4x4 transform: three rows of four floats from +0x70,
    // then the position at +0xB0. Spawner dumps showed a plain rotation,
    // (0,0,-1,0), (0,1,0,0), (1,0,0,0). Scaling it was the only way to vary an
    // exploder's look without a second character asset.

    // The character asset a creature uses is named in plain text, and the entity
    // keeps a pointer to it at +0x668. Class 0x71BC20FF property 36 writes it: its
    // case at 0x0047580C calls 0x0073A9D0, which only checks for the string type
    // (0x25E3) and returns the pointer, with no hash or lookup in between.
    // Measured: an Exploder reads "exploder", a Leaper "leaper", and every Zombie
    // "slasher" whatever its variant.
    //
    // This was an experiment: point an exploder at another creature the run
    // already streams and see whether the model changes. The code that consumes
    // the pointer was never found.
    //
    // The replacement strings are static, so the pointer stays valid for the life
    // of the process; the authored text lives in streamed archive memory.
    constexpr unsigned kEntityModelName = 0x668;

    // An entity's records decode one after another into the same object, and the
    // Exploder record comes first: at its epilogue only the five floats are set,
    // the transform at +0x70 is zero and the model name at +0x668 still null.
    //
    // Class 0x71BC20FF is decoded later and owns the model name. Its loop runs
    // from 0x004754A4 to 0x00475B46; at 0x00475B8F the record is decoded and EDI
    // still holds the object. That site fires for every creature, so only an
    // object that reads "exploder" is touched.
    //
    // Writing the field after the decode changed nothing on screen, so the hook
    // goes one step earlier: 0x00475807 resolves the authored string and
    // 0x0047580C stores it. Replacing EAX between the two puts the name where the
    // authored one would go, and the rest of the decode and the finaliser at
    // 0x00475B96 see it.
    constexpr uintptr_t kModelNameStore = 0x0047580C;
    // "mov dword ptr [edi + 0x668], eax"
    constexpr unsigned char kModelNameStoreBytes[] = {
        0x89, 0x87, 0x68, 0x06, 0x00, 0x00};
    safetyhook::MidHook gModelNameStoreHook;

    constexpr uintptr_t kModelDecodeDone = 0x00475B8F;
    // "mov edx, [edi] / mov eax, [edx + 0x3c]"
    constexpr unsigned char kModelDecodeDoneBytes[] = {
        0x8B, 0x17, 0x8B, 0x42, 0x3C, 0x8B, 0xCF};
    safetyhook::MidHook gModelDecodeDoneHook;
    // "sup_expl" is the second exploder asset in ds1_randomizer_assets, listed in
    // the character stream manifest so the streaming policy keeps it resident. The
    // retail names are for comparison: if only sup_expl works, the difference is
    // residency rather than the field.
    const char* const kModelNameProbes[] = {
        "sup_expl", "leaper", "lurker", "twitcher", "slasher", "super_slasher",
    };
    constexpr unsigned kModelNameProbeCount =
        sizeof(kModelNameProbes) / sizeof(kModelNameProbes[0]);
    // "mov eax, edi / pop edi / pop esi / pop ebp / pop ebx"
    constexpr unsigned char kExploderEpilogueBytes[] = {
        0x8B, 0xC7, 0x5F, 0x5E, 0x5D, 0x5B};

    safetyhook::MidHook gExploderEpilogueHook;
    bool gEnhancedExploders = false;
    bool gEnhancedExploderRuntimeStats = false;
    int gEnhancedExploderPercent = 50;
    int gEnhancedExploderStrength = 150;   // per cent of the authored value
    int gExploderModelProbe = 0;           // 0 off, else 1-based into the list

    unsigned gExplodersSeen = 0;
    unsigned gExplodersEnhanced = 0;
    // The per-spawn share and its count moved to the variant table in
    // variant_runtime.inc. Only the two settings stay here because existing INIs
    // name them.

    // Defined below; the poll needs it before the definition is reached.
    void ApplyExploderModelProbe(uintptr_t object);



    // Runs once a creature's model name exists. Returns true when the object is an
    // exploder this run enhances.
    // Every exploder in an area is an instance of one authored entity
    // (filtration_z01 holds all 59 behind a single template), so a decision here
    // covers all of them. The name pass fires once per template, not per creature.
    //
    // A coin flip per template once came up heads twice and made every exploder
    // super, so the templates alternate instead: first enhanced, second not, and so
    // on. The seed decides which goes first.
    constexpr unsigned kTemplateMemory = 16;
    Guid128 gSeenTemplates[kTemplateMemory]{};
    unsigned gSeenTemplateCount = 0;

    bool ChosenByEntityId(uintptr_t object)
    {
        Guid128 id{};
        __try
        {
            std::memcpy(&id, reinterpret_cast<const void*>(object + kEntityId),
                        sizeof(id));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        unsigned index = kTemplateMemory;
        for (unsigned i = 0; i < gSeenTemplateCount; ++i)
            if (SameGuid(gSeenTemplates[i], id))
            {
                index = i;
                break;
            }
        if (index == kTemplateMemory)
        {
            if (gSeenTemplateCount >= kTemplateMemory)
                return false;
            index = gSeenTemplateCount;
            gSeenTemplates[gSeenTemplateCount++] = id;
        }
        if (gEnhancedExploderPercent >= 100)
            return true;
        if (gEnhancedExploderPercent <= 0)
            return false;
        // Which of the two alternating halves comes first is the seed's job;
        // everything after that alternates strictly.
        uint32_t phase = gStartingSuitSeed ^ 0x45585044u;
        phase ^= phase >> 16;
        phase *= 0x7FEB352Du;
        phase ^= phase >> 15;
        const unsigned every = 100u / static_cast<unsigned>(
            gEnhancedExploderPercent > 0 ? gEnhancedExploderPercent : 100);
        return ((index + (phase & 1u)) % (every ? every : 1u)) == 0u;
    }

    bool ChosenEnhancedExploder(uintptr_t object)
    {
        const char* name = nullptr;
        __try
        {
            std::memcpy(&name, reinterpret_cast<const void*>(
                            object + kEntityModelName), sizeof(name));
            // By now the name may already be ours, so either spelling counts.
            if (!name || (std::strncmp(name, "exploder", 9) != 0 &&
                          std::strncmp(name, "sup_expl", 9) != 0))
                return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return ChosenByEntityId(object);
    }

    void ModelDecodeDone_Hook(safetyhook::Context& ctx)
    {
        if (!gEnhancedExploders)
            return;
        const uintptr_t object = ctx.edi;
        if (!object || !ChosenEnhancedExploder(object))
            return;
    }

    // EAX holds the authored name and EDI the object. Only a chosen exploder is
    // redirected, and only the name changes; the store itself still happens.
    void ModelNameStore_Hook(safetyhook::Context& ctx)
    {
        if (!gEnhancedExploders || gExploderModelProbe <= 0 ||
            gExploderModelProbe > static_cast<int>(kModelNameProbeCount))
            return;
        // Renaming a creature after an archive that is not loaded would leave it
        // with no model at all. sup_expl has to be resident first; the retail
        // names are streamed by the level anyway.
        if (gExploderModelProbe == 1 && !gSuperExploderResident)
        {
            // The archive may still be loading when the pin first checks it; the
            // residency watch updates the flag, so a later exploder gets the name.
            static unsigned quiet = 0;
            if (quiet < 3)
            {
                ++quiet;
                Log("model name: sup_expl not resident yet, this exploder "
                    "stays as it is");
            }
            return;
        }
        const char* authored = reinterpret_cast<const char*>(ctx.eax);
        if (!authored)
            return;
        bool isExploder = false;
        __try
        {
            isExploder = std::strncmp(authored, "exploder", 9) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        if (!isExploder)
            return;
        // The entity id is already in place before the records decode, which
        // is what lets the same half be chosen here and in the stat pass.
        if (!ChosenByEntityId(ctx.edi))
            return;
        const char* wanted = kModelNameProbes[gExploderModelProbe - 1];
        ctx.eax = reinterpret_cast<uintptr_t>(wanted);
        static unsigned reported = 0;
        if (reported < 6)
        {
            ++reported;
            Log("model name: exploder %08X decoded as \"%s\"",
                static_cast<unsigned>(ctx.edi), wanted);
        }
    }

    void ApplyExploderModelProbe(uintptr_t object)
    {
        if (gExploderModelProbe <= 0 ||
            gExploderModelProbe > static_cast<int>(kModelNameProbeCount))
            return;
        const char* wanted = kModelNameProbes[gExploderModelProbe - 1];
        const char* current = nullptr;
        __try
        {
            std::memcpy(&current, reinterpret_cast<const void*>(
                            object + kEntityModelName), sizeof(current));
            // Only replace a pointer that still reads as the authored name.
            if (!current || std::strncmp(current, "exploder", 9) != 0)
            {
                if (gExplodersEnhanced < 4)
                    Log("model probe: %08X has +0x668 = %08X (%s), not the "
                        "authored name - left alone",
                        static_cast<unsigned>(object),
                        static_cast<unsigned>(
                            reinterpret_cast<uintptr_t>(current)),
                        current ? current : "null");
                return;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        SIZE_T written = 0;
        if (WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(
                                   object + kEntityModelName),
                               &wanted, sizeof(wanted), &written) &&
            gExplodersEnhanced < 4)
            Log("model probe: %08X now names \"%s\" instead of \"exploder\"",
                static_cast<unsigned>(object), wanted);
    }

    void ApplyEnhancedExploder(uintptr_t object)
    {
        // Off by default. The sup_expl donor already carries the stronger numbers
        // and the spawn path swaps it in for every second exploder. This pass works
        // per template, so with one template behind all 59 exploders of a level it
        // hits all of them or none.
        if (!gEnhancedExploderRuntimeStats || !gEnhancedExploders || !object)
            return;

        Guid128 id{};
        float stats[kExploderStatCount]{};
        __try
        {
            std::memcpy(&id, reinterpret_cast<const void*>(object + kEntityId),
                        sizeof(id));
            for (unsigned i = 0; i < kExploderStatCount; ++i)
                std::memcpy(&stats[i],
                            reinterpret_cast<const void*>(
                                object + kExploderStats[i]),
                            sizeof(float));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        // An exploder whose values are not the authored ones is either
        // already enhanced or not the object this was measured on.
        bool authored = true;
        for (unsigned i = 0; i < kExploderStatCount; ++i)
            if (stats[i] <= 0.0f || stats[i] > 100.0f)
                authored = false;
        if (!authored)
            return;

        ++gExplodersSeen;
        // Same alternation as the name pass; a separate coin flip here made every
        // exploder super.
        if (!ChosenByEntityId(object))
            return;

        const float factor = static_cast<float>(gEnhancedExploderStrength) / 100.0f;
        for (unsigned i = 0; i < kExploderStatCount; ++i)
        {
            const float value = stats[i] * factor;
            SIZE_T written = 0;
            WriteProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(
                                   object + kExploderStats[i]),
                               &value, sizeof(value), &written);
        }
        ++gExplodersEnhanced;
        if (gExplodersEnhanced <= 8)
            Log("enhanced exploder %08X (id %08X-%08X): %.0f %.0f %.0f %.0f "
                "%.0f -> x%.2f", static_cast<unsigned>(object),
                id.value[0], id.value[1], stats[0], stats[1], stats[2],
                stats[3], stats[4], factor);
    }

    void ExploderEpilogue_Hook(safetyhook::Context& ctx)
    {
        ApplyEnhancedExploder(ctx.edi);
    }

    void LoadExploderSettings()
    {
        // Retired: the pool carries the retail Exploder and no sup_expl donor, so
        // the setting is only read to log that it does nothing.
        gEnhancedExploders = false;
        if (GetPrivateProfileIntA("DeadSpace2008Randomizer", "EnhancedExploders", 0, gIniPath) != 0)
            Log("EnhancedExploders=1 is ignored: the Super Exploder is retired, the pool offers the normal Exploder");
        gEnhancedExploderRuntimeStats = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "EnhancedExploderRuntimeStats", 0, gIniPath) != 0;
        // Read by the streaming policy in dllmain.cpp to keep sup_expl resident.
        gSuperExploderWanted = gEnhancedExploders;
        gEnhancedExploderPercent = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "EnhancedExploderPercent", 50, gIniPath);
        const int requestedStrength = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "EnhancedExploderStrength", 150, gIniPath);
        gEnhancedExploderStrength = 150;
        if (gEnhancedExploderPercent < 0)
            gEnhancedExploderPercent = 0;
        if (gEnhancedExploderPercent > 100)
            gEnhancedExploderPercent = 100;
        if (requestedStrength != gEnhancedExploderStrength)
            Log("enhanced exploders: strength %d%% ignored; the authored "
                "sup_expl entity is fixed at %d%%", requestedStrength,
                gEnhancedExploderStrength);
        // EnhancedExploderScale is gone. +0x70 is not the creature's transform, and
        // the old code kept raw object pointers and wrote 48 bytes into them once a
        // second, long after the objects could be freed (ten access violation dumps
        // in one evening). The model now comes from a separate authored spawn
        // target; per-template model probes could not mix two looks in one fight.
        gExploderModelProbe = 0;
    }

    void InstallExploderHooks()
    {
        if (!gEnhancedExploders)
            return;
        Log("enhanced exploders: authored sup_expl spawn target enabled "
            "(%d%% of Exploder spawns, authored strength %d%%)",
            gEnhancedExploderPercent, gEnhancedExploderStrength);
    }
