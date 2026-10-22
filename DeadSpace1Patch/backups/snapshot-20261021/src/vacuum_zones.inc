    // Optional ZoneRender vacuum rules. The game still owns air and effects.
    constexpr uintptr_t kVacuumZoneHandleEvents = 0x004908D0;
    constexpr uintptr_t kVacuumZoneDestructor = 0x004907C0;
    constexpr uintptr_t kVacuumZoneQuery = 0x00490CA0;
    constexpr uintptr_t kVacuumZoneContainsPoint = 0x004968A0;
    constexpr uintptr_t kVacuumZoneListHead = 0x00F45964;
    constexpr uintptr_t kVacuumZoneVtable = 0x00C63B18;
    constexpr uintptr_t kVacuumHelperVtable = 0x00C64484;
    constexpr unsigned kVacuumEntityGuid = 0x20;
    constexpr unsigned kVacuumEntityHelper = 0x1D0;
    constexpr unsigned kVacuumHelperPriority = 0x08;
    constexpr unsigned kVacuumHelperSceneMask = 0xD0;
    constexpr uint32_t kVacuumSceneBit = 1;

    bool gVacuumZones = false;
    bool gVacuumZoneDiagnostics = false;

    struct VacuumZoneRule
    {
        char name[64]{};
        Guid128 guid{};
    };

    struct LiveVacuumZone
    {
        uintptr_t entity = 0;
        uintptr_t helper = 0;
        Guid128 guid{};
        uint32_t originalMask = 0;
        bool changed = false;
    };

    constexpr unsigned kMaxVacuumZoneRules = 128;
    constexpr unsigned kMaxLiveVacuumZones = 256;
    VacuumZoneRule gVacuumZoneRules[kMaxVacuumZoneRules]{};
    LiveVacuumZone gLiveVacuumZones[kMaxLiveVacuumZones]{};
    unsigned gVacuumZoneRuleCount = 0;
    unsigned gLiveVacuumZoneCount = 0;
    unsigned gVacuumHandleEventsCalls = 0;
    unsigned gVacuumEntityRejects = 0;
    unsigned gVacuumHelperRejects = 0;
    unsigned gVacuumNativeScans = 0;
    unsigned gVacuumNativeNodes = 0;
    unsigned gVacuumNativeListRejects = 0;
    unsigned gVacuumQueryCalls = 0;
    uint32_t gVacuumLastQueryMask = UINT_MAX;
    unsigned gVacuumLastLoggedLive = UINT_MAX;
    unsigned gVacuumLastLoggedNodes = UINT_MAX;
    ULONGLONG gVacuumLastNativeScan = 0;
    SRWLOCK gVacuumZoneLock = SRWLOCK_INIT;
    safetyhook::InlineHook gVacuumZoneHandleEventsHook;
    safetyhook::InlineHook gVacuumZoneDestructorHook;
    safetyhook::InlineHook gVacuumZoneQueryHook;

    bool PlayerTransform(float* out12);

    bool VacuumMemoryRange(uintptr_t address, size_t size, bool write)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!address || !size || address + size < address ||
            !VirtualQuery(reinterpret_cast<void*>(address), &region,
                          sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) +
                              region.RegionSize;
        if (address + size > end)
            return false;
        if (!write)
            return true;
        const DWORD protection = region.Protect & 0xFF;
        return protection == PAGE_READWRITE ||
               protection == PAGE_WRITECOPY ||
               protection == PAGE_EXECUTE_READWRITE ||
               protection == PAGE_EXECUTE_WRITECOPY;
    }

    bool ValidVacuumEntity(uintptr_t entity)
    {
        return VacuumMemoryRange(entity, 0x220, false) &&
               *reinterpret_cast<const uintptr_t*>(entity) == kVacuumZoneVtable;
    }

    bool ValidVacuumHelper(uintptr_t helper, bool write)
    {
        return VacuumMemoryRange(helper, kVacuumHelperSceneMask + 4, write) &&
               *reinterpret_cast<const uintptr_t*>(helper) == kVacuumHelperVtable;
    }

    const VacuumZoneRule* FindVacuumZoneRule(const Guid128& guid)
    {
        for (unsigned i = 0; i < gVacuumZoneRuleCount; ++i)
            if (SameGuid(gVacuumZoneRules[i].guid, guid))
                return &gVacuumZoneRules[i];
        return nullptr;
    }

    void FormatVacuumGuid(const Guid128& guid, char* text, size_t size)
    {
        std::snprintf(text, size, "%08X-%08X-%08X-%08X", guid.value[0],
                      guid.value[1], guid.value[2], guid.value[3]);
    }

    void RemoveVacuumZoneLocked(uintptr_t entity)
    {
        for (unsigned i = 0; i < gLiveVacuumZoneCount; ++i)
        {
            LiveVacuumZone& zone = gLiveVacuumZones[i];
            if (zone.entity != entity)
                continue;
            if (zone.changed && ValidVacuumEntity(entity) &&
                *reinterpret_cast<uintptr_t*>(entity + kVacuumEntityHelper) ==
                    zone.helper && ValidVacuumHelper(zone.helper, true))
            {
                uint32_t& mask = *reinterpret_cast<uint32_t*>(
                    zone.helper + kVacuumHelperSceneMask);
                if (mask == zone.originalMask ||
                    mask == (zone.originalMask | kVacuumSceneBit))
                    mask = zone.originalMask;
            }
            gLiveVacuumZones[i] = gLiveVacuumZones[gLiveVacuumZoneCount - 1];
            --gLiveVacuumZoneCount;
            return;
        }
    }

    void RemoveVacuumZone(uintptr_t entity)
    {
        AcquireSRWLockExclusive(&gVacuumZoneLock);
        RemoveVacuumZoneLocked(entity);
        ReleaseSRWLockExclusive(&gVacuumZoneLock);
    }

    void RegisterVacuumZone(uintptr_t entity)
    {
        if (!ValidVacuumEntity(entity))
        {
            ++gVacuumEntityRejects;
            if (gVacuumZoneDiagnostics && gVacuumEntityRejects <= 4)
            {
                uintptr_t vtable = 0;
                if (VacuumMemoryRange(entity, sizeof(vtable), false))
                    vtable = *reinterpret_cast<const uintptr_t*>(entity);
                Log("vacuum entity rejected: object=%08X vtable=%08X",
                    static_cast<unsigned>(entity),
                    static_cast<unsigned>(vtable));
            }
            return;
        }
        const Guid128 guid = *reinterpret_cast<const Guid128*>(
            entity + kVacuumEntityGuid);
        const uintptr_t helper = *reinterpret_cast<const uintptr_t*>(
            entity + kVacuumEntityHelper);
        if (!ValidVacuumHelper(helper, gVacuumZones))
        {
            ++gVacuumHelperRejects;
            if (gVacuumZoneDiagnostics && gVacuumHelperRejects <= 4)
            {
                uintptr_t vtable = 0;
                if (VacuumMemoryRange(helper, sizeof(vtable), false))
                    vtable = *reinterpret_cast<const uintptr_t*>(helper);
                Log("vacuum helper rejected: entity=%08X helper=%08X "
                    "vtable=%08X",
                    static_cast<unsigned>(entity),
                    static_cast<unsigned>(helper),
                    static_cast<unsigned>(vtable));
            }
            return;
        }

        AcquireSRWLockExclusive(&gVacuumZoneLock);
        for (unsigned i = 0; i < gLiveVacuumZoneCount; ++i)
        {
            LiveVacuumZone& current = gLiveVacuumZones[i];
            if (current.entity != entity)
                continue;
            if (current.helper != helper)
            {
                RemoveVacuumZoneLocked(entity);
                break;
            }
            const VacuumZoneRule* rule = FindVacuumZoneRule(guid);
            if (gVacuumZones && rule && !current.changed)
            {
                current.originalMask = *reinterpret_cast<const uint32_t*>(
                    helper + kVacuumHelperSceneMask);
                *reinterpret_cast<uint32_t*>(helper + kVacuumHelperSceneMask) =
                    current.originalMask | kVacuumSceneBit;
                current.changed = true;
                if (gVacuumZoneDiagnostics)
                    Log("vacuum zone applied: %s", rule->name);
            }
            ReleaseSRWLockExclusive(&gVacuumZoneLock);
            return;
        }
        if (gLiveVacuumZoneCount < kMaxLiveVacuumZones)
        {
            LiveVacuumZone& zone =
                gLiveVacuumZones[gLiveVacuumZoneCount++];
            zone.entity = entity;
            zone.helper = helper;
            zone.guid = guid;
            zone.originalMask = *reinterpret_cast<const uint32_t*>(
                helper + kVacuumHelperSceneMask);
            const VacuumZoneRule* rule = FindVacuumZoneRule(guid);
            if (gVacuumZones && rule)
            {
                *reinterpret_cast<uint32_t*>(helper + kVacuumHelperSceneMask) =
                    zone.originalMask | kVacuumSceneBit;
                zone.changed = true;
                if (gVacuumZoneDiagnostics)
                    Log("vacuum zone applied: %s", rule->name);
            }
        }
        ReleaseSRWLockExclusive(&gVacuumZoneLock);
    }

    void SyncVacuumZonesFromNativeList()
    {
        const ULONGLONG now = GetTickCount64();
        if (gVacuumLastNativeScan && now - gVacuumLastNativeScan < 250)
            return;
        gVacuumLastNativeScan = now;
        ++gVacuumNativeScans;

        uintptr_t node = 0;
        if (!VacuumMemoryRange(kVacuumZoneListHead, sizeof(node), false))
        {
            ++gVacuumNativeListRejects;
            return;
        }
        node = *reinterpret_cast<const uintptr_t*>(kVacuumZoneListHead);
        unsigned visited = 0;
        while (node && visited < 512)
        {
            if (!VacuumMemoryRange(node, sizeof(uintptr_t) * 2, false))
            {
                ++gVacuumNativeListRejects;
                break;
            }
            const uintptr_t entity =
                *reinterpret_cast<const uintptr_t*>(node);
            const uintptr_t next =
                *reinterpret_cast<const uintptr_t*>(node + sizeof(uintptr_t));
            RegisterVacuumZone(entity);
            ++visited;
            if (next == node)
            {
                ++gVacuumNativeListRejects;
                break;
            }
            node = next;
        }
        if (node && visited == 512)
            ++gVacuumNativeListRejects;
        gVacuumNativeNodes = visited;

        AcquireSRWLockShared(&gVacuumZoneLock);
        const unsigned live = gLiveVacuumZoneCount;
        ReleaseSRWLockShared(&gVacuumZoneLock);
        if (gVacuumZoneDiagnostics &&
            (gVacuumNativeScans <= 3 || live != gVacuumLastLoggedLive ||
             visited != gVacuumLastLoggedNodes))
        {
            Log("vacuum native scan %u: %u node(s), %u live, entity "
                "rejects=%u helper rejects=%u list rejects=%u",
                gVacuumNativeScans, visited, live, gVacuumEntityRejects,
                gVacuumHelperRejects, gVacuumNativeListRejects);
            gVacuumLastLoggedLive = live;
            gVacuumLastLoggedNodes = visited;
        }
    }

    uint32_t __cdecl VacuumZoneQuery_Hook(const float* point)
    {
        SyncVacuumZonesFromNativeList();
        const uint32_t mask = gVacuumZoneQueryHook.ccall<uint32_t>(point);
        ++gVacuumQueryCalls;
        if (gVacuumZoneDiagnostics &&
            (gVacuumQueryCalls <= 3 || mask != gVacuumLastQueryMask))
            Log("vacuum query %u: mask=0x%08X vacuum=%s",
                gVacuumQueryCalls, mask,
                (mask & kVacuumSceneBit) ? "yes" : "no");
        gVacuumLastQueryMask = mask;
        return mask;
    }

    void __fastcall VacuumZoneHandleEvents_Hook(void* self, void*, void* message)
    {
        ++gVacuumHandleEventsCalls;
        RemoveVacuumZone(reinterpret_cast<uintptr_t>(self));
        gVacuumZoneHandleEventsHook.thiscall<void>(self, message);
        RegisterVacuumZone(reinterpret_cast<uintptr_t>(self));
    }

    void __fastcall VacuumZoneDestructor_Hook(void* self, void*)
    {
        RemoveVacuumZone(reinterpret_cast<uintptr_t>(self));
        gVacuumZoneDestructorHook.thiscall<void>(self);
    }

    bool VacuumSignatureMatches(uintptr_t address, const BYTE* bytes, size_t size)
    {
        return VacuumMemoryRange(address, size, false) &&
               std::memcmp(reinterpret_cast<const void*>(address), bytes, size) == 0;
    }

    void LoadVacuumZoneSettings()
    {
        gVacuumZones = GetPrivateProfileIntA(
            "DeadSpace1Patch", "VacuumZones", 0, gIniPath) != 0;
        gVacuumZoneDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "VacuumZoneDiagnostics", 0, gIniPath) != 0;
        gVacuumZoneRuleCount = 0;
        char section[8192]{};
        const DWORD used = GetPrivateProfileSectionA(
            "VacuumZones", section, sizeof(section) - 2, gIniPath);
        for (const char* row = section;
             used && *row && gVacuumZoneRuleCount < kMaxVacuumZoneRules;
             row += std::strlen(row) + 1)
        {
            const char* equals = std::strchr(row, '=');
            if (!equals || equals == row)
                continue;
            VacuumZoneRule rule;
            const size_t nameLength = static_cast<size_t>(equals - row);
            unsigned words[4]{};
            int consumed = 0;
            const char* value = equals + 1;
            const bool validGuid =
                std::sscanf(value, "%8x-%8x-%8x-%8x%n", &words[0],
                            &words[1], &words[2], &words[3], &consumed) == 4 &&
                value[consumed] == 0;
            if (nameLength >= sizeof(rule.name) || !validGuid)
            {
                Log("vacuum zone rule ignored: %s", row);
                continue;
            }
            for (unsigned i = 0; i < 4; ++i)
                rule.guid.value[i] = words[i];
            std::memcpy(rule.name, row, nameLength);
            gVacuumZoneRules[gVacuumZoneRuleCount++] = rule;
        }
        if (gVacuumZones || gVacuumZoneDiagnostics)
            Log("vacuum zones: %s, diagnostics %s, %u rule(s)",
                gVacuumZones ? "enabled" : "disabled",
                gVacuumZoneDiagnostics ? "enabled" : "disabled",
                gVacuumZoneRuleCount);
    }

    bool VacuumZoneContains(uintptr_t helper, const float* point)
    {
        unsigned char result = 0;
        const uintptr_t function = kVacuumZoneContainsPoint;
        __asm
        {
            mov ecx, helper
            mov eax, point
            call dword ptr [function]
            mov result, al
        }
        return result != 0;
    }

    void LogCurrentVacuumZone()
    {
        if (!gVacuumZoneDiagnostics)
            return;
        float transform[12]{};
        if (!PlayerTransform(transform))
        {
            Log("vacuum zone: no player transform");
            return;
        }
        float point[4]{transform[9], transform[10], transform[11], 1.0f};
        LiveVacuumZone selected{};
        int bestPriority = INT_MIN;
        uint32_t selectedMask = 0;
        AcquireSRWLockShared(&gVacuumZoneLock);
        for (unsigned i = 0; i < gLiveVacuumZoneCount; ++i)
        {
            const LiveVacuumZone& zone = gLiveVacuumZones[i];
            if (!ValidVacuumEntity(zone.entity) ||
                *reinterpret_cast<const uintptr_t*>(
                    zone.entity + kVacuumEntityHelper) != zone.helper ||
                !ValidVacuumHelper(zone.helper, false) ||
                !VacuumZoneContains(zone.helper, point))
                continue;
            const int priority = *reinterpret_cast<const int*>(
                zone.helper + kVacuumHelperPriority);
            if (priority > bestPriority)
            {
                bestPriority = priority;
                selected = zone;
                selectedMask = *reinterpret_cast<const uint32_t*>(
                    zone.helper + kVacuumHelperSceneMask);
            }
        }
        ReleaseSRWLockShared(&gVacuumZoneLock);
        if (!selected.entity)
        {
            Log("vacuum zone: no containing ZoneRender among %u live; "
                "events=%u queries=%u scans=%u nodes=%u entity rejects=%u helper "
                "rejects=%u list rejects=%u",
                gLiveVacuumZoneCount, gVacuumHandleEventsCalls,
                gVacuumQueryCalls, gVacuumNativeScans, gVacuumNativeNodes,
                gVacuumEntityRejects, gVacuumHelperRejects,
                gVacuumNativeListRejects);
            return;
        }
        char guid[48]{};
        FormatVacuumGuid(selected.guid, guid, sizeof(guid));
        const VacuumZoneRule* rule = FindVacuumZoneRule(selected.guid);
        if (rule)
            Log("vacuum zone: %s (%s), priority %d, mask 0x%08X", guid,
                rule->name, bestPriority, selectedMask);
        else
            Log("vacuum zone: %s, priority %d, mask 0x%08X", guid,
                bestPriority, selectedMask);
    }

    void InstallVacuumZoneHooks()
    {
        if (!gVacuumZones && !gVacuumZoneDiagnostics)
            return;
        static const BYTE handleSignature[]{
            0x83, 0xEC, 0x78, 0x53, 0x55, 0x56, 0x8B, 0xB4,
            0x24, 0x88, 0x00, 0x00, 0x00, 0x57, 0x8B, 0xF9};
        static const BYTE destructorSignature[]{
            0x83, 0xEC, 0x08, 0x53, 0x56, 0x57, 0x8B, 0xF9,
            0x8D, 0x44, 0x24, 0x10, 0x8D, 0x5F, 0x3C};
        static const BYTE querySignature[]{
            0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC,
            0x14, 0x53, 0x8B, 0x1D, 0x64, 0x59, 0xF4, 0x00};
        if (!VacuumSignatureMatches(kVacuumZoneHandleEvents, handleSignature,
                                    sizeof(handleSignature)) ||
            !VacuumSignatureMatches(kVacuumZoneDestructor,
                                    destructorSignature,
                                    sizeof(destructorSignature)) ||
            !VacuumSignatureMatches(kVacuumZoneQuery, querySignature,
                                    sizeof(querySignature)))
        {
            Log("vacuum zones: retail signatures do not match; hooks skipped");
            return;
        }
        gVacuumZoneHandleEventsHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kVacuumZoneHandleEvents),
            reinterpret_cast<void*>(&VacuumZoneHandleEvents_Hook));
        gVacuumZoneDestructorHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kVacuumZoneDestructor),
            reinterpret_cast<void*>(&VacuumZoneDestructor_Hook));
        gVacuumZoneQueryHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kVacuumZoneQuery),
            reinterpret_cast<void*>(&VacuumZoneQuery_Hook));
        if (!gVacuumZoneHandleEventsHook || !gVacuumZoneDestructorHook ||
            !gVacuumZoneQueryHook)
        {
            gVacuumZoneHandleEventsHook.reset();
            gVacuumZoneDestructorHook.reset();
            gVacuumZoneQueryHook.reset();
            Log("vacuum zones: hook installation failed");
            return;
        }
        Log("vacuum zones: ZoneRender hooks active at %08X, %08X and %08X",
            static_cast<unsigned>(kVacuumZoneHandleEvents),
            static_cast<unsigned>(kVacuumZoneDestructor),
            static_cast<unsigned>(kVacuumZoneQuery));
    }
