    // ---------------------------------------------------------------------
    // Level order: the single place every chapter transition passes through.
    //
    // 0x00401EC0 copies two names into the pending level request - the folder
    // in EAX, the archive in ECX - and it has exactly four callers:
    //
    //   0x005B67AE  LevelChange, the authored in-game transitions
    //   0x005F76B3  the general "go to level X at checkpoint Y" wrapper
    //   0x0061562D  New Game
    //   0x006156A2  New Game
    //
    // Which of those a call came from is the one thing a randomizer has to
    // know, because a save being restored must not be redirected while a real
    // transition must.  The return address answers that, so this logs it and
    // changes nothing until a run has shown which caller does what.
    //
    // The order itself needs no bookkeeping: chapter 12 stays where it is and
    // the other eleven are a seeded permutation, so every chapter is visited
    // exactly once and the finale is always last.  What the redirect needs is
    // the position of the chapter you are leaving, not a counter, which is why
    // the last granted destination is remembered.
    // ---------------------------------------------------------------------
    constexpr uintptr_t kLevelRequestNames = 0x00401EC0;
    constexpr uintptr_t kLevelCallerLevelChange = 0x005B67B3;
    constexpr uintptr_t kLevelCallerGeneric = 0x005F76B8;
    constexpr uintptr_t kLevelCallerNewGameA = 0x00615632;
    constexpr uintptr_t kLevelCallerNewGameB = 0x006156A7;

    const char* const kLevelChapters[] = {
        "ch01_flt", "ch02_med", "ch03_eng", "ch04_brg", "ch05_med", "ch06_hyd",
        "ch07_min", "ch08_brg", "ch09_mil", "ch10_crw", "ch11_flt", "ch12_col",
    };
    constexpr unsigned kLevelChapterCount =
        sizeof(kLevelChapters) / sizeof(kLevelChapters[0]);
    // Chapter 12 is the finale and never moves.
    constexpr unsigned kLevelShuffledCount = kLevelChapterCount - 1;

    safetyhook::MidHook gLevelRequestHook;
    unsigned gLevelOrder[kLevelChapterCount]{};
    bool gLevelOrderBuilt = false;
    unsigned gLevelCurrentChapter = 0;   // 1-based, 0 = unknown

    const char* LevelCallerName(uintptr_t caller)
    {
        switch (caller)
        {
        case kLevelCallerLevelChange: return "LevelChange";
        case kLevelCallerGeneric:     return "generic go-to-level";
        case kLevelCallerNewGameA:
        case kLevelCallerNewGameB:    return "New Game";
        default:                      return "unknown";
        }
    }

    // "CH04_brg\" or "ch04_brg.str" both answer 4.
    unsigned ChapterFromName(const char* text)
    {
        if (!text)
            return 0;
        for (unsigned i = 0; i < kLevelChapterCount; ++i)
            if (_strnicmp(text, kLevelChapters[i], 8) == 0)
                return i + 1;
        return 0;
    }

    void BuildLevelOrder()
    {
        if (gLevelOrderBuilt)
            return;
        gLevelOrderBuilt = true;
        for (unsigned i = 0; i < kLevelChapterCount; ++i)
            gLevelOrder[i] = i + 1;
        // Fisher-Yates over the first eleven, driven by the same stable seed
        // the enemy randomizer uses, so a run is reproducible from the seed
        // alone and a reload cannot reshuffle it.
        for (unsigned i = kLevelShuffledCount; i > 1; --i)
        {
            Guid128 left{};
            left.value[0] = gEnemyRandomizerSeed;
            left.value[1] = i;
            Guid128 right{};
            const uint32_t roll = StableRandomizerHash(left, right, 0x4C564C4Fu);
            const unsigned j = roll % i;
            const unsigned keep = gLevelOrder[i - 1];
            gLevelOrder[i - 1] = gLevelOrder[j];
            gLevelOrder[j] = keep;
        }
        char text[160]{};
        for (unsigned i = 0; i < kLevelChapterCount; ++i)
        {
            char part[16]{};
            std::snprintf(part, sizeof(part), "%s%u", i ? " -> " : "",
                          gLevelOrder[i]);
            std::strncat(text, part, sizeof(text) - std::strlen(text) - 1);
        }
        Log("level order: %s", text);
    }

    unsigned LevelOrderPosition(unsigned chapter)
    {
        for (unsigned i = 0; i < kLevelChapterCount; ++i)
            if (gLevelOrder[i] == chapter)
                return i;
        return kLevelChapterCount;
    }

    void LevelRequest_Hook(safetyhook::Context& ctx)
    {
        if (!gLevelOrderDiagnostics && !gLevelOrderRandomizer)
            return;
        __try
        {
            const char* folder = reinterpret_cast<const char*>(ctx.eax);
            const char* archive = reinterpret_cast<const char*>(ctx.ecx);
            const uintptr_t caller =
                *reinterpret_cast<const uintptr_t*>(ctx.esp);
            const unsigned chapter = ChapterFromName(folder);
            Log("level request: folder='%s' archive='%s' from %s (%08X), "
                "chapter %u, current %u",
                folder ? folder : "(null)", archive ? archive : "(null)",
                LevelCallerName(caller), static_cast<unsigned>(caller),
                chapter, gLevelCurrentChapter);
            if (chapter)
                gLevelCurrentChapter = chapter;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("level request: unreadable names (eax=%08X ecx=%08X)",
                static_cast<unsigned>(ctx.eax), static_cast<unsigned>(ctx.ecx));
        }
    }

    void InstallLevelOrderHook()
    {
        if (!gLevelOrderDiagnostics && !gLevelOrderRandomizer)
            return;
        BuildLevelOrder();
        gLevelRequestHook = safetyhook::create_mid(
            reinterpret_cast<void*>(kLevelRequestNames), &LevelRequest_Hook);
        Log("level order: request watch %s at %08X (randomizer %s)",
            gLevelRequestHook ? "active" : "NOT installed",
            static_cast<unsigned>(kLevelRequestNames),
            gLevelOrderRandomizer ? "armed" : "observing only");
    }
