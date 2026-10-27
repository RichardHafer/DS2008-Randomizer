    // Ammo and credit amounts ([DeadSpace2008Randomizer] LootAmmoPercent and
    // LootCreditsPercent, 100 = as authored; DS2's [Loot] AmmoPercent and
    // CreditsPercent). analysis/DeadSpace1_PickupAmounts_2026-10-21.md.
    //
    // Ammo: PickupItem+0x318, authored by PickupItem[4] (Plasma Cutter 6, Line Gun
    // 2, Contact Beam 1, ...). Every world or dropped ammo pickup passes the
    // difficulty scaling 0x00513A50 once (x1.5 on Easy), which sets the done bit
    // 0x40 at +0x2FC on its last instruction; the hook sits on that instruction.
    // Kiosk items and owned inventory never get there.
    // Credits: PowerUp+0x64C, rolled between PowerUp[8] and [9] in hundreds by
    // 0x00515750 at every decode; the hook sits on the store. Kiosk buying and
    // selling never call it.
    // The fraction is rounded up or down from the seed per pickup (its GUID at
    // +0x20), so 75% of the Force Gun's 3 rounds averages 2.25 and a placed pickup
    // gives the same in every run of a seed. Nothing goes below 1 round or 100
    // credits, since a pickup worth 0 vanishes empty.
    //
    // LootAutoScale=1 derives both percentages from the More enemies counts
    // (EnemyType_<class>, else SpawnsPerTrigger): each class weighs by how often
    // the campaign spawns it and how much ammo one costs, so doubling the Slashers
    // counts for far more than doubling the Brutes. Only half of that extra is
    // given, since the added enemies drop loot themselves.
    constexpr uintptr_t kAmmoAmountSite = 0x00513AA0;    // or dword [ecx+2FCh],40h
    constexpr uintptr_t kCreditAmountSite = 0x005157AE;  // mov [esi+64Ch],ecx
    constexpr uintptr_t kAmmoScaleReturn = 0x005162E3;
    constexpr uint32_t kSpareAmmoVtable = 0x00C700A0;
    constexpr uint32_t kPowerUpVtable = 0x00C6FAA0;

    struct AmmoKind
    {
        uint32_t hash;   // SpareAmmo[0] at +0x630
        const char* name;
    };
    constexpr AmmoKind kAmmoKinds[] = {
        {0xD0B167F2u, "Plasma Cutter Battery"}, {0x8C042160u, "Pulse Rifle Bullet"},
        {0x646C309Bu, "Line Gun Rack"}, {0xC0A48795u, "Ripper Ammo"},
        {0xE3964211u, "Flamethrower Fuel"}, {0x86BFB146u, "Force Gun Ammo"},
        {0x0A2F5F9Eu, "Contact Beam Ammo"},
    };
    constexpr unsigned kAmmoKindCount = sizeof(kAmmoKinds) / sizeof(kAmmoKinds[0]);

    unsigned gLootAmmoPercent = 100;
    unsigned gLootCreditsPercent = 100;
    bool gLootAutoScale = false;

    // Authored spawners per class (the 455 reviewed sources; Swarm and Divider counted by hand) and the ammo
    // one enemy costs next to a Slasher. Estimates: they decide the shares, not exact amounts.
    struct LootScaleClass
    {
        const char* name;
        unsigned spawners;
        unsigned cost;   // tenths of a Slasher
    };
    constexpr LootScaleClass kLootScaleClasses[] = {
        {"Zombie", 232, 10}, {"Lurker", 72, 8}, {"Leaper", 64, 9}, {"Exploder", 46, 5}, {"Pregnant", 23, 15},
        {"Infector", 8, 7}, {"Brute", 5, 60}, {"SwarmEnemy", 6, 2}, {"DividerCore", 4, 25},
    };

    // What the More enemies counts add up to, in per cent of the authored fights.
    unsigned LootAutoPercent(char* detail, size_t detailSize)
    {
        const unsigned fallback = static_cast<unsigned>(
            GetPrivateProfileIntA("DeadSpace2008Randomizer", "SpawnsPerTrigger", 1, gIniPath));
        uint64_t authored = 0, now = 0;
        int used = 0;
        for (const LootScaleClass& c : kLootScaleClasses)
        {
            char key[64];
            std::snprintf(key, sizeof(key), "EnemyType_%s", c.name);
            int count = GetPrivateProfileIntA("DeadSpace2008Randomizer", key, static_cast<int>(fallback), gIniPath);
            count = count < 1 ? 1 : count > 16 ? 16 : count;   // 0 leaves the class alone, like the spawner
            const uint64_t weight = static_cast<uint64_t>(c.spawners) * c.cost;
            authored += weight;
            now += weight * static_cast<unsigned>(count);
            if (count > 1 && used >= 0 && used < static_cast<int>(detailSize) - 32)
                used += std::snprintf(detail + used, detailSize - used, "%s%s x%d", used ? ", " : "", c.name, count);
        }
        unsigned percent = authored ? static_cast<unsigned>((now * 100 + authored / 2) / authored) : 100;
        percent = percent < 100 ? 100 : percent > 500 ? 500 : percent;
        // Half the extra only, since the added enemies drop loot too; the full
        // share was too much (179% for five classes at x2).
        return 100 + (percent - 100 + 1) / 2;
    }
    safetyhook::MidHook gAmmoAmountHook;
    safetyhook::MidHook gCreditAmountHook;
    volatile LONG gAmmoScaled = 0, gAmmoRejected = 0, gCreditsScaled = 0, gCreditsRejected = 0, gAmountFaults = 0;
    volatile LONG gAmmoBefore[kAmmoKindCount]{}, gAmmoAfter[kAmmoKindCount]{};
    volatile LONG gCreditsBefore = 0, gCreditsAfter = 0, gAmountLines = 0;

    void AmmoAmount_Hook(safetyhook::Context& ctx)
    {
        __try
        {
            unsigned char* pickup = reinterpret_cast<unsigned char*>(ctx.ecx);
            const uint32_t caller = *reinterpret_cast<const uint32_t*>(ctx.esp + 8);
            if (!pickup || *reinterpret_cast<const uint32_t*>(pickup) != kSpareAmmoVtable ||
                caller != kAmmoScaleReturn || (*reinterpret_cast<const uint32_t*>(pickup + 0x2FC) & 0x40))
            {
                InterlockedIncrement(&gAmmoRejected);
                return;
            }
            const uint32_t hash = *reinterpret_cast<const uint32_t*>(pickup + 0x630);
            unsigned kind = 0;
            while (kind < kAmmoKindCount && kAmmoKinds[kind].hash != hash)
                ++kind;
            int32_t& amount = *reinterpret_cast<int32_t*>(pickup + 0x318);
            if (kind == kAmmoKindCount || amount <= 0)
            {
                InterlockedIncrement(&gAmmoRejected);
                return;
            }
            const int32_t before = amount;
            const unsigned total = static_cast<unsigned>(before) * gLootAmmoPercent;
            Guid128 identity{};
            std::memcpy(&identity, pickup + 0x20, sizeof(identity));
            int32_t after = static_cast<int32_t>(total / 100u);
            if (StableRandomizerHash(identity, identity, 0x414D4D4Fu + kind) % 100u < total % 100u)
                ++after;
            if (after < 1)
                after = 1;   // never empty
            amount = after;
            InterlockedIncrement(&gAmmoScaled);
            InterlockedExchangeAdd(&gAmmoBefore[kind], before);
            InterlockedExchangeAdd(&gAmmoAfter[kind], after);
            if (InterlockedIncrement(&gAmountLines) <= 32)
                Log("loot amount: %s pickup %08X %d -> %d rounds (%u%%)", kAmmoKinds[kind].name,
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(pickup)), before, after, gLootAmmoPercent);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gAmountFaults);
        }
    }

    void CreditAmount_Hook(safetyhook::Context& ctx)
    {
        __try
        {
            const unsigned char* powerUp = reinterpret_cast<const unsigned char*>(ctx.esi);
            const int32_t before = static_cast<int32_t>(ctx.ecx);
            if (!powerUp || *reinterpret_cast<const uint32_t*>(powerUp) != kPowerUpVtable ||
                !powerUp[0x630] || before <= 0)
            {
                InterlockedIncrement(&gCreditsRejected);
                return;
            }
            // In hundreds like the game's own roll, never below 100, never past the wallet's cap.
            int64_t after = (static_cast<int64_t>(before) * gLootCreditsPercent + 5000) / 10000 * 100;
            after = after < 100 ? 100 : after > 999999 ? 999999 : after;
            ctx.ecx = static_cast<uintptr_t>(after);
            InterlockedIncrement(&gCreditsScaled);
            InterlockedExchangeAdd(&gCreditsBefore, before);
            InterlockedExchangeAdd(&gCreditsAfter, static_cast<LONG>(after));
            if (InterlockedIncrement(&gAmountLines) <= 32)
                Log("loot amount: credits %08X %d -> %d (%u%%)",
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(powerUp)), before,
                    static_cast<int>(after), gLootCreditsPercent);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gAmountFaults);
        }
    }

    void LoadLootAmountSettings()
    {
        auto percent = [](const char* key) {
            int value = GetPrivateProfileIntA("DeadSpace2008Randomizer", key, 100, gIniPath);
            return static_cast<unsigned>(value < 10 ? 10 : value > 500 ? 500 : value);
        };
        gLootAmmoPercent = percent("LootAmmoPercent");
        gLootCreditsPercent = percent("LootCreditsPercent");
        gLootAutoScale = GetPrivateProfileIntA("DeadSpace2008Randomizer", "LootAutoScale", 0, gIniPath) != 0;
        if (!gLootAutoScale)
            return;
        char detail[256]{};
        const unsigned automatic = LootAutoPercent(detail, sizeof(detail));
        Log("loot amounts: LootAutoScale=1, More enemies (%s) -> %u%% ammo and credits; LootAmmoPercent=%u and "
            "LootCreditsPercent=%u are not used", detail[0] ? detail : "all as authored", automatic, gLootAmmoPercent,
            gLootCreditsPercent);
        gLootAmmoPercent = automatic;
        gLootCreditsPercent = automatic;
    }

    void InstallLootAmountHooks()
    {
        if (gLootAmmoPercent != 100)
            gAmmoAmountHook = VerifiedMid(reinterpret_cast<void*>(kAmmoAmountSite), &AmmoAmount_Hook);
        if (gLootCreditsPercent != 100)
            gCreditAmountHook = VerifiedMid(reinterpret_cast<void*>(kCreditAmountSite), &CreditAmount_Hook);
        Log("loot amounts: ammo %u%% (%s at %08X), credits %u%% (%s at %08X); 100 = as authored",
            gLootAmmoPercent, gLootAmmoPercent == 100 ? "off" : gAmmoAmountHook ? "active" : "NOT installed",
            static_cast<unsigned>(kAmmoAmountSite), gLootCreditsPercent,
            gLootCreditsPercent == 100 ? "off" : gCreditAmountHook ? "active" : "NOT installed",
            static_cast<unsigned>(kCreditAmountSite));
    }

    void LogLootAmountTotals()
    {
        static LONG last[2] = {-1, -1};
        const LONG now[2] = {InterlockedCompareExchange(&gAmmoScaled, 0, 0),
                             InterlockedCompareExchange(&gCreditsScaled, 0, 0)};
        if ((!gAmmoAmountHook && !gCreditAmountHook) || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        char kinds[512]{};
        int used = 0;
        for (unsigned i = 0; i < kAmmoKindCount && used >= 0 && used < static_cast<int>(sizeof(kinds)) - 64; ++i)
            if (gAmmoBefore[i])
                used += std::snprintf(kinds + used, sizeof(kinds) - used, "%s%s %ld->%ld", used ? ", " : "",
                                      kAmmoKinds[i].name, gAmmoBefore[i], gAmmoAfter[i]);
        Log("loot amount totals: %ld ammo pickups scaled (%s), %ld rejected; %ld credit pickups %ld -> %ld, "
            "%ld rejected; %ld faulted", now[0], kinds[0] ? kinds : "none", gAmmoRejected, now[1],
            gCreditsBefore, gCreditsAfter, gCreditsRejected, gAmountFaults);
    }
