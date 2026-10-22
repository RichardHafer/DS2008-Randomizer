    // =====================================================================
    // Pregnant swarm release.
    //
    // A Pregnant cut open spawns its swarm template (Pregnant[4], +0xAD8) from
    // 0x004B11E0: key lookup 0x006F7DC0, spawn 0x006F5730, result at
    // [esp+0x20]. Everything after that dereferences the result unchecked
    // (0x004B161A). The spawn fails while swarm.str is not resident, and only
    // levels with authored Pregnants load it, so a randomized one crashed the
    // game when shot in the belly (2026-10-22 00:11, ch01 hangar; the key of
    // D5110004-E000002D existed, its spawn returned 0). The preload now keeps
    // swarm.str beside the Pregnant package, and a failed spawn resumes at the
    // end of that block (0x004B1688). The swarm count at +0xAE8 is already spent.
    // =====================================================================
    constexpr uintptr_t kPregnantSwarmSpawned = 0x004B127F;   // mov eax,[0x00F64978], right after the spawn
    constexpr uintptr_t kPregnantSwarmDone = 0x004B1688;
    constexpr unsigned kPregnantSwarmTemplate = 0xAD8;
    safetyhook::MidHook gPregnantSwarmHook;
    volatile LONG gPregnantSwarmSpawned = 0, gPregnantSwarmFailed = 0;

    void PregnantSwarmSpawned_Hook(safetyhook::Context& ctx)
    {
        uintptr_t spawned = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ctx.esp + 0x20), &spawned,
                               sizeof(spawned), nullptr))
            return;
        if (spawned)
        {
            InterlockedIncrement(&gPregnantSwarmSpawned);
            return;
        }
        if (InterlockedIncrement(&gPregnantSwarmFailed) <= 8)
        {
            Guid128 swarm{};
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ctx.ebx + kPregnantSwarmTemplate), &swarm,
                              sizeof(swarm), nullptr);
            Log("pregnant %08X: swarm %08X-%08X-%08X-%08X could not be spawned (key %s); this one is skipped",
                static_cast<unsigned>(ctx.ebx), swarm.value[0], swarm.value[1], swarm.value[2], swarm.value[3],
                TargetKeyFor(swarm) ? "present, so its resources are not resident" : "missing");
        }
        ctx.eip = kPregnantSwarmDone;
    }

    void InstallPregnantSwarmGuard()
    {
        gPregnantSwarmHook = VerifiedMid(reinterpret_cast<void*>(kPregnantSwarmSpawned), PregnantSwarmSpawned_Hook);
        Log("pregnant swarm guard %s at %08X", gPregnantSwarmHook ? "active" : "NOT installed",
            static_cast<unsigned>(kPregnantSwarmSpawned));
    }

    void LogPregnantSwarmTotals()
    {
        const LONG spawned = InterlockedCompareExchange(&gPregnantSwarmSpawned, 0, 0);
        const LONG failed = InterlockedCompareExchange(&gPregnantSwarmFailed, 0, 0);
        static LONG lastSpawned = 0, lastFailed = 0;
        if ((spawned || failed) && (spawned != lastSpawned || failed != lastFailed))
            Log("pregnant swarm totals: %ld spawned, %ld failed and skipped", spawned, failed);
        lastSpawned = spawned;
        lastFailed = failed;
    }
