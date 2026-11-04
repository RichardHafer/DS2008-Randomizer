    // GiveItem uses eax=context, ecx=GUID and three caller-cleaned stack arguments.
    // Requests from the overlay run on the game thread through the air-check hook.
    constexpr unsigned kDlcEntries = 0x1C;
    constexpr unsigned kDlcEntrySize = 0x1DC;
    constexpr unsigned kDlcEntryCapacity = 0xA0;
    constexpr unsigned kDlcEntryCount = 0x1299C;
    constexpr unsigned kDlcEntryModel = 0x88;
    constexpr unsigned kDlcEntryKey = 0x108;
    constexpr unsigned kDlcEntryFolder = 0x10C;
    constexpr unsigned kDlcEntryItem = 0x1C8;
    constexpr unsigned kPlayerWeaponSlots = 0xC9C;    // four 12-byte slots from +4: item + 0x44 at +0, taken byte at +8
    constexpr unsigned kWeaponManagerPending = 0x64;
    constexpr unsigned kWeaponManagerQueued = 0x1CD8;
    constexpr unsigned kPickupAmountField = 0x318;
    constexpr unsigned kItemNameHash = 0x310;
    constexpr unsigned kCheatWeaponAmmoPickups = 2;   // with a weapon
    constexpr unsigned kCheatAmmoPickups = 3;         // the ammo row
    constexpr uintptr_t kGunProxyOnPickup = 0x004F9860;
    constexpr uintptr_t kSpareAmmoOnPickup = 0x0050C810;

    bool gCheatGiveReady = false;

    struct CheatWeapon
    {
        char name[48];
        Guid128 proxy;
        Guid128 ammo;
        Guid128 gun;   // retail only
        bool dlc;
    };
    struct CheatSuit
    {
        unsigned index;
        char name[48];
    };
    constexpr unsigned kRetailWeaponCount = sizeof(kFirstWeapons) / sizeof(kFirstWeapons[0]);
    CheatWeapon gCheatWeapons[kRetailWeaponCount + kDlcEntryCapacity]{};
    unsigned gCheatWeaponCount = 0;
    unsigned gCheatWeaponIndex = 0;
    CheatSuit gCheatSuits[kRetailSuitCount + kDlcEntryCapacity]{};
    unsigned gCheatSuitCount = 0;

    struct CheatGiveRequest
    {
        char name[48];
        Guid128 proxy;   // zero: ammo only
        Guid128 ammo;
        Guid128 gun;
        unsigned ammoPickups;
    };
    CheatGiveRequest gCheatGive{};
    volatile LONG gCheatGivePending = 0;   // 1 filed, 2 running, 3 done
    DWORD gCheatGiveRequestedAt = 0;
    char gCheatGiveFeedback[96]{};
    // Poll thread: the gun created after a give
    volatile LONG gCheatGiveWatch = 0;
    DWORD gCheatGiveWatchStart = 0;
    uintptr_t gCheatGiveWatchManager = 0;
    CheatGiveRequest gCheatGiveWatched{};

    bool CheatGuidSet(const Guid128& guid)
    {
        return guid.value[0] || guid.value[1] || guid.value[2] || guid.value[3];
    }

    // The live definition a GUID names (SimManager key +0x14), 0 when none is resident.
    uintptr_t CheatObjectFor(const Guid128& guid)
    {
        const uintptr_t key = TargetKeyFor(guid);
        return key ? ReadField<uintptr_t>(reinterpret_cast<const void*>(key), 0x14) : 0;
    }

    // Kept per path: the list is rebuilt on key presses, the files do not change while the game runs.
    bool CheatArchiveInstalled(const char* folder, const char* model)
    {
        struct Known
        {
            char path[MAX_PATH];
            bool present;
        };
        static Known known[2 * kDlcEntryCapacity];
        static unsigned knownCount = 0;
        char path[MAX_PATH];
        std::snprintf(path, sizeof(path), "%s%s%s.str", gGameRoot, folder, model);
        for (unsigned i = 0; i < knownCount; ++i)
            if (_stricmp(known[i].path, path) == 0)
                return known[i].present;
        const bool present = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
        if (knownCount < sizeof(known) / sizeof(known[0]))
        {
            strncpy_s(known[knownCount].path, path, _TRUNCATE);
            known[knownCount++].present = present;
        }
        return present;
    }

    // The retail weapon a DLC model archive reskins, for its ammo.
    const FirstWeapon* RetailWeaponOf(const char* model)
    {
        static const char* const kinds[kRetailWeaponCount] = {"plasma_cutter", "line_gun", "pulse_rifle",
                                                              "flamethrower", "force_gun", "ripper", "contact_beam"};
        for (unsigned i = 0; i < kRetailWeaponCount; ++i)
            if (std::strstr(model, kinds[i]) ||
                (i == 2 && std::strstr(model, "pulserifle")))
                return &kFirstWeapons[i];
        return nullptr;
    }

    // Overlay thread: the retail weapons and suits plus every DLC line whose archive is installed.
    void RefreshCheatLists()
    {
        unsigned weapons = 0, suits = 0, dlcWeapons = 0, dlcSuits = 0, missing = 0, outside = 0, unnamed = 0,
                 undecoded = 0;
        for (const FirstWeapon& retail : kFirstWeapons)
        {
            CheatWeapon& weapon = gCheatWeapons[weapons++];
            strncpy_s(weapon.name, retail.name, _TRUNCATE);
            weapon.proxy = retail.proxy;
            weapon.ammo = retail.ammo;
            weapon.gun = retail.gun;
            weapon.dlc = false;
        }
        for (unsigned i = 0; i < kRetailSuitCount; ++i)
        {
            gCheatSuits[suits].index = i;
            std::snprintf(gCheatSuits[suits++].name, sizeof(gCheatSuits[0].name), "LEVEL %u SUIT", i + 1);
        }
        uintptr_t manager = 0;
        uint32_t count = 0;
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(kSuitDlcManager), &manager,
                          sizeof(manager), nullptr);
        if (manager)
            count = ReadField<uint32_t>(reinterpret_cast<const void*>(manager), kDlcEntryCount);
        for (uint32_t e = 0; e < count && e < kDlcEntryCapacity; ++e)
        {
            const uintptr_t entry = manager + kDlcEntries + e * kDlcEntrySize;
            char model[0x80]{}, folder[0x80]{};
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(entry + kDlcEntryModel), model,
                              sizeof(model) - 1, nullptr);
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(entry + kDlcEntryFolder), folder,
                              sizeof(folder) - 1, nullptr);
            if (!model[0] || !CheatArchiveInstalled(folder, model))
            {
                ++missing;
                continue;
            }
            const char* display = gDlcNameFn ? gDlcNameFn(model) : nullptr;
            if (!display)
            {
                display = model;
                ++unnamed;
            }
            const uint32_t key = ReadField<uint32_t>(reinterpret_cast<const void*>(entry), kDlcEntryKey);
            if (ReadField<uint8_t>(reinterpret_cast<const void*>(entry), 0))
            {
                const Guid128 item = ReadField<Guid128>(reinterpret_cast<const void*>(entry), kDlcEntryItem);
                const FirstWeapon* retail = RetailWeaponOf(model);
                // +0x1C8 is filled once the store archive decodes
                if (!CheatGuidSet(item))
                {
                    ++undecoded;
                    continue;
                }
                CheatWeapon& weapon = gCheatWeapons[weapons++];
                strncpy_s(weapon.name, display, _TRUNCATE);
                weapon.proxy = item;
                weapon.ammo = retail ? retail->ammo : Guid128{};
                weapon.gun = Guid128{};
                weapon.dlc = true;
                ++dlcWeapons;
                continue;
            }
            if (key < kRetailSuitCount || key >= UniformSlotCount())
            {
                ++outside;
                continue;
            }
            bool duplicate = false;
            for (unsigned i = 0; i < suits; ++i)
                duplicate = duplicate || gCheatSuits[i].index == key;
            if (duplicate)
                continue;
            gCheatSuits[suits].index = key;
            strncpy_s(gCheatSuits[suits++].name, display, _TRUNCATE);
            ++dlcSuits;
        }
        gCheatWeaponCount = weapons;
        gCheatSuitCount = suits;
        if (gCheatWeaponIndex >= weapons)
            gCheatWeaponIndex = 0;
        if (gSuitMenuIndex >= suits)
            gSuitMenuIndex = 0;

        static uint32_t lastDigest = 0;
        static unsigned reports = 0;
        const uint32_t digest = (count * 2654435761u) ^ (weapons << 8) ^ (suits << 16) ^ (missing << 24) ^
                                undecoded ^ (UniformSlotCount() << 4);
        if (digest == lastDigest || reports >= 6)
            return;
        lastDigest = digest;
        ++reports;
        Log("cheat menu: %u weapon(s) (%u DLC), %u suit(s) (%u DLC); DLC manager %08X with %u entr%s: %u without "
            "an installed archive, %u weapon(s) whose store item is not decoded yet, %u suit(s) outside the table "
            "of %u, %u without a name (%s)",
            weapons, dlcWeapons, suits, dlcSuits, static_cast<unsigned>(manager), count, count == 1 ? "y" : "ies",
            missing, undecoded, outside, UniformSlotCount(), unnamed,
            gDlcNameFn ? "names from the extension" : "no DS2008_DlcName, archive names shown");
        static bool listed = false;
        if (listed || !dlcSuits)
            return;
        listed = true;
        for (unsigned i = kRetailSuitCount; i < suits; ++i)
            Log("cheat menu: suit entry %u = %s", gCheatSuits[i].index, gCheatSuits[i].name);
    }

    const char* CheatSuitName(unsigned index)
    {
        for (unsigned i = 0; i < gCheatSuitCount; ++i)
            if (gCheatSuits[i].index == index)
                return gCheatSuits[i].name;
        return nullptr;
    }

    void CycleCheatWeapon(int delta)
    {
        RefreshCheatLists();
        if (gCheatWeaponCount)
            gCheatWeaponIndex = static_cast<unsigned>(
                (static_cast<int>(gCheatWeaponIndex) + delta + static_cast<int>(gCheatWeaponCount)) %
                static_cast<int>(gCheatWeaponCount));
    }

    void CycleSuitMenu(int delta)
    {
        RefreshCheatLists();
        if (gCheatSuitCount)
            gSuitMenuIndex = static_cast<unsigned>(
                (static_cast<int>(gSuitMenuIndex) + delta + static_cast<int>(gCheatSuitCount)) %
                static_cast<int>(gCheatSuitCount));
    }

    // Overlay thread.
    void RequestCheatSuit(bool random)
    {
        RefreshCheatLists();
        if (!gCheatSuitCount)
            return;
        unsigned pick = gSuitMenuIndex;
        if (random && gCheatSuitCount > 1)
        {
            unsigned current = 0xFFFFFFFFu;
            CurrentSuitIndex(&current);
            const uint32_t roll = MixStartingSuitSeed(static_cast<uint32_t>(GetTickCount()) ^ 0x5A17C0DEu);
            pick = roll % gCheatSuitCount;
            if (gCheatSuits[pick].index == current)
                pick = (pick + 1) % gCheatSuitCount;   // never the suit already worn
        }
        RequestSuitChange(gCheatSuits[pick].index, gCheatSuits[pick].name);
    }

    // Overlay thread: files the request only.
    void RequestCheatGive(bool withWeapon)
    {
        RefreshCheatLists();
        if (!gCheatGiveReady)
        {
            std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback),
                          "Giving is off; the log says why (cheat give: ...)");
            return;
        }
        const LONG state = InterlockedCompareExchange(&gCheatGivePending, 0, 0);
        if (state == 1 || state == 2 || !gCheatWeaponCount)
        {
            std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "%s",
                          gCheatWeaponCount ? "Previous give still waits for a game frame" : "No weapons listed");
            return;
        }
        const CheatWeapon& weapon = gCheatWeapons[gCheatWeaponIndex];
        if (!withWeapon && !CheatGuidSet(weapon.ammo))
        {
            std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "No ammo known for the %s", weapon.name);
            return;
        }
        strncpy_s(gCheatGive.name, weapon.name, _TRUNCATE);
        gCheatGive.proxy = withWeapon ? weapon.proxy : Guid128{};
        gCheatGive.ammo = weapon.ammo;
        gCheatGive.gun = weapon.gun;
        gCheatGive.ammoPickups = withWeapon ? kCheatWeaponAmmoPickups : kCheatAmmoPickups;
        if (!CheatGuidSet(gCheatGive.ammo))
            gCheatGive.ammoPickups = 0;
        gCheatGiveRequestedAt = GetTickCount();
        InterlockedExchange(&gCheatGivePending, 1);
        std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "%s %s requested", weapon.name,
                      withWeapon ? "with ammo" : "ammo");
        Log("cheat give: %s%s requested (%s, proxy %08X-%08X, ammo %08X-%08X x%u)", weapon.name,
            withWeapon ? "" : " ammo", weapon.dlc ? "DLC" : "retail", gCheatGive.proxy.value[0],
            gCheatGive.proxy.value[1], gCheatGive.ammo.value[0], gCheatGive.ammo.value[1], gCheatGive.ammoPickups);
    }

    // The four weapon slots as 0x0050A350 and 0x0050A510 read them.
    unsigned DescribeWeaponSlots(uintptr_t player, uint32_t nameHash, bool* carried, char* text, size_t capacity)
    {
        unsigned free = 0;
        int used = 0;
        *carried = false;
        text[0] = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const uintptr_t slot = player + kPlayerWeaponSlots + 4 + i * 12;
            const uintptr_t link = ReadField<uintptr_t>(reinterpret_cast<const void*>(slot), 0);
            const uint8_t taken = ReadField<uint8_t>(reinterpret_cast<const void*>(slot), 8);
            const uintptr_t item = link > 0x44 ? link - 0x44 : 0;
            char name[32]{};
            uint32_t hash = 0;
            if (item)
            {
                hash = ReadField<uint32_t>(reinterpret_cast<const void*>(item), kItemNameHash);
                const uintptr_t data = ReadField<uintptr_t>(reinterpret_cast<const void*>(item), 0x300);
                if (data)
                    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(data), name,
                                      sizeof(name) - 1, nullptr);
                *carried = *carried || (nameHash && hash == nameHash);
            }
            else if (!taken)
                ++free;
            if (used >= 0 && used < static_cast<int>(capacity))
                used += std::snprintf(text + used, capacity - used, "%s[%u] %s", i ? " " : "", i,
                                      item ? (name[0] ? name : "?") : taken ? "reserved" : "free");
        }
        return free;
    }

    // GiveItem(player, 0, 1): the 1 is what all 310 EquipCommands pass last (0x0050908B); the 0 goes on to
    // OnPickup and RequestWeapon's request +8 and is what a kiosk purchase passes (0x0059FFEB), so the gun
    // arrives as a bought one does.
    __declspec(noinline) uintptr_t CheatGiveItem(const Guid128* item, uintptr_t player)
    {
        const uintptr_t function = kEquipGiveItem;
        const uintptr_t context = ReadField<uintptr_t>(reinterpret_cast<const void*>(kEquipContext), 0);
        uintptr_t result = 0;
        gCheatGiveActive = true;
        __asm
        {
            push 1
            push 0
            push player
            mov eax, context
            mov ecx, item
            mov edx, function
            call edx
            add esp, 12
            mov result, eax
        }
        gCheatGiveActive = false;
        return result;
    }

    // Game thread, from the air check: self is the player the frame updates.
    void ServeCheatGive(uintptr_t self)
    {
        if (InterlockedCompareExchange(&gCheatGivePending, 0, 0) != 1)
            return;
        const DWORD waited = GetTickCount() - gCheatGiveRequestedAt;
        // Filed in a menu or a loading screen: a late give would surprise more than help.
        if (waited > 20000)
        {
            if (InterlockedCompareExchange(&gCheatGivePending, 3, 1) == 1)
            {
                Log("cheat give: %s expired, no game frame ran for %lu ms after the request", gCheatGive.name,
                    waited);
                std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "Give expired; ask again in game");
            }
            return;
        }
        const uintptr_t player = PlayerObject();
        if (!player || player != self)
        {
            if (waited > 5000 && InterlockedCompareExchange(&gCheatGivePending, 3, 1) == 1)
            {
                Log("cheat give: %s cancelled; in 5 s the player object (%08X) never matched the air check's "
                    "(%08X)", gCheatGive.name, static_cast<unsigned>(player), static_cast<unsigned>(self));
                std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "Give cancelled: no player");
            }
            return;
        }
        const uintptr_t context = ReadField<uintptr_t>(reinterpret_cast<const void*>(kEquipContext), 0);
        const uintptr_t manager = ReadField<uintptr_t>(reinterpret_cast<const void*>(player), kPlayerWeaponManager);
        if (!context || !manager)
        {
            if (InterlockedCompareExchange(&gCheatGivePending, 3, 1) == 1)
            {
                // Without a WeaponManager GunProxy::OnPickup consumes the proxy for nothing (0x004F9919).
                Log("cheat give: %s cancelled; SimManager %08X, player %08X WeaponManager %08X",
                    gCheatGive.name, static_cast<unsigned>(context), static_cast<unsigned>(player),
                    static_cast<unsigned>(manager));
                std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "Give cancelled: no %s",
                              context ? "WeaponManager" : "SimManager");
            }
            return;
        }
        if (InterlockedCompareExchange(&gCheatGivePending, 2, 1) != 1)
            return;
        const CheatGiveRequest request = gCheatGive;
        uintptr_t given = 0;
        bool weaponRefused = false;
        if (CheatGuidSet(request.proxy))
        {
            const bool registered = TargetKeyFor(request.proxy) != 0;
            const uintptr_t proxy = CheatObjectFor(request.proxy);
            const uint32_t nameHash = proxy ? ReadField<uint32_t>(reinterpret_cast<const void*>(proxy), kItemNameHash)
                                            : 0;
            bool carried = false;
            char slots[160];
            const unsigned free = DescribeWeaponSlots(player, nameHash, &carried, slots, sizeof(slots));
            const LONG pendingBefore = ReadField<LONG>(reinterpret_cast<const void*>(manager), kWeaponManagerPending);
            given = registered ? CheatGiveItem(&request.proxy, player) : 0;
            weaponRefused = !given;
            Log("cheat give: %s: player %08X, WeaponManager %08X (pending %ld, queued %ld), proxy %08X-%08X %s "
                "(object %08X, name hash %08X), slots before: %s; GiveItem %08X: %s",
                request.name, static_cast<unsigned>(player), static_cast<unsigned>(manager), pendingBefore,
                ReadField<LONG>(reinterpret_cast<const void*>(manager), kWeaponManagerQueued),
                request.proxy.value[0], request.proxy.value[1], registered ? "registered" : "NOT registered",
                static_cast<unsigned>(proxy), nameHash, slots, static_cast<unsigned>(given),
                given ? "given"
                : !registered ? "not called, the proxy is not resident"
                : carried ? "refused, a slot holds that weapon already"
                : !free ? "refused, no free weapon slot"
                : "refused by GunProxy::OnPickup (slots read differently than expected)");
            if (given)
            {
                DescribeWeaponSlots(player, 0, &carried, slots, sizeof(slots));
                Log("cheat give: slots after: %s", slots);
                gCheatGiveWatchStart = GetTickCount();
                gCheatGiveWatchManager = manager;
                gCheatGiveWatched = request;
                InterlockedExchange(&gCheatGiveWatch, 1);
            }
        }
        unsigned ammoGiven = 0;
        int32_t perPickup = 0;
        if (request.ammoPickups)
        {
            const bool registered = TargetKeyFor(request.ammo) != 0;
            const uintptr_t ammo = CheatObjectFor(request.ammo);
            const bool spareAmmo = ammo && ReadField<uint32_t>(reinterpret_cast<const void*>(ammo), 0) ==
                                               kSpareAmmoPickupVtable;
            perPickup = ammo ? ReadField<int32_t>(reinterpret_cast<const void*>(ammo), kPickupAmountField) : 0;
            for (unsigned i = 0; registered && i < request.ammoPickups; ++i)
            {
                if (!CheatGiveItem(&request.ammo, player))
                    break;
                ++ammoGiven;
            }
            Log("cheat give: %s ammo %08X-%08X %s (object %08X, %s, %d per pickup): %u of %u pickup(s) given%s",
                request.name, request.ammo.value[0], request.ammo.value[1], registered ? "registered" : "NOT registered",
                static_cast<unsigned>(ammo), spareAmmo ? "SpareAmmo" : "NOT the SpareAmmo class", perPickup,
                ammoGiven, request.ammoPickups,
                ammoGiven < request.ammoPickups && registered ? "; the rest did not fit the inventory" : "");
        }
        if (CheatGuidSet(request.proxy))
            std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "%s %s, %u ammo pickup(s)", request.name,
                          given ? "given" : weaponRefused ? "refused (carried, or no free slot)" : "not given",
                          ammoGiven);
        else
            std::snprintf(gCheatGiveFeedback, sizeof(gCheatGiveFeedback), "%u of %u %s ammo pickup(s) given",
                          ammoGiven, request.ammoPickups, request.name);
        InterlockedExchange(&gCheatGivePending, 3);
    }

    // Poll thread: the weapon request drains once the gun exists.
    void PollCheatGiveDelivery()
    {
        if (!InterlockedCompareExchange(&gCheatGiveWatch, 0, 0))
            return;
        const DWORD waited = GetTickCount() - gCheatGiveWatchStart;
        const LONG pending = ReadField<LONG>(reinterpret_cast<const void*>(gCheatGiveWatchManager),
                                             kWeaponManagerPending);
        const bool retail = CheatGuidSet(gCheatGiveWatched.gun);
        const bool gun = retail && TargetKeyFor(gCheatGiveWatched.gun) != 0;
        if (pending == 0 && (!retail || gun))
        {
            Log("cheat give: %s delivered within %lu ms (WeaponManager pending 0%s)", gCheatGiveWatched.name, waited,
                retail ? ", gun registered" : "; a DLC gun's model load shows as 'console DLC: load ... from 005079A3'");
            InterlockedExchange(&gCheatGiveWatch, 0);
        }
        else if (waited > 10000)
        {
            Log("cheat give: %s NOT delivered 10 s after the give (WeaponManager pending %ld, gun %s)",
                gCheatGiveWatched.name, pending, retail ? (gun ? "registered" : "NOT registered") : "DLC, not checked");
            InterlockedExchange(&gCheatGiveWatch, 0);
        }
    }

    // Once the code is decrypted, after InstallFirstWeapon: the call target and the two OnPickups it reaches.
    void InstallCheatGive()
    {
        if (!gCheatsMenu)
            return;
        static constexpr unsigned char kGiveItemRetail[] = {0x51, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56, 0x8B, 0xF0, 0x57};
        const bool retail = std::memcmp(reinterpret_cast<const void*>(kEquipGiveItem), kGiveItemRetail,
                                        sizeof(kGiveItemRetail)) == 0;
        // Our own first weapon hook sits there after it checked the same bytes.
        const bool ours = !retail && static_cast<bool>(gEquipGiveItemHook);
        const uintptr_t gunSlot = ReadField<uintptr_t>(reinterpret_cast<const void*>(kGunProxyVtable), 0x16C);
        const uintptr_t ammoSlot = ReadField<uintptr_t>(reinterpret_cast<const void*>(kSpareAmmoPickupVtable), 0x16C);
        gCheatGiveReady = (retail || ours) && gunSlot == kGunProxyOnPickup && ammoSlot == kSpareAmmoOnPickup;
        Log("cheat give: GiveItem %08X %s, GunProxy::OnPickup slot %08X (%s), SpareAmmo::OnPickup slot %08X (%s); "
            "giving %s, %u ammo pickup(s) with a weapon, %u on the ammo row",
            static_cast<unsigned>(kEquipGiveItem), retail ? "retail" : ours ? "behind our first weapon hook" : "UNKNOWN",
            static_cast<unsigned>(gunSlot), gunSlot == kGunProxyOnPickup ? "retail" : "UNKNOWN",
            static_cast<unsigned>(ammoSlot), ammoSlot == kSpareAmmoOnPickup ? "retail" : "UNKNOWN",
            gCheatGiveReady ? "on" : "OFF", kCheatWeaponAmmoPickups, kCheatAmmoPickups);
    }
