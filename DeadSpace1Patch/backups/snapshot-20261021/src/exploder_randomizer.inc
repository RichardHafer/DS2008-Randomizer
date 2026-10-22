    // =====================================================================
    // Enhanced exploders.
    //
    // Exploder (class hash 0x73553751, decoder at 0x004CAE70, dispatch table
    // 0x004CB100) is the smallest creature class in the game: five floats,
    // and every one of the 59 exploders in the game authors the same values.
    //
    //   property 0 -> +0xA68   20
    //   property 1 -> +0xA6C    3
    //   property 2 -> +0xA48    3
    //   property 3 -> +0xA54    3
    //   property 4 -> +0xA60    3
    //
    // The last three sit twelve bytes apart and 0x004CAF22 writes that region
    // through an index, so they are one small array; the pair at +0xA68 is
    // separate.  Which one is blast radius and which is damage could not be
    // settled by reading the executable, so this scales all five by the same
    // factor rather than pretending to know.  That is deliberately blunt: a
    // uniform scale cannot turn the creature into something incoherent.
    //
    // Unlike every other creature, the exploder has no second authored
    // tuning to copy - no super_exploder exists - so these numbers are ours,
    // not the game's.  The factor is therefore kept modest.
    //
    // Hook site.  The decoder takes `this` in EAX, not ECX
    // ("mov edi, eax" at 0x004CAE73), so an ordinary __fastcall inline hook
    // would read the wrong register and quietly corrupt an unrelated object.
    // Instead this sits on the epilogue at 0x004CB0E1, where EDI still holds
    // the object and the five bytes of "mov eax, edi / pop edi / pop esi /
    // pop ebp" are a clean patch site.
    // =====================================================================
    constexpr uintptr_t kExploderDecodeEpilogue = 0x004CB0E1;
    constexpr unsigned kExploderStats[] = {0xA48, 0xA54, 0xA60, 0xA68, 0xA6C};
    constexpr unsigned kExploderStatCount =
        sizeof(kExploderStats) / sizeof(kExploderStats[0]);
    // Entity ids live at +0x20; the crate randomizer measured that in game.
    constexpr unsigned kEntityId = 0x20;
    // An entity carries a 4x4 transform: three rows of four floats from
    // +0x70, then the position at +0xB0.  Read out of the spawner dumps in
    // the patch log, where the rows came out as (0,0,-1,0), (0,1,0,0),
    // (1,0,0,0) - a plain rotation - with the position beside them.
    // Scaling the rotation part is the only way to make one exploder look
    // different from another without a second character asset, which the
    // game does not have for this creature.

    // The character asset a creature uses is named in plain text, and the
    // entity keeps a pointer to that text at +0x668.  Class 0x71BC20FF
    // property 36 writes it, and its case at 0x0047580C calls 0x0073A9D0,
    // which only checks the attribute is of the string type (0x25E3) and
    // hands back the pointer - there is no hash and no lookup table in
    // between, so the field really is "which character am I".
    // Measured values: an Exploder says "exploder", a Leaper "leaper", and
    // every Zombie says "slasher" regardless of its variant.
    //
    // Whether pointing this at a different name actually swaps the model is
    // the open question - the code that consumes the pointer has not been
    // found, and the two functions it is passed to turned out to do
    // something else entirely.  So this is an experiment, not a feature:
    // point an exploder at another creature that the run already streams
    // and look at the screen.  If the model changes, the same lever will
    // reach a character asset of our own; if nothing happens, the name is
    // resolved somewhere earlier and this approach is dead.
    //
    // The strings are ours and static, so the pointer stays valid for the
    // life of the process - the authored text lives in streamed archive
    // memory and would not.
    constexpr unsigned kEntityModelName = 0x668;

    // Where the model name actually exists.
    //
    // An entity's records decode one after another into the same object, and
    // the Exploder record comes first: at its epilogue the object has its
    // five floats and nothing else - the transform at +0x70 is all zeroes and
    // the model name at +0x668 is still null.  Measured, after two failed
    // attempts that both assumed otherwise.
    //
    // Class 0x71BC20FF is decoded later and owns the model name.  Its loop
    // sits at 0x004754A4 and leaves at 0x00475B46; by 0x00475B8F the record
    // is fully decoded and EDI still holds the object, which is the first
    // moment the name can be read or replaced.  That site fires for every
    // creature, so the name itself is the filter: only an object that reads
    // "exploder" is one of ours.
    // Where the name is decided.
    //
    // Writing the field after the whole record had decoded changed nothing on
    // screen, so this goes one step earlier: 0x00475807 resolves the authored
    // string and 0x0047580C stores it.  Replacing EAX between the two puts our
    // name in exactly where the authored one would have gone, which is as
    // close to authored as a runtime patch can get - everything the rest of
    // the decode and the finaliser at 0x00475B96 does then sees our value.
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
    // "sup_expl" is ours - the second exploder asset in
    // ds1_randomizer_assets, registered in the character stream manifest so
    // the streaming policy keeps it resident.  The others are retail names,
    // kept for comparison: if a retail one does nothing but ours works, the
    // difference is residency rather than the field.
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
    // The per-spawn share and its running count moved to the variant table in
    // variant_runtime.inc, which does the same thing for any creature.  Only
    // the two settings stay here, because an existing INI names them.

    // Defined below; the poll needs it before the definition is reached.
    void ApplyExploderModelProbe(uintptr_t object);



    // Runs once a creature's model name exists.  Returns true when the
    // object is an exploder this run has chosen to enhance.
    // Every exploder in an area is an instance of ONE authored entity -
    // filtration_z01 holds all 59 of the game's exploders behind a single
    // template - so a decision taken here covers all of them, and the model
    // name in particular cannot differ per creature: it is resolved from the
    // authored record.  Measured: the name pass fires twice in a run, once
    // per template, not once per creature.
    //
    // A coin flip per template therefore does not give "every second
    // exploder"; with two templates it gave two heads and every exploder in
    // the game turned super.  So the templates are alternated instead - the
    // first is enhanced, the second is not, and so on.  At fifty per cent
    // that is exactly every second one, and it is a guarantee rather than an
    // average.  The seed still decides which of the two goes first.
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

    // EAX holds the authored name at this point and EDI the object.  Only an
    // exploder that this run has chosen is redirected, and only the name is
    // touched - the store itself still happens, with our pointer.
    void ModelNameStore_Hook(safetyhook::Context& ctx)
    {
        if (!gEnhancedExploders || gExploderModelProbe <= 0 ||
            gExploderModelProbe > static_cast<int>(kModelNameProbeCount))
            return;
        // Renaming a creature after an archive that is not loaded would leave
        // it with no model at all, which is worse than leaving it alone.  Our
        // own asset has to be resident first; the retail names in the list are
        // only there for comparison and are streamed by the level anyway.
        if (gExploderModelProbe == 1 && !gSuperExploderResident)
        {
            // Not an error on its own: the archive is still loading when the
            // pin first looks at it.  The residency watch keeps the flag up to
            // date, so a later exploder gets its name after all - which is why
            // this only reports, and only a few times.
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
            // Only touch it when it still reads as the authored name, so a
            // pointer that has already been swapped or was never a string
            // is left alone.
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
        // Off by default, and it should stay off.  The authored sup_expl
        // donor already carries the stronger numbers, and the spawn path
        // swaps it in per spawn - every second exploder, exactly.  This pass
        // works per authored template instead, so with a single template
        // behind all 59 exploders of a level it hits either all of them or
        // none, which is what made every exploder in the game come out super.
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
        // The same alternation the name pass uses.  This used to carry its own
        // coin flip, which is why both templates came up enhanced and every
        // exploder in the game turned super.
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
        gEnhancedExploders = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnhancedExploders", 0, gIniPath) != 0;
        gEnhancedExploderRuntimeStats = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnhancedExploderRuntimeStats", 0, gIniPath) != 0;
        // Tells the streaming policy, declared far above this file, to keep
        // our second exploder archive resident.
        gSuperExploderWanted = gEnhancedExploders;
        gEnhancedExploderPercent = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnhancedExploderPercent", 50, gIniPath);
        const int requestedStrength = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnhancedExploderStrength", 150, gIniPath);
        gEnhancedExploderStrength = 150;
        if (gEnhancedExploderPercent < 0)
            gEnhancedExploderPercent = 0;
        if (gEnhancedExploderPercent > 100)
            gEnhancedExploderPercent = 100;
        if (requestedStrength != gEnhancedExploderStrength)
            Log("enhanced exploders: strength %d%% ignored; the authored "
                "sup_expl entity is fixed at %d%%", requestedStrength,
                gEnhancedExploderStrength);
        // EnhancedExploderScale is gone.  It never worked - +0x70 is not the
        // creature's transform, the rows read as zeroes every time - and the
        // way it waited for them was unsafe: it kept raw object pointers in a
        // list and wrote forty-eight bytes into them once a second, long
        // after the object could have been freed and its memory reused.  Ten
        // crash dumps in one evening, every one an access violation at a
        // different address inside the game and the last of them in a
        // reference-count decrement, are exactly what that looks like.
        // The model now comes from a separate authored spawn target. Runtime
        // model-pointer probes selected whole templates and could never mix
        // two looks inside one encounter.
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
