    // Watchdog ([DeadSpace2008Randomizer] Watchdog, WatchdogRelease; DS2's
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

    // Defined in scene_gates.inc, driven by the same tick.
    void SceneGateTick();
    void SceneGateReset(const char* why);

    bool gWatchdog = true;
    bool gWatchdogRelease = true;
    // A replacement destroyed alive (not by its own stream unloading) still owes the room its death message.
    bool gWatchdogReleaseOnVanish = true;
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
        uint32_t sourceStream;   // the spawner's owner stream; 0 = unknown
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
    LONG gWatchVanishSent = 0, gWatchVanishStream = 0;

    // Streams the SimManager tore down (scripted_cast.inc's hook), with the running time.
    struct StreamTeardownNote
    {
        uint32_t stream;
        unsigned long long atMs;
    };
    constexpr unsigned kStreamTeardownNotes = 32;
    StreamTeardownNote gStreamTeardownNotes[kStreamTeardownNotes]{};
    unsigned gStreamTeardownNext = 0;
    bool gStreamTeardownWatched = false;   // scripted_cast.inc's teardown hook is installed
    void DeadBodySweepStreamTeardown(uint32_t stream);   // below, with the sweep

    void NoteStreamTeardown(uint32_t stream)
    {
        gStreamTeardownNotes[gStreamTeardownNext % kStreamTeardownNotes] = {stream, gRunningMs};
        ++gStreamTeardownNext;
        DeadBodySweepStreamTeardown(stream);
    }

    bool StreamTornDownSince(uint32_t stream, unsigned long long sinceMs)
    {
        if (!stream)
            return false;
        for (unsigned i = 0; i < kStreamTeardownNotes && i < gStreamTeardownNext; ++i)
            if (gStreamTeardownNotes[i].stream == stream && gStreamTeardownNotes[i].atMs >= sinceMs)
                return true;
        return false;
    }
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

    // Through a slot of its own, like the counters' own senders. No SEH: the send
    // runs the listeners, and a fault in one of them must not be hidden.
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
                     const Guid128& spawner, const char* role, uint32_t sourceStream = 0)
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
        entry.sourceStream = sourceStream;
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

    void DeadBodySweepReset();   // below, with the sweep

    void WatchdogReset(const char* why)
    {
        if (gWatchEntryCount && ++gWatchLines <= 64)
            Log("watchdog: %u entries dropped (%s)", gWatchEntryCount, why);
        gWatchEntryCount = 0;
        DeadBodySweepReset();
        SceneGateReset(why);
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
        const char* changed =
            *reinterpret_cast<const uint32_t*>(e.actor) != e.actorVtable ? "vtable" :
            std::memcmp(reinterpret_cast<const void*>(e.actor + 0x20), &e.actorGuid, sizeof(Guid128)) != 0 ? "guid" :
            *reinterpret_cast<const uintptr_t*>(e.actor + 0x38) != e.components ? "components" :
            e.health && !IsHealthComponentOf(e.health, e.actor) ? "health" :
            e.spawning && ComponentOf(e.actor, kTypeIdSpawning) != e.spawning ? "spawning" : nullptr;
        if (changed)
        {
            // which ones go, and how: a multiplied copy that vanishes looks like a spawn that never doubled
            if (++gWatchVanished <= 48)
                Log("watchdog: %s actor %08X for %s vanished after %u ms (%s changed; last damage %.0f, initialised %d, "
                    "activated %d, vent cleared %d, moved %d, shown %d)", e.role ? e.role : "?",
                    static_cast<unsigned>(e.actor), e.expectedName ? e.expectedName : "?", e.ageMs, changed,
                    e.lastDamage, e.initialised, e.activated, e.ventCleared, e.moved, e.shown);
            // Gone without dying: the room still counts on its message, unless the spawner's own stream went
            // with it (the authored enemy would have gone the same way, silently).
            if (!e.expectedHash || !gWatchdogRelease || !gWatchdogReleaseOnVanish)
                return WatchAction::Drop;
            if (StreamTornDownSince(e.sourceStream, e.addedMs))
            {
                ++gWatchVanishStream;
                Log("watchdog: %s at %s went with its spawner's stream %X; %s not sent", e.role,
                    SpawnerName(e.spawner), e.sourceStream, e.expectedName);
                return WatchAction::Drop;
            }
            ++gWatchVanishSent;
            Log("watchdog verdict: %s at %s was destroyed alive after %u s; %s broadcast so the room can go on",
                e.role, SpawnerName(e.spawner), e.ageMs / 1000, e.expectedName);
            return WatchAction::Send;
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
            // and the room never heard its death.
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
        // "hidden" reads +0x154, which stays set on some enemies the player fights; a hurt one is reachable.
        const bool release = std::strcmp(verdict, "inert") == 0 || std::strcmp(verdict, "stuck_in_vent") == 0 ||
                             (std::strcmp(verdict, "hidden") == 0 && !e.damaged) ||
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

    // Remove spawned necromorphs left dead without a ragdoll through deferred destroy (0x0044FFF0).
    // Identity and lifecycle checks exclude reused objects, authored corpses and regenerating enemies.
    // Stream teardown during a scan holds destruction; a missing teardown hook disables it.
    struct SweepEntry
    {
        uintptr_t actor;
        uint32_t vtable;
        Guid128 guid;
        uintptr_t components;
        unsigned long long deadSinceMs;   // running time when first seen dead, 0 = alive
    };
    constexpr unsigned kMaxSweepEntries = 512;
    constexpr unsigned kSweepAfterMs = 10000;
    constexpr unsigned kSweepPerScan = 4;
    constexpr uint32_t kSweepClasses[] = {
        0x00C678E0,   // Zombie (slashers and twitchers)
        0x00C69580,   // Exploder
        0x00C65408,   // Leaper
        0x00C65C18,   // Lurker
        0x00C64DD8,   // Infector
        0x00C61708,   // Brute
        0x00C66478,   // Pregnant
    };
    constexpr unsigned kEntityHandler = 0x3C, kEntityActiveFlags = 0x40;
    bool gDeadBodySweep = true;
    SweepEntry gSweepEntries[kMaxSweepEntries]{};
    unsigned gSweepEntryCount = 0;
    LONG gSweepAdded = 0, gSweepFull = 0, gSweepConverted = 0, gSweepDestroyed = 0, gSweepFaults = 0, gSweepLines = 0,
         gSweepOtherClass = 0, gSweepBornDead = 0, gSweepInactive = 0, gSweepOddHandler = 0,
         gSweepTeardownHolds = 0, gSweepTeardowns = 0, gSweepNoTeardownHook = 0;
    volatile LONG gSweepTeardownStream = 0;   // nonzero: a stream unloaded since the scan started

    void DeadBodySweepAdd(uintptr_t actor)
    {
        if (!gDeadBodySweep || !actor)
            return;
        for (unsigned i = 0; i < gSweepEntryCount; ++i)
            if (gSweepEntries[i].actor == actor)
                return;
        if (gSweepEntryCount >= kMaxSweepEntries)
        {
            ++gSweepFull;
            return;
        }
        SweepEntry e{};
        e.actor = actor;
        __try
        {
            e.vtable = *reinterpret_cast<const uint32_t*>(actor);
            bool known = false;
            for (uint32_t cls : kSweepClasses)
                known = known || e.vtable == cls;
            if (!known)
            {
                ++gSweepOtherClass;
                return;
            }
            const uintptr_t health = HealthComponentOf(actor);
            if (!health || !(*reinterpret_cast<const float*>(health + 0x120) > 0.0f))
            {
                ++gSweepBornDead;
                return;
            }
            std::memcpy(&e.guid, reinterpret_cast<const void*>(actor + 0x20), sizeof(e.guid));
            e.components = *reinterpret_cast<const uintptr_t*>(actor + 0x38);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++gSweepFaults;
            return;
        }
        gSweepEntries[gSweepEntryCount++] = e;
        ++gSweepAdded;
    }

    enum class SweepAction { Keep, Drop, Destroy };

    // Game thread, from the running tick. Reads only; the destroy is done by the caller.
    SweepAction DeadBodySweepScan(SweepEntry& e)
    {
        if (*reinterpret_cast<const uint32_t*>(e.actor) != e.vtable ||
            std::memcmp(reinterpret_cast<const void*>(e.actor + 0x20), &e.guid, sizeof(Guid128)) != 0 ||
            *reinterpret_cast<const uintptr_t*>(e.actor + 0x38) != e.components)
        {
            if (e.deadSinceMs)
                ++gSweepConverted;   // the game took it after all
            return SweepAction::Drop;
        }
        const uintptr_t health = HealthComponentOf(e.actor);
        if (!health || *reinterpret_cast<const float*>(health + 0x120) > 0.0f)
        {
            e.deadSinceMs = 0;
            return SweepAction::Keep;
        }
        if (!e.deadSinceMs)
            e.deadSinceMs = gRunningMs ? gRunningMs : 1;
        if (gRunningMs - e.deadSinceMs < kSweepAfterMs)
            return SweepAction::Keep;
        if (!(*reinterpret_cast<const uint32_t*>(e.actor + kEntityActiveFlags) & 1u))
        {
            ++gSweepInactive;   // already deactivated: the game is deleting it itself
            return SweepAction::Drop;
        }
        const uint32_t handler = *reinterpret_cast<const uint32_t*>(e.actor + kEntityHandler);
        const uint32_t prepare = handler >= 0x00C00000u && handler < 0x00D80000u
                                     ? *reinterpret_cast<const uint32_t*>(handler + 8) : 0;
        if (!prepare || prepare < 0x00401000u || prepare >= 0x00BC9000u)
        {
            if (++gSweepOddHandler <= 8)
                Log("dead body sweep: actor %08X (vtable %08X) has handler vtable %08X, slot 2 %08X; left alone",
                    static_cast<unsigned>(e.actor), e.vtable, handler, prepare);
            return SweepAction::Drop;
        }
        return SweepAction::Destroy;
    }

    void DeadBodySweepTick()
    {
        static unsigned long long lastScan = 0;
        if (!gSweepEntryCount || gRunningMs - lastScan < kWatchScanMs)
            return;
        lastScan = gRunningMs;
        InterlockedExchange(&gSweepTeardownStream, 0);
        unsigned keep = 0, destroyed = 0;
        for (unsigned i = 0; i < gSweepEntryCount; ++i)
        {
            SweepEntry& e = gSweepEntries[i];
            SweepAction action = SweepAction::Drop;
            __try
            {
                action = DeadBodySweepScan(e);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                ++gSweepFaults;
            }
            if (action == SweepAction::Destroy && !gStreamTeardownWatched)
            {
                if (++gSweepNoTeardownHook == 1)
                    Log("dead body sweep: actor %08X qualifies, but the stream teardown hook is missing; nothing is "
                        "destroyed", static_cast<unsigned>(e.actor));
                action = SweepAction::Keep;
            }
            if (action == SweepAction::Destroy && destroyed >= kSweepPerScan)
                action = SweepAction::Keep;   // the next scan takes it
            if (action == SweepAction::Destroy && gSweepTeardownStream)
            {
                ++gSweepTeardownHolds;   // a stream went during this scan: the next scan checks it again
                action = SweepAction::Keep;
            }
            if (action == SweepAction::Destroy)
            {
                float at[3]{};
                uint32_t drawn = 0;
                __try
                {
                    std::memcpy(at, reinterpret_cast<const void*>(e.actor + 0xC0), sizeof(at));
                    drawn = *reinterpret_cast<const uint32_t*>(e.actor + 0x154);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    ++gSweepFaults;
                    continue;   // unreadable after the scan read it: do not hand it to the game
                }
                if (++gSweepLines <= 40)
                    Log("dead body sweep: actor %08X (vtable %08X, %08X-%08X) dead for %u s at %.1f %.1f %.1f, drawn "
                        "flag %08X, never became a ragdoll; handed to the deferred destroy", static_cast<unsigned>(e.actor),
                        e.vtable, e.guid.value[0], e.guid.value[1], kSweepAfterMs / 1000, at[0], at[1], at[2], drawn);
                DeferDestroy(reinterpret_cast<void*>(e.actor));
                ++gSweepDestroyed;
                ++destroyed;
                continue;
            }
            if (action == SweepAction::Keep)
                gSweepEntries[keep++] = e;
        }
        gSweepEntryCount = keep;
    }

    void DeadBodySweepReset()
    {
        gSweepEntryCount = 0;
    }

    // The teardown may come from another thread than the tick, so it only raises a flag the scan checks.
    void DeadBodySweepStreamTeardown(uint32_t stream)
    {
        InterlockedIncrement(&gSweepTeardowns);
        InterlockedExchange(&gSweepTeardownStream, static_cast<LONG>(stream ? stream : 1));
    }

    void RunningTick_Hook()
    {
        gRunningTickHook.call<void>();
        ReplayHeldSpawns();
        static ULONGLONG last = 0;
        static unsigned long long lastScan = 0;
        const ULONGLONG now = GetTickCount64();
        // Running time only: a pause or a load leaves a long gap, which counts as one frame.
        const ULONGLONG gap = last ? now - last : 0;
        last = now;
        gRunningMs += gap > 100 ? 33 : gap;
        SceneGateTick();
        if (!gWatchTickThread)
            gWatchTickThread = GetCurrentThreadId();
        else if (gWatchTickThread != GetCurrentThreadId())
        {
            ++gWatchWrongThread;
            return;
        }
        DeadBodySweepTick();
        if (!gWatchEntryCount || gRunningMs - lastScan < kWatchScanMs)
            return;
        lastScan = gRunningMs;
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
                // no component, or the actor is gone: broadcast only
                BroadcastDeathMessage(entry.expectedHash);
                break;
            }
        }
        gWatchEntryCount = keep;
    }

    void LoadWatchdogSettings()
    {
        gWatchdog = GetPrivateProfileIntA("DeadSpace2008Randomizer", "Watchdog", 1, gIniPath) != 0;
        gWatchdogRelease = GetPrivateProfileIntA("DeadSpace2008Randomizer", "WatchdogRelease", 1, gIniPath) != 0;
        gWatchdogReleaseWaitingTrigger =
            GetPrivateProfileIntA("DeadSpace2008Randomizer", "WatchdogReleaseWaitingTrigger", 1, gIniPath) != 0;
        gWatchdogReleaseOnVanish =
            GetPrivateProfileIntA("DeadSpace2008Randomizer", "WatchdogReleaseOnVanish", 1, gIniPath) != 0;
        int leash = GetPrivateProfileIntA("DeadSpace2008Randomizer", "WatchdogLeashSeconds", 90, gIniPath);
        int probe = GetPrivateProfileIntA("DeadSpace2008Randomizer", "WatchdogProbeSeconds", 20, gIniPath);
        leash = leash < 20 ? 20 : leash > 600 ? 600 : leash;
        probe = probe < 5 ? 5 : probe > leash ? leash : probe;
        gWatchdogLeashMs = static_cast<unsigned>(leash) * 1000u;
        gWatchdogProbeMs = static_cast<unsigned>(probe) * 1000u;
        gDeadBodySweep = GetPrivateProfileIntA("DeadSpace2008Randomizer", "DeadBodySweep", 1, gIniPath) != 0;
    }

    // Only replacements are watched, so the hooks follow the randomizer; the dead body sweep needs them too.
    void InstallWatchdog()
    {
        if ((!gWatchdog || !gEnemyRandomizer) && !gDeadBodySweep)
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
        Log("watchdog %s: capture %08X, tick %08X; leash %u s, probe %u s, release %s%s%s%s",
            gRunningTickHook ? "active" : "NOT installed", static_cast<unsigned>(kWatchCaptureSite),
            static_cast<unsigned>(kRunningTickTask), gWatchdogLeashMs / 1000, gWatchdogProbeMs / 1000,
            gWatchdogRelease ? "on" : "off (report only)",
            gWatchdogReleaseWaitingTrigger ? ", also pool donors waiting for their trigger" : "",
            gWatchdogReleaseOnVanish ? ", and the message of one destroyed alive" : "",
            gDeathMessageTransplant ? "" : "; WARNING: DeathMessageTransplant=0, so a working replacement "
                                           "still dies with its donor's message");
        Log("dead body sweep %s (DeadBodySweep): a spawned necromorph dead for %u s without a ragdoll is destroyed",
            gDeadBodySweep && gRunningTickHook ? "active" : gDeadBodySweep ? "NOT active, the hooks are missing" : "off",
            kSweepAfterMs / 1000);
    }

    void LogWatchdogTotals()
    {
        static LONG lastSweep[3] = {-1, -1, -1};
        const LONG sweep[3] = {gSweepAdded, gSweepDestroyed, gSweepConverted};
        if (gRunningTickHook && gDeadBodySweep && std::memcmp(lastSweep, sweep, sizeof(sweep)) != 0)
        {
            std::memcpy(lastSweep, sweep, sizeof(sweep));
            Log("dead body sweep totals: %ld spawned necromorphs noted, %ld destroyed, %ld became ragdolls while "
                "counted dead, %u watched now; skipped %ld other classes, %ld born dead; %ld found already "
                "inactive, %ld odd handlers, %ld held by %ld stream teardowns, %ld held without the teardown "
                "hook, %ld list full, %ld faults", sweep[0], sweep[1], sweep[2], gSweepEntryCount, gSweepOtherClass,
                gSweepBornDead, gSweepInactive, gSweepOddHandler, gSweepTeardownHolds, gSweepTeardowns,
                gSweepNoTeardownHook, gSweepFull, gSweepFaults);
        }
        static LONG last[6] = {-1, -1, -1, -1, -1, -1};
        const LONG now[6] = {gWatchAdded, gWatchDied, gWatchKilled, gWatchBroadcast, gWatchReportOnly, gWatchFaults};
        if (!gRunningTickHook || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("watchdog totals: %ld watched, %ld became working enemies, %ld died, %ld released by ForceKill, "
            "%ld broadcast, %ld kill failed, %ld left alone at the leash, %ld vanished (%ld of them sent, %ld with their "
            "stream), %ld left the world alive, %ld faults, %ld wrong thread; vent exit %u x, %u-%u ms, mean %llu ms",
            now[0], gWatchActive, now[1], now[2], now[3], gWatchKillFailed, now[4], gWatchVanished, gWatchVanishSent,
            gWatchVanishStream, gWatchGone, now[5],
            gWatchWrongThread, gVentClearCount, gVentClearCount ? gVentClearMinMs : 0, gVentClearMaxMs,
            gVentClearCount ? gVentClearSumMs / gVentClearCount : 0ull);
    }
