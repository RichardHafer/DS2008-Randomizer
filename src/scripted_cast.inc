    // Scripted spawners ([DeadSpace2008Randomizer] EnemyRandomizerScriptedVariants).
    //
    // A ScriptedSequence names its actor at +0x290 and finds it by GUID: the start
    // (0x005AFE51) asks 0x006F5500 for the live entity under that GUID, and a spawn
    // notification (0x005AEF65) compares a new entity's +0x20 with it. A pool donor
    // carries its own template GUID, so the sequence never finds a replacement: the
    // ch01 elevator (ss_zombie_liftA_se_start -> mm_elevator_01_se_complete) waited
    // forever and the donor stayed hidden. The sequences also play the authored
    // class's own animations.
    //
    // So a source with kSpawnContextScripted only takes a variant of its own class,
    // arriving as the authored one did, from a donor template with no live instance
    // (key+0x14 null, so the sequence's lookup returns the donor). Every sequence
    // naming the actor is pointed at the donor just before the retail spawn and
    // pointed back if the replacement is rejected.
    //
    // Lifetime: an entity belongs to the stream its spawner passed as owner
    // (FuncSpawn+0x1C; 0x006F58F7 checks it with the stream state lookup). On
    // iMsgStreamBeginUnload, SimManager 0x006F5220 destroys every entity of that
    // stream (vt[3], the [0x00F4F970] message, vt[0](1)) before the stream's
    // resources go. A donor belongs to its sidecar stream, so it outlived its scene
    // and kept evaluating the scene's clip (crash at 0x007B6721, clip only in
    // liftA_z01_zombie_se.str). Each recast donor is now destroyed the same way,
    // right after its scene stream's entities.
    struct ScriptedSequenceLink
    {
        Guid128 actor;
        Guid128 sequence;
    };
    constexpr ScriptedSequenceLink kScriptedSequenceLinks[] = {
#include "scripted_sequence_links_generated.inc"
    };
    constexpr unsigned kScriptedSequenceLinkCount = sizeof(kScriptedSequenceLinks) / sizeof(kScriptedSequenceLinks[0]);
    constexpr uintptr_t kScriptedSequenceVtable = 0x00C75A60;
    constexpr uintptr_t kScriptedEventSequenceVtable = 0x00C75848;
    constexpr unsigned kSequenceActor = 0x290;
    constexpr unsigned kMaxRecastSequences = 8;
    constexpr uintptr_t kStreamEntityTeardown = 0x006F5220;   // stdcall(SimManager, stream id), ret 8
    constexpr uintptr_t kEntityUnloadMessage = 0x00F4F970;
    constexpr unsigned kEntityLiveFlags = 0x40;                // bit 0 cleared by vt[3] (0x00740270)

    bool gEnemyRandomizerScriptedVariants = true;

    struct SequenceRecast
    {
        Guid128 authored{};
        Guid128 donor{};
        uintptr_t sequences[kMaxRecastSequences]{};
        unsigned count = 0;
        bool applied = false;
    };

    // A recast donor and the stream its authored actor would have belonged to.
    struct RecastActor
    {
        uintptr_t actor;
        uint32_t vtable;
        Guid128 donor;
        uint32_t stream;
    };
    constexpr unsigned kMaxRecastActors = 32;
    RecastActor gRecastActors[kMaxRecastActors]{};
    unsigned gRecastActorCount = 0;   // game thread only: spawn handler and stream messages
    safetyhook::InlineHook gStreamEntityTeardownHook;

    volatile LONG gScriptedKeptOff = 0, gScriptedKeptFixed = 0, gScriptedKeptUnloaded = 0, gScriptedKeptBusy = 0,
                  gScriptedRecast = 0, gScriptedReverted = 0, gScriptedVerifyFailed = 0, gScriptedTornDown = 0,
                  gScriptedGoneBefore = 0;
    volatile LONG gScriptedLines = 0;

    bool ScriptedLine()
    {
        return InterlockedIncrement(&gScriptedLines) <= 96;
    }

    // Pool sidecar GUIDs end in "DSP1OOL1", so such a value was written by a recast.
    bool PoolDonorGuid(const Guid128& guid)
    {
        return guid.value[2] == 0x50315344u && guid.value[3] == 0x314C4F4Fu;
    }

    uintptr_t LiveEntityFor(const Guid128& guid)
    {
        const uintptr_t key = TargetKeyFor(guid);
        uintptr_t object = 0;
        if (!key || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(key + 0x14), &object,
                                       sizeof(object), nullptr))
            return 0;
        return object;
    }

    // No live instance: the sequence's first lookup returns the head of this key's list.
    bool DonorIsFree(const Guid128& donor)
    {
        return TargetKeyFor(donor) && !LiveEntityFor(donor);
    }

    // Still the live entity the recast made: same vtable and GUID, active, and in its key's chain (+0x10).
    bool RecastActorAlive(const RecastActor& recast)
    {
        uint32_t vtable = 0, flags = 0;
        Guid128 guid{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(recast.actor), &vtable, sizeof(vtable),
                               nullptr) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(recast.actor + 0x20), &guid, sizeof(guid),
                               nullptr) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(recast.actor + kEntityLiveFlags), &flags,
                               sizeof(flags), nullptr) ||
            vtable != recast.vtable || !SameGuid(guid, recast.donor) || !(flags & 1))
            return false;
        uintptr_t instance = LiveEntityFor(recast.donor);
        for (unsigned step = 0; instance && step < 64; ++step)
        {
            if (instance == recast.actor)
                return true;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(instance + 0x10), &instance,
                                   sizeof(instance), nullptr))
                break;
        }
        return false;
    }

    // Drop entries whose actor is gone; true if a slot is free afterwards.
    bool RecastSlotFree()
    {
        if (gRecastActorCount < kMaxRecastActors)
            return true;
        unsigned kept = 0;
        for (unsigned i = 0; i < gRecastActorCount; ++i)
            if (RecastActorAlive(gRecastActors[i]))
                gRecastActors[kept++] = gRecastActors[i];
            else
                InterlockedIncrement(&gScriptedGoneBefore);
        gRecastActorCount = kept;
        return gRecastActorCount < kMaxRecastActors;
    }

    // Every sequence naming `authored` must be loaded and still name it (or the donor it was given).
    bool PrepareSequenceRecast(const Guid128& authored, SequenceRecast* recast, const char* where)
    {
        *recast = SequenceRecast{};
        recast->authored = authored;
        if (!gStreamEntityTeardownHook || !RecastSlotFree())
        {
            InterlockedIncrement(&gScriptedKeptUnloaded);
            if (ScriptedLine())
                Log("scripted spawner %s: %s; the authored enemy stays", where,
                    gStreamEntityTeardownHook ? "all recast slots are in use"
                                              : "the stream teardown hook is not installed");
            return false;
        }
        for (unsigned i = 0; i < kScriptedSequenceLinkCount; ++i)
        {
            const ScriptedSequenceLink& link = kScriptedSequenceLinks[i];
            if (!SameGuid(link.actor, authored))
                continue;
            const uintptr_t object = LiveEntityFor(link.sequence);
            uintptr_t vtable = 0;
            Guid128 named{};
            const bool readable = object &&
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object), &vtable, sizeof(vtable),
                                  nullptr) &&
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kSequenceActor), &named,
                                  sizeof(named), nullptr);
            const bool sequence = vtable == kScriptedSequenceVtable || vtable == kScriptedEventSequenceVtable;
            if (!readable || !sequence || (!SameGuid(named, authored) && !PoolDonorGuid(named)) ||
                recast->count >= kMaxRecastSequences)
            {
                InterlockedIncrement(&gScriptedKeptUnloaded);
                if (ScriptedLine())
                    Log("scripted spawner %s: sequence %08X-%08X is %s; the authored enemy stays", where,
                        link.sequence.value[0], link.sequence.value[1],
                        !object ? "not loaded" : !sequence ? "not a sequence" :
                        recast->count >= kMaxRecastSequences ? "one too many" : "naming another actor");
                return false;
            }
            recast->sequences[recast->count++] = object;
        }
        if (!recast->count)
        {
            InterlockedIncrement(&gScriptedKeptUnloaded);
            if (ScriptedLine())
                Log("scripted spawner %s: no sequence link for %08X-%08X; the authored enemy stays", where,
                    authored.value[0], authored.value[1]);
            return false;
        }
        return true;
    }

    void WriteSequenceActors(const SequenceRecast& recast, const Guid128& actor)
    {
        for (unsigned i = 0; i < recast.count; ++i)
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(recast.sequences[i] + kSequenceActor),
                               &actor, sizeof(actor), nullptr);
    }

    void ApplySequenceRecast(SequenceRecast* recast, const Guid128& donor)
    {
        if (!recast || !recast->count)
            return;
        recast->donor = donor;
        WriteSequenceActors(*recast, donor);
        recast->applied = true;
    }

    void RevertSequenceRecast(SequenceRecast* recast, const char* why)
    {
        if (!recast || !recast->applied)
            return;
        WriteSequenceActors(*recast, recast->authored);
        recast->applied = false;
        InterlockedIncrement(&gScriptedReverted);
        if (ScriptedLine())
            Log("scripted spawner: %u sequence(s) point at the authored %08X-%08X again (%s)", recast->count,
                recast->authored.value[0], recast->authored.value[1], why);
    }

    // After an accepted replacement: the donor key's first live entity must be ours, and it
    // is bound to `stream`, the owner the authored actor would have had.
    void VerifySequenceRecast(const SequenceRecast& recast, uintptr_t actor, uint32_t stream, const char* where)
    {
        if (!recast.applied)
            return;
        const uintptr_t head = LiveEntityFor(recast.donor);
        const bool ok = actor && head == actor;
        InterlockedIncrement(ok ? &gScriptedRecast : &gScriptedVerifyFailed);
        RecastActor entry{actor, 0, recast.donor, stream};
        const bool bound = actor && stream && gRecastActorCount < kMaxRecastActors &&
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(actor), &entry.vtable, sizeof(entry.vtable),
                              nullptr);
        if (bound)
            gRecastActors[gRecastActorCount++] = entry;
        // Owner 0 is no stream at all: the authored actor would never be unloaded either.
        if (ScriptedLine())
            Log("scripted spawner %s: %u sequence(s) now play %08X-%08X (actor %08X, donor key head %08X)%s; "
                "goes with stream %X%s", where, recast.count, recast.donor.value[0], recast.donor.value[1],
                static_cast<unsigned>(actor), static_cast<unsigned>(head),
                ok ? "" : "; MISMATCH, another instance may be cast", stream,
                bound || !stream ? "" : " - NOT BOUND, it may outlive its scene");
    }

    void NoteScriptedKept(volatile LONG* counter, const char* where, const char* why)
    {
        InterlockedIncrement(counter);
        if (ScriptedLine())
            Log("scripted spawner %s: %s; the authored enemy stays", where, why);
    }

    // 0x006F5280-0x006F52BF for one entity: deactivate, the unload message, delete.
    void DestroyLikeStreamUnload(uintptr_t entity)
    {
        using EntityCall = void(__thiscall*)(void*);
        using EntityMessage = void(__thiscall*)(void*, void*);
        using EntityDelete = void*(__thiscall*)(void*, int);
        (*reinterpret_cast<EntityCall* const*>(entity))[3](reinterpret_cast<void*>(entity));
        uintptr_t target = entity;
        if (*reinterpret_cast<const uint32_t*>(entity + 0x14) & 0x08000000u)
        {
            target = entity + 0x3C;
            if (*reinterpret_cast<const uint8_t*>(target + 4) & 2)
            {
                struct
                {
                    uint32_t id;
                    uintptr_t sender;
                    uint8_t handled;
                } message{*reinterpret_cast<const uint32_t*>(kEntityUnloadMessage), target, 0};
                (*reinterpret_cast<EntityMessage* const*>(target))[1](reinterpret_cast<void*>(target), &message);
            }
        }
        (*reinterpret_cast<EntityDelete* const*>(target))[0](reinterpret_cast<void*>(target), 1);
    }

    // After the scene stream's own entities: its recast donors go the same way.
    void __stdcall StreamEntityTeardown_Hook(void* simManager, uint32_t stream)
    {
        gStreamEntityTeardownHook.stdcall<void>(simManager, stream);
        unsigned kept = 0;
        for (unsigned i = 0; i < gRecastActorCount; ++i)
        {
            const RecastActor recast = gRecastActors[i];
            if (recast.stream != stream)
            {
                gRecastActors[kept++] = recast;
                continue;
            }
            const bool alive = RecastActorAlive(recast);
            WatchdogForget(recast.actor);
            if (alive)
                DestroyLikeStreamUnload(recast.actor);
            InterlockedIncrement(alive ? &gScriptedTornDown : &gScriptedGoneBefore);
            if (ScriptedLine())
                Log("scripted spawner: stream %X unloads, recast actor %08X (%08X-%08X) %s", stream,
                    static_cast<unsigned>(recast.actor), recast.donor.value[0], recast.donor.value[1],
                    alive ? "destroyed with it" : "was already gone");
        }
        gRecastActorCount = kept;
    }

    void InstallScriptedCast()
    {
        if (!gEnemyRandomizer || !gEnemyRandomizerScriptedVariants)
            return;
        gStreamEntityTeardownHook = VerifiedInline(reinterpret_cast<void*>(kStreamEntityTeardown),
                                                   reinterpret_cast<void*>(&StreamEntityTeardown_Hook));
        Log("scripted spawners: stream teardown hook %s at %08X, %u sequence links%s",
            gStreamEntityTeardownHook ? "active" : "NOT installed", static_cast<unsigned>(kStreamEntityTeardown),
            kScriptedSequenceLinkCount,
            gStreamEntityTeardownHook ? "" : "; scripted spawners keep their authored enemy");
    }

    void LogScriptedTotals()
    {
        static LONG last[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
        const LONG now[9] = {gScriptedRecast, gScriptedVerifyFailed, gScriptedReverted, gScriptedTornDown,
                             gScriptedGoneBefore, gScriptedKeptOff, gScriptedKeptFixed, gScriptedKeptUnloaded,
                             gScriptedKeptBusy};
        if (std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("scripted spawner totals: %ld recast, %ld recast but unverified, %ld reverted, %ld destroyed with their "
            "scene, %ld gone before it; kept: %ld switch off, %ld fixed, %ld sequence or slot unavailable, "
            "%ld no free donor", now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7], now[8]);
    }
