    // Starting suit debt ([DeadSpace2008Randomizer] StartingSuitCreditShare, with
    // StartingSuitRandomizer).
    //
    // A better starting suit is paid off over the run instead of taken from the
    // wallet: StartingSuitCreditShare percent of every credit income goes to the
    // suit until its kiosk price is paid, so the player can still buy things, just
    // more slowly. The prices are the kiosk suits' Item[36] in global_assets
    // (level 2 10000, 3 20000, 4 35000, 5 60000, 6 99000).
    // Income is a credit pickup (0x0051572F, eax = amount added) or a kiosk sale
    // (0x005A0237, ebx = amount paid, eax = the player). The remaining debt lives in
    // DeadSpace2008Randomizer_RunState.ini under the run's seed, so a restart
    // continues the run; dying and reloading before a checkpoint can count a pickup
    // twice, which only shortens the debt.
    constexpr unsigned kSuitPrices[6] = {0, 10000, 20000, 35000, 60000, 99000};
    constexpr uintptr_t kCreditPickupAdd = 0x0051572F;   // mov edx,[ecx+0CE4h]; add eax,edx
    constexpr uintptr_t kCreditSaleAdd = 0x005A0237;     // mov ecx,[eax+0CE4h]; ...; add ecx,ebx
    constexpr unsigned kWalletOffset = 0xCE4;

    unsigned gSuitDebtShare = 50;
    unsigned gSuitDebtTotal = 0;
    unsigned gSuitDebtRemaining = 0;
    unsigned gSuitDebtLevel = 0;
    LONG gSuitDebtLines = 0;
    char gRunStatePath[MAX_PATH]{};
    safetyhook::MidHook gCreditPickupHook;
    safetyhook::MidHook gCreditSaleHook;
    // Income only marks the state; the polling thread writes the file. Four profile
    // writes on the game thread held every credit pickup up for half a second.
    volatile LONG gSuitDebtDirty = 0;

    void SaveSuitDebt()
    {
        char seed[16]{}, value[16]{};
        std::snprintf(seed, sizeof(seed), "0x%08X", gStartingSuitSeed);
        WritePrivateProfileStringA("SuitDebt", "Seed", seed, gRunStatePath);
        std::snprintf(value, sizeof(value), "%u", gSuitDebtLevel);
        WritePrivateProfileStringA("SuitDebt", "SuitLevel", value, gRunStatePath);
        std::snprintf(value, sizeof(value), "%u", gSuitDebtTotal);
        WritePrivateProfileStringA("SuitDebt", "Price", value, gRunStatePath);
        std::snprintf(value, sizeof(value), "%u", gSuitDebtRemaining);
        WritePrivateProfileStringA("SuitDebt", "Remaining", value, gRunStatePath);
    }

    // The share of one income that goes to the suit, taken off what is owed.
    unsigned TakeSuitDebt(unsigned income, const char* source)
    {
        if (!gSuitDebtRemaining || !income)
            return 0;
        unsigned taken = static_cast<unsigned>((static_cast<unsigned long long>(income) * gSuitDebtShare + 50) / 100);
        if (taken > gSuitDebtRemaining)
            taken = gSuitDebtRemaining;
        gSuitDebtRemaining -= taken;
        InterlockedExchange(&gSuitDebtDirty, 1);
        if (++gSuitDebtLines <= 32 || !gSuitDebtRemaining)
            Log("suit debt: %s of %u credits, %u to the level %u suit, %u of %u still owed%s", source, income, taken,
                gSuitDebtLevel, gSuitDebtRemaining, gSuitDebtTotal, gSuitDebtRemaining ? "" : " - paid off");
        return taken;
    }

    void CreditPickup_Hook(safetyhook::Context& ctx)
    {
        const int32_t income = static_cast<int32_t>(ctx.eax);
        if (income > 0)
            ctx.eax = static_cast<uintptr_t>(income - static_cast<int32_t>(TakeSuitDebt(
                static_cast<unsigned>(income), "pickup")));
    }

    // The wallet is lowered before the game adds the sale, so it never goes below where it was.
    void CreditSale_Hook(safetyhook::Context& ctx)
    {
        const int32_t income = static_cast<int32_t>(ctx.ebx);
        if (income <= 0 || !ctx.eax)
            return;
        const unsigned taken = TakeSuitDebt(static_cast<unsigned>(income), "kiosk sale");
        if (taken)
            *reinterpret_cast<int32_t*>(ctx.eax + kWalletOffset) -= static_cast<int32_t>(taken);
    }

    // From the polling thread.
    void PollSuitDebtSave()
    {
        if (InterlockedExchange(&gSuitDebtDirty, 0))
            SaveSuitDebt();
    }

    // Called once the starting suit randomizer's change took (TryApplyPendingStartingSuit).
    void SuitDebtBegin(unsigned suitIndex)
    {
        if (!gSuitDebtShare || suitIndex >= 6)
            return;
        gSuitDebtLevel = suitIndex + 1;
        gSuitDebtTotal = kSuitPrices[suitIndex];
        gSuitDebtRemaining = gSuitDebtTotal;
        SaveSuitDebt();
        Log("suit debt: the level %u suit costs %u credits; %u%% of every credit income goes to it until paid",
            gSuitDebtLevel, gSuitDebtTotal, gSuitDebtShare);
    }

    void LoadSuitDebtSettings()
    {
        int share = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StartingSuitCreditShare", 50, gIniPath);
        gSuitDebtShare = static_cast<unsigned>(share < 0 ? 0 : share > 100 ? 100 : share);
        std::snprintf(gRunStatePath, sizeof(gRunStatePath), "%sDeadSpace2008Randomizer_RunState.ini", gGameRoot);
        // A run of this seed that is still paying continues where it was.
        char seed[16]{}, mine[16]{};
        GetPrivateProfileStringA("SuitDebt", "Seed", "", seed, sizeof(seed), gRunStatePath);
        std::snprintf(mine, sizeof(mine), "0x%08X", gStartingSuitSeed);
        if (_stricmp(seed, mine) == 0)
        {
            gSuitDebtLevel = static_cast<unsigned>(GetPrivateProfileIntA("SuitDebt", "SuitLevel", 0, gRunStatePath));
            gSuitDebtTotal = static_cast<unsigned>(GetPrivateProfileIntA("SuitDebt", "Price", 0, gRunStatePath));
            gSuitDebtRemaining = static_cast<unsigned>(GetPrivateProfileIntA("SuitDebt", "Remaining", 0, gRunStatePath));
            if (gSuitDebtRemaining > gSuitDebtTotal)
                gSuitDebtRemaining = gSuitDebtTotal;
        }
    }

    void InstallSuitDebt()
    {
        if (!gStartingSuitRandomizer || !gSuitDebtShare)
        {
            Log("suit debt off (%s)", gStartingSuitRandomizer ? "StartingSuitCreditShare=0" : "StartingSuitRandomizer=0");
            return;
        }
        gCreditPickupHook = VerifiedMid(reinterpret_cast<void*>(kCreditPickupAdd), &CreditPickup_Hook);
        gCreditSaleHook = VerifiedMid(reinterpret_cast<void*>(kCreditSaleAdd), &CreditSale_Hook);
        Log("suit debt: %u%% of credit income (pickup hook %s, kiosk sale hook %s); %s", gSuitDebtShare,
            gCreditPickupHook ? "active" : "NOT installed", gCreditSaleHook ? "active" : "NOT installed",
            gSuitDebtRemaining ? "this seed's run still owes" : "nothing owed until a New Game picks a suit");
        if (gSuitDebtRemaining)
            Log("suit debt: level %u suit, %u of %u credits still owed (from %s)", gSuitDebtLevel,
                gSuitDebtRemaining, gSuitDebtTotal, gRunStatePath);
    }
