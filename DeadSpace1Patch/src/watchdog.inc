    // =====================================================================
    // Watchdog ([DeadSpace1Patch] Watchdog, WatchdogRelease; DS2's
    // WatchdogReleaseDeathMessage). analysis/DeadSpace1_Watchdog_2026-10-21.md.
    //
    // A replacement that never becomes an enemy (stuck in its vent, never
    // initialised, never shown) blocks a room that counts kills. Every actor
    // the spawn hook makes for a replacement is captured at 0x005B58BD and
    // checked from the game's own per-frame task 0x00449470. At the leash an
    // incapable one is killed through HealthComponent ForceKill (vtable
    // +0x28), whose SetHealth sends the death message held at +0x130 itself;
    // only an actor without a usable component has its message broadcast
    // instead, never both. Visible enemies the player can reach, and ones that
    // move or get hurt outside their vent, are never released. One that waits
    // for its activation trigger is released only when its donor came from the
    // global pool: that trigger belongs to another level and never arrives.
    // =====================================================================
    constexpr uintptr_t kWatchCaptureSite = 0x005B58BD;   // mov esi,eax; test esi,esi
    constexpr uintptr_t kRunningTickTask = 0x00449470;    // void(void), iMsgRunningPostTick
    constexpr uintptr_t kSlotSend = 0x00401490;           // eax = &slot, push force byte, caller pops
    constexpr uintptr_t kTypeIdHealth = 0x00F440BC;
    constexpr uintptr_t kTypeIdDismemberableHealth = 0x00F440A0;
    constexpr uintptr_t kTypeIdSpawning = 0x00F440A8;
    constexpr uintptr_t kTypeIdMovement = 0x00F4409C;
    constexpr uintptr_t kTypeIdVent = 0x00F440E4;
    constexpr uint32_t kHealthVtable = 0x00C613AC;
    constexpr uint32_t kDismemberableHealthVtable = 0x00C60F78;
    constexpr unsigned kWatchScanMs = 500;

    bool gWatchdog = true;
    bool gWatchdogRelease = true;
    bool gWatchdogReleaseWaitingTrigger = true;
    unsigned gWatchdogLeashMs = 90000;
    unsigned gWatchdogProbeMs = 20000;
    safetyhook::MidHook gWatchCaptureHook;
    safetyhook::InlineHook gRunningTickHook;
    thread_local bool gWatchCaptureArmed = false;
    thread_local uintptr_t gWatchCapturedActor = 0;
    DWORD gWatchTickThread = 0;   // the thread of the first tick
    unsigned long long gRunningMs = 0;   // running time, advanced by the tick only

    struct WatchEntry
    {
        uintptr_t actor;
        uint32_t actorVtable;
        Guid128 actorGuid;
        uintptr_t components;    // actor[+0x38]: with the two below, one actor and not its successor
        uintptr_t health;
        uintptr_t spawning;
        Guid128 source;          // the authored target, whose message the room waits for
        uint32_t expectedHash;   // 0 = the source sends none
        const char* expectedName;
        Guid128 spawner;
        const char* role;
        bool foreignDonor;       // from the global pool, so its own triggers belong to another level
        unsigned long long addedMs;
        unsigned ageMs;
        float startPosition[3];
        bool positionKnown;
        float lastDamage;
        bool scanned, initialised, activated, ventExitBegan, ventCleared, moved, shown, damaged, waitsForTrigger;
        bool probed, released, working;
        unsigned releaseAgeMs;
        unsigned goneSinceMs;    // age at which a working one was first seen inactive, 0 = active
    };
    constexpr unsigned kMaxWatchEntries = 128;
    // A working enemy a room counts that stays inactive this long while alive is released.
    constexpr unsigned kWatchGoneMs = 15000;
    WatchEntry gWatchEntries[kMaxWatchEntries]{};
    unsigned gWatchEntryCount = 0;
    LONG gWatchAdded = 0, gWatchFull = 0, gWatchDied = 0, gWatchVanished = 0, gWatchKilled = 0,
         gWatchBroadcast = 0, gWatchKillFailed = 0, gWatchReportOnly = 0, gWatchFaults = 0, gWatchWrongThread = 0,
         gWatchActive = 0, gWatchLines = 0, gWatchGone = 0;
    unsigned gVentClearCount = 0, gVentClearMinMs = 0xFFFFFFFFu, gVentClearMaxMs = 0;
    unsigned long long gVentClearSumMs = 0;

    void WatchCapture_Hook(safetyhook::Context& ctx)
    {
        if (gWatchCaptureArmed && ctx.eax && !gWatchCapturedActor)
            gWatchCapturedActor = ctx.eax;
    }

    // Around one retail spawn call, nested like TransplantState: End returns
    // the actor this call built, or 0, and puts the caller's capture back.
    struct WatchCaptureState
    {
        bool armed;
        uintptr_t actor;
    };

    WatchCaptureState WatchCaptureBegin()
    {
        const WatchCaptureState outer{gWatchCaptureArmed, gWatchCapturedActor};
        gWatchCaptureArmed = gWatchCaptureHook ? true : false;
        gWatchCapturedActor = 0;
        return outer;
    }

    uintptr_t WatchCaptureEnd(const WatchCaptureState& outer)
    {
        const uintptr_t actor = gWatchCapturedActor;
        gWatchCaptureArmed = outer.armed;
        gWatchCapturedActor = outer.actor;
        return actor;
    }

    // The engine's own lookup (0x00431620): a per-class index table at actor[+0x34], components at actor[+0x38].
    uintptr_t ComponentOf(uintptr_t actor, uintptr_t typeIdGlobal)
    {
        const uint32_t typeId = *reinterpret_cast<const uint32_t*>(typeIdGlobal);
        const uintptr_t table = *reinterpret_cast<const uintptr_t*>(actor + 0x34);
        const uintptr_t components = *reinterpret_cast<const uintptr_t*>(actor + 0x38);
        if (!table || !components || typeId > 0x400)
            return 0;
        const uint16_t index = *reinterpret_cast<const uint16_t*>(table + typeId * 2 + 0xE);
        if (index == 0xFFFF)
            return 0;
        const uintptr_t component = *reinterpret_cast<const uintptr_t*>(components + index * 4u);
        return component && *reinterpret_cast<const uintptr_t*>(component + 8) == actor ? component : 0;
    }

    bool IsHealthComponentOf(uintptr_t component, uintptr_t actor)
    {
        if (!component || *reinterpret_cast<const uintptr_t*>(component + 8) != actor)
            return false;
        const uint32_t vtable = *reinterpret_cast<const uint32_t*>(component);
        return vtable == kHealthVtable || vtable == kDismemberableHealthVtable;
    }

    uintptr_t HealthComponentOf(uintptr_t actor)
    {
        for (uintptr_t id : {kTypeIdHealth, kTypeIdDismemberableHealth})
        {
            const uintptr_t component = ComponentOf(actor, id);
            if (IsHealthComponentOf(component, actor))
                return component;
        }
        return 0;
    }

    uint32_t SlotHash(uintptr_t health)
    {
        const uintptr_t id = *reinterpret_cast<const uintptr_t*>(health + kDeathMessageSlot);
        return id ? *reinterpret_cast<const uint32_t*>(id + 0x10) : 0;
    }

    __declspec(noinline) void MessageSlotSend(void* slot)
    {
        const uintptr_t function = kSlotSend;
        __asm
        {
            push 0
            mov eax, slot
            mov ecx, function
            call ecx
            add esp, 4
        }
    }

    // Through a slot of our own, like the counters' own senders. No SEH: the
    // send runs the listeners, and a fault in one of them must not be hidden.
    void BroadcastDeathMessage(uint32_t hash)
    {
        uint32_t slot[2]{};
        MessageSlotAcquire(hash, slot);
        MessageSlotSend(slot);
        MessageSlotRelease(slot);
        ++gWatchBroadcast;
    }

    void ForceKill(uintptr_t health)
    {
        using ForceKillFn = void(__thiscall*)(void*);
        const uintptr_t* vtable = *reinterpret_cast<const uintptr_t* const*>(health);
        reinterpret_cast<ForceKillFn>(vtable[0x28 / 4])(reinterpret_cast<void*>(health));
    }

    bool ForeignDonor(const Guid128& donor)
    {
        if (!gEnemyRandomizerGlobalTemplates)
            return false;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
            if (SameGuid(kGlobalEnemyTargets[i].target, donor))
                return true;
        return false;
    }

    void WatchdogAdd(uintptr_t actor, uintptr_t health, const Guid128& source, const Guid128& donor,
                     const Guid128& spawner, const char* role)
    {
        if (!gWatchdog || !gRunningTickHook || !actor)
            return;
        if (gWatchEntryCount >= kMaxWatchEntries)
        {
            if (++gWatchFull <= 4)
                Log("watchdog: table full (%u), %s at %s not watched", kMaxWatchEntries, role, SpawnerName(spawner));
            return;
        }
        WatchEntry entry{};
        __try
        {
            entry.actor = actor;
            entry.actorVtable = *reinterpret_cast<const uint32_t*>(actor);
            std::memcpy(&entry.actorGuid, reinterpret_cast<const void*>(actor + 0x20), sizeof(Guid128));
            entry.components = *reinterpret_cast<const uintptr_t*>(actor + 0x38);
            // The transplant's component, but only when it really is this actor's.
            entry.health = IsHealthComponentOf(health, actor) ? health : HealthComponentOf(actor);
            entry.spawning = ComponentOf(actor, kTypeIdSpawning);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++gWatchFaults;
            return;
        }
        const DeathMessageEntry* message = DeathMessageFor(source);
        entry.source = source;
        entry.expectedHash = message ? message->hash : 0;
        entry.expectedName = message ? message->name : "no death message";
        entry.spawner = spawner;
        entry.role = role;
        entry.foreignDonor = ForeignDonor(donor);
        entry.addedMs = gRunningMs;
        gWatchEntries[gWatchEntryCount++] = entry;
        ++gWatchAdded;
        if (++gWatchLines <= 64)
            Log("watch add: %s actor %08X health %08X at %s (%s donor), waits for %s", role,
                static_cast<unsigned>(actor), static_cast<unsigned>(entry.health), SpawnerName(spawner),
                entry.foreignDonor ? "pool" : "local", entry.expectedName);
    }

    // An actor someone else destroys on purpose (scripted_cast.inc) leaves the watch.
    void WatchdogForget(uintptr_t actor)
    {
        unsigned kept = 0;
        for (unsigned i = 0; i < gWatchEntryCount; ++i)
            if (gWatchEntries[i].actor != actor)
                gWatchEntries[kept++] = gWatchEntries[i];
        gWatchEntryCount = kept;
    }

    void WatchdogReset(const char* why)
    {
        if (gWatchEntryCount && ++gWatchLines <= 64)
            Log("watchdog: %u entries dropped (%s)", gWatchEntryCount, why);
        gWatchEntryCount = 0;
    }

    // A replacement's donor comes from another level: an activation trigger it
    // still waits for belongs to that level and never arrives here. A visible
    // enemy the player can reach is never released, unless it is still in its
    // vent, where the spawn's invulnerability (mode 2) keeps it from dying.
    const char* WatchVerdict(const WatchEntry& e, uint32_t kind)
    {
        if (!e.shown)
        {
            if (e.waitsForTrigger && !e.activated)
                return "waiting_trigger";
            if (kind == 1 && !e.ventCleared)
                return "stuck_in_vent";
            if (e.spawning && kind != 4 && !e.initialised)
                return "inert";
            return "hidden";
        }
        if (kind == 1 && !e.ventCleared)
            return "stuck_in_vent";
        if (e.spawning && kind != 4 && !e.initialised)
            return "inert_visible";
        return "idle";
    }

    enum class WatchAction
    {
        Keep,
        Drop,
        Kill,          // ForceKill: the component sends the room its message itself
        MuteAndSend,   // the slot could not be given the right message: silence it, kill, broadcast
        Send,          // no component: broadcast only
    };

    // Reads only; every game call happens in the caller, outside SEH.
    WatchAction WatchdogScan(WatchEntry& e)
    {
        e.ageMs = static_cast<unsigned>(gRunningMs - e.addedMs);
        if (*reinterpret_cast<const uint32_t*>(e.actor) != e.actorVtable ||
            std::memcmp(reinterpret_cast<const void*>(e.actor + 0x20), &e.actorGuid, sizeof(Guid128)) != 0 ||
            *reinterpret_cast<const uintptr_t*>(e.actor + 0x38) != e.components ||
            (e.health && !IsHealthComponentOf(e.health, e.actor)) ||
            (e.spawning && ComponentOf(e.actor, kTypeIdSpawning) != e.spawning))
        {
            ++gWatchVanished;
            return WatchAction::Drop;
        }
        if (!e.health)
            e.health = HealthComponentOf(e.actor);
        if (e.health && *reinterpret_cast<const float*>(e.health + 0x120) <= 0.0f)
        {
            ++gWatchDied;
            if (e.released)
            {
                ++gWatchKilled;
                Log("watchdog: %s at %s released after %u s, it sent %s itself",
                    e.role, SpawnerName(e.spawner), e.releaseAgeMs / 1000, e.expectedName);
            }
            return WatchAction::Drop;
        }
        if (e.released)
        {
            // ForceKill did not take: silence the slot and say the message ourselves, once.
            if (e.ageMs < e.releaseAgeMs + 1000)
                return WatchAction::Keep;
            ++gWatchKillFailed;
            Log("watchdog: ForceKill left %s at %s alive; its slot is muted and %s broadcast instead",
                e.role, SpawnerName(e.spawner), e.expectedName);
            return WatchAction::MuteAndSend;
        }

        const uintptr_t vent = ComponentOf(e.actor, kTypeIdVent);
        const uintptr_t movement = ComponentOf(e.actor, kTypeIdMovement);
        const uint32_t kind = e.spawning ? *reinterpret_cast<const uint32_t*>(e.spawning + 0x0C) : 0;
        const uint32_t bits = e.spawning ? *reinterpret_cast<const uint32_t*>(e.spawning + 0x4C) : 0;
        e.initialised |= (bits & 0x10) != 0;
        e.activated |= (bits & 0x05) != 0;   // the trigger arrived, or the vent exit began
        e.ventExitBegan |= (bits & 0x04) != 0;
        e.waitsForTrigger |= e.spawning && *reinterpret_cast<const uint32_t*>(e.spawning + 0x38) != 0;
        if (vent && !e.ventCleared && e.initialised && !(*reinterpret_cast<const uint32_t*>(vent + 0x68) & 1))
        {
            e.ventCleared = true;
            ++gVentClearCount;
            gVentClearSumMs += e.ageMs;
            gVentClearMinMs = e.ageMs < gVentClearMinMs ? e.ageMs : gVentClearMinMs;
            gVentClearMaxMs = e.ageMs > gVentClearMaxMs ? e.ageMs : gVentClearMaxMs;
        }
        e.shown |= *reinterpret_cast<const int32_t*>(e.actor + 0x154) == 0;
        if (movement)
        {
            const float* position = reinterpret_cast<const float*>(movement + 0xC0);
            if (!e.positionKnown)
            {
                std::memcpy(e.startPosition, position, sizeof(e.startPosition));
                e.positionKnown = true;
            }
            const float dx = position[0] - e.startPosition[0], dy = position[1] - e.startPosition[1],
                        dz = position[2] - e.startPosition[2];
            e.moved |= dx * dx + dy * dy + dz * dz > 0.25f;
        }
        if (e.health)
        {
            const float damage = *reinterpret_cast<const float*>(e.health + 0x100);
            e.damaged |= e.scanned && damage != e.lastDamage;
            e.lastDamage = damage;
        }
        e.scanned = true;

        const char* verdict = WatchVerdict(e, kind);
        if (!e.probed && e.ageMs >= gWatchdogProbeMs)
        {
            e.probed = true;
            if (++gWatchLines <= 64)
                Log("watch probe: %s at %s after %u s: kind %u, bits %02X, vent %s, %s, %s, %s -> %s", e.role,
                    SpawnerName(e.spawner), e.ageMs / 1000, kind, bits & 0xFF,
                    vent ? (e.ventCleared ? "cleared" : "hidden") : "none", e.shown ? "shown" : "never shown",
                    e.moved ? "moved" : "never moved", e.damaged ? "damaged" : "unhurt", verdict);
        }
        // Out of its vent, visible and moving or hurt: a working enemy. Only one a room
        // counts stays watched, for the case below.
        if (!e.working && (e.moved || e.damaged) && e.shown && (kind != 1 || e.ventCleared))
        {
            ++gWatchActive;
            e.working = true;
            if (!e.expectedHash)
                return WatchAction::Drop;
        }
        if (e.working)
        {
            // It can still leave the world alive: a Lurker out of a Zombie's vent in the
            // ch02 quarantine went inactive (+0x40 bit 0 clear) and hidden at full health,
            // and the room never heard its death (2026-10-22).
            if (*reinterpret_cast<const uint32_t*>(e.actor + 0x40) & 1)
            {
                e.goneSinceMs = 0;
                return WatchAction::Keep;
            }
            if (!e.goneSinceMs)
            {
                e.goneSinceMs = e.ageMs ? e.ageMs : 1;
                if (++gWatchLines <= 64)
                    Log("watchdog: %s at %s went inactive alive after %u s; released in %u s unless it comes back",
                        e.role, SpawnerName(e.spawner), e.ageMs / 1000, kWatchGoneMs / 1000);
            }
            if (e.ageMs - e.goneSinceMs < kWatchGoneMs || !gWatchdogRelease)
                return WatchAction::Keep;
            ++gWatchGone;
            e.released = true;
            e.releaseAgeMs = e.ageMs;
            Log("watchdog verdict: %s at %s left the world alive; released so the room hears %s", e.role,
                SpawnerName(e.spawner), e.expectedName);
            if (!e.health)
                return WatchAction::Send;
            return SlotHash(e.health) == e.expectedHash ? WatchAction::Kill : WatchAction::MuteAndSend;
        }
        if (e.ageMs < gWatchdogLeashMs)
            return WatchAction::Keep;
        const bool waiting = std::strcmp(verdict, "waiting_trigger") == 0;
        const bool release = std::strcmp(verdict, "inert") == 0 || std::strcmp(verdict, "stuck_in_vent") == 0 ||
                             std::strcmp(verdict, "hidden") == 0 ||
                             (waiting && gWatchdogReleaseWaitingTrigger && e.foreignDonor);
        if (!release || !gWatchdogRelease || !e.expectedHash)
        {
            ++gWatchReportOnly;
            Log("watchdog verdict: %s at %s after %u s is %s; %s", e.role, SpawnerName(e.spawner),
                e.ageMs / 1000, verdict,
                !release ? (waiting ? "its trigger may still come (local donor), left alone" : "left alone")
                         : !gWatchdogRelease ? "WatchdogRelease=0, report only"
                                             : "its source sends no death message, left alone");
            return WatchAction::Drop;
        }
        e.released = true;
        e.releaseAgeMs = e.ageMs;
        Log("watchdog verdict: %s at %s after %u s is %s; released so the room hears %s", e.role,
            SpawnerName(e.spawner), e.ageMs / 1000, verdict, e.expectedName);
        if (!e.health)
            return WatchAction::Send;
        return SlotHash(e.health) == e.expectedHash ? WatchAction::Kill : WatchAction::MuteAndSend;
    }

    void RunningTick_Hook()
    {
        gRunningTickHook.call<void>();
        static ULONGLONG last = 0;
        static unsigned long long lastScan = 0;
        const ULONGLONG now = GetTickCount64();
        // Running time only: a pause or a load leaves a long gap, which counts as one frame.
        const ULONGLONG gap = last ? now - last : 0;
        last = now;
        gRunningMs += gap > 100 ? 33 : gap;
        if (!gWatchEntryCount || gRunningMs - lastScan < kWatchScanMs)
            return;
        lastScan = gRunningMs;
        if (!gWatchTickThread)
            gWatchTickThread = GetCurrentThreadId();
        else if (gWatchTickThread != GetCurrentThreadId())
        {
            ++gWatchWrongThread;
            return;
        }
        unsigned keep = 0;
        for (unsigned i = 0; i < gWatchEntryCount; ++i)
        {
            WatchEntry& entry = gWatchEntries[i];
            WatchAction action = WatchAction::Drop;
            __try
            {
                action = WatchdogScan(entry);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                ++gWatchFaults;
            }
            switch (action)
            {
            case WatchAction::Keep:
                gWatchEntries[keep++] = entry;
                break;
            case WatchAction::Drop:
                break;
            case WatchAction::Kill:
                ForceKill(entry.health);
                gWatchEntries[keep++] = entry;   // the next scan confirms the death
                break;
            case WatchAction::MuteAndSend:
                // Silence first, so the kill cannot carry a second (or the donor's) message.
                if (SetDeathMessage(reinterpret_cast<void*>(entry.health), 0) == SlotResult::Changed ||
                    SlotHash(entry.health) == 0)
                {
                    if (*reinterpret_cast<const float*>(entry.health + 0x120) > 0.0f)
                        ForceKill(entry.health);
                }
                else
                    Log("watchdog: the slot of %s at %s could not be muted; only the broadcast goes out",
                        entry.role, SpawnerName(entry.spawner));
                BroadcastDeathMessage(entry.expectedHash);
                break;
            case WatchAction::Send:
                BroadcastDeathMessage(entry.expectedHash);
                break;
            }
        }
        gWatchEntryCount = keep;
    }

    void LoadWatchdogSettings()
    {
        gWatchdog = GetPrivateProfileIntA("DeadSpace1Patch", "Watchdog", 1, gIniPath) != 0;
        gWatchdogRelease = GetPrivateProfileIntA("DeadSpace1Patch", "WatchdogRelease", 1, gIniPath) != 0;
        gWatchdogReleaseWaitingTrigger =
            GetPrivateProfileIntA("DeadSpace1Patch", "WatchdogReleaseWaitingTrigger", 1, gIniPath) != 0;
        int leash = GetPrivateProfileIntA("DeadSpace1Patch", "WatchdogLeashSeconds", 90, gIniPath);
        int probe = GetPrivateProfileIntA("DeadSpace1Patch", "WatchdogProbeSeconds", 20, gIniPath);
        leash = leash < 20 ? 20 : leash > 600 ? 600 : leash;
        probe = probe < 5 ? 5 : probe > leash ? leash : probe;
        gWatchdogLeashMs = static_cast<unsigned>(leash) * 1000u;
        gWatchdogProbeMs = static_cast<unsigned>(probe) * 1000u;
    }

    // Only replacements are watched, so the hooks follow the randomizer.
    void InstallWatchdog()
    {
        if (!gWatchdog || !gEnemyRandomizer)
        {
            Log("watchdog off (%s)", gWatchdog ? "EnemyRandomizer=0" : "Watchdog=0");
            return;
        }
        gWatchCaptureHook = VerifiedMid(reinterpret_cast<void*>(kWatchCaptureSite), &WatchCapture_Hook);
        if (gWatchCaptureHook)
            gRunningTickHook = VerifiedInline(reinterpret_cast<void*>(kRunningTickTask),
                                              reinterpret_cast<void*>(&RunningTick_Hook));
        if (!gRunningTickHook)
            gWatchCaptureHook = {};
        Log("watchdog %s: capture %08X, tick %08X; leash %u s, probe %u s, release %s%s%s",
            gRunningTickHook ? "active" : "NOT installed", static_cast<unsigned>(kWatchCaptureSite),
            static_cast<unsigned>(kRunningTickTask), gWatchdogLeashMs / 1000, gWatchdogProbeMs / 1000,
            gWatchdogRelease ? "on" : "off (report only)",
            gWatchdogReleaseWaitingTrigger ? ", also pool donors waiting for their trigger" : "",
            gDeathMessageTransplant ? "" : "; WARNING: DeathMessageTransplant=0, so a working replacement "
                                           "still dies with its donor's message");
    }

    void LogWatchdogTotals()
    {
        static LONG last[6] = {-1, -1, -1, -1, -1, -1};
        const LONG now[6] = {gWatchAdded, gWatchDied, gWatchKilled, gWatchBroadcast, gWatchReportOnly, gWatchFaults};
        if (!gRunningTickHook || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("watchdog totals: %ld watched, %ld became working enemies, %ld died, %ld released by ForceKill, "
            "%ld broadcast, %ld kill failed, %ld left alone at the leash, %ld vanished, %ld left the world alive, %ld faults, %ld wrong thread; "
            "vent exit %u x, %u-%u ms, mean %llu ms",
            now[0], gWatchActive, now[1], now[2], now[3], gWatchKillFailed, now[4], gWatchVanished, gWatchGone, now[5],
            gWatchWrongThread, gVentClearCount, gVentClearCount ? gVentClearMinMs : 0, gVentClearMaxMs,
            gVentClearCount ? gVentClearSumMs / gVentClearCount : 0ull);
    }
