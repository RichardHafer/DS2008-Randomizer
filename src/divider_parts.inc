    // Divider part diagnostics.
    //
    // A Divider core falls apart into the parts its fields 16-20 name
    // (+0xA90 + 16 * i). For each, at 0x004C4B20 ecx is the core and edi the
    // part's key, then key lookup 0x006F7DC0 and spawn 0x006F5730 leave the part in
    // ebp; a null part jumps to 0x004C4DAD, so 0x004C4B4E runs only for a part that
    // exists. The pool's Divider (D5110003-E000000B) names its own five parts in the
    // same sidecar; these counters show whether they appear once it is randomized in.
    constexpr uintptr_t kDividerPartRequest = 0x004C4B20;
    constexpr uintptr_t kDividerPartSpawned = 0x004C4B4E;
    safetyhook::MidHook gDividerPartRequestHook, gDividerPartSpawnedHook;
    volatile LONG gDividerPartsRequested = 0, gDividerPartsSpawned = 0;
    volatile LONG gPoolDividerPartsRequested = 0, gPoolDividerPartsSpawned = 0;
    thread_local uintptr_t tDividerCore = 0;
    thread_local Guid128 tDividerPart{};

    bool PoolEntity(const Guid128& guid)
    {
        return (guid.value[0] & 0xFFFF0000u) == 0xD5110000u;
    }

    void DividerPartRequest_Hook(safetyhook::Context& ctx)
    {
        tDividerCore = ctx.ecx;
        tDividerPart = Guid128{};
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ctx.edi), &tDividerPart, sizeof(tDividerPart),
                          nullptr);
        InterlockedIncrement(&gDividerPartsRequested);
        const Guid128 core = ReadField<Guid128>(reinterpret_cast<const void*>(ctx.ecx), 0x20);
        if (PoolEntity(core) && InterlockedIncrement(&gPoolDividerPartsRequested) <= 20)
            Log("divider part: pool core %08X (%08X-%08X) asks for part %08X-%08X (key %s)",
                static_cast<unsigned>(ctx.ecx), core.value[0], core.value[1], tDividerPart.value[0],
                tDividerPart.value[1], TargetKeyFor(tDividerPart) ? "present" : "MISSING");
    }

    void DividerPartSpawned_Hook(safetyhook::Context& ctx)
    {
        InterlockedIncrement(&gDividerPartsSpawned);
        const Guid128 core = tDividerCore
            ? ReadField<Guid128>(reinterpret_cast<const void*>(tDividerCore), 0x20) : Guid128{};
        if (PoolEntity(core) && InterlockedIncrement(&gPoolDividerPartsSpawned) <= 20)
            Log("divider part: pool core %08X spawned part %08X-%08X as %08X", static_cast<unsigned>(tDividerCore),
                tDividerPart.value[0], tDividerPart.value[1], static_cast<unsigned>(ctx.ebp));
    }

    void InstallDividerPartDiagnostics()
    {
        gDividerPartRequestHook = VerifiedMid(reinterpret_cast<void*>(kDividerPartRequest), DividerPartRequest_Hook);
        gDividerPartSpawnedHook = VerifiedMid(reinterpret_cast<void*>(kDividerPartSpawned), DividerPartSpawned_Hook);
        Log("divider part diagnostics %s at %08X/%08X",
            gDividerPartRequestHook && gDividerPartSpawnedHook ? "active" : "NOT installed",
            static_cast<unsigned>(kDividerPartRequest), static_cast<unsigned>(kDividerPartSpawned));
    }

    void LogDividerPartTotals()
    {
        const LONG requested = InterlockedCompareExchange(&gDividerPartsRequested, 0, 0);
        const LONG spawned = InterlockedCompareExchange(&gDividerPartsSpawned, 0, 0);
        const LONG poolRequested = InterlockedCompareExchange(&gPoolDividerPartsRequested, 0, 0);
        const LONG poolSpawned = InterlockedCompareExchange(&gPoolDividerPartsSpawned, 0, 0);
        static LONG lastRequested = 0, lastSpawned = 0;
        if (requested != lastRequested || spawned != lastSpawned)
            Log("divider part totals: %ld asked for, %ld spawned (pool cores: %ld asked for, %ld spawned)", requested,
                spawned, poolRequested, poolSpawned);
        lastRequested = requested;
        lastSpawned = spawned;
    }
