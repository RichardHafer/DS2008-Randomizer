    // =====================================================================
    // Store gate ([DeadSpace1Patch] ProgressionStoreGate, with
    // ProgressionRandomizer; analysis/DeadSpace1_Store_Source_2026-10-21.md).
    //
    // All 18 kiosks feed the same 26-item list (58C8CB2B) into the store, and
    // InsertEntry 0x00596A40 admits an item that is not forced only when its
    // PickupItem[20] <= the chapter (store+0x30) and [21] <= the completed
    // games (store+0x34). Every chapter schematic gives an item whose [20] is
    // the schematic's chapter + 2, so a reward the randomizer moved still came
    // back through the kiosk at its authored chapter. A mid hook at the compare
    // (0x00596B9B; edi = the entry, ebp = the store) gives a moved reward the
    // requirement of the reward whose schematic it took. Schematic uploads and
    // save restores are forced and pass anyway.
    //
    // ProgressionStartingWeapons: the seeded pair from kStartingWeaponPool
    // replaces Line Gun and Pulse Rifle in the first kiosk. Each newcomer
    // trades schematic places and requirements with the weapon it replaces,
    // and so does its ammo, so every weapon stays reachable.
    //
    // FirstWeaponRandomizer alone also installs the gate, for one rule: the
    // seeded first weapon's ammo takes the Plasma Cutter Battery's requirement.
    // =====================================================================
#include "store_items_generated.inc"
    constexpr uintptr_t kStoreGate = 0x00596B9B;
    constexpr unsigned kStoreEntryForced = 0x19;
    constexpr unsigned kStoreEntryChapter = 0x1C;
    constexpr unsigned kStoreEntryPlayed = 0x20;
    constexpr unsigned kStoreChapter = 0x30;
    constexpr unsigned kStorePlayed = 0x34;
    constexpr unsigned kStoreCount = 0x08;

    struct StoreGateRule
    {
        const StoreItemRequirement* item;
        int32_t chapter;
        int32_t played;
        const char* place;        // the reward whose requirement it takes
        uint32_t loggedChapters;  // one line per store chapter
    };
    constexpr unsigned kMaxStoreGateRules = 32;
    StoreGateRule gStoreGateRules[kMaxStoreGateRules]{};
    unsigned gStoreGateRuleCount = 0;
    safetyhook::MidHook gStoreGateHook;
    volatile LONG gStoreGateMoved = 0, gStoreGateForced = 0, gStoreGateForeign = 0, gStoreGateFaults = 0;

    const StoreItemRequirement* StoreItemFor(const Guid128& guid)
    {
        for (unsigned i = 0; i < kStoreItemCount; ++i)
            if (SameGuid(kStoreItems[i].item, guid))
                return &kStoreItems[i];
        return nullptr;
    }

    // An item placed twice keeps the earlier requirement.
    void SetStoreGateRule(const StoreItemRequirement* item, int32_t chapter, int32_t played, const char* place)
    {
        for (unsigned i = 0; i < gStoreGateRuleCount; ++i)
        {
            StoreGateRule& rule = gStoreGateRules[i];
            if (rule.item != item)
                continue;
            if (chapter < rule.chapter)
            {
                rule.chapter = chapter;
                rule.played = played;
                rule.place = place;
            }
            return;
        }
        if (gStoreGateRuleCount < kMaxStoreGateRules)
            gStoreGateRules[gStoreGateRuleCount++] = {item, chapter, played, place, 0};
        else
            Log("store gate: more than %u rules, %s keeps its own requirement", kMaxStoreGateRules, item->name);
    }

    // Every schematic that gave `from` gives `to` instead, and the other way round.
    unsigned SwapSchematicTargets(const Guid128& from, const char* fromName, const Guid128& to, const char* toName)
    {
        unsigned changed = 0;
        for (unsigned i = 0; i < gSchematicMappingCount; ++i)
        {
            SchematicMapping& m = gSchematicMappings[i];
            if (SameGuid(m.target, from))
            {
                m.target = to;
                strncpy_s(m.targetName, toName, _TRUNCATE);
                ++changed;
            }
            else if (SameGuid(m.target, to))
            {
                m.target = from;
                strncpy_s(m.targetName, fromName, _TRUNCATE);
                ++changed;
            }
        }
        return changed;
    }

    // Newcomer and the weapon it replaces, filled by SwapStartingWeapons.
    const StartingWeaponPair* gStartingWeaponIn[2]{};
    const StartingWeaponPair* gStartingWeaponOut[2]{};
    unsigned gStartingWeaponSwaps = 0;

    // The weapon slots Line Gun and Pulse Rifle of the first kiosk, as kStartingWeaponPool rows.
    // Runs before BuildStoreGateRules, which reads the swapped rows.
    void SwapStartingWeapons()
    {
        constexpr unsigned count = static_cast<unsigned>(sizeof(kStartingWeaponPool) / sizeof(kStartingWeaponPool[0]));
        // The first weapon is free at the cutter's place already. Given a kiosk slot as
        // well, it lent the cutter that slot's "always" (seed 0x6690023F: Ripper).
        const FirstWeapon* firstWeapon = gFirstWeapon && gFirstWeapon != &kFirstWeapons[0] ? gFirstWeapon : nullptr;
        unsigned candidates[count]{};
        unsigned candidateCount = 0;
        for (unsigned i = 0; i < count; ++i)
            if (!firstWeapon || std::strcmp(kStartingWeaponPool[i].weaponName, firstWeapon->name) != 0)
                candidates[candidateCount++] = i;
        if (candidateCount < 2)
            return;
        const unsigned firstPick = MixStartingStoreSeed(gStartingSuitSeed) % candidateCount;
        unsigned secondPick = MixStartingStoreSeed(gStartingSuitSeed ^ 0x9E3779B9u) % (candidateCount - 1);
        if (secondPick >= firstPick)
            ++secondPick;
        const StartingWeaponPair* chosen[2] = {&kStartingWeaponPool[candidates[firstPick]],
                                               &kStartingWeaponPool[candidates[secondPick]]};
        if (firstWeapon)
            Log("store gate starting weapons: the %s is the first weapon and stays out of the first kiosk",
                firstWeapon->name);
        const StartingWeaponPair* authored[2] = {nullptr, nullptr};
        for (const StartingWeaponPair& pair : kStartingWeaponPool)
        {
            if (std::strcmp(pair.weaponName, "Line Gun") == 0)
                authored[0] = &pair;
            else if (std::strcmp(pair.weaponName, "Pulse Rifle") == 0)
                authored[1] = &pair;
        }
        if (!authored[0] || !authored[1])
            return;
        // Set semantics: a pick of Line Gun or Pulse Rifle keeps that weapon wherever it was.
        const StartingWeaponPair* leaving[2]{};
        const StartingWeaponPair* arriving[2]{};
        unsigned leaves = 0, arrives = 0;
        for (const StartingWeaponPair* old : authored)
            if (old != chosen[0] && old != chosen[1])
                leaving[leaves++] = old;
        for (const StartingWeaponPair* pick : chosen)
            if (pick != authored[0] && pick != authored[1])
                arriving[arrives++] = pick;
        Log("store gate starting weapons (seed 0x%08X): %s and %s in the first kiosk",
            gStartingSuitSeed, chosen[0]->weaponName, chosen[1]->weaponName);
        gStartingWeaponSwaps = 0;
        for (unsigned i = 0; i < leaves && i < arrives; ++i)
        {
            const StartingWeaponPair& in = *arriving[i];
            const StartingWeaponPair& out = *leaving[i];
            const unsigned weapons = SwapSchematicTargets(in.weapon, in.weaponName, out.weapon, out.weaponName);
            const unsigned ammo = SwapSchematicTargets(in.ammo, in.ammoName, out.ammo, out.ammoName);
            gStartingWeaponIn[gStartingWeaponSwaps] = &in;
            gStartingWeaponOut[gStartingWeaponSwaps++] = &out;
            Log("store gate starting weapons: %s takes the place of %s, %s of %s (%u weapon and %u ammo schematic rows)",
                in.weaponName, out.weaponName, in.ammoName, out.ammoName, weapons, ammo);
        }
    }

    // After BuildStoreGateRules: a newcomer and its ammo enter the kiosk when their predecessors did.
    void AddStartingWeaponRules()
    {
        for (unsigned i = 0; i < gStartingWeaponSwaps; ++i)
        {
            const StoreItemRequirement* outWeapon = StoreItemFor(gStartingWeaponOut[i]->weapon);
            const StoreItemRequirement* outAmmo = StoreItemFor(gStartingWeaponOut[i]->ammo);
            const StoreItemRequirement* inWeapon = StoreItemFor(gStartingWeaponIn[i]->weapon);
            const StoreItemRequirement* inAmmo = StoreItemFor(gStartingWeaponIn[i]->ammo);
            if (outWeapon && inWeapon)
                SetStoreGateRule(inWeapon, outWeapon->chapter, outWeapon->played, outWeapon->name);
            if (outAmmo && inAmmo)
                SetStoreGateRule(inAmmo, outAmmo->chapter, outAmmo->played, outAmmo->name);
        }
    }

    void BuildStoreGateRules()
    {
        gStoreGateRuleCount = 0;
        unsigned unknown = 0, bonus = 0;
        for (unsigned i = 0; i < gSchematicMappingCount; ++i)
        {
            const SchematicMapping& m = gSchematicMappings[i];
            const StoreItemRequirement* source = StoreItemFor(m.source);
            const StoreItemRequirement* target = StoreItemFor(m.target);
            if (!source || !target)
            {
                ++unknown;
                continue;
            }
            // A place that needs a completed game would hide its new reward for the first one.
            if (source->played >= 0)
            {
                ++bonus;
                continue;
            }
            if (source != target)
                SetStoreGateRule(target, source->chapter, source->played, source->name);
        }
        Log("store gate: %u rule(s) from %u schematic row(s) (%u with an unknown item, %u bonus place(s) left out)",
            gStoreGateRuleCount, gSchematicMappingCount, unknown, bonus);
    }

    void LogStoreGateRules()
    {
        for (unsigned i = 0; i < gStoreGateRuleCount; ++i)
        {
            const StoreGateRule& rule = gStoreGateRules[i];
            Log("store gate rule: %s chapter %d -> %d%s (place of %s)", rule.item->name, rule.item->chapter,
                rule.chapter, rule.played >= 0 ? ", after a completed game" : "", rule.place);
        }
    }

    const char* StoreGateCaller(uintptr_t esp)
    {
        // Return addresses at fixed frame offsets from the gate (the store research, section 7).
        const uintptr_t insert = *reinterpret_cast<const uintptr_t*>(esp + 0x90);
        if (insert == 0x005972EC)
            return "save restore";
        if (insert != 0x0059688F)
            return "unknown";
        const uintptr_t add = *reinterpret_cast<const uintptr_t*>(esp + 0xCC);
        if (add == 0x005A0A43)
            return "schematic upload";
        if (add == 0x005A0F30)
            return "content registry";
        if (add != 0x00596949)
            return "AddItem";
        const uintptr_t list = *reinterpret_cast<const uintptr_t*>(esp + 0xFC);
        return list == 0x005A0EE0 ? "kiosk" : list == 0x00525466 ? "player init" : "AddStoreList";
    }

    bool LevelOrderStoreAdmit(safetyhook::Context& ctx);   // level_order.inc
    bool LevelOrderArmed();                                // level_order.inc

    void StoreGateRules(safetyhook::Context& ctx)
    {
        __try
        {
            ApplyFirstWeaponPrices();
            unsigned char* entry = reinterpret_cast<unsigned char*>(ctx.edi);
            const unsigned char* store = reinterpret_cast<const unsigned char*>(ctx.ebp);
            Guid128 guid{};
            std::memcpy(&guid, entry, sizeof(guid));
            StoreGateRule* rule = nullptr;
            for (unsigned i = 0; i < gStoreGateRuleCount && !rule; ++i)
                if (SameGuid(gStoreGateRules[i].item->item, guid))
                    rule = &gStoreGateRules[i];
            if (!rule)
                return;
            if (entry[kStoreEntryForced])
            {
                InterlockedIncrement(&gStoreGateForced);
                return;
            }
            int32_t* chapter = reinterpret_cast<int32_t*>(entry + kStoreEntryChapter);
            int32_t* played = reinterpret_cast<int32_t*>(entry + kStoreEntryPlayed);
            const int32_t storeChapter = *reinterpret_cast<const int32_t*>(store + kStoreChapter);
            const int32_t storePlayed = *reinterpret_cast<const int32_t*>(store + kStorePlayed);
            const int32_t before = *chapter;
            // Only the authored value moves; anything else was set by someone else.
            const bool authored = *chapter == rule->item->chapter && *played == rule->item->played;
            if (authored)
            {
                *chapter = rule->chapter;
                *played = rule->played;
                InterlockedIncrement(&gStoreGateMoved);
            }
            else
                InterlockedIncrement(&gStoreGateForeign);
            const uint32_t bit = 1u << (storeChapter & 31);
            if (rule->loggedChapters & bit)
                return;
            rule->loggedChapters |= bit;
            const bool admitted = *chapter <= storeChapter && *played <= storePlayed;
            Log("store gate: %s from %s, store chapter %d, %d game(s) done, %u in store: requirement %d -> %d%s, %s",
                rule->item->name, StoreGateCaller(ctx.esp), storeChapter, storePlayed,
                *reinterpret_cast<const unsigned*>(store + kStoreCount), before, *chapter,
                authored ? "" : " (NOT the authored value, left alone)", admitted ? "admitted" : "rejected");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gStoreGateFaults);
        }
    }

    void StoreGate_Hook(safetyhook::Context& ctx)
    {
        StoreGateRules(ctx);
        __try
        {
            LevelOrderStoreAdmit(ctx);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gStoreGateFaults);
        }
    }

    // FirstWeaponRandomizer: the seeded weapon takes the Plasma Cutter's place in the store
    // (always there, free: first_weapon.inc swaps the prices) and the cutter takes the
    // weapon's: its schematic rows (before BuildStoreGateRules) and its chapter.
    void SwapFirstWeaponSchematics(const FirstWeapon& weapon)
    {
        const unsigned rows = SwapSchematicTargets(weapon.proxy, weapon.name, kPlasmaCutterProxy, "Plasma Cutter");
        Log("store gate first weapon: the Plasma Cutter takes the %s's %u schematic row(s)", weapon.name, rows);
    }

    // After the other rules. Its ammo is sold when the battery is.
    void AddFirstWeaponRule(const FirstWeapon& weapon)
    {
        const StoreItemRequirement* battery = StoreItemFor(kPlasmaCutterBattery);
        const StoreItemRequirement* ammo = StoreItemFor(weapon.ammo);
        if (battery && ammo)
            SetStoreGateRule(ammo, battery->chapter, battery->played, battery->name);
        else
            Log("store gate: the %s's ammo or the Plasma Cutter Battery is not a store item", weapon.name);
        const StoreItemRequirement* cutter = StoreItemFor(kPlasmaCutterProxy);
        const StoreItemRequirement* target = StoreItemFor(weapon.proxy);
        if (!cutter || !target)
        {
            Log("store gate: the %s or the Plasma Cutter is not a store item", weapon.name);
            return;
        }
        // The cutter's place comes from the weapon's schematic rows it took. Only a weapon
        // with none (Line Gun, Pulse Rifle) hands over its kiosk chapter instead.
        bool cutterPlaced = false;
        int32_t chapter = target->chapter, played = target->played;
        for (unsigned i = 0; i < gStoreGateRuleCount; ++i)
        {
            cutterPlaced = cutterPlaced || gStoreGateRules[i].item == cutter;
            if (gStoreGateRules[i].item == target)
            {
                chapter = gStoreGateRules[i].chapter;
                played = gStoreGateRules[i].played;
            }
        }
        if (!cutterPlaced)
            SetStoreGateRule(cutter, chapter, played, target->name);
        SetStoreGateRule(target, cutter->chapter, cutter->played, cutter->name);
    }

    // After the progression manifest is loaded and reseeded.
    void InstallStoreGate()
    {
        const FirstWeapon* firstWeapon = gFirstWeapon;   // InstallFirstWeapon runs first
        bool schematics = gProgressionStoreGate;
        if (!gProgressionStoreGate)
            Log("store gate off (ProgressionStoreGate=0): moved schematic rewards return through the kiosk "
                "at their authored chapter");
        else if (!gProgressionRandomizer || !gSchematicMappingCount)
        {
            Log("store gate: NOT installed for schematics, it needs ProgressionRandomizer=1 and a schematic manifest");
            schematics = false;
        }
        if (!schematics && !firstWeapon && !LevelOrderArmed())
            return;
        gStoreGateRuleCount = 0;
        if (schematics)
        {
            if (gStartingStoreRandomizer || gExistingSaveStoreMigration || gProgressionStoreCatalog)
                Log("store gate WARNING: StartingStoreRandomizer, ExistingSaveStoreMigration or "
                    "ProgressionStoreCatalog is on as well; those rewrite lists the gate then judges, turn them off");
            if (gProgressionStartingWeapons)
                SwapStartingWeapons();
            if (firstWeapon)
                SwapFirstWeaponSchematics(*firstWeapon);
            BuildStoreGateRules();
            AddStartingWeaponRules();
        }
        if (firstWeapon)
            AddFirstWeaponRule(*firstWeapon);
        LogStoreGateRules();
        gStoreGateHook = VerifiedMid(reinterpret_cast<void*>(kStoreGate), &StoreGate_Hook);
        Log("store gate %s at %08X: %u rule(s), %u store items known",
            gStoreGateHook ? "active" : "NOT installed", static_cast<unsigned>(kStoreGate),
            gStoreGateRuleCount, kStoreItemCount);
    }

    void LogStoreGateTotals()
    {
        static LONG last[4] = {-1, -1, -1, -1};
        const LONG now[4] = {InterlockedCompareExchange(&gStoreGateMoved, 0, 0),
                             InterlockedCompareExchange(&gStoreGateForced, 0, 0),
                             InterlockedCompareExchange(&gStoreGateForeign, 0, 0),
                             InterlockedCompareExchange(&gStoreGateFaults, 0, 0)};
        if (!gStoreGateHook || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("store gate totals: %ld moved, %ld forced (upload or restore), %ld not authored, %ld faulted",
            now[0], now[1], now[2], now[3]);
    }
