    // A bounds check the game is missing.
    //
    // Two WER full dumps of the same crash show the same call chain:
    //
    //   0052A9C0 -> 0050A0B0 -> 0050A6B0 -> 0050AE00 -> 0042C090
    //
    // 0x0050A0B0 removes an entry from a small registry by swapping it with the
    // last one. The registry is four inline slots of twelve bytes at +4..+0x34 of
    // its owner (the constructor at 0x00509340 pushes 0xC and 4), and the owner's
    // own slot index sits at +0x38. Three such registries live inline in a larger
    // object at +0xC9C, +0xCF0 and +0xD44; their field descriptor at 0x00C6F128
    // names them "m_handle", "TAG", "r_weapon".
    //
    // The constructor never writes +0x38 (it clears +0x34 and +0x3C..+0x4C), so an
    // owner torn down before it got a slot carries heap garbage there. Both dumps
    // agree on every other field (+0x34 = 4, +0x44 = 8, +0x4C = 2) while +0x38
    // reads 0xD5D5D5D5 in one and 0x080028E0 in the other. The removal at
    // 0x0050A311 only checks whether the two indices are equal, so a garbage index
    // times twelve becomes a pointer half a gigabyte away. The matching assignment
    // 0x0050A390 does refuse a negative index before writing +0x38.
    //
    // The hook sits on that compare and makes an impossible index equal to the
    // other one, so the game's own `je` skips the swap. Nothing is written and an
    // owner that was never registered is just not removed.
    //
    // A real index is 0..3. The first version only refused indices from 256 up and
    // the crash came back with it installed. The swap (0x0050A6B0) addresses both
    // indices as slots of the same registry at edx + 4 + index * 12, so anything at
    // or above the slot count (+0x34, 4 in every dump) writes past the slots. Both
    // indices are now held to the count, or to four when +0x34 does not look like one.
    constexpr uintptr_t kSlotSwapCompare = 0x0050A311;
    constexpr uintptr_t kSlotCount = 0x34;
    constexpr unsigned kSlotCountFallback = 4;

    safetyhook::MidHook gSlotSwapHook;
    unsigned gSlotSwapsBlocked = 0;
    bool gSlotListGuard = true;

    void SlotSwapCompare_Hook(SafetyHookContext& ctx)
    {
        // ecx is the owner's stored slot index, eax the one it would swap with,
        // edx the registry (its index at +0x38 was just read). The hook stands on
        // the compare that decides the swap.
        unsigned count = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<const void*>(ctx.edx + kSlotCount),
                               &count, sizeof(count), nullptr) ||
            count == 0 || count > 16)
            count = kSlotCountFallback;
        if ((ctx.ecx < count && ctx.eax < count) || ctx.ecx == ctx.eax)
            return;
        // Read before overwriting: these values are the whole evidence.
        const unsigned bad = static_cast<unsigned>(ctx.ecx);
        const unsigned other = static_cast<unsigned>(ctx.eax);
        ctx.ecx = ctx.eax;
        ++gSlotSwapsBlocked;
        if (gSlotSwapsBlocked <= 8)
            Log("slot guard: refused a swap of slot %08X with slot %08X in registry %08X "
                "of %u slots (#%u) - the owner was never given a slot", bad, other,
                static_cast<unsigned>(ctx.edx), count, gSlotSwapsBlocked);
    }

    void LoadSlotListSettings()
    {
        gSlotListGuard = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "SlotListGuard", 1, gIniPath) != 0;
    }

    void InstallSlotListGuard()
    {
        if (!gSlotListGuard)
            return;
        gSlotSwapHook = VerifiedMid(
            reinterpret_cast<void*>(kSlotSwapCompare), SlotSwapCompare_Hook);
        Log("slot guard: %s at %08X",
            gSlotSwapHook ? "active" : "NOT installed",
            static_cast<unsigned>(kSlotSwapCompare));
    }
