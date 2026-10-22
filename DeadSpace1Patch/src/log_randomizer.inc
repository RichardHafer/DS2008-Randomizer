    // =====================================================================
    // Log randomizer ([DeadSpace1Patch] LogRandomizer).
    //
    // Every log is a LogEntry. global_assets holds the 191 definitions; a
    // world pickup names the one it grants in PickupItem[17] (+0x344), and
    // LogEntry::Collect (0x0056F530) files the resolved definition into the
    // chapter list chosen by the pickup's own chapter (+0x65C) and type
    // (+0x664). So a pickup takes its new definition's chapter, index (+0x660),
    // type and text hashes with it (texts resolve later from +0x650..+0x658,
    // 0x0056F350). Its done message (+0xEFC) and pickup message stay with the
    // location; no pooled location has a gameplay listener on them.
    //
    // The pool is the 43 audio and text logs that have a world pickup, a
    // global stream and no scripted delivery (analysis/
    // DeadSpace1_Logs_Randomizer_Research_2026-10-21.md). The seed maps each
    // definition to another of its type, and everything that granted X grants
    // the mapped one instead: world pickups (decoder 0x0056EDA0), EquipCommand
    // restores (decoder 0x00508A20) and the one crate (ch03 decon, 3.5).
    // =====================================================================
    struct LogDefinition
    {
        Guid128 id;
        uint32_t type;      // 2 audio, 4 text
        uint32_t chapter;
        uint32_t index;
        uint32_t title, body, speaker;
        Guid128 stream;
        Guid128 images[3];   // LogEntry[13..15]
    };
    constexpr LogDefinition kLogDefinitions[] = {
#include "log_pool_generated.inc"
    };
    constexpr unsigned kLogDefinitionCount = sizeof(kLogDefinitions) / sizeof(kLogDefinitions[0]);

    struct LogWorldPickup
    {
        Guid128 world;
        Guid128 definition;
    };
    constexpr LogWorldPickup kLogWorldPickups[] = {
#include "log_world_generated.inc"
    };

    constexpr uintptr_t kLogEntryDecode = 0x0056EDA0;
    constexpr uintptr_t kLogEntryCollect = 0x0056F530;   // thiscall(self, user, arg), ret 8, al = filed
    constexpr uintptr_t kEquipCommandDecode = 0x00508A20;
    constexpr uintptr_t kLogEntryVtable = 0x00C73698;
    constexpr unsigned kLogGrant = 0x344;
    constexpr unsigned kLogTexts = 0x640;        // three resolved pointers
    constexpr unsigned kLogTextHashes = 0x650;   // title, body, speaker
    constexpr unsigned kLogChapter = 0x65C;
    constexpr unsigned kLogIndex = 0x660;
    constexpr unsigned kLogType = 0x664;
    constexpr unsigned kLogStream = 0xF04;
    constexpr unsigned kLogImages = 0x66C;       // LogEntry[13..15], 0x10 apart (decoder 0x0056F0F9..)
    constexpr unsigned kEquipItems = 0x64;       // ten GUIDs, 0x14 apart
    constexpr unsigned kEquipItemCount = 10;
    constexpr unsigned kEquipItemStride = 0x14;
    uintptr_t TargetKeyFor(const Guid128& target);   // dllmain.cpp

    bool gLogRandomizer = false;
    bool gLogRandomizerSameChapter = false;
    unsigned gLogMap[kLogDefinitionCount]{};
    safetyhook::InlineHook gLogEntryDecodeHook, gLogCollectHook, gEquipCommandDecodeHook;
    volatile LONG gLogPickupsRemapped = 0, gLogDefinitionsSkipped = 0, gLogNoIdentity = 0, gLogNotPooled = 0,
                  gLogByGrant = 0, gLogEquipsRemapped = 0, gLogCratesRemapped = 0, gLogCollects = 0,
                  gLogCollectsRefused = 0, gLogFaults = 0;

    int LogDefinitionIndex(const Guid128& id)
    {
        for (unsigned i = 0; i < kLogDefinitionCount; ++i)
            if (SameGuid(kLogDefinitions[i].id, id))
                return static_cast<int>(i);
        return -1;
    }

    int LogWorldIndex(const Guid128& id)
    {
        for (const LogWorldPickup& pickup : kLogWorldPickups)
            if (SameGuid(pickup.world, id))
                return LogDefinitionIndex(pickup.definition);
        return -1;
    }

    const char* LogKind(uint32_t type)
    {
        return type == 2 ? "audio" : type == 4 ? "text" : "?";
    }

    // One permutation per type (and per chapter with LogRandomizerSameChapter),
    // from the run seed, so a reload maps every log the same way.
    void BuildLogMap()
    {
        for (unsigned i = 0; i < kLogDefinitionCount; ++i)
            gLogMap[i] = i;
        uint32_t state = gStartingSuitSeed ^ 0x4C4F4753u;
        auto next = [&state]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        bool grouped[kLogDefinitionCount]{};
        for (unsigned first = 0; first < kLogDefinitionCount; ++first)
        {
            if (grouped[first])
                continue;
            unsigned members[kLogDefinitionCount]{};
            unsigned count = 0;
            for (unsigned i = first; i < kLogDefinitionCount; ++i)
                if (!grouped[i] && kLogDefinitions[i].type == kLogDefinitions[first].type &&
                    (!gLogRandomizerSameChapter || kLogDefinitions[i].chapter == kLogDefinitions[first].chapter))
                {
                    grouped[i] = true;
                    members[count++] = i;
                }
            unsigned shuffled[kLogDefinitionCount]{};
            std::memcpy(shuffled, members, sizeof(unsigned) * count);
            for (unsigned i = count; i > 1; --i)
            {
                const unsigned j = next() % i;
                const unsigned swap = shuffled[i - 1];
                shuffled[i - 1] = shuffled[j];
                shuffled[j] = swap;
            }
            for (unsigned i = 0; i < count; ++i)
                gLogMap[members[i]] = shuffled[i];
        }
        unsigned moved = 0;
        for (unsigned i = 0; i < kLogDefinitionCount; ++i)
            moved += gLogMap[i] != i;
        Log("log randomizer: seed %08X, %u logs (%s), %u moved", gStartingSuitSeed, kLogDefinitionCount,
            gLogRandomizerSameChapter ? "within their chapter" : "across chapters", moved);
        for (unsigned i = 0; i < kLogDefinitionCount; ++i)
        {
            const LogDefinition& from = kLogDefinitions[i];
            const LogDefinition& to = kLogDefinitions[gLogMap[i]];
            Log("  %s log %u.%u -> %u.%u", LogKind(from.type), from.chapter, from.index, to.chapter, to.index);
        }
    }

    // The resolved text pointers of the definition, when it is resident;
    // otherwise the hashes alone, which the text message resolves later.
    bool DefinitionTexts(const Guid128& id, uint32_t texts[3])
    {
        const uintptr_t key = TargetKeyFor(id);
        uintptr_t instance = 0, vtable = 0;
        if (!key ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(key + 0x14), &instance, sizeof(instance),
                               nullptr) ||
            !instance ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(instance), &vtable, sizeof(vtable),
                               nullptr) ||
            vtable != kLogEntryVtable)
            return false;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(instance + kLogTexts), texts,
                                 3 * sizeof(uint32_t), nullptr) &&
               texts[0] && texts[1];
    }

    template <typename T>
    bool PutLogField(uintptr_t object, unsigned offset, const T& value)
    {
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + offset), &value, sizeof(value),
                                  nullptr) != FALSE;
    }

    void ApplyLogEntry(void* self)
    {
        if (!gLogRandomizer || !self)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        uintptr_t vtable = 0;
        Guid128 own{}, grant{};
        if (!ReadProcessMemory(GetCurrentProcess(), self, &vtable, sizeof(vtable), nullptr) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + 0x20), &own, sizeof(own),
                               nullptr) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kLogGrant), &grant,
                               sizeof(grant), nullptr))
        {
            InterlockedIncrement(&gLogFaults);
            return;
        }
        if (vtable != kLogEntryVtable)
            return;
        if (!(own.value[0] | own.value[1] | own.value[2] | own.value[3]))
        {
            // Without its id a definition cannot be told from a pickup, and
            // rewriting a definition would move it in every chapter list.
            if (InterlockedIncrement(&gLogNoIdentity) <= 4)
                Log("log %08X: no entity id at +0x20 yet, left alone", static_cast<unsigned>(object));
            return;
        }
        if (SameGuid(own, grant) || LogDefinitionIndex(own) >= 0)
        {
            InterlockedIncrement(&gLogDefinitionsSkipped);
            return;
        }
        int from = LogWorldIndex(own);
        const char* how = "its own id";
        if (from < 0)
        {
            from = LogWorldIndex(grant);   // a chain through a chapter-local copy
            if (from < 0)
                from = LogDefinitionIndex(grant);
            how = "what it grants";
            if (from >= 0)
                InterlockedIncrement(&gLogByGrant);
        }
        if (from < 0)
        {
            InterlockedIncrement(&gLogNotPooled);
            return;
        }
        const LogDefinition& source = kLogDefinitions[from];
        const LogDefinition& target = kLogDefinitions[gLogMap[from]];
        const uint32_t hashes[3] = {target.title, target.body, target.speaker};
        uint32_t texts[3]{};
        const bool resolved = DefinitionTexts(target.id, texts);
        bool ok = PutLogField(object, kLogGrant, target.id) && PutLogField(object, kLogChapter, target.chapter) &&
                  PutLogField(object, kLogIndex, target.index) && PutLogField(object, kLogType, target.type) &&
                  PutLogField(object, kLogTextHashes, hashes) && PutLogField(object, kLogStream, target.stream) &&
                  PutLogField(object, kLogImages, target.images);
        if (ok && resolved)
            ok = PutLogField(object, kLogTexts, texts);
        if (!ok)
        {
            InterlockedIncrement(&gLogFaults);
            Log("log %08X (%08X-%08X): write failed", static_cast<unsigned>(object), own.value[0], own.value[1]);
            return;
        }
        if (InterlockedIncrement(&gLogPickupsRemapped) <= 64)
            Log("log %08X (%08X-%08X, found by %s): %s log %u.%u now gives %u.%u (%08X-%08X), texts %s",
                static_cast<unsigned>(object), own.value[0], own.value[1], how, LogKind(source.type), source.chapter,
                source.index, target.chapter, target.index, target.id.value[0], target.id.value[1],
                resolved ? "copied" : "from the hashes");
    }

    void __fastcall LogEntryDecode_Hook(void* self, void*, void* attributes)
    {
        gLogEntryDecodeHook.thiscall<void>(self, attributes);
        ApplyLogEntry(self);
    }

    // SkipTutorialTips: a tutorial popup is a tip LogEntry (type 8) an
    // EquipCommand (training_tip_*, and the checkpoint restores) hands out.
    // Those slots are emptied, GUID and count, which is what an unused slot
    // holds. Tip 8 stays: its done message hands out audio log 1.10.
    constexpr Guid128 kTutorialTips[] = {
#include "tutorial_tips_generated.inc"
    };
    bool gSkipTutorialTips = false;
    volatile LONG gTutorialTipsSkipped = 0;

    void SkipTutorialTips(uintptr_t object)
    {
        for (unsigned slot = 0; slot < kEquipItemCount; ++slot)
        {
            const uintptr_t at = object + kEquipItems + slot * kEquipItemStride;
            Guid128 item{};
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), &item, sizeof(item), nullptr))
                return;
            bool tip = false;
            for (const Guid128& candidate : kTutorialTips)
                tip = tip || SameGuid(candidate, item);
            if (!tip)
                continue;
            const unsigned char empty[kEquipItemStride]{};
            if (WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), empty, sizeof(empty), nullptr) &&
                InterlockedIncrement(&gTutorialTipsSkipped) <= 24)
                Log("tutorial tip %08X-%08X taken out of grant %08X", item.value[0], item.value[1],
                    static_cast<unsigned>(object));
        }
    }

    // Logs restored by a checkpoint or a "_silent" grant follow the location.
    void __fastcall EquipCommandDecode_Hook(void* self, void*, void* attributes)
    {
        gEquipCommandDecodeHook.thiscall<void>(self, attributes);
        if (!self)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        if (gSkipTutorialTips)
            SkipTutorialTips(object);
        if (!gLogRandomizer)
            return;
        for (unsigned slot = 0; slot < kEquipItemCount; ++slot)
        {
            const uintptr_t at = object + kEquipItems + slot * kEquipItemStride;
            Guid128 item{};
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), &item, sizeof(item), nullptr))
                return;
            int from = LogDefinitionIndex(item);
            if (from < 0)
                from = LogWorldIndex(item);
            if (from < 0 || gLogMap[from] == static_cast<unsigned>(from))
                continue;
            const LogDefinition& target = kLogDefinitions[gLogMap[from]];
            if (!PutLogField(at, 0, target.id))
            {
                InterlockedIncrement(&gLogFaults);
                continue;
            }
            if (InterlockedIncrement(&gLogEquipsRemapped) <= 32)
                Log("log grant %08X slot %u: %s log %u.%u -> %u.%u", static_cast<unsigned>(object), slot,
                    LogKind(target.type), kLogDefinitions[from].chapter, kLogDefinitions[from].index, target.chapter,
                    target.index);
        }
    }

    // StandardContainer +0x2E4, called from the crate decoder hook.
    void ApplyLogCrate(void* self, unsigned itemOffset)
    {
        if (!gLogRandomizer || !self)
            return;
        const uintptr_t at = reinterpret_cast<uintptr_t>(self) + itemOffset;
        Guid128 item{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), &item, sizeof(item), nullptr))
            return;
        const int from = LogDefinitionIndex(item);
        if (from < 0 || gLogMap[from] == static_cast<unsigned>(from))
            return;
        const LogDefinition& target = kLogDefinitions[gLogMap[from]];
        if (!PutLogField(at, 0, target.id))
            return;
        InterlockedIncrement(&gLogCratesRemapped);
        Log("crate %08X: %s log %u.%u -> %u.%u", static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)),
            LogKind(target.type), kLogDefinitions[from].chapter, kLogDefinitions[from].index, target.chapter,
            target.index);
    }

    // Observe only: false means the chapter list refused the slot.
    bool __fastcall LogCollect_Hook(void* self, void*, void* user, uint32_t arg)
    {
        const bool filed = gLogCollectHook.thiscall<bool>(self, user, arg);
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        Guid128 own{}, grant{};
        uint32_t chapter = 0, index = 0, type = 0;
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + 0x20), &own, sizeof(own), nullptr);
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kLogGrant), &grant, sizeof(grant),
                          nullptr);
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kLogChapter), &chapter,
                          sizeof(chapter), nullptr);
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kLogIndex), &index, sizeof(index),
                          nullptr);
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kLogType), &type, sizeof(type),
                          nullptr);
        InterlockedIncrement(filed ? &gLogCollects : &gLogCollectsRefused);
        const LONG lines = InterlockedCompareExchange(&gLogCollects, 0, 0) +
                           InterlockedCompareExchange(&gLogCollectsRefused, 0, 0);
        if (lines <= 64)
            Log("log picked up: %08X-%08X gives %08X-%08X (%s %u.%u, pooled %s): %s", own.value[0], own.value[1],
                grant.value[0], grant.value[1], LogKind(type), chapter, index,
                LogDefinitionIndex(grant) >= 0 ? "yes" : "no", filed ? "filed" : "REFUSED by the log list");
        return filed;
    }

    void LoadLogSettings()
    {
        gLogRandomizer = GetPrivateProfileIntA("DeadSpace1Patch", "LogRandomizer", 0, gIniPath) != 0;
        gLogRandomizerSameChapter =
            GetPrivateProfileIntA("DeadSpace1Patch", "LogRandomizerSameChapter", 0, gIniPath) != 0;
        gSkipTutorialTips = GetPrivateProfileIntA("DeadSpace1Patch", "SkipTutorialTips", 0, gIniPath) != 0;
        if (gLogRandomizer)
            BuildLogMap();
    }

    void InstallLogHooks()
    {
        if (gSkipTutorialTips || gLogRandomizer)
            gEquipCommandDecodeHook = VerifiedInline(reinterpret_cast<void*>(kEquipCommandDecode),
                                                     reinterpret_cast<void*>(&EquipCommandDecode_Hook));
        if (gSkipTutorialTips)
            Log("tutorial tips: %u of 21 taken out of every grant (tip 8 stays); grant decoder %s at %08X",
                static_cast<unsigned>(sizeof(kTutorialTips) / sizeof(kTutorialTips[0])),
                gEquipCommandDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kEquipCommandDecode));
        if (!gLogRandomizer)
            return;
        gLogEntryDecodeHook =
            VerifiedInline(reinterpret_cast<void*>(kLogEntryDecode), reinterpret_cast<void*>(&LogEntryDecode_Hook));
        gLogCollectHook =
            VerifiedInline(reinterpret_cast<void*>(kLogEntryCollect), reinterpret_cast<void*>(&LogCollect_Hook));
        Log("log randomizer: log decoder %s at %08X, grant decoder %s at %08X, pickup probe %s at %08X; %u world "
            "pickups known",
            gLogEntryDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kLogEntryDecode),
            gEquipCommandDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kEquipCommandDecode),
            gLogCollectHook ? "active" : "NOT installed", static_cast<unsigned>(kLogEntryCollect),
            static_cast<unsigned>(sizeof(kLogWorldPickups) / sizeof(kLogWorldPickups[0])));
    }

    void LogLogRandomizerTotals()
    {
        if (!gLogRandomizer)
            return;
        // Called every second; only a change is worth a line.
        static LONG last[10] = {-1};
        const LONG now[10] = {gLogPickupsRemapped, gLogByGrant, gLogDefinitionsSkipped, gLogNotPooled,
                              gLogNoIdentity, gLogEquipsRemapped, gLogCratesRemapped, gLogCollects,
                              gLogCollectsRefused, gLogFaults};
        if (std::memcmp(now, last, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("log randomizer totals: %ld pickups remapped (%ld found by what they grant), %ld definitions skipped, "
            "%ld not pooled, %ld without id, %ld grants and %ld crates remapped, %ld filed, %ld refused, %ld faults",
            gLogPickupsRemapped, gLogByGrant, gLogDefinitionsSkipped, gLogNotPooled, gLogNoIdentity,
            gLogEquipsRemapped, gLogCratesRemapped, gLogCollects, gLogCollectsRefused, gLogFaults);
    }
