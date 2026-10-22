    // =====================================================================
    // Limb randomizer.
    //
    // DismemberableHealthComponent (class hash 0x17C7DFBF, decoder at
    // 0x004507A0, dispatch table 0x00450FCC) holds three parallel float
    // vectors with room for ten entries each.  The index is a body part -
    // the damage path at 0x00451BD1 bounds it with `cmp ebp, 0xa` and takes
    // it from a bone lookup - and the three vectors are:
    //
    //   +0x2E4  what the part has left, runtime only, count at +0x30C
    //   +0x310  what it takes to sever the part, count at +0x338
    //   +0x33C  health the body loses when the part comes off, count at +0x364
    //
    // The insert routine at 0x00450580 writes the same value into the first
    // two, so +0x310 is the authored maximum and +0x2E4 the running value.
    // At 0x00451BEA the third is compared against total health at +0x120 and
    // subtracted, which is what pins its meaning.
    //
    // A stock Slasher authors 10 10 10 15 15 2 10 10 10 10: one part gives
    // way after almost no damage, and that is the weak spot every player
    // learns.  Moving it is the randomization.
    //
    // Safety: bosses, story characters and props use the same component with
    // values like 1000, 10000 or 1000000, and Kendra carries a flat 100.
    // Rewriting those could make a scripted encounter unwinnable or a story
    // character dismemberable, so this does not work from a range check.  It
    // works from a positive list: a component is only touched when its ten
    // values exactly match one of the ordinary profiles measured across all
    // 794 archives.  Anything unrecognised is left exactly as authored.
    // =====================================================================
    constexpr uintptr_t kDismemberDecode = 0x004507A0;
    constexpr unsigned kLimbSeverDamage = 0x310;
    // Each of the three vectors keeps its own length right behind it, and the
    // insert routine at 0x00450593 reads this one and refuses to grow past
    // ten.  It matters: a Slasher authors ten sever values but only five
    // costs, so rotating all ten of the second list would push zeroes into
    // the entries the game actually uses.
    constexpr unsigned kLimbSeverDamageCount = 0x338;
    constexpr unsigned kLimbSeverCost = 0x33C;
    constexpr unsigned kLimbSeverCostCount = 0x364;
    constexpr unsigned kLimbCount = 10;
    constexpr unsigned kLimbLogLimit = 12;

    struct LimbProfile
    {
        int part[kLimbCount];
    };

    // Every distinct ordinary profile in the game, most common first.  The
    // comment is how many components carried it.  Kendra's flat 100 profile
    // is deliberately absent so she is never matched, and every profile above
    // 120 - all of them bosses and props - is absent for the same reason.
    constexpr LimbProfile kLimbProfiles[] = {
        {{2, 2, 2, 2, 2, 10, 10, 10, 10, 10}},        // 293
        {{10, 10, 10, 15, 15, 2, 10, 10, 10, 10}},    // 280, the Slasher
        {{10, 10, 10, 10, 10, 10, 10, 10, 10, 10}},   // 223, no weak spot
        {{1, 1, 1, 1, 1, 1, 1, 10, 10, 10}},          // 108
        {{10, 10, 10, 2, 2, 10, 10, 10, 10, 10}},     // 87
        {{30, 30, 30, 30, 30, 2, 10, 10, 10, 10}},    // 75
        {{10, 10, 10, 10, 10, 10, 15, 10, 10, 10}},   // 59
        {{15, 15, 15, 20, 20, 2, 10, 10, 10, 10}},    // 51
        {{1, 1, 1, 1, 1, 1, 1, 20, 20, 20}},          // 35
        {{10, 20, 20, 15, 15, 8, 10, 10, 10, 10}},    // 32
        {{2, 2, 2, 2, 2, 2, 2, 10, 10, 10}},          // 26
        {{15, 25, 25, 2, 2, 25, 10, 10, 10, 10}},     // 25
        {{20, 20, 20, 25, 25, 10, 10, 10, 10, 10}},   // 22
        {{20, 10, 10, 10, 10, 10, 10, 10, 10, 10}},   // 20
        {{40, 40, 40, 50, 50, 40, 40, 40, 40, 40}},   // 8
        {{10, 10, 10, 40, 40, 2, 10, 10, 10, 10}},    // 6
        {{30, 120, 120, 120, 120, 10, 10, 10, 10, 10}},  // 4, the toughest
        {{30, 60, 60, 60, 60, 10, 10, 10, 10, 10}},   // 3
        {{1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},             // 2, falls apart
        {{10, 10, 10, 10, 10, 10, 1, 10, 10, 10}},    // 1, the Exploder
    };
    constexpr unsigned kLimbProfileCount =
        sizeof(kLimbProfiles) / sizeof(kLimbProfiles[0]);
    static_assert(kLimbProfileCount >= 2,
                  "a pool of one profile would swap every enemy onto itself");

    safetyhook::InlineHook gDismemberDecodeHook;
    bool gLimbWeakSpotRandomizer = false;
    bool gLimbProfilePool = false;
    bool gLimbSeverCostRandomizer = false;
    unsigned gLimbComponentsSeen = 0;

    // Authored limb values are whole numbers in every profile measured, so a
    // component carrying anything else is not one of ours and is skipped
    // rather than rounded into a match.
    bool LimbValueAsInt(float value, int& out)
    {
        const float rounded = static_cast<float>(static_cast<int>(
            value < 0.0f ? value - 0.5f : value + 0.5f));
        if (value < 0.5f || value > 200.0f)
            return false;
        if (rounded - value > 0.01f || value - rounded > 0.01f)
            return false;
        out = static_cast<int>(rounded);
        return true;
    }

    int MatchLimbProfile(const int* values)
    {
        for (unsigned p = 0; p < kLimbProfileCount; ++p)
        {
            bool same = true;
            for (unsigned i = 0; i < kLimbCount && same; ++i)
                same = kLimbProfiles[p].part[i] == values[i];
            if (same)
                return static_cast<int>(p);
        }
        return -1;
    }

    uint32_t MixLimbKey(int profile, uint32_t salt)
    {
        uint32_t mixed = gStartingSuitSeed ^ salt;
        mixed = (mixed ^ static_cast<uint32_t>(profile)) * 0x01000193u;
        mixed ^= mixed >> 16;
        mixed *= 0x7FEB352Du;
        mixed ^= mixed >> 15;
        return mixed;
    }

    // Rotating a vector in place, honouring the length the object carries.
    // Same numbers, different body parts - the enemy is never made tougher or
    // weaker overall, only differently shaped.
    bool RotateLimbVector(uintptr_t object, unsigned valueOffset,
                          unsigned countOffset, unsigned step,
                          float* out, unsigned& length)
    {
        float values[kLimbCount]{};
        int count = 0;
        __try
        {
            std::memcpy(values,
                        reinterpret_cast<const void*>(object + valueOffset),
                        sizeof(values));
            std::memcpy(&count,
                        reinterpret_cast<const void*>(object + countOffset),
                        sizeof(count));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (count < 2 || count > static_cast<int>(kLimbCount))
            return false;
        length = static_cast<unsigned>(count);
        const unsigned turn = 1 + (step % (length - 1));
        for (unsigned i = 0; i < kLimbCount; ++i)
            out[i] = values[i];
        for (unsigned i = 0; i < length; ++i)
            out[i] = values[(i + turn) % length];
        return true;
    }

    void ApplyLimbSeverCost(uintptr_t object, int profile)
    {
        if (!gLimbSeverCostRandomizer)
            return;
        float replacement[kLimbCount]{};
        unsigned length = 0;
        const unsigned step = MixLimbKey(profile, 0x434F5354u);
        if (!RotateLimbVector(object, kLimbSeverCost, kLimbSeverCostCount,
                              step, replacement, length))
            return;
        SIZE_T written = 0;
        if (!WriteProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<void*>(object + kLimbSeverCost),
                                replacement, sizeof(replacement), &written))
            return;
        if (gProgressionDiagnostics && gLimbComponentsSeen < kLimbLogLimit)
            Log("limb randomizer %08X: %u sever costs rotated -> "
                "%.0f %.0f %.0f %.0f %.0f", static_cast<unsigned>(object),
                length, replacement[0], replacement[1], replacement[2],
                replacement[3], replacement[4]);
    }

    void ApplyLimbProfile(void* self)
    {
        if ((!gLimbWeakSpotRandomizer && !gLimbProfilePool &&
             !gLimbSeverCostRandomizer) || !self)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);
        const uintptr_t base = object + kLimbSeverDamage;

        float raw[kLimbCount]{};
        int severCount = 0;
        __try
        {
            std::memcpy(raw, reinterpret_cast<const void*>(base), sizeof(raw));
            std::memcpy(&severCount,
                        reinterpret_cast<const void*>(
                            object + kLimbSeverDamageCount),
                        sizeof(severCount));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("limb randomizer: %08X unreadable", static_cast<unsigned>(object));
            return;
        }

        // Every profile in the list authors all ten, so a shorter vector is
        // not one of ours no matter what the values look like.
        if (severCount != static_cast<int>(kLimbCount))
        {
            ++gLimbComponentsSeen;
            return;
        }

        int values[kLimbCount]{};
        for (unsigned i = 0; i < kLimbCount; ++i)
        {
            if (LimbValueAsInt(raw[i], values[i]))
                continue;
            if (gLimbComponentsSeen < kLimbLogLimit)
                Log("limb randomizer %08X: value %u is %.2f, not an ordinary "
                    "profile - left alone",
                    static_cast<unsigned>(object), i, raw[i]);
            ++gLimbComponentsSeen;
            return;
        }

        const int profile = MatchLimbProfile(values);
        if (profile < 0)
        {
            // Bosses, story characters and props land here, which is the
            // point, so this is only worth reporting while diagnosing.
            if (gProgressionDiagnostics && gLimbComponentsSeen < kLimbLogLimit)
                Log("limb randomizer %08X: %d %d %d %d %d %d %d %d %d %d is "
                    "not a known profile - left alone",
                    static_cast<unsigned>(object), values[0], values[1],
                    values[2], values[3], values[4], values[5], values[6],
                    values[7], values[8], values[9]);
            ++gLimbComponentsSeen;
            return;
        }

        ApplyLimbSeverCost(object, profile);

        if (!gLimbWeakSpotRandomizer && !gLimbProfilePool)
        {
            ++gLimbComponentsSeen;
            return;
        }

        float replacement[kLimbCount]{};
        const char* what = "";
        if (gLimbProfilePool)
        {
            // A different species' profile.  Keyed on the profile itself, so
            // every enemy of a kind gets the same substitute for the whole
            // run and the player can actually learn the new weak spot.
            const unsigned step =
                1 + (MixLimbKey(profile, 0x4C494D42u) % (kLimbProfileCount - 1));
            const unsigned pick =
                (static_cast<unsigned>(profile) + step) % kLimbProfileCount;
            for (unsigned i = 0; i < kLimbCount; ++i)
                replacement[i] = static_cast<float>(kLimbProfiles[pick].part[i]);
            what = "given a foreign profile";
        }
        else
        {
            // The enemy keeps its own numbers; only their order changes, so
            // the weak spot moves to a different body part without making the
            // enemy tougher or weaker overall.
            const unsigned step =
                1 + (MixLimbKey(profile, 0x57454B53u) % (kLimbCount - 1));
            for (unsigned i = 0; i < kLimbCount; ++i)
                replacement[i] =
                    static_cast<float>(values[(i + step) % kLimbCount]);
            what = "weak spot moved";
        }

        SIZE_T written = 0;
        if (!WriteProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<void*>(base),
                                replacement, sizeof(replacement), &written))
        {
            Log("limb randomizer %08X: write failed", static_cast<unsigned>(object));
            return;
        }

        if (gProgressionDiagnostics && gLimbComponentsSeen < kLimbLogLimit)
            Log("limb randomizer %08X: profile %d %s -> "
                "%.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f",
                static_cast<unsigned>(object), profile, what,
                replacement[0], replacement[1], replacement[2], replacement[3],
                replacement[4], replacement[5], replacement[6], replacement[7],
                replacement[8], replacement[9]);
        ++gLimbComponentsSeen;
        if (gLimbComponentsSeen == kLimbLogLimit)
            Log("limb randomizer: further enemies changed without logging");
    }

    void __fastcall DismemberDecode_Hook(void* self, void*, void* attributes)
    {
        gDismemberDecodeHook.thiscall<void>(self, attributes);
        ApplyLimbProfile(self);
    }


    // =====================================================================
    // Oxygen.
    //
    // PlayerAir (class hash 0x7C6FDD49, decoder at 0x005343C0, dispatch table
    // 0x00534570) has five properties, and property 2 at +0x6C is the whole
    // air budget in seconds.  The shipped value is 60, authored once in
    // global_assets, which is why every vacuum section in the game feels the
    // same on a replay.  +0x74 beside it is the fraction at which the suit
    // starts warning, authored 0.25.
    //
    // The running value lives at +0x70 and is refilled by 0x00534590; this
    // only touches the budget, so refills and cannisters keep working.
    //
    // Worth randomizing because it costs one number and changes every vacuum
    // stretch at once, including the rooms the vacuum hook empties itself.
    // =====================================================================
    constexpr uintptr_t kPlayerAirDecode = 0x005343C0;
    constexpr unsigned kPlayerAirBudget = 0x6C;
    // Below about twenty seconds the longer authored sections stop being
    // survivable at all, and past two minutes the timer stops mattering.
    constexpr float kAirBudgetLow = 25.0f;
    constexpr float kAirBudgetHigh = 120.0f;

    safetyhook::InlineHook gPlayerAirDecodeHook;
    bool gOxygenRandomizer = false;
    float gOxygenBudget = 0.0f;
    bool gOxygenReported = false;

    void ApplyPlayerAir(void* self)
    {
        if (!gOxygenRandomizer || !self || gOxygenBudget <= 0.0f)
            return;
        const uintptr_t slot =
            reinterpret_cast<uintptr_t>(self) + kPlayerAirBudget;
        float authored = 0.0f;
        __try
        {
            std::memcpy(&authored, reinterpret_cast<const void*>(slot),
                        sizeof(authored));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("oxygen: %08X unreadable",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)));
            return;
        }
        // A budget outside anything the game authors is not the field this
        // was measured on, so leave it alone rather than guess.
        if (authored < 1.0f || authored > 600.0f)
            return;
        SIZE_T written = 0;
        if (!WriteProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<void*>(slot),
                                &gOxygenBudget, sizeof(gOxygenBudget),
                                &written))
            return;
        if (!gOxygenReported)
        {
            gOxygenReported = true;
            Log("oxygen: budget %.0f seconds -> %.0f seconds", authored,
                gOxygenBudget);
        }
    }

    void __fastcall PlayerAirDecode_Hook(void* self, void*, void* attributes)
    {
        gPlayerAirDecodeHook.thiscall<void>(self, attributes);
        ApplyPlayerAir(self);
    }

    void LoadLimbSettings()
    {
        gLimbWeakSpotRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LimbWeakSpotRandomizer", 0, gIniPath) != 0;
        gLimbProfilePool = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LimbProfilePool", 0, gIniPath) != 0;
        gLimbSeverCostRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LimbSeverCostRandomizer", 0, gIniPath) != 0;
        gOxygenRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "OxygenRandomizer", 0, gIniPath) != 0;
        if (gOxygenRandomizer)
        {
            // One budget for the whole run, drawn from the same seed as
            // everything else, so a run has a consistent feel and the player
            // can learn how far their air actually goes.
            uint32_t mixed = gStartingSuitSeed ^ 0x4149524Du;
            mixed ^= mixed >> 16;
            mixed *= 0x7FEB352Du;
            mixed ^= mixed >> 15;
            const unsigned span =
                static_cast<unsigned>(kAirBudgetHigh - kAirBudgetLow) + 1u;
            gOxygenBudget = kAirBudgetLow +
                static_cast<float>(mixed % span);
            Log("oxygen randomizer: this run gets %.0f seconds of air",
                gOxygenBudget);
        }
    }

    void InstallLimbHooks()
    {
        if (gOxygenRandomizer)
        {
            gPlayerAirDecodeHook = safetyhook::create_inline(
                reinterpret_cast<void*>(kPlayerAirDecode),
                reinterpret_cast<void*>(&PlayerAirDecode_Hook));
            Log("oxygen: decoder %s at %08X",
                gPlayerAirDecodeHook ? "active" : "NOT installed",
                static_cast<unsigned>(kPlayerAirDecode));
        }
        if (!gLimbWeakSpotRandomizer && !gLimbProfilePool &&
            !gLimbSeverCostRandomizer)
            return;
        gDismemberDecodeHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kDismemberDecode),
            reinterpret_cast<void*>(&DismemberDecode_Hook));
        Log("limb randomizer: decoder %s at %08X (%s, sever costs %s, "
            "%u known profiles)",
            gDismemberDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kDismemberDecode),
            gLimbProfilePool ? "foreign profiles" :
                (gLimbWeakSpotRandomizer ? "weak spot moved" : "limbs untouched"),
            gLimbSeverCostRandomizer ? "rotated" : "untouched",
            kLimbProfileCount);
    }
