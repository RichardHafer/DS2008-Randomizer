    // Scene gates and vent anchors. The tables come from tools/ds1_scene_gates.py.
    //
    // A ScriptedSequence that drives a spawned actor often gates the level with its completion message: ch01
    // se_1_15 removes the invisible door blocker (ld_gondola_gate_mm) only through remove_door_blocker_se_1_15,
    // the completion of ss_01_se_1_15_zombie. If a recast donor cannot play the scene, the blocker stays and the
    // actor waits behind it.
    // - EnemyRandomizerKeepSceneActors: a spawner whose actor plays a scene clip (se_*) or carries a scene bank
    //   keeps its authored enemy.
    // - SceneGateFallback: after a recast, every completion of the actor's sequence chain that does not arrive
    //   within SceneGateFallbackSeconds is sent once by the plugin.
    // - EnemyRandomizerSourceVentAnchor: a replacement climbing out of a vent uses the source's VentMarker. A donor
    //   names its own, which exists in the donor's chapter (ch03 decon: the arena Zombie left through the arena).
#include "scene_gates_generated.inc"

    bool gKeepSceneActors = true;
    bool gSceneGateFallback = true;
    unsigned gSceneGateTimeoutMs = 30000;
    bool gSourceVentAnchor = true;
    bool gSceneGateListening = false;   // the SendMsg hook is in (message_probe.inc)

    constexpr unsigned kMaxSceneGateRuns = 16;
    constexpr unsigned kMaxSceneGateSteps = 8;
    constexpr unsigned kSceneGateLifetimeMs = 600000;

    struct SceneGateRun
    {
        Guid128 source;
        unsigned first;          // index into kSceneGateSteps
        unsigned count;
        uint32_t armed, done, fired;
        volatile LONG heard;     // set by the SendMsg observer
        unsigned long long armedAt[kMaxSceneGateSteps];
        unsigned long long startedAt;
        uintptr_t actor;
    };
    SceneGateRun gSceneGateRuns[kMaxSceneGateRuns]{};
    volatile LONG gSceneGateRunCount = 0;
    LONG gSceneGateArmed = 0, gSceneGateCompleted = 0, gSceneGateFired = 0, gSceneGateFull = 0, gSceneGateNoChain = 0,
         gSceneGateLines = 0, gSceneClipKept = 0;
    LONG gVentAnchorApplied = 0, gVentAnchorSame = 0, gVentAnchorUnknown = 0, gVentAnchorUnused = 0,
         gVentAnchorLines = 0;
    // Armed around one retail spawn call; consumed by the first vent arrival decoded in it.
    bool gVentAnchorPending = false;
    Guid128 gVentAnchorGuid{};
    Guid128 gVentAnchorSpawner{};

    bool SceneLine()
    {
        return InterlockedIncrement(&gSceneGateLines) <= 120;
    }

    const SceneClipSource* SceneClipSourceFor(const Guid128& source)
    {
        for (unsigned i = 0; i < kSceneClipSourceCount; ++i)
            if (SameGuid(kSceneClipSources[i].source, source))
                return &kSceneClipSources[i];
        return nullptr;
    }

    // True when the authored actor must stay; logs why (bounded by the scripted line budget).
    bool SceneClipKeepsAuthored(const Guid128& source)
    {
        if (!gKeepSceneActors)
            return false;
        const SceneClipSource* clip = SceneClipSourceFor(source);
        if (!clip)
            return false;
        InterlockedIncrement(&gSceneClipKept);
        if (SceneLine())
            Log("scene gate: %s plays %s, which a donor cannot; the authored enemy stays", clip->name, clip->clip);
        return true;
    }

    void LoadSceneGateSettings()
    {
        gKeepSceneActors =
            GetPrivateProfileIntA("DeadSpace2008Randomizer", "EnemyRandomizerKeepSceneActors", 1, gIniPath) != 0;
        gSceneGateFallback = GetPrivateProfileIntA("DeadSpace2008Randomizer", "SceneGateFallback", 1, gIniPath) != 0;
        int seconds = GetPrivateProfileIntA("DeadSpace2008Randomizer", "SceneGateFallbackSeconds", 30, gIniPath);
        seconds = seconds < 10 ? 10 : seconds > 300 ? 300 : seconds;
        gSceneGateTimeoutMs = static_cast<unsigned>(seconds) * 1000u;
        gSourceVentAnchor =
            GetPrivateProfileIntA("DeadSpace2008Randomizer", "EnemyRandomizerSourceVentAnchor", 1, gIniPath) != 0;
    }

    void SceneGateReset(const char* why)
    {
        const LONG open = gSceneGateRunCount;
        if (open && SceneLine())
            Log("scene gate: %ld open chain(s) dropped (%s)", open, why);
        InterlockedExchange(&gSceneGateRunCount, 0);
    }

    // After an accepted recast at a scripted spawner: watch the completions of its sequence chain.
    void SceneGateArm(const Guid128& source, uintptr_t actor)
    {
        if (!gSceneGateFallback || !gRunningTickHook || !gSceneGateListening)
            return;
        unsigned first = kSceneGateStepCount, count = 0;
        for (unsigned i = 0; i < kSceneGateStepCount; ++i)
            if (SameGuid(kSceneGateSteps[i].source, source))
            {
                if (first == kSceneGateStepCount)
                    first = i;
                ++count;
            }
        if (!count)
        {
            InterlockedIncrement(&gSceneGateNoChain);
            return;
        }
        if (gSceneGateRunCount >= static_cast<LONG>(kMaxSceneGateRuns))
        {
            InterlockedIncrement(&gSceneGateFull);
            if (SceneLine())
                Log("scene gate: table full, %s not watched", SpawnerName(source));
            return;
        }
        SceneGateRun run{};
        run.source = source;
        run.first = first;
        run.count = count > kMaxSceneGateSteps ? kMaxSceneGateSteps : count;
        run.startedAt = gRunningMs;
        run.actor = actor;
        for (unsigned k = 0; k < run.count; ++k)
            if (!kSceneGateSteps[first + k].trigger)
            {
                run.armed |= 1u << k;
                run.armedAt[k] = gRunningMs;
            }
        gSceneGateRuns[gSceneGateRunCount] = run;
        InterlockedIncrement(&gSceneGateRunCount);
        InterlockedIncrement(&gSceneGateArmed);
        if (SceneLine())
            Log("scene gate: %s recast to actor %08X, %u completion(s) watched, first %s, fallback after %u s",
                SpawnerName(source), static_cast<unsigned>(actor), run.count, kSceneGateSteps[first].name,
                gSceneGateTimeoutMs / 1000);
    }

    // From the SendMsg hook, hot path: only compares while chains are open.
    void SceneGateHeard(uint32_t hash)
    {
        const LONG runs = gSceneGateRunCount;
        for (LONG r = 0; r < runs; ++r)
        {
            SceneGateRun& run = gSceneGateRuns[r];
            for (unsigned k = 0; k < run.count; ++k)
                if (kSceneGateSteps[run.first + k].completion == hash)
                    InterlockedOr(&run.heard, static_cast<LONG>(1u << k));
        }
    }

    unsigned BitCount(uint32_t bits)
    {
        unsigned n = 0;
        for (; bits; bits &= bits - 1)
            ++n;
        return n;
    }

    void SceneGateSend(uint32_t hash)
    {
        uint32_t slot[2]{};
        MessageSlotAcquire(hash, slot);
        MessageSlotSend(slot);
        MessageSlotRelease(slot);
    }

    void SceneGateFinishStep(SceneGateRun& run, unsigned k)
    {
        run.done |= 1u << k;
        const uint32_t completion = kSceneGateSteps[run.first + k].completion;
        for (unsigned j = 0; j < run.count; ++j)
            if (!(run.armed & (1u << j)) && kSceneGateSteps[run.first + j].trigger == completion)
            {
                run.armed |= 1u << j;
                run.armedAt[j] = gRunningMs;
            }
    }

    // From the running tick (watchdog.inc), game thread.
    void SceneGateTick()
    {
        const LONG runs = gSceneGateRunCount;
        if (!runs)
            return;
        LONG kept = 0;
        for (LONG r = 0; r < runs; ++r)
        {
            SceneGateRun run = gSceneGateRuns[r];
            const uint32_t heard = static_cast<uint32_t>(InterlockedExchange(&gSceneGateRuns[r].heard, 0));
            run.heard = 0;
            for (unsigned k = 0; k < run.count; ++k)
                if ((heard & (1u << k)) && !(run.done & (1u << k)))
                {
                    if (!(run.armed & (1u << k)))
                    {
                        run.armed |= 1u << k;
                        run.armedAt[k] = gRunningMs;
                    }
                    InterlockedIncrement(&gSceneGateCompleted);
                    if (SceneLine())
                        Log("scene gate: %s heard %s after %llu ms", SpawnerName(run.source),
                            kSceneGateSteps[run.first + k].name, gRunningMs - run.armedAt[k]);
                    SceneGateFinishStep(run, k);
                }
            for (unsigned k = 0; k < run.count; ++k)
            {
                const uint32_t bit = 1u << k;
                if (!(run.armed & bit) || (run.done & bit) || gRunningMs - run.armedAt[k] < gSceneGateTimeoutMs)
                    continue;
                run.fired |= bit;
                InterlockedIncrement(&gSceneGateFired);
                Log("scene gate fallback: %s never sent %s within %u s (actor %08X); sent by the plugin",
                    SpawnerName(run.source), kSceneGateSteps[run.first + k].name, gSceneGateTimeoutMs / 1000,
                    static_cast<unsigned>(run.actor));
                SceneGateFinishStep(run, k);
                SceneGateSend(kSceneGateSteps[run.first + k].completion);
            }
            const bool open = (run.armed & ~run.done) != 0;
            if (open && gRunningMs - run.startedAt < kSceneGateLifetimeMs)
            {
                gSceneGateRuns[kept++] = run;
                continue;
            }
            if (SceneLine())
                Log("scene gate: %s closed, %u of %u completion(s) heard, %u sent by the plugin, %u never armed",
                    SpawnerName(run.source), BitCount(run.done & ~run.fired), run.count,
                    BitCount(run.fired),
                    BitCount(~run.armed & ((1u << run.count) - 1u)));
        }
        InterlockedExchange(&gSceneGateRunCount, kept);
    }

    bool SourceVentAnchorWanted()
    {
        return gSourceVentAnchor && gEnemyRandomizer;
    }

    const Guid128* SourceVentAnchorFor(const Guid128& target)
    {
        for (unsigned i = 0; i < kSourceVentAnchorCount; ++i)
            if (SameGuid(kSourceVentAnchors[i].target, target))
                return &kSourceVentAnchors[i].vent;
        return nullptr;
    }

    // Before a retail spawn call that builds a replacement arriving through a vent.
    void ArmSourceVentAnchor(const Guid128& authored, const Guid128& spawner)
    {
        gVentAnchorPending = false;
        if (!gSourceVentAnchor || !gSpawningDecodeHook)
            return;
        const Guid128* vent = SourceVentAnchorFor(authored);
        if (!vent)
        {
            InterlockedIncrement(&gVentAnchorUnknown);
            return;
        }
        gVentAnchorGuid = *vent;
        gVentAnchorSpawner = spawner;
        gVentAnchorPending = true;
    }

    void DisarmSourceVentAnchor()
    {
        if (gVentAnchorPending)
            InterlockedIncrement(&gVentAnchorUnused);
        gVentAnchorPending = false;
    }

    // From SpawningDecode_Hook after the decoder: +0x0C arrival kind, +0x14 the VentMarker GUID.
    void ApplySourceVentAnchor(void* component)
    {
        if (!gVentAnchorPending || !component)
            return;
        unsigned char* object = static_cast<unsigned char*>(component);
        uint32_t kind = 0;
        std::memcpy(&kind, object + 0x0C, sizeof(kind));
        if (kind != 1)
            return;
        gVentAnchorPending = false;
        Guid128 donorVent{};
        std::memcpy(&donorVent, object + 0x14, sizeof(donorVent));
        if (SameGuid(donorVent, gVentAnchorGuid))
        {
            InterlockedIncrement(&gVentAnchorSame);
            return;
        }
        std::memcpy(object + 0x14, &gVentAnchorGuid, sizeof(gVentAnchorGuid));
        InterlockedIncrement(&gVentAnchorApplied);
        if (InterlockedIncrement(&gVentAnchorLines) <= 48)
            Log("vent anchor: replacement at %s leaves through the source's vent %08X-%08X, not the donor's %08X-%08X",
                SpawnerName(gVentAnchorSpawner), gVentAnchorGuid.value[0], gVentAnchorGuid.value[1],
                donorVent.value[0], donorVent.value[1]);
    }

    void LogSceneGateSetup()
    {
        Log("scene gates: keep scene actors %s (%u spawners), fallback %s after %u s (%u steps%s), source vent anchor "
            "%s (%u anchors%s)",
            gKeepSceneActors ? "on" : "off", kSceneClipSourceCount, gSceneGateFallback ? "on" : "off",
            gSceneGateTimeoutMs / 1000, kSceneGateStepCount,
            gSceneGateFallback && !gRunningTickHook ? ", NO running tick: the watchdog is off" :
            gSceneGateFallback && !gSceneGateListening ? ", NOT listening: SendMsg is not hooked" : "",
            gSourceVentAnchor ? "on" : "off", kSourceVentAnchorCount,
            gSourceVentAnchor && !gSpawningDecodeHook ? ", NO spawning decoder hook" : "");
    }

    void LogSceneGateTotals()
    {
        static LONG last[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
        const LONG now[9] = {gSceneClipKept, gSceneGateArmed, gSceneGateCompleted, gSceneGateFired, gSceneGateFull,
                             gVentAnchorApplied, gVentAnchorSame, gVentAnchorUnknown, gVentAnchorUnused};
        if (std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("scene gate totals: %ld scene actors kept, %ld chains armed (%ld without a chain), %ld completions heard, "
            "%ld sent by the fallback, %ld not watched (full); vent anchor: %ld moved to the source's vent, %ld already "
            "there, %ld source without an anchor, %ld armed but no vent arrival",
            now[0], now[1], gSceneGateNoChain, now[2], now[3], now[4], now[5], now[6], now[7], now[8]);
    }
