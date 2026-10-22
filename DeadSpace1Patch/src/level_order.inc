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
    // Chapter 1 (the intro, the first weapon, stasis) always opens a run and
    // chapter 12, the finale, always ends it; chapters 2 to 11 are shuffled.
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
        // Fisher-Yates over positions 1..10 (chapters 2 to 11), driven by the
        // same stable seed the enemy randomizer uses, so a run is reproducible
        // from the seed alone and a reload cannot reshuffle it.
        for (unsigned i = kLevelShuffledCount - 1; i > 1; --i)
        {
            Guid128 left{};
            left.value[0] = gEnemyRandomizerSeed;
            left.value[1] = i;
            Guid128 right{};
            const uint32_t roll = StableRandomizerHash(left, right, 0x4C564C4Fu);
            const unsigned j = 1 + roll % i;
            const unsigned keep = gLevelOrder[i];
            gLevelOrder[i] = gLevelOrder[j];
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

    // For the randomizer's difficulty ramp and its one Hunter per chapter: how
    // many chapters this run has reached, which with the level order
    // randomizer armed is the chapter's place in the order, not its number.
    void UpdateLevelPlayPosition(unsigned chapter)
    {
        const unsigned position = gLevelOrderRandomizer && gLevelOrderBuilt
            ? LevelOrderPosition(chapter) + 1 : chapter;
        if (position > kLevelChapterCount || position == gLevelPlayPosition)
            return;
        gLevelPlayPosition = position;
        if (gEnemyRandomizerDifficultyRamp)
            Log("randomizer difficulty ramp: chapter %u is number %u of %u played, late enemies at %u/12 weight",
                chapter, position, kLevelChapterCount, position);
    }

    void LevelRequest_Hook(safetyhook::Context& ctx)
    {
        if (!gLevelOrderDiagnostics && !gLevelOrderRandomizer && !gEnemyRandomizerDifficultyRamp &&
            !gEnemyRandomizer)
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
            {
                gLevelCurrentChapter = chapter;
                UpdateLevelPlayPosition(chapter);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("level request: unreadable names (eax=%08X ecx=%08X)",
                static_cast<unsigned>(ctx.eax), static_cast<unsigned>(ctx.ecx));
        }
        WatchdogReset("level request");
    }

    // ---------------------------------------------------------------------
    // Redirect. A chapter end names the next chapter twice: a LevelChange
    // record (alone, or the base of UIEndOfLevelData, whose decoder 0x006108A0
    // runs 0x005B67C0 on the same object first) and the end-of-chapter
    // SaveKiosk. LevelChange keeps its start checkpoint as text at +0x54, the
    // folder at +0x64 and the archive at +0x74; its trigger 0x005B66F0 parses
    // the text into the checkpoint globals 0x00F48194 and passes folder and
    // archive to 0x00401EC0. SaveKiosk keeps the parsed checkpoint at +0x308,
    // the folder at +0x488 and the archive at +0x498. Strings are {data,
    // length, capacity, free} (0x006BB310). The vanilla destination D comes
    // from chapter D-1, so it is replaced in place, right after decoding, by
    // what follows D-1 in the order. Every name has the same length in every
    // chapter, so the authored buffers take the new text unchanged in size.
    //
    // Kinesis and stasis need nothing: Player's constructor sets both on
    // (+0xC68 stasis, +0xC69 kinesis, 0x00522161), and only chapter 1
    // (stasis_disable, tk_disable) and chapter 2 (turn_off_tk at its first
    // checkpoint, turn_on_tk from the module there) ever clear them. Every
    // chapter builds its own Player, so a chapter 3-11 played before chapter
    // 2 has kinesis, and chapter 2 still hands out its module whenever it comes.
    // ---------------------------------------------------------------------
    struct ChapterStart
    {
        unsigned chapter;
        const char* folder;
        const char* archive;
        const char* checkpointText;
        Guid128 checkpoint;       // where the transition starts it
        Guid128 saveCheckpoint;   // where the end-of-chapter save starts it
    };
    constexpr ChapterStart kChapterStarts[] = {
#include "chapter_starts_generated.inc"
    };
    constexpr uintptr_t kLevelChangeDecode = 0x005B67C0;
    constexpr uintptr_t kSaveKioskDecode = 0x005F5A40;
    safetyhook::InlineHook gLevelChangeDecodeHook, gSaveKioskDecodeHook;
    volatile LONG gLevelRedirects = 0, gLevelRedirectRejects = 0;

    const ChapterStart* ChapterStartFor(unsigned chapter)
    {
        for (const ChapterStart& start : kChapterStarts)
            if (start.chapter == chapter)
                return &start;
        return nullptr;
    }

    // What the run plays after `chapter`, 0 at the end.
    unsigned NextChapterAfter(unsigned chapter)
    {
        const unsigned position = LevelOrderPosition(chapter);
        return position + 1 < kLevelChapterCount ? gLevelOrder[position + 1] : 0;
    }

    struct GameString
    {
        char* data;
        uint32_t length;
        uint32_t capacity;
        void* release;
    };

    bool ReadGameString(uintptr_t at, GameString* out, char* text, size_t size)
    {
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, sizeof(*out), nullptr) ||
            !out->data || !out->length || out->length >= size ||
            !ReadProcessMemory(GetCurrentProcess(), out->data, text, out->length, nullptr))
            return false;
        text[out->length] = 0;
        return true;
    }

    bool WriteGameString(const GameString& string, const char* text)
    {
        const size_t length = std::strlen(text);
        return length == string.length &&
               WriteProcessMemory(GetCurrentProcess(), string.data, text, length + 1, nullptr) != FALSE;
    }

    // The authored destination of a chapter end, and the start the order puts there; null when nothing moves.
    const ChapterStart* RedirectFor(const char* folder, unsigned* authored)
    {
        *authored = ChapterFromName(folder);
        if (!gLevelOrderRandomizer || *authored < 2)
            return nullptr;
        const unsigned next = NextChapterAfter(*authored - 1);
        return next && next != *authored ? ChapterStartFor(next) : nullptr;
    }

    void __fastcall LevelChangeDecode_Hook(void* self, void*, void* attributes)
    {
        gLevelChangeDecodeHook.thiscall<void>(self, attributes);
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        GameString folder{}, archive{}, checkpoint{};
        char folderText[64]{}, archiveText[64]{}, checkpointText[64]{};
        if (!gLevelOrderRandomizer || !self ||
            !ReadGameString(object + 0x64, &folder, folderText, sizeof(folderText)))
            return;
        unsigned authored = 0;
        const ChapterStart* start = RedirectFor(folderText, &authored);
        if (!start)
            return;
        const bool ok = ReadGameString(object + 0x74, &archive, archiveText, sizeof(archiveText)) &&
                        ReadGameString(object + 0x54, &checkpoint, checkpointText, sizeof(checkpointText)) &&
                        folder.length == std::strlen(start->folder) &&
                        archive.length == std::strlen(start->archive) &&
                        checkpoint.length == std::strlen(start->checkpointText) &&
                        WriteGameString(folder, start->folder) && WriteGameString(archive, start->archive) &&
                        WriteGameString(checkpoint, start->checkpointText);
        InterlockedIncrement(ok ? &gLevelRedirects : &gLevelRedirectRejects);
        Log("level order: the end of chapter %u (LevelChange %08X) %s chapter %u at %s instead of %s at %s", authored - 1,
            static_cast<unsigned>(object), ok ? "now leads to" : "could NOT be pointed at", start->chapter,
            start->checkpointText, folderText, checkpointText);
    }

    void __fastcall SaveKioskDecode_Hook(void* self, void*, void* attributes)
    {
        gSaveKioskDecodeHook.thiscall<void>(self, attributes);
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        GameString folder{}, archive{};
        char folderText[64]{}, archiveText[64]{};
        if (!gLevelOrderRandomizer || !self ||
            !ReadGameString(object + 0x488, &folder, folderText, sizeof(folderText)))
            return;   // an ordinary save station names no chapter
        unsigned authored = 0;
        const ChapterStart* start = RedirectFor(folderText, &authored);
        if (!start)
            return;
        const bool ok = ReadGameString(object + 0x498, &archive, archiveText, sizeof(archiveText)) &&
                        folder.length == std::strlen(start->folder) &&
                        archive.length == std::strlen(start->archive) &&
                        WriteGameString(folder, start->folder) && WriteGameString(archive, start->archive) &&
                        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + 0x308),
                                           &start->saveCheckpoint, sizeof(start->saveCheckpoint), nullptr);
        InterlockedIncrement(ok ? &gLevelRedirects : &gLevelRedirectRejects);
        Log("level order: the end-of-chapter save of chapter %u (SaveKiosk %08X) %s chapter %u instead of %s",
            authored - 1, static_cast<unsigned>(object), ok ? "now starts" : "could NOT be pointed at",
            start->chapter, folderText);
    }

    // The store admits an item when its requirement (entry +0x1C) is at most
    // the chapter at store+0x30, and its game count (+0x20) at most +0x34
    // (0x00596B9B..0x00596BA9). That chapter is the chapter's own number
    // (store chapter 4 when chapter 4 came second, 2026-10-22), so with the
    // order shuffled the requirement is held against how many chapters were
    // played. The store gate's hook at 0x00596B9B covers the compare, so it
    // asks this last and, on an answer, resumes at the admit (0x00596BB5) or
    // the forced check (0x00596BAB) itself. Entry and store stay untouched.
    volatile LONG gStoreAdmitLines = 0;

    bool LevelOrderArmed()
    {
        return gLevelOrderRandomizer;
    }

    bool LevelOrderStoreAdmit(safetyhook::Context& ctx)
    {
        if (!gLevelOrderRandomizer || !gLevelPlayPosition)
            return false;
        const int32_t storeChapter = *reinterpret_cast<const int32_t*>(ctx.ebp + 0x30);
        const int32_t storeGames = *reinterpret_cast<const int32_t*>(ctx.ebp + 0x34);
        const int32_t required = *reinterpret_cast<const int32_t*>(ctx.edi + 0x1C);
        const int32_t games = *reinterpret_cast<const int32_t*>(ctx.edi + 0x20);
        if (storeChapter <= 0 || required <= 0)
            return false;
        const bool admitted = required <= static_cast<int32_t>(gLevelPlayPosition) && games <= storeGames;
        ctx.eip = admitted ? 0x00596BB5u : 0x00596BABu;
        if (InterlockedIncrement(&gStoreAdmitLines) <= 40)
            Log("level order: store item %08X-%08X needs chapter %d; chapter %d is number %u played, so %s",
                *reinterpret_cast<const uint32_t*>(ctx.edi), *reinterpret_cast<const uint32_t*>(ctx.edi + 4),
                required, storeChapter, gLevelPlayPosition, admitted ? "admitted" : "not yet");
        return true;
    }

    void InstallLevelOrderHook()
    {
        if (!gLevelOrderDiagnostics && !gLevelOrderRandomizer && !gEnemyRandomizerDifficultyRamp &&
            !gEnemyRandomizer)
            return;
        BuildLevelOrder();
        gLevelRequestHook = VerifiedMid(
            reinterpret_cast<void*>(kLevelRequestNames), &LevelRequest_Hook);
        Log("level order: request watch %s at %08X (randomizer %s)",
            gLevelRequestHook ? "active" : "NOT installed",
            static_cast<unsigned>(kLevelRequestNames),
            gLevelOrderRandomizer ? "armed" : "observing only");
        if (!gLevelOrderRandomizer)
            return;
        gLevelChangeDecodeHook =
            VerifiedInline(reinterpret_cast<void*>(kLevelChangeDecode), reinterpret_cast<void*>(&LevelChangeDecode_Hook));
        gSaveKioskDecodeHook =
            VerifiedInline(reinterpret_cast<void*>(kSaveKioskDecode), reinterpret_cast<void*>(&SaveKioskDecode_Hook));
        Log("level order: the store admits by chapters played through the store gate hook (%s)",
            gStoreGateHook ? "active" : "NOT installed");
        Log("level order: chapter ends redirected through LevelChange %s at %08X and SaveKiosk %s at %08X; "
            "%u chapter starts known",
            gLevelChangeDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kLevelChangeDecode),
            gSaveKioskDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kSaveKioskDecode),
            static_cast<unsigned>(sizeof(kChapterStarts) / sizeof(kChapterStarts[0])));
    }
