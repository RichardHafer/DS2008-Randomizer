    // ---------------------------------------------------------------------
    // World editor, stage one: see what is there.
    //
    // Everything the player can walk up to is an entity built by a factory in
    // the class table.  The constructor addresses below were read out of a
    // full memory dump: for each class the factory's last call is its
    // constructor, and a constructor takes its object in ECX.  Hooking there
    // gives a registry of live objects without needing a vtable, which is the
    // part that turned out unreliable offline - several of these classes share
    // a base vtable and would have been indistinguishable.
    //
    // The transform offset differs per class (0x7C on a PowerUp, 0x90 on a
    // LogEntry, 0xE0 on a container), so it is not hard-coded.  The first time
    // a class is examined its object is scanned for a 4x4 with three unit-ish
    // axes and a w of 1, and that offset is then remembered for the class.
    // ---------------------------------------------------------------------
    struct EditorClass
    {
        const char* name;
        uintptr_t ctor;
        // Placed world objects start with a vtable, which is how a
        // freed pointer is recognised.  Managers do not, so they are
        // checked a weaker way.
        bool vtableAtStart = true;
        // The vtable this class writes into its object, read out of the
        // constructor in the decrypted image.  It is what tells a real
        // object from whatever else a register happened to hold.
        uintptr_t vtable = 0;
    };

    constexpr EditorClass kEditorClasses[] = {
        { "LogEntry",           0x0056EA20, true, 0x00C73698 },   // audio and text logs
        { "PowerUp",            0x00514CA0, true, 0x00C6FAA0 },   // med packs, stasis, air
        { "SpareAmmo",          0x00515FD0, true, 0x00C700A0 },
        { "KeyItem",            0x0050FCD0, true, 0x00C6F4B0 },
        { "Schematic",          0x00515A10, true, 0x00C6FDB0 },
        { "StandardContainer",  0x005F2710, true, 0x00C78FB8 },   // crates and lockers
        { "BreakableContainer", 0x005F1C00, true, 0x00C78D08 },
        { "ItemDropList",       0x0050DE30, true, 0x00C6F438 },   // what a container drops
        { "RechargeStation",    0x005FC100, true, 0x00C7A070 },
        { "UpgradeBench",       0x005DE470, true, 0x00C78228 },
        { "SaveKiosk",          0x005F53C0, true, 0x00C792C0 },
        { "ShipStore",          0x005F93E0, true, 0x00C79DA0 },
        { "StandardDoor",       0x004E4B60, true, 0x00C6C078 },
        { "GravityField",       0x00491D90, true, 0x00C64190 },
        // The real one, read out of the class table: id 77A210A2, 0x220 bytes.
        // Its bounding sphere sits at +0x1D0 (centre) and +0x1DC (radius), in
        // the same world units the placement readout prints, so the zone the
        // player is standing in can be named without asking the game.
        { "ZoneRender",         0x00732340, true, 0x00C842F0 },
        // The three the cheat page needs.  Their constructors come out of
        // the class registry: the factory listed there is a thunk that
        // allocates and then calls the constructor, and reading that call
        // from the decrypted dump gives these.  Verified against PowerUp,
        // whose factory 00409F80 resolves to the constructor we already
        // knew.
        // These three are managers, not placed world objects, and they do
        // not start with a vtable: the first inventory read showed a heap
        // pointer at offset 0.  The false marks them so the liveness test
        // uses the freed-block fill instead of the vtable range.
        { "PlayerMX9Inventory", 0x0072D590, false, 0x00C85278 },   // the RIG inventory
        { "WeaponManager",      0x00506DC0, false, 0x00C6ED28 },
        { "PickupItem",         0x00512650, false, 0x00C6F808 },
        // Playing dead does not survive an extra spawn - measured in game:
        // the enemies came out of the host spawner's vent and behaved
        // normally.  So the state has to be found and set, and for that we
        // need to read a real one.  Chapter 1 has an authored corpse in
        // hallway B; diffing it against an ordinary Zombie shows the field.
        { "Zombie",             0x004BA170, true, 0x00C678E0 },
        // Lighting.  ZoneRender turned out to be a visibility hull - its
        // three flags were cleared in game and nothing changed - while
        // AnimateLight has 51 properties and 2136 instances, which is
        // where fog and ambience have to live.
        { "AnimateLight",       0x005E4E70, true, 0x00C787AC },
    };
    constexpr unsigned kEditorClassCount =
        sizeof(kEditorClasses) / sizeof(kEditorClasses[0]);
    constexpr unsigned kEditorRegistryCapacity = 1024;

    // Which register holds the object at constructor entry is not the same
    // for every class - measured offline, the constructors move it into
    // esi, edi or ebp before writing the vtable, and for at least one the
    // object never passes through ecx at all.  Rather than guess a
    // convention per class, remember the candidates and let the vtable
    // decide later, once the constructor has actually written it.
    struct EditorObject
    {
        uintptr_t candidates[4];
        unsigned classIndex;
    };

    EditorObject gEditorObjects[kEditorRegistryCapacity]{};
    // The registry is a ring, so a rare object is overwritten by the
    // hundreds of ordinary ones that follow it - the shop was recorded
    // and gone again by the time it was asked for.  Keeping the newest of
    // each class separately costs one pointer per class and survives any
    // amount of churn.
    // One-based index into the ring, so 0 means "nothing yet".
    unsigned gNewestEditorObject[kEditorClassCount]{};
    volatile LONG gEditorWriteCursor = 0;
    volatile LONG gEditorCount = 0;
    int gEditorMatrixOffset[kEditorClassCount];
    safetyhook::MidHook gEditorHooks[kEditorClassCount];

    // An object is remembered when it is built and never forgotten when it
    // dies, so the registry fills up with freed memory - the zone probe
    // printed a centre of 1e17 and the inventory read came back full of
    // BADBADBA/BEEFBEEF, the allocator's freed-block fill.  Every entity
    // of the game starts with a vtable in the module's read-only data,
    // between roughly 00C00000 and 00E00000; a heap pointer or a fill
    // pattern there means the object is gone.  This is not proof of life,
    // but it rejects every stale pointer we have actually seen.
    constexpr uintptr_t kVtableLow = 0x00C00000;
    constexpr uintptr_t kVtableHigh = 0x00E00000;

    uintptr_t ResolveEditorObject(const EditorObject& entry)
    {
        if (entry.classIndex >= kEditorClassCount)
            return 0;
        const uintptr_t wanted = kEditorClasses[entry.classIndex].vtable;
        for (unsigned i = 0; i < 4; ++i)
        {
            const uintptr_t candidate = entry.candidates[i];
            if (!candidate || (candidate & 3) != 0)
                continue;
            __try
            {
                if (*reinterpret_cast<const uintptr_t*>(candidate) == wanted)
                    return candidate;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        return 0;
    }

    bool LooksLikeLiveObject(uintptr_t object, bool expectsVtable = true)
    {
        if (!object || (object & 3) != 0)
            return false;
        __try
        {
            const auto* words =
                reinterpret_cast<const uintptr_t*>(object);
            if (expectsVtable)
                return words[0] >= kVtableLow && words[0] < kVtableHigh &&
                       (words[0] & 3) == 0;
            // No vtable to check, so look for the allocator's freed-block
            // fill instead.  It is weaker evidence, but it is exactly what
            // the stale inventory read was full of.
            for (unsigned i = 0; i < 8; ++i)
                if (words[i] == 0xBADBADBAu || words[i] == 0xBEEFBEEFu ||
                    words[i] == 0xFEEEFEEEu || words[i] == 0xDDDDDDDDu)
                    return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RecordEditorObject(const uintptr_t* candidates, unsigned classIndex)
    {
        if (!candidates || classIndex >= kEditorClassCount)
            return;
        bool any = false;
        for (unsigned i = 0; i < 4; ++i)
            any = any || candidates[i] != 0;
        if (!any)
            return;
        const LONG write = InterlockedIncrement(&gEditorWriteCursor) - 1;
        const unsigned index =
            static_cast<unsigned>(write) % kEditorRegistryCapacity;
        for (unsigned i = 0; i < 4; ++i)
            gEditorObjects[index].candidates[i] = candidates[i];
        gEditorObjects[index].classIndex = classIndex;
        gNewestEditorObject[classIndex] = index + 1;
        LONG count = InterlockedCompareExchange(&gEditorCount, 0, 0);
        while (count < static_cast<LONG>(kEditorRegistryCapacity))
        {
            const LONG previous =
                InterlockedCompareExchange(&gEditorCount, count + 1, count);
            if (previous == count)
                break;
            count = previous;
        }
    }

    template <unsigned Index>
    void EditorCtor_Hook(safetyhook::Context& ctx)
    {
        const uintptr_t candidates[4] = {ctx.ecx, ctx.edi, ctx.esi,
                                         ctx.ebp};
        RecordEditorObject(candidates, Index);
    }

    using EditorHookFn = void (*)(safetyhook::Context&);
    EditorHookFn EditorHookFor(unsigned slot)
    {
        static const EditorHookFn table[] = {
            &EditorCtor_Hook<0>,  &EditorCtor_Hook<1>,  &EditorCtor_Hook<2>,
            &EditorCtor_Hook<3>,  &EditorCtor_Hook<4>,  &EditorCtor_Hook<5>,
            &EditorCtor_Hook<6>,  &EditorCtor_Hook<7>,  &EditorCtor_Hook<8>,
            &EditorCtor_Hook<9>,  &EditorCtor_Hook<10>, &EditorCtor_Hook<11>,
            &EditorCtor_Hook<12>, &EditorCtor_Hook<13>,
            &EditorCtor_Hook<14>, &EditorCtor_Hook<15>,
            &EditorCtor_Hook<16>, &EditorCtor_Hook<17>,
            &EditorCtor_Hook<18>, &EditorCtor_Hook<19>,
        };
        // Bound by the table, not by the class count.  Those two were
        // the same until three classes were added and the table was
        // not: slots past its end returned whatever followed it in
        // memory, that got installed as a hook, and the game jumped
        // into it the moment a WeaponManager was constructed.
        constexpr unsigned handlers = sizeof(table) / sizeof(table[0]);
        static_assert(handlers >= kEditorClassCount,
                      "one hook handler per editor class");
        return slot < handlers ? table[slot] : nullptr;
    }

    void InstallWorldEditorHooks()
    {
        if (!gWorldEditor)
            return;
        unsigned installed = 0;
        for (unsigned i = 0; i < kEditorClassCount; ++i)
        {
            gEditorMatrixOffset[i] = -1;
            gEditorHooks[i] = safetyhook::create_mid(
                reinterpret_cast<void*>(kEditorClasses[i].ctor),
                EditorHookFor(i));
            if (gEditorHooks[i])
                ++installed;
            else
                Log("world editor: %s constructor NOT hooked at %08X",
                    kEditorClasses[i].name,
                    static_cast<unsigned>(kEditorClasses[i].ctor));
        }
        Log("world editor: %u of %u class constructors hooked",
            installed, kEditorClassCount);
    }

    // Reads a 4x4 out of an object and hands back axes and position in the
    // same twelve-float layout the placement readout already uses.
    bool EditorTransform(const EditorObject& entry, float* out12)
    {
        const uintptr_t object = ResolveEditorObject(entry);
        if (!object)
            return false;
        auto readMatrix = [&](int offset, float* rows) {
            return ReadProcessMemory(GetCurrentProcess(),
                                     reinterpret_cast<void*>(object + offset),
                                     rows, sizeof(float) * 16, nullptr) != FALSE;
        };
        auto plausible = [](const float* rows) {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const float* row = rows + axis * 4;
                const float length = row[0] * row[0] + row[1] * row[1] +
                                     row[2] * row[2];
                if (length < 0.90f || length > 1.10f)
                    return false;
            }
            const float* position = rows + 12;
            if (rows[15] < 0.99f || rows[15] > 1.01f)
                return false;
            return position[0] != 0.0f || position[1] != 0.0f ||
                   position[2] != 0.0f;
        };
        float rows[16]{};
        int offset = gEditorMatrixOffset[entry.classIndex];
        if (offset < 0)
        {
            for (int probe = 0; probe < 0x300; probe += 4)
            {
                if (!readMatrix(probe, rows) || !plausible(rows))
                    continue;
                offset = probe;
                gEditorMatrixOffset[entry.classIndex] = probe;
                Log("world editor: %s transform at object+0x%X",
                    kEditorClasses[entry.classIndex].name, probe);
                break;
            }
            if (offset < 0)
                return false;
        }
        else if (!readMatrix(offset, rows) || !plausible(rows))
            return false;

        for (unsigned axis = 0; axis < 3; ++axis)
        {
            out12[axis * 3 + 0] = rows[axis * 4 + 0];
            out12[axis * 3 + 1] = rows[axis * 4 + 1];
            out12[axis * 3 + 2] = rows[axis * 4 + 2];
        }
        out12[9] = rows[12];
        out12[10] = rows[13];
        out12[11] = rows[14];
        return true;
    }

    unsigned EditorRegistryCount()
    {
        const LONG count = InterlockedCompareExchange(&gEditorCount, 0, 0);
        return count < 0 ? 0u : static_cast<unsigned>(count);
    }

    // What is around you, nearest first.  Objects whose area has been unloaded
    // simply fail the transform read and drop out; the registry is a ring, so
    // stale entries are overwritten rather than pruned.
    void LogWorldEditorNearby()
    {
        float me[12]{};
        if (!PlayerTransform(me))
        {
            Log("world editor: no player transform yet");
            return;
        }
        struct Nearby { double distance; unsigned entry; float position[3]; };
        Nearby best[16]{};
        unsigned found = 0;
        const unsigned count = EditorRegistryCount();
        for (unsigned i = 0; i < count; ++i)
        {
            float where[12]{};
            if (!EditorTransform(gEditorObjects[i], where))
                continue;
            const double dx = where[9] - me[9];
            const double dy = where[10] - me[10];
            const double dz = where[11] - me[11];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > 40.0)
                continue;
            unsigned slot = found < 16 ? found : 16;
            while (slot > 0 && best[slot - 1].distance > distance)
                --slot;
            if (slot >= 16)
                continue;
            for (unsigned move = found < 16 ? found : 15; move > slot; --move)
                best[move] = best[move - 1];
            best[slot].distance = distance;
            best[slot].entry = i;
            best[slot].position[0] = where[9];
            best[slot].position[1] = where[10];
            best[slot].position[2] = where[11];
            if (found < 16)
                ++found;
        }
        Log("world editor: %u object(s) within 40 m, %u in the registry",
            found, count);
        for (unsigned i = 0; i < found; ++i)
        {
            const EditorObject& entry = gEditorObjects[best[i].entry];
            const uintptr_t object = ResolveEditorObject(entry);
            Guid128 guid{};
            ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(object + 0x20),
                              &guid, sizeof(guid), nullptr);
            Log("  %5.1f m  %-20s %08X-%08X-%08X-%08X  %.1f %.1f %.1f",
                best[i].distance, kEditorClasses[entry.classIndex].name,
                guid.value[0], guid.value[1], guid.value[2], guid.value[3],
                best[i].position[0], best[i].position[1], best[i].position[2]);
        }
    }

    // CharacterMovementComponent owns the authoritative position.  Writing
    // Player+0xC0 only changes the render transform until Havok publishes the
    // controller state again.  SetPosition updates both copies.
    constexpr uintptr_t kCharacterMovementUpdate = 0x004487D0;
    constexpr uintptr_t kCharacterMovementSetPosition = 0x0044E690;
    constexpr uintptr_t kCharacterMovementVtable = 0x00C60E04;
    constexpr uintptr_t kPlayerFallCheckTransition = 0x0053F230;
    constexpr unsigned kPlayerFallTimeoutState = 0x0B;
    constexpr unsigned kCharacterMovementOwner = 0x08;

    safetyhook::InlineHook gCharacterMovementUpdateHook;
    safetyhook::InlineHook gPlayerFallCheckTransitionHook;
    volatile LONG gNoClipActive = FALSE;
    volatile LONG gNoClipPlayer = 0;
    // The per-frame hook must never ask user32 for the keyboard.  This process
    // also installs a low-level keyboard hook for the F9 overlay, and that hook
    // runs on the thread that set it; a game thread blocking inside user32 for
    // the input state while that thread waits its turn is exactly how a build
    // ends up hung.  The background poller samples the keys instead and the
    // frame hook only reads these numbers.
    volatile LONG gNoClipInput = 0;
    enum : LONG
    {
        kNoClipForward = 1 << 0,
        kNoClipBack    = 1 << 1,
        kNoClipRight   = 1 << 2,
        kNoClipLeft    = 1 << 3,
        kNoClipUp      = 1 << 4,
        kNoClipDown    = 1 << 5,
        kNoClipFast    = 1 << 6,
    };

    bool __fastcall PlayerFallCheckTransition_Hook(
        void* stateMachine, void*, uint32_t first, uint32_t timeBits,
        uint32_t state, uint32_t last)
    {
        if (state == kPlayerFallTimeoutState &&
            InterlockedCompareExchange(&gNoClipActive, FALSE, FALSE) != FALSE)
            return false;

        return gPlayerFallCheckTransitionHook.thiscall<bool>(
            stateMachine, first, timeBits, state, last);
    }

    void SetMovementPosition(void* movement, float* position)
    {
        const uintptr_t setter = kCharacterMovementSetPosition;
        __asm
        {
            mov eax, movement
            mov edx, position
            call setter
        }
    }

    void __stdcall CharacterMovementUpdate_Hook(void* movement)
    {
        const int sizeSample = BeginVariantSizeSample(movement);
        gCharacterMovementUpdateHook.stdcall<void>(movement);
        EndVariantSizeSample(movement, sizeSample);

        static bool positioned = false;
        static float position[4]{};
        static ULONGLONG previousTick = 0;
        static ULONGLONG previousApply = 0;
        static uintptr_t cachedPlayer = 0;
        static void* cachedMovement = nullptr;

        if (InterlockedCompareExchange(&gNoClipActive, FALSE, FALSE) == FALSE)
        {
            positioned = false;
            previousTick = 0;
            previousApply = 0;
            cachedPlayer = 0;
            cachedMovement = nullptr;
            return;
        }

        const uintptr_t player = static_cast<uintptr_t>(
            InterlockedCompareExchange(&gNoClipPlayer, 0, 0));
        if (!player || !movement)
            return;

        if (player != cachedPlayer)
        {
            cachedPlayer = player;
            cachedMovement = nullptr;
            positioned = false;
            previousTick = 0;
            previousApply = 0;
        }

        if (!cachedMovement)
        {
            uintptr_t vtable = 0;
            uintptr_t owner = 0;
            if (!ReadPointer(reinterpret_cast<uintptr_t>(movement), &vtable) ||
                vtable != kCharacterMovementVtable ||
                !ReadPointer(reinterpret_cast<uintptr_t>(movement) +
                             kCharacterMovementOwner, &owner) || owner != player)
                return;
            cachedMovement = movement;
            Log("noclip: player movement resolved at %08X",
                static_cast<unsigned>(
                    reinterpret_cast<uintptr_t>(cachedMovement)));
        }

        if (movement != cachedMovement)
            return;

        float rows[16]{};
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(player + kPlayerMatrix),
                               rows, sizeof(rows), nullptr))
            return;

        const LONG input = InterlockedCompareExchange(&gNoClipInput, 0, 0);
        auto held = [input](LONG bit) { return (input & bit) != 0; };
        float forward = 0.0f;
        float right = 0.0f;
        float up = 0.0f;
        if (held(kNoClipForward)) forward += 1.0f;
        if (held(kNoClipBack))    forward -= 1.0f;
        if (held(kNoClipRight))   right += 1.0f;
        if (held(kNoClipLeft))    right -= 1.0f;
        if (held(kNoClipUp))      up += 1.0f;
        if (held(kNoClipDown))    up -= 1.0f;

        const ULONGLONG now = GetTickCount64();
        const bool moving = forward != 0.0f || right != 0.0f || up != 0.0f;
        if (!moving)
        {
            position[0] = rows[12];
            position[1] = rows[13];
            position[2] = rows[14];
            position[3] = 0.0f;
            positioned = true;
            previousTick = now;
            previousApply = 0;
            return;
        }

        if (!positioned)
        {
            position[0] = rows[12];
            position[1] = rows[13];
            position[2] = rows[14];
            position[3] = 0.0f;
            positioned = true;
            previousTick = now;
        }

        const float driftX = rows[12] - position[0];
        const float driftY = rows[13] - position[1];
        const float driftZ = rows[14] - position[2];
        if (driftX * driftX + driftY * driftY + driftZ * driftZ > 16.0f)
        {
            position[0] = rows[12];
            position[1] = rows[13];
            position[2] = rows[14];
            previousTick = now;
        }

        if (previousApply && now - previousApply < 16)
            return;

        const float elapsed = previousTick && now > previousTick ?
            static_cast<float>(now - previousTick) / 1000.0f : 0.0f;
        const float frameTime = elapsed < 0.05f ? elapsed : 0.05f;
        previousTick = now;
        previousApply = now;

        const float speed = gNoClipSpeed * 10.0f *
                            (held(kNoClipFast) ? 3.0f : 1.0f);
        for (unsigned axis = 0; axis < 3; ++axis)
            // The matrix X axis points to the player's left, not right -
            // measured in game, where the arrow keys moved the wrong way.
            // Z is forward and needs no sign.
            position[axis] += (rows[8 + axis] * forward -
                               rows[axis] * right) * speed * frameTime;
        position[1] += up * speed * frameTime;

        SetMovementPosition(movement, position);
    }

    void InstallMovementTeleportHook()
    {
        if (!gNoClip && !gVariantSizeDiagnostics)
            return;
        constexpr unsigned char expected[] = {
            0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0,
            0x81, 0xEC, 0xB4, 0x03, 0x00, 0x00,
        };
        if (std::memcmp(reinterpret_cast<const void*>(kCharacterMovementUpdate),
                        expected, sizeof(expected)) != 0)
        {
            Log("movement hook rejected: retail signature differs at %08X",
                static_cast<unsigned>(kCharacterMovementUpdate));
            return;
        }
        gCharacterMovementUpdateHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kCharacterMovementUpdate),
            reinterpret_cast<void*>(&CharacterMovementUpdate_Hook));
        if (gNoClip)
            gPlayerFallCheckTransitionHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kPlayerFallCheckTransition),
            reinterpret_cast<void*>(&PlayerFallCheckTransition_Hook));
        Log("noclip: CharacterMovement update hook %s at %08X, "
            "SetPosition=%08X",
            gCharacterMovementUpdateHook ? "active" : "NOT installed",
            static_cast<unsigned>(kCharacterMovementUpdate),
            static_cast<unsigned>(kCharacterMovementSetPosition));
        Log("noclip: fall timeout hook %s at %08X",
            gPlayerFallCheckTransitionHook ? "active" : "NOT installed",
            static_cast<unsigned>(kPlayerFallCheckTransition));
    }

    // ---------------------------------------------------------------------
    // Zone probe.  Ambience - fog, visibility, lighting - is authored per zone
    // and none of it carries progression state, so it is the one part of the
    // world that can be experimented with without risking a save.  The same
    // block is where the vacuum flag has to live, so whatever this teaches us
    // about one teaches us about the other.
    //
    // Read-only unless the INI names an offset.  Nothing here guesses what a
    // field means; it prints the numbers and lets a test decide.
    // ---------------------------------------------------------------------
    constexpr unsigned kZoneSphereOffset = 0x1D0;   // centre xyz, then radius
    constexpr unsigned kZoneProbeFirst = 0x100;
    constexpr unsigned kZoneProbeLast = 0x21C;

    unsigned ZoneRenderClassIndex()
    {
        for (unsigned i = 0; i < kEditorClassCount; ++i)
            if (std::strcmp(kEditorClasses[i].name, "ZoneRender") == 0)
                return i;
        return kEditorClassCount;
    }

    void LogZoneProbe()
    {
        float me[12]{};
        if (!PlayerTransform(me))
        {
            Log("zone probe: no player transform yet");
            return;
        }
        const unsigned zoneClass = ZoneRenderClassIndex();
        Log("zone probe: player at %.2f %.2f %.2f", me[9], me[10], me[11]);
        // "0 zone(s) known" says nothing about why.  Count the raw
        // registry entries of the class and show what the first few
        // actually start with, so a rejected zone can be told apart
        // from a zone that was never recorded.
        {
            unsigned raw = 0;
            unsigned shown = 0;
            const unsigned total = EditorRegistryCount();
            for (unsigned i = 0; i < total; ++i)
            {
                if (gEditorObjects[i].classIndex != zoneClass)
                    continue;
                const uintptr_t raw_object =
                    ResolveEditorObject(gEditorObjects[i]);
                if (!raw_object)
                    continue;
                ++raw;
                if (shown >= 4)
                    continue;
                ++shown;
                unsigned first = 0;
                __try
                {
                    first = *reinterpret_cast<const unsigned*>(raw_object);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    first = 0xDEADBEEFu;
                }
                Log("   raw zone %08X starts with %08X",
                    static_cast<unsigned>(raw_object),
                    first);
            }
            Log("zone probe: %u ZoneRender entr(ies) recorded of %u",
                raw, total);
        }

        unsigned seen = 0, inside = 0;
        const unsigned count = EditorRegistryCount();
        for (unsigned i = 0; i < count; ++i)
        {
            const EditorObject& entry = gEditorObjects[i];
            if (entry.classIndex != zoneClass)
                continue;
            const uintptr_t object = ResolveEditorObject(entry);
            if (!object)
                continue;
            float sphere[4]{};
            Guid128 guid{};
            if (!ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(object + kZoneSphereOffset),
                    sphere, sizeof(sphere), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(object + 0x20),
                    &guid, sizeof(guid), nullptr))
                continue;
            ++seen;
            const double dx = me[9] - sphere[0];
            const double dy = me[10] - sphere[1];
            const double dz = me[11] - sphere[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const bool contains = sphere[3] > 0.0f && distance <= sphere[3];
            if (!contains)
                continue;
            ++inside;
            Log("  zone %08X-%08X-%08X-%08X at %08X, centre %.1f %.1f %.1f "
                "radius %.1f, you are %.1f away",
                guid.value[0], guid.value[1], guid.value[2], guid.value[3],
                static_cast<unsigned>(object),
                sphere[0], sphere[1], sphere[2], sphere[3], distance);
            for (unsigned off = kZoneProbeFirst; off <= kZoneProbeLast;
                 off += 4)
            {
                unsigned raw = 0;
                if (!ReadProcessMemory(GetCurrentProcess(),
                        reinterpret_cast<void*>(object + off),
                        &raw, sizeof(raw), nullptr))
                    continue;
                float value = 0.0f;
                std::memcpy(&value, &raw, sizeof(value));
                const bool floaty = value > 1e-3f && value < 1e6f;
                if (!raw)
                    continue;
                if (floaty)
                    Log("     +0x%03X  %08X   %.4f", off, raw, value);
                else
                    Log("     +0x%03X  %08X", off, raw);
            }
            if (gZoneProbeOffset && gZoneProbeOffset >= kZoneProbeFirst &&
                gZoneProbeOffset <= kZoneProbeLast)
            {
                SIZE_T written = 0;
                if (WriteProcessMemory(GetCurrentProcess(),
                        reinterpret_cast<void*>(object + gZoneProbeOffset),
                        &gZoneProbeValue, sizeof(gZoneProbeValue), &written))
                    Log("     wrote %08X to +0x%03X of this zone",
                        gZoneProbeValue, gZoneProbeOffset);
            }
        }
        Log("zone probe: %u zone(s) known, %u contain you", seen, inside);
    }

    void PollNoClip()
    {
        if (!gNoClip || !gNoClipHotkey)
            return;
        const bool toggle = (GetAsyncKeyState(gNoClipHotkey) & 0x8000) != 0;
        static bool wasToggle = false;
        if (toggle && !wasToggle)
        {
            const LONG active =
                InterlockedCompareExchange(&gNoClipActive, FALSE, FALSE);
            InterlockedExchange(&gNoClipActive, active ? FALSE : TRUE);
            Log("noclip: %s", active ? "off" : "on");
        }
        wasToggle = toggle;

        // Sampled here, on the background thread, and handed to the frame hook
        // as a single value.
        auto down = [](int key) {
            return (GetAsyncKeyState(key) & 0x8000) != 0;
        };
        LONG input = 0;
        if (down(VK_UP))    input |= kNoClipForward;
        if (down(VK_DOWN))  input |= kNoClipBack;
        if (down(VK_RIGHT)) input |= kNoClipRight;
        if (down(VK_LEFT))  input |= kNoClipLeft;
        if (down(VK_PRIOR)) input |= kNoClipUp;
        if (down(VK_NEXT))  input |= kNoClipDown;
        if (down(VK_SHIFT)) input |= kNoClipFast;
        InterlockedExchange(&gNoClipInput, input);

        const LONG active =
            InterlockedCompareExchange(&gNoClipActive, FALSE, FALSE);
        const uintptr_t player = active ? PlayerObject() : 0;
        InterlockedExchange(&gNoClipPlayer, static_cast<LONG>(player));
    }
