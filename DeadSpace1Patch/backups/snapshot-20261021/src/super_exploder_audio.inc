// Give the authored Super Exploder its private event namespace.

    constexpr uintptr_t kEntityPlaySoundHash = 0x00730100;
    constexpr uintptr_t kExploderVtable = 0x00C69580;

    struct SuperExploderEvent
    {
        uint32_t base;
        uint32_t privateId;
        const char* name;
    };

    constexpr SuperExploderEvent kSuperExploderEvents[] = {
        {0x588C55E5u, 0xDCFCF214u, "footstep"},
        {0x85894791u, 0x6A959CA2u, "head clack"},
        {0x57070BC7u, 0xB6BF6276u, "hit react"},
        {0x230CF779u, 0x4805360Au, "walk"},
        {0x8194F79Eu, 0xA0C557CDu, "short mutter"},
        {0x639954DEu, 0xBAE2F8EFu, "long mutter"},
        {0x148A6258u, 0x0CAA6869u, "attack"},
        {0x230BD084u, 0x48040F15u, "idle"},
    };

    safetyhook::InlineHook gEntityPlaySoundHashHook;
    volatile LONG gSuperExploderAudioSeen = 0;
    volatile LONG gSuperExploderAudioMapped = 0;
    volatile LONG gSuperExploderAudioEventMask = 0;

    bool IsSuperExploderAudioOwner(void* self)
    {
        if (!self || !gEnhancedExploders)
            return false;
        __try
        {
            const uintptr_t object = reinterpret_cast<uintptr_t>(self);
            if (*reinterpret_cast<const uintptr_t*>(object) != kExploderVtable)
                return false;
            const char* model = *reinterpret_cast<const char* const*>(
                object + kEntityModelName);
            return model && std::memcmp(model, "sup_expl", 9) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    uint32_t PrivateSuperExploderEvent(uint32_t eventId, unsigned* index)
    {
        for (unsigned i = 0; i < _countof(kSuperExploderEvents); ++i)
        {
            if (kSuperExploderEvents[i].base != eventId)
                continue;
            if (index)
                *index = i;
            return kSuperExploderEvents[i].privateId;
        }
        return eventId;
    }

    uint32_t __fastcall EntityPlaySoundHash_Hook(
        void* self, void*, uint32_t eventId, void* options)
    {
        const uint32_t originalId = eventId;
        unsigned mappedIndex = 0;
        bool report = false;
        if (IsSuperExploderAudioOwner(self))
        {
            InterlockedIncrement(&gSuperExploderAudioSeen);
            unsigned index = 0;
            const uint32_t privateId =
                PrivateSuperExploderEvent(eventId, &index);
            if (privateId != eventId)
            {
                InterlockedIncrement(&gSuperExploderAudioMapped);
                const LONG bit = 1L << index;
                const LONG previous =
                    InterlockedOr(&gSuperExploderAudioEventMask, bit);
                report = !(previous & bit);
                mappedIndex = index;
                eventId = privateId;
            }
        }
        const uint32_t playback = gEntityPlaySoundHashHook.thiscall<uint32_t>(
            self, eventId, options);
        if (report)
            Log("super exploder audio: %s %08X -> %08X object=%08X playback=%08X (%s)",
                kSuperExploderEvents[mappedIndex].name, originalId, eventId,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)),
                playback, playback ? "accepted" : "not started");
        return playback;
    }

    void InstallSuperExploderAudioHook()
    {
        constexpr unsigned char expected[] = {
            0x83, 0xEC, 0x20, 0x53, 0x55, 0x8B, 0x6C, 0x24,
            0x2C, 0x33, 0xDB, 0x33, 0xC0, 0x3B, 0xEB, 0x57,
            0x8B, 0xF9,
        };
        if (std::memcmp(reinterpret_cast<const void*>(kEntityPlaySoundHash),
                        expected, sizeof(expected)) != 0)
        {
            Log("super exploder audio hook rejected: retail bytes differ at %08X",
                static_cast<unsigned>(kEntityPlaySoundHash));
            return;
        }
        gEntityPlaySoundHashHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kEntityPlaySoundHash),
            reinterpret_cast<void*>(&EntityPlaySoundHash_Hook));
        Log("super exploder audio hook %s at %08X, 8 private events",
            gEntityPlaySoundHashHook ? "active" : "NOT installed",
            static_cast<unsigned>(kEntityPlaySoundHash));
    }
