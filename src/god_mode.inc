    // God mode ([DeadSpace2008Randomizer] GodMode, GodModeHotkey; a test aid).
    //
    // The player's HealthComponent (vtable 0x00C613AC) keeps health at +0x120 and
    // the maximum at +0x11C. SetHealth (0x0045D100, vtable +0x24) never lowers the
    // health while the mode at +0x104 is 3, so that mode is held on the player's
    // component while god mode is on and restored afterwards. ForceKill (0x0045D290,
    // +0x28) clears the mode before it kills, so it is hooked as well.
    constexpr uintptr_t kHealthForceKill = 0x0045D290;
    constexpr unsigned kHealthMode = 0x104;
    constexpr unsigned kHealthCurrent = 0x120;
    constexpr unsigned kHealthMaximum = 0x11C;
    constexpr uint32_t kHealthModeKeep = 3;
    bool gGodMode = false;
    int gGodModeHotkey = 0;   // off unless set, F8 is 0x77
    uintptr_t gGodModeHealth = 0;   // the component last held, to put its mode back
    uint32_t gGodModeSavedMode = 0;
    safetyhook::InlineHook gHealthForceKillHook;
    volatile LONG gGodModeSpared = 0;

    uintptr_t PlayerHealthComponent()
    {
        __try
        {
            const uintptr_t player = PlayerObject();
            return player ? HealthComponentOf(player) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall HealthForceKill_Hook(void* self, void*)
    {
        if (gGodMode && self && reinterpret_cast<uintptr_t>(self) == PlayerHealthComponent())
        {
            if (InterlockedIncrement(&gGodModeSpared) <= 16)
                Log("god mode: a kill of the player was refused");
            return;
        }
        gHealthForceKillHook.thiscall<void>(self);
    }

    void ReleaseGodMode()
    {
        if (!gGodModeHealth)
            return;
        __try
        {
            if (*reinterpret_cast<const uint32_t*>(gGodModeHealth + kHealthMode) == kHealthModeKeep)
                *reinterpret_cast<uint32_t*>(gGodModeHealth + kHealthMode) = gGodModeSavedMode;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        gGodModeHealth = 0;
    }

    // From the polling thread, every 100 ms: the player is rebuilt with every
    // chapter and checkpoint, so the mode is put on whatever component is current.
    void PollGodMode()
    {
        if (gGodModeHotkey)
        {
            const bool down = (GetAsyncKeyState(gGodModeHotkey) & 0x8000) != 0;
            static bool wasDown = false;
            if (down && !wasDown)
            {
                gGodMode = !gGodMode;
                Log("god mode %s", gGodMode ? "ON" : "off");
                if (!gGodMode)
                    ReleaseGodMode();
            }
            wasDown = down;
        }
        if (!gGodMode)
            return;
        const uintptr_t health = PlayerHealthComponent();
        if (!health)
            return;
        __try
        {
            uint32_t* mode = reinterpret_cast<uint32_t*>(health + kHealthMode);
            if (health != gGodModeHealth)
            {
                gGodModeHealth = health;
                gGodModeSavedMode = *mode == kHealthModeKeep ? 0 : *mode;
                Log("god mode: holding the player's health component %08X (mode was %u)",
                    static_cast<unsigned>(health), gGodModeSavedMode);
            }
            if (*mode != kHealthModeKeep)
                *mode = kHealthModeKeep;
            float* current = reinterpret_cast<float*>(health + kHealthCurrent);
            const float maximum = *reinterpret_cast<const float*>(health + kHealthMaximum);
            if (*current > 0.0f && *current < maximum)
                *current = maximum;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gGodModeHealth = 0;
        }
    }

    void InstallGodMode()
    {
        gGodMode = GetPrivateProfileIntA("DeadSpace2008Randomizer", "GodMode", 0, gIniPath) != 0;
        gGodModeHotkey = GetPrivateProfileIntA("DeadSpace2008Randomizer", "GodModeHotkey", 0, gIniPath);
        if (!gGodMode && !gGodModeHotkey)
            return;
        gHealthForceKillHook =
            VerifiedInline(reinterpret_cast<void*>(kHealthForceKill), reinterpret_cast<void*>(&HealthForceKill_Hook));
        Log("god mode: %s, hotkey VK=0x%02X; kill guard %s at %08X", gGodMode ? "ON" : "off", gGodModeHotkey,
            gHealthForceKillHook ? "active" : "NOT installed", static_cast<unsigned>(kHealthForceKill));
    }

    // Damage cheat ([DeadSpace2008Randomizer] DamageMultiplier,
    // DamageMultiplierHotkey; a test aid).
    //
    // The dismemberable damage path 0x00451650 (eax = damage info, [ebp+8] = the
    // component in ebx, ret 0xC) copies the damage info to its stack (0x00451AA0).
    // From 0x0045168C the amount is the float at [esp+0x80], which feeds both the
    // limb loss (0x00451400) and the body's health (0x0045D010). Only that copy is
    // scaled, so damage info shared by several targets (an explosion) is never
    // scaled twice. The player's own component is left alone.
    constexpr uintptr_t kDismemberDamageCopied = 0x0045168C;
    constexpr unsigned kDamageCopyAmount = 0x80;
    float gDamageMultiplier = 1.0f;
    bool gDamageCheat = false;
    int gDamageHotkey = 0;
    safetyhook::MidHook gDamageHook;
    volatile LONG gDamageScaled = 0;

    void DismemberDamage_Hook(safetyhook::Context& ctx)
    {
        if (!gDamageCheat || ctx.ebx == PlayerHealthComponent())
            return;
        float* amount = reinterpret_cast<float*>(ctx.esp + kDamageCopyAmount);
        if (*amount > 0.0f)
        {
            *amount *= gDamageMultiplier;
            InterlockedIncrement(&gDamageScaled);
        }
    }

    void PollDamageCheat()
    {
        if (!gDamageHotkey || !gDamageHook)
            return;
        const bool down = (GetAsyncKeyState(gDamageHotkey) & 0x8000) != 0;
        static bool wasDown = false;
        if (down && !wasDown)
        {
            gDamageCheat = !gDamageCheat;
            Log("damage cheat %s (x%.0f, %ld hits scaled so far)", gDamageCheat ? "ON" : "off", gDamageMultiplier,
                gDamageScaled);
        }
        wasDown = down;
    }

    void InstallDamageCheat()
    {
        char text[32]{};
        GetPrivateProfileStringA("DeadSpace2008Randomizer", "DamageMultiplier", "1", text, sizeof(text), gIniPath);
        gDamageMultiplier = static_cast<float>(std::atof(text));
        gDamageHotkey = GetPrivateProfileIntA("DeadSpace2008Randomizer", "DamageMultiplierHotkey", 0, gIniPath);
        if (!(gDamageMultiplier > 1.0f) || gDamageMultiplier > 100000.0f)
            return;
        gDamageCheat = !gDamageHotkey;   // with a hotkey it starts off and the key turns it on
        gDamageHook = VerifiedMid(reinterpret_cast<void*>(kDismemberDamageCopied), &DismemberDamage_Hook);
        Log("damage cheat: x%.0f, %s, hotkey VK=0x%02X; hook %s at %08X", gDamageMultiplier,
            gDamageCheat ? "ON" : "off until the hotkey", gDamageHotkey, gDamageHook ? "active" : "NOT installed",
            static_cast<unsigned>(kDismemberDamageCopied));
    }
