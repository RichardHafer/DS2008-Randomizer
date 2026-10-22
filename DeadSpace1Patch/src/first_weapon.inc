    // =====================================================================
    // First weapon ([DeadSpace1Patch] FirstWeaponRandomizer).
    // analysis/DeadSpace1_FirstWeapon_AllNine_2026-10-21.md.
    //
    // A gun pickup never reads PickupItem+0x344: Gun::OnPickup 0x004F88A0 puts
    // the world object itself into the inventory, so rewriting that field
    // never changed the first weapon. The kiosk's way works: EquipCommand::
    // GiveItem 0x00508DD0 with a GunProxy spawns the proxy, whose OnPickup
    // streams the weapon archive on demand and creates the gun.
    // - The chapter 1 cutter (a FuncSpawn copy of entity 4BC8DF17, so it is
    //   matched by its PlasmaCutter vtable and the use handler's return
    //   address 0x0056007F) gives the seeded weapon and retires itself. The
    //   use handler still sends mm_plasma_cutter_picked_up, so melee, the
    //   first door and the tutorials go on as authored.
    // - Every EquipCommand grant of the cutter goes through GiveItem (none of
    //   the 310 sets the existing-instance flag, prop 24), so the checkpoint
    //   loadouts and equip_player_with_plasma give the seeded weapon too, and
    //   their Plasma Cutter Batteries its ammo.
    // - The two chapter 1 battery pickups give its ammo, and the store gate
    //   admits that ammo from the start: the Flamethrower, Force Gun, Ripper
    //   and Contact Beam have none in chapter 1 otherwise.
    // One of seven from the seed, the Plasma Cutter among them.
    // =====================================================================
    uintptr_t TargetKeyFor(const Guid128& target);   // dllmain.cpp

    constexpr uintptr_t kGunOnPickup = 0x004F88A0;       // thiscall(user, flag), ret 8
    constexpr uintptr_t kGunOnPickupCaller = 0x0056007F; // the use handler's call
    constexpr uintptr_t kEquipGiveItem = 0x00508DD0;     // eax = [kEquipContext], ecx = &GUID, cdecl(player, flag, equip)
    constexpr uintptr_t kEquipContext = 0x00F64930;
    constexpr uintptr_t kDeferDestroy = 0x0044FFF0;      // edi = object + 0x3C
    constexpr uint32_t kPlayerInterface = 0x383225A1u;   // user->vt[0x10](id, &out), as both OnPickups ask
    constexpr unsigned kPlayerWeaponManager = 0x15C4;
    constexpr uint32_t kPlasmaCutterVtable = 0x00C6DB20;
    constexpr uint32_t kSpareAmmoPickupVtable = 0x00C700A0;
    constexpr Guid128 kFirstWeaponPickup = {{0x4BC8DF17u, 0xC674E941u, 0x544E4543u, 0x53574F4Bu}};
    constexpr Guid128 kPlasmaCutterProxy = {{0x36C8BC4Fu, 0x07EADE56u, 0x44455257u, 0x34303245u}};
    constexpr Guid128 kPlasmaCutterBattery = {{0x4BC78C36u, 0x9D651322u, 0x544E4543u, 0x53574F4Bu}};
    // The only ammo lying in chapter 1, both placed at stream load: hallB_z02 #95 and mainBay_z01 #151.
    constexpr Guid128 kChapterOneBatteries[] = {
        {{0x4BC8573Eu, 0x8F60DE85u, 0x544E4543u, 0x53574F4Bu}},
        {{0x49C89454u, 0xAAB45DF2u, 0x4B544E45u, 0x4B53574Fu}},
    };

    struct FirstWeapon
    {
        const char* name;
        Guid128 proxy;   // what is given
        Guid128 gun;     // what appears once its archive is in
        Guid128 ammo;
        int32_t price;   // the GunProxy's Item[36]; the cutter has none, so it costs 0
    };
    constexpr FirstWeapon kFirstWeapons[] = {
        {"Plasma Cutter", {{0x36C8BC4Fu, 0x07EADE56u, 0x44455257u, 0x34303245u}},
         {{0x4BC78C36u, 0x9DB15E65u, 0x544E4543u, 0x53574F4Bu}}, {{0x4BC78C36u, 0x9D651322u, 0x544E4543u, 0x53574F4Bu}}, 0},
        {"Line Gun", {{0x36C8BC49u, 0x5F163CD0u, 0x44455257u, 0x34303245u}},
         {{0x4BC78C36u, 0x9DBAE7CDu, 0x544E4543u, 0x53574F4Bu}}, {{0x4BC78CECu, 0xDFF02CCFu, 0x544E4543u, 0x53574F4Bu}}, 9000},
        {"Pulse Rifle", {{0x36C8BC4Fu, 0x67BB5F38u, 0x44455257u, 0x34303245u}},
         {{0x4BC78C36u, 0x9DC20EDBu, 0x544E4543u, 0x53574F4Bu}}, {{0x4BC78C36u, 0x9D67757Cu, 0x544E4543u, 0x53574F4Bu}}, 7000},
        {"Flamethrower", {{0x36C8BC40u, 0x0C67B69Au, 0x44455257u, 0x34303245u}},
         {{0x4BC83EB4u, 0xFAEA95D6u, 0x544E4543u, 0x53574F4Bu}}, {{0x30C8692Du, 0x6DBBF822u, 0x4E484F4Au, 0x324E4F53u}}, 11000},
        {"Force Gun", {{0x36C8BC48u, 0xED477CBAu, 0x44455257u, 0x34303245u}},
         {{0x30C87CE1u, 0x0FCEC34Au, 0x4C454D50u, 0x4B534544u}}, {{0x30C87D90u, 0xE7570C3Eu, 0x4C454D50u, 0x4B534544u}}, 11000},
        {"Ripper", {{0x36C8BC4Fu, 0x6F9078E2u, 0x44455257u, 0x34303245u}},
         {{0x38C84295u, 0x4AEC79D9u, 0x4E4F4F4Eu, 0x30314E41u}}, {{0x30C8692Du, 0x79E101DCu, 0x4E484F4Au, 0x324E4F53u}}, 8000},
        {"Contact Beam", {{0x36C8BC3Fu, 0xF55615C8u, 0x44455257u, 0x34303245u}},
         {{0x30C87370u, 0x12C113D7u, 0x4C454D50u, 0x4B534544u}}, {{0x30C87377u, 0xE3BC55EAu, 0x4C454D50u, 0x4B534544u}}, 9000},
    };
    constexpr unsigned kFirstWeaponCount = sizeof(kFirstWeapons) / sizeof(kFirstWeapons[0]);

    const FirstWeapon* gFirstWeapon = nullptr;   // set by InstallFirstWeapon while the hooks are in
    safetyhook::InlineHook gGunOnPickupHook;
    safetyhook::MidHook gEquipGiveItemHook;
    volatile LONG gFirstWeaponGiven = 0, gFirstWeaponFallbacks = 0, gFirstWeaponLoadouts = 0,
                  gFirstWeaponLoadoutAmmo = 0, gFirstWeaponBatteries = 0, gFirstWeaponOtherCutters = 0;
    // Written on the game thread, polled on the watch thread.
    volatile LONG gFirstWeaponPending = 0, gFirstWeaponGivenAt = 0;

    void MarkFirstWeaponGiven()
    {
        InterlockedExchange(&gFirstWeaponGivenAt, static_cast<LONG>(GetTickCount()));
        InterlockedExchange(&gFirstWeaponPending, 1);
    }

    // The seed's weapon, or null when the randomizer is off or the seed keeps the cutter.
    const FirstWeapon* PickFirstWeapon()
    {
        if (!gFirstWeaponRandomizer)
            return nullptr;
        const FirstWeapon* pick = &kFirstWeapons[MixStartingSuitSeed(gStartingSuitSeed ^ 0x46525354u) % kFirstWeaponCount];
        return pick == &kFirstWeapons[0] ? nullptr : pick;
    }

    __declspec(noinline) uintptr_t EquipGiveItem(const Guid128* item, void* player)
    {
        const uintptr_t function = kEquipGiveItem;
        const uintptr_t context = *reinterpret_cast<const uintptr_t*>(kEquipContext);
        uintptr_t result = 0;
        __asm
        {
            push 1          // equip it
            push 1
            push player
            mov eax, context
            mov ecx, item
            mov edx, function
            call edx
            add esp, 12
            mov result, eax
        }
        return result;
    }

    __declspec(noinline) void DeferDestroy(void* object)
    {
        const uintptr_t function = kDeferDestroy;
        const uintptr_t handle = reinterpret_cast<uintptr_t>(object) + 0x3C;
        __asm
        {
            mov edi, handle
            mov eax, function
            call eax
        }
    }

    void* PlayerOf(void* user)
    {
        using Query = bool(__thiscall*)(void*, uint32_t, void**);
        void* player = nullptr;
        const Query query = (*reinterpret_cast<Query* const*>(user))[0x10 / 4];
        return query(user, kPlayerInterface, &player) ? player : nullptr;
    }

    template <typename T> T ReadField(const void* object, unsigned offset)
    {
        T value{};
        ReadProcessMemory(GetCurrentProcess(), static_cast<const char*>(object) + offset, &value, sizeof(value), nullptr);
        return value;
    }

    bool __fastcall GunOnPickup_Hook(void* self, void*, void* user, int flag)
    {
        const FirstWeapon* weapon = gFirstWeapon;
        if (!weapon || !self || !user || reinterpret_cast<uintptr_t>(_ReturnAddress()) != kGunOnPickupCaller ||
            ReadField<uint32_t>(self, 0) != kPlasmaCutterVtable)
            return gGunOnPickupHook.thiscall<bool>(self, user, flag);
        // Gun::OnPickup's own early outs: not in a container yet, still live.
        const uintptr_t container = ReadField<uintptr_t>(self, 0x2F4);
        if ((container && container != 0x44) || !(ReadField<uint8_t>(self, 0x40) & 1))
            return gGunOnPickupHook.thiscall<bool>(self, user, flag);
        const Guid128 own = ReadField<Guid128>(self, 0x20);
        if (!SameGuid(own, kFirstWeaponPickup))
            InterlockedIncrement(&gFirstWeaponOtherCutters);
        void* player = PlayerOf(user);
        const uintptr_t manager = player ? ReadField<uintptr_t>(player, kPlayerWeaponManager) : 0;
        const bool registered = TargetKeyFor(weapon->proxy) != 0;
        // GunProxy::OnPickup consumes the proxy even without a weapon manager, so check first.
        const uintptr_t given = player && manager && registered ? EquipGiveItem(&weapon->proxy, player) : 0;
        Log("first weapon: cutter %08X (%08X-%08X) picked up by %08X, player %08X, weapon manager %08X, "
            "%s proxy %s, GiveItem %08X: %s",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)), own.value[0], own.value[1],
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(user)),
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(player)), static_cast<unsigned>(manager), weapon->name,
            registered ? "registered" : "NOT registered", static_cast<unsigned>(given),
            given ? "given, the cutter is retired" : "failed, the Plasma Cutter stays");
        if (!given)
        {
            InterlockedIncrement(&gFirstWeaponFallbacks);
            return gGunOnPickupHook.thiscall<bool>(self, user, flag);
        }
        DeferDestroy(self);
        InterlockedIncrement(&gFirstWeaponGiven);
        MarkFirstWeaponGiven();
        return true;
    }

    // EquipCommand grants by GUID; the cutter comes as its pickup entity or its GunProxy.
    void EquipGiveItem_Hook(safetyhook::Context& ctx)
    {
        const FirstWeapon* weapon = gFirstWeapon;
        Guid128 given{};
        if (!weapon || !ctx.ecx ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(ctx.ecx), &given, sizeof(given), nullptr))
            return;
        if (SameGuid(given, kFirstWeaponPickup) || SameGuid(given, kPlasmaCutterProxy))
        {
            ctx.ecx = reinterpret_cast<uintptr_t>(&weapon->proxy);
            MarkFirstWeaponGiven();
            if (InterlockedIncrement(&gFirstWeaponLoadouts) <= 8)
                Log("first weapon: a loadout's Plasma Cutter (%08X-%08X) becomes the %s", given.value[0],
                    given.value[1], weapon->name);
        }
        else if (SameGuid(given, kPlasmaCutterBattery) && TargetKeyFor(weapon->ammo))
        {
            ctx.ecx = reinterpret_cast<uintptr_t>(&weapon->ammo);
            if (InterlockedIncrement(&gFirstWeaponLoadoutAmmo) <= 8)
                Log("first weapon: a loadout's Plasma Cutter Battery becomes %s ammo", weapon->name);
        }
    }

    // From the pickup decoder: the chapter 1 batteries carry the first weapon's ammo.
    void ApplyFirstWeaponAmmo(void* pickup)
    {
        const FirstWeapon* weapon = gFirstWeapon;
        if (!weapon || !pickup)
            return;
        const unsigned char* object = static_cast<const unsigned char*>(pickup);
        if (ReadField<uint32_t>(object, 0) != kSpareAmmoPickupVtable)
            return;
        const Guid128 own = ReadField<Guid128>(object, 0x20);
        bool known = false;
        for (const Guid128& battery : kChapterOneBatteries)
            known = known || SameGuid(own, battery);
        if (!known || !SameGuid(ReadField<Guid128>(object, kPickupGrantedItem), kPlasmaCutterBattery))
            return;
        // An unregistered grant makes AddToInventory fall back to the pickup or worse.
        const bool registered = TargetKeyFor(weapon->ammo) != 0;
        const bool written = registered &&
            WriteProcessMemory(GetCurrentProcess(), const_cast<unsigned char*>(object) + kPickupGrantedItem,
                               &weapon->ammo, sizeof(weapon->ammo), nullptr);
        if (written)
            InterlockedIncrement(&gFirstWeaponBatteries);
        Log("first weapon: chapter 1 Plasma Cutter Battery %08X (%08X-%08X): %s",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(pickup)), own.value[0], own.value[1],
            written ? "now holds the first weapon's ammo"
                    : registered ? "write FAILED, the battery stays" : "that ammo is NOT registered, the battery stays");
    }

    // The kiosk prices a GunProxy from its Item[36] at +0x61C (0x005A16D7). The seeded weapon
    // costs what the cutter did (nothing) and the cutter what the weapon did. The resident
    // proxies are checked at every store admission, so a re-decoded proxy is patched again.
    constexpr unsigned kItemPrice = 0x61C;
    constexpr uint32_t kGunProxyVtable = 0x00C6D080;
    volatile LONG gFirstWeaponPriceLines = 0;

    uintptr_t ResidentProxy(const Guid128& proxy)
    {
        const uintptr_t key = TargetKeyFor(proxy);
        const uintptr_t object = key ? ReadField<uintptr_t>(reinterpret_cast<const void*>(key), 0x14) : 0;
        return object && ReadField<uint32_t>(reinterpret_cast<const void*>(object), 0) == kGunProxyVtable ? object : 0;
    }

    // What the chapter 1 cutter and its batteries say comes from what they now
    // give: Item[22..25] (+0x50C, +0x51C, +0x52C, +0x53C) of the weapon's world
    // pickup and of its ammo. Those items are templates without a live instance
    // when the cutter decodes (12:53 on 2026-10-22: "not resident"), so the keys
    // are read offline (first_weapon_texts.py). The cutter is the template the
    // FuncSpawn copies, so its decode carries the texts into the copy.
    // The pickup prompt shows PickupItem[0], a name string at +0x300 whose
    // hash (0x006C2040: lower case, h * 0x1003F + c) sits at +0x310, and the
    // icons PickupItem[10], [12] and [13] (+0x41C, +0x444, +0x454).
    struct PickupLabel
    {
        uint32_t keys[4];
        const char* name;
        uint32_t icons[3];
    };
    struct FirstWeaponTexts
    {
        PickupLabel gun;
        PickupLabel ammo;
    };
    constexpr FirstWeaponTexts kFirstWeaponTexts[] = {
#include "first_weapon_texts_generated.inc"
    };
    static_assert(sizeof(kFirstWeaponTexts) / sizeof(kFirstWeaponTexts[0]) ==
                      sizeof(kFirstWeapons) / sizeof(kFirstWeapons[0]),
                  "one text row per first weapon");
    constexpr unsigned kItemTextFields[4] = {0x50C, 0x51C, 0x52C, 0x53C};
    volatile LONG gFirstWeaponLabels = 0;

    constexpr unsigned kPickupName = 0x300;
    constexpr unsigned kPickupNameHash = 0x310;
    constexpr unsigned kPickupIcons[3] = {0x41C, 0x444, 0x454};

    uint32_t PickupNameHash(const char* text)
    {
        uint32_t hash = 0;
        for (; *text; ++text)
        {
            uint32_t c = static_cast<unsigned char>(*text);
            if (c - 'A' <= 'Z' - 'A')
                c += 0x20;
            hash = hash * 0x1003F + c;
        }
        return hash;
    }

    // Name into the authored buffer when it fits (every first weapon's name is
    // shorter than the cutter's or the battery's), then the hash and the icons.
    bool WritePickupNameAndIcons(uintptr_t to, const PickupLabel& label)
    {
        struct
        {
            char* data;
            uint32_t length;
            uint32_t capacity;
        } name{};
        const size_t length = std::strlen(label.name);
        bool named = false;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(to + kPickupName), &name, sizeof(name),
                              nullptr) &&
            name.data && length && length < name.capacity &&
            WriteProcessMemory(GetCurrentProcess(), name.data, label.name, length + 1, nullptr))
        {
            const uint32_t newLength = static_cast<uint32_t>(length);
            const uint32_t hash = PickupNameHash(label.name);
            named = WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(to + kPickupName + 4), &newLength,
                                       sizeof(newLength), nullptr) &&
                    WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(to + kPickupNameHash), &hash,
                                       sizeof(hash), nullptr);
        }
        for (unsigned i = 0; i < 3; ++i)
        {
            if (!label.icons[i])
                continue;
            const Guid128 icon{{label.icons[i], label.icons[i], label.icons[i], label.icons[i]}};
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(to + kPickupIcons[i]), &icon,
                               sizeof(icon), nullptr);
        }
        return named;
    }

    unsigned WriteItemTexts(uintptr_t to, const uint32_t (&keys)[4])
    {
        unsigned written = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (!keys[i])
                continue;
            const Guid128 key{{keys[i], keys[i], keys[i], keys[i]}};
            if (WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(to + kItemTextFields[i]), &key,
                                   sizeof(key), nullptr))
                ++written;
        }
        return written;
    }

    void ApplyFirstWeaponLabels(void* pickup)
    {
        const FirstWeapon* weapon = gFirstWeapon;
        if (!weapon || !pickup || weapon == &kFirstWeapons[0])
            return;
        const FirstWeaponTexts& texts = kFirstWeaponTexts[weapon - kFirstWeapons];
        const unsigned char* object = static_cast<const unsigned char*>(pickup);
        const uint32_t vtable = ReadField<uint32_t>(object, 0);
        const Guid128 own = ReadField<Guid128>(object, 0x20);
        const PickupLabel* label = nullptr;
        const char* what = nullptr;
        if (vtable == kPlasmaCutterVtable && SameGuid(own, kFirstWeaponPickup))
        {
            label = &texts.gun;
            what = "cutter";
        }
        else if (vtable == kSpareAmmoPickupVtable &&
                 SameGuid(ReadField<Guid128>(object, kPickupGrantedItem), weapon->ammo))
        {
            bool known = false;
            for (const Guid128& battery : kChapterOneBatteries)
                known = known || SameGuid(own, battery);
            if (!known)
                return;
            label = &texts.ammo;
            what = "battery";
        }
        if (!label)
            return;
        const unsigned written = WriteItemTexts(reinterpret_cast<uintptr_t>(pickup), label->keys);
        const bool named = WritePickupNameAndIcons(reinterpret_cast<uintptr_t>(pickup), *label);
        if (InterlockedIncrement(&gFirstWeaponLabels) <= 8)
            Log("first weapon: chapter 1 %s %08X (%08X-%08X) now carries the %s's texts (%u of 4 fields) and %s",
                what, static_cast<unsigned>(reinterpret_cast<uintptr_t>(pickup)), own.value[0], own.value[1],
                weapon->name, written, named ? "the name in its prompt" : "NOT its prompt name");
    }

    void ApplyFirstWeaponPrices()
    {
        const FirstWeapon* weapon = gFirstWeapon;
        if (!weapon)
            return;
        const uintptr_t target = ResidentProxy(weapon->proxy);
        const uintptr_t cutter = ResidentProxy(kPlasmaCutterProxy);
        if (!target || !cutter)
            return;
        const int32_t targetPrice = ReadField<int32_t>(reinterpret_cast<const void*>(target), kItemPrice);
        const int32_t cutterPrice = ReadField<int32_t>(reinterpret_cast<const void*>(cutter), kItemPrice);
        if (targetPrice == 0 && cutterPrice == weapon->price)
            return;
        // Both authored or nothing: a half-done swap would sell two weapons for free.
        const bool authored = targetPrice == weapon->price && cutterPrice == 0;
        if (authored)
        {
            const int32_t free = 0;
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(target + kItemPrice), &free, sizeof(free),
                               nullptr);
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(cutter + kItemPrice), &weapon->price,
                               sizeof(weapon->price), nullptr);
        }
        if (InterlockedIncrement(&gFirstWeaponPriceLines) <= 8)
            Log("first weapon: kiosk price of the %s %d -> %s, of the Plasma Cutter %d -> %s", weapon->name,
                targetPrice, authored ? "0" : "unchanged", cutterPrice,
                authored ? "its price" : "unchanged (NOT the authored prices, left alone)");
    }

    // From the watch loop: the gun shows up once its archive is in (about 0.4 s).
    void PollFirstWeaponDelivery()
    {
        const FirstWeapon* weapon = gFirstWeapon;
        if (!weapon || !InterlockedCompareExchange(&gFirstWeaponPending, 0, 0))
            return;
        const DWORD waited = GetTickCount() - static_cast<DWORD>(InterlockedCompareExchange(&gFirstWeaponGivenAt, 0, 0));
        if (TargetKeyFor(weapon->gun))
        {
            Log("first weapon: the %s is registered within %lu ms of being given", weapon->name, waited);
            InterlockedExchange(&gFirstWeaponPending, 0);
        }
        else if (waited > 10000)
        {
            Log("first weapon: the %s is NOT registered 10 s after it was given (its weapn_str stream did not load)",
                weapon->name);
            InterlockedExchange(&gFirstWeaponPending, 0);
        }
    }

    void LogFirstWeaponTotals()
    {
        static LONG last[6] = {-1, -1, -1, -1, -1, -1};
        const LONG now[6] = {gFirstWeaponGiven, gFirstWeaponFallbacks, gFirstWeaponLoadouts,
                             gFirstWeaponLoadoutAmmo, gFirstWeaponBatteries, gFirstWeaponOtherCutters};
        if (!gFirstWeapon || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("first weapon totals: %ld given at the pickup, %ld fell back to the cutter, %ld loadout cutters and "
            "%ld loadout batteries redirected, %ld batteries retargeted, %ld cutters with another GUID",
            now[0], now[1], now[2], now[3], now[4], now[5]);
    }

    void InstallFirstWeapon()
    {
        if (!gFirstWeaponRandomizer)
            return;
        const FirstWeapon* pick = PickFirstWeapon();
        if (!pick)
        {
            Log("first weapon: the seed (0x%08X) keeps the Plasma Cutter", gStartingSuitSeed);
            return;
        }
        gGunOnPickupHook = VerifiedInline(reinterpret_cast<void*>(kGunOnPickup),
                                          reinterpret_cast<void*>(&GunOnPickup_Hook));
        gEquipGiveItemHook = VerifiedMid(reinterpret_cast<void*>(kEquipGiveItem), &EquipGiveItem_Hook);
        const bool pickupHook = static_cast<bool>(gGunOnPickupHook);
        const bool loadoutHook = static_cast<bool>(gEquipGiveItemHook);
        // Both or neither: a loadout alone would hand out a second weapon beside the cutter.
        if (pickupHook && loadoutHook)
            gFirstWeapon = pick;
        else
        {
            gGunOnPickupHook = {};
            gEquipGiveItemHook = {};
        }
        Log("first weapon: the %s (seed 0x%08X); pickup hook %s at %08X, loadout hook %s at %08X%s", pick->name,
            gStartingSuitSeed, pickupHook ? "active" : "NOT installed", static_cast<unsigned>(kGunOnPickup),
            loadoutHook ? "active" : "NOT installed", static_cast<unsigned>(kEquipGiveItem),
            gFirstWeapon ? "" : "; both removed, the Plasma Cutter stays");
    }
