    // EnemyRandomizerNeverUnload (experimental, default off;
    // analysis/DS1_NeverUnload_Research_2026-10-25.md,
    // analysis/DS1_OwnPoolCopies_Research_2026-10-25.md).
    //
    // The pool loads its own copies (ds1_randomizer_assets\pool, keys 0xD513xxxx),
    // streams no level zone set knows. In this mode the character copies keep
    // their persistent bit through a world teardown, so a death or chapter change
    // no longer reloads them. Sound banks cannot stay: the world change empties the
    // bank table without releasing a single holder (55 -> 1 banks at a death, 40
    // holders still "alive"), and a resident stream never creates its banks again.
    // So every archive that carries a bank (the voice copies and each character's
    // split-off <name>_sbk) loses the bit here, goes with the world and is
    // requested again as soon as the new world runs.
    //
    // Resource registry diagnostics (retail): 0x006E4730 releases a group's
    // records of one category (eax = group), 0x006E477E is reached with a record at
    // reference 0 (edi), 0x006A35E0 releases a bank (esi = holder).
    // Record +0 type node (+8 type hash), +0x16 group links, +0x18 GUID.
    // Groups are keyed [stream+0x18].
    constexpr uintptr_t kResourceReleaseCategory = 0x006E4730;   // eax = group, push category; ret
    constexpr uintptr_t kResourceDestroyRecord = 0x006E477E;     // edi = record at reference 0, before Destroy
    constexpr uintptr_t kSoundBankRelease = 0x006A35E0;          // eax = registry, edx = bank, esi = holder
    constexpr uint32_t kSbkTypeHash = 0x39030F7C;

    bool gNeverUnload = false;
    safetyhook::MidHook gReleaseCategoryHook, gDestroyRecordHook, gBankReleaseHook;
    uintptr_t gReleasingGroup = 0;
    uint32_t gPoolGroupKeys[kMaxPersistentStreams]{};
    unsigned gPoolGroupKeyCount = 0;
    volatile LONG gAudioRecoverNow = 0;   // set at a kept teardown: the voice archives are requested at once
    volatile LONG gSbkDestroys = 0, gSbkDestroysPool = 0, gBankReleases = 0, gTeardownsKept = 0,
        gBankArchivesReleased = 0;

    bool PoolGroupKey(uint32_t key)
    {
        for (unsigned i = 0; i < gPoolGroupKeyCount; ++i)
            if (gPoolGroupKeys[i] == key)
                return true;
        return false;
    }

    // An archive whose sound bank has to be read again in every world: a voice copy or a character's bank.
    bool PoolStreamCarriesBank(void* manager, uintptr_t object)
    {
        for (unsigned k = 0; k < gGlobalAudioHandleCount; ++k)
            if (gGlobalAudioHandles[k] && StreamObjectFor(manager, gGlobalAudioHandles[k]) == object)
                return true;
        return false;
    }

    void ReleaseCategory_Hook(safetyhook::Context& ctx)
    {
        gReleasingGroup = ctx.eax;
    }

    // A sound bank record whose references reached 0: which group let it go (a pool group means a bank archive of
    // ours went with the world, as it should).
    void DestroyRecord_Hook(safetyhook::Context& ctx)
    {
        __try
        {
            const uintptr_t record = ctx.edi;
            const uintptr_t type = *reinterpret_cast<const uintptr_t*>(record);
            if (*reinterpret_cast<const uint32_t*>(type + 8) != kSbkTypeHash)
                return;
            const uint32_t key = gReleasingGroup ? *reinterpret_cast<const uint32_t*>(gReleasingGroup) : 0;
            const bool pool = PoolGroupKey(key);
            const LONG n = InterlockedIncrement(&gSbkDestroys);
            if (pool)
                InterlockedIncrement(&gSbkDestroysPool);
            if (n <= 32)
            {
                const uint32_t* guid = reinterpret_cast<const uint32_t*>(record + 0x18);
                Log("randomizer never unload: sound bank record %08X destroyed by group key %08X (%s), guid "
                    "%08X-%08X-%08X-%08X, links %u", static_cast<unsigned>(record), key,
                    pool ? "a pool stream" : "not the pool", guid[0], guid[1], guid[2], guid[3],
                    *reinterpret_cast<const uint16_t*>(record + 0x16));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void BankRelease_Hook(safetyhook::Context& ctx)
    {
        const uintptr_t from = *reinterpret_cast<const uintptr_t*>(ctx.esp);
        if (InterlockedIncrement(&gBankReleases) <= 32)
            Log("randomizer never unload: bank %08X released (holder %08X, from %08X: %s)",
                static_cast<unsigned>(ctx.edx), static_cast<unsigned>(ctx.esi), static_cast<unsigned>(from),
                from == 0x00773A2C ? "resource destroy" : from == 0x00773AE8 ? "holder callback" : "other path");
    }

    void InstallNeverUnload()
    {
        gNeverUnload = GetPrivateProfileIntA("DeadSpace2008Randomizer", "EnemyRandomizerNeverUnload", 0, gIniPath) != 0;
        if (!gNeverUnload)
        {
            Log("randomizer never unload off (EnemyRandomizerNeverUnload=0): the pool reloads with every world");
            return;
        }
        if (!gPersistentPool)
        {
            Log("randomizer never unload: NOT active, it needs EnemyRandomizerPersistentPool=1");
            gNeverUnload = false;
            return;
        }
        gReleaseCategoryHook = VerifiedMid(reinterpret_cast<void*>(kResourceReleaseCategory), &ReleaseCategory_Hook);
        gDestroyRecordHook = VerifiedMid(reinterpret_cast<void*>(kResourceDestroyRecord), &DestroyRecord_Hook);
        gBankReleaseHook = VerifiedMid(reinterpret_cast<void*>(kSoundBankRelease), &BankRelease_Hook);
        Log("randomizer never unload ON (experimental): the character copies stay through a world teardown, the "
            "archives with sound banks reload with every world; diagnostics: release %s, destroy %s, bank release %s",
            gReleaseCategoryHook ? "ok" : "MISSING", gDestroyRecordHook ? "ok" : "MISSING",
            gBankReleaseHook ? "ok" : "MISSING");
    }

    void LogNeverUnloadTotals()
    {
        static LONG last[5] = {-1, -1, -1, -1, -1};
        const LONG now[5] = {gTeardownsKept, gBankArchivesReleased, gSbkDestroys, gSbkDestroysPool, gBankReleases};
        if (!gNeverUnload || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("randomizer never unload totals: %ld teardown(s) kept the characters, %ld bank archive(s) sent with the "
            "world, %ld sound bank records destroyed (%ld from a pool group), %ld banks released",
            now[0], now[1], now[2], now[3], now[4]);
    }

    // Game thread, from the heartbeat while a level plays: the pool's group keys for the diagnostics.
    void NeverUnloadHeartbeat(void* manager)
    {
        if (!gNeverUnload || !manager)
            return;
        for (unsigned i = 0; i < gPersistentStreamCount; ++i)
        {
            const PersistentStream& kept = gPersistentStreams[i];
            if (StreamObjectFor(manager, kept.id) != kept.object)
                continue;
            __try
            {
                const uint32_t key = *reinterpret_cast<const uint32_t*>(kept.object + 0x18);
                if (key && !PoolGroupKey(key) && gPoolGroupKeyCount < kMaxPersistentStreams)
                    gPoolGroupKeys[gPoolGroupKeyCount++] = key;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }

    // At UnloadAll: the characters stay; every archive with a bank loses the bit, leaves the record list and goes
    // with the world. Returns how many pool streams stay.
    unsigned NeverUnloadTeardown(void* manager)
    {
        unsigned staying = 0, released = 0;
        for (unsigned i = 0; i < gPersistentStreamCount;)
        {
            const PersistentStream kept = gPersistentStreams[i];
            if (StreamObjectFor(manager, kept.id) == kept.object && PoolStreamCarriesBank(manager, kept.object))
            {
                __try
                {
                    *reinterpret_cast<uint16_t*>(kept.object + kStreamFlagsOffset) &=
                        static_cast<uint16_t>(~kStreamPersistentBit);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
                gPersistentStreams[i] = gPersistentStreams[--gPersistentStreamCount];
                ++released;
                continue;
            }
            staying += StreamObjectFor(manager, kept.id) == kept.object;
            ++i;
        }
        // their handles name streams that are gone now: the recovery requests them again
        for (unsigned k = 0; k < gGlobalAudioHandleCount; ++k)
            gGlobalAudioHandles[k] = 0;
        InterlockedIncrement(&gTeardownsKept);
        InterlockedExchangeAdd(&gBankArchivesReleased, static_cast<LONG>(released));
        InterlockedExchange(&gAudioRecoverNow, 1);
        if (gTeardownsKept <= 32)
            Log("randomizer never unload: world unload, %u character stream(s) stay resident, %u archive(s) with "
                "sound banks go with the world and come back once it runs", staying, released);
        return staying;
    }
