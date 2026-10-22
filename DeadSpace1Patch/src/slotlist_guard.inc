    // =====================================================================
    // A bounds check the game forgot.
    //
    // Found by reading two WER full dumps of the same crash.  The call chain
    // is always identical:
    //
    //   0052A9C0 -> 0050A0B0 -> 0050A6B0 -> 0050AE00 -> 0042C090
    //
    // 0x0050A0B0 removes an entry from a small registry by swapping it with
    // the last one.  The registry is four inline slots of twelve bytes at
    // +4..+0x34 of its owner - the constructor at 0x00509340 builds exactly
    // four (`push 0xC` for the stride, `push 4` for the count) - and the
    // owner's own slot index sits at +0x38.  Three such registries live
    // inline in a larger object at +0xC9C, +0xCF0 and +0xD44; their field
    // descriptor at 0x00C6F128 names them "m_handle", "TAG", "r_weapon".
    //
    // Two things go wrong together:
    //
    //   * The constructor never writes +0x38.  It clears +0x34 and +0x3C
    //     through +0x4C and skips the field between them, so an owner that is
    //     torn down before it was ever given a slot carries whatever the heap
    //     left there.  Both dumps show exactly that: every other field is
    //     identical and sane (+0x34 = 4, +0x44 = 8, +0x4C = 2) while +0x38
    //     reads 0xD5D5D5D5 in one and 0x080028E0 in the other.
    //   * The removal at 0x0050A311 only checks whether the two indices are
    //     equal.  There is no range check, so a garbage index is multiplied
    //     by twelve and turned into a pointer half a gigabyte away.
    //
    // 0x0050A390, the matching assignment, refuses a negative index and only
    // then writes +0x38 - so the game does have the notion of "no slot", it
    // just never puts it there to begin with.
    //
    // The fix rides the check the game already has.  Sitting on the compare,
    // an impossible index is made equal to the other one, and the game's own
    // `je` skips the swap.  Nothing is written, no control flow is redirected,
    // and an owner that was never registered is simply not removed - which is
    // the correct outcome.
    //
    // A real index is 0..3. The first guard (09-08) only refused indices from
    // 256 up; the crash came back on 09-13 with it installed. The swap itself
    // (0x0050A6B0) addresses BOTH indices as slots of the same registry at
    // edx + 4 + index * 12, so any index at or above the registry's slot count
    // (+0x34, 4 in every dump) writes past its four slots. Heap leftovers below
    // 256 (a stray 5 or 8) slipped through; now both indices are held to the
    // count, falling back to four when +0x34 does not look like one.
    // =====================================================================
    constexpr uintptr_t kSlotSwapCompare = 0x0050A311;
    constexpr uintptr_t kSlotCount = 0x34;
    constexpr unsigned kSlotCountFallback = 4;

    safetyhook::MidHook gSlotSwapHook;
    unsigned gSlotSwapsBlocked = 0;
    bool gSlotListGuard = true;

    void SlotSwapCompare_Hook(SafetyHookContext& ctx)
    {
        // ecx is the owner's stored slot index, eax the one it would trade
        // places with.  Both have just been loaded; the compare that decides
        // whether to swap is the instruction this hook stands on.
        // edx is the registry: the game has just read its index at +0x38.
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
            "DeadSpace1Patch", "SlotListGuard", 1, gIniPath) != 0;
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
