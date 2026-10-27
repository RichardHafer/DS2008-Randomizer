    // Store hooks an extension can ask for (ds2008_extension.h, wantsStoreHooks).
    //
    // PlayerStore::AddItem 0x00596830 (eax = &GUID, stack: store, force; ret 8)
    // copies the GUID first (0x00596833) and passes the copy to InsertEntry, which
    // returns on a zero GUID. A mid hook at the entry can point eax at another GUID,
    // or at zeros to keep an item out, without touching the source list. The gate
    // (store_gate.inc) then lets the extension set the entry's requirement after
    // the host's rules.
    constexpr uintptr_t kPlayerStoreAddItem = 0x00596830;
    safetyhook::MidHook gStoreAddItemHook;
    thread_local Guid128 gStoreAddItemGuid{};
    volatile LONG gStoreAddItemCalls = 0, gStoreAddItemReplaced = 0, gStoreAddItemHidden = 0,
                  gStoreAddItemFaults = 0;

    uint32_t ExtensionSeed()
    {
        return LoadStartingSuitSeed();
    }

    uintptr_t ExtensionObjectFor(const uint32_t* guid)
    {
        if (!guid)
            return 0;
        Guid128 value{};
        std::memcpy(&value, guid, sizeof(value));
        const uintptr_t key = TargetKeyFor(value);
        return key ? ReadField<uintptr_t>(reinterpret_cast<const void*>(key), 0x14) : 0;
    }

    bool ExtensionFirstWeaponItem(uint32_t* guid)
    {
        if (!guid || !gFirstWeapon)
            return false;
        std::memcpy(guid, gFirstWeapon->proxy.value, sizeof(gFirstWeapon->proxy.value));
        return true;
    }

    bool ExtensionStoreRequirement(const uint32_t* guid, int32_t* chapter, int32_t* played)
    {
        if (!guid || !chapter || !played)
            return false;
        Guid128 value{};
        std::memcpy(&value, guid, sizeof(value));
        for (unsigned i = 0; i < gStoreGateRuleCount; ++i)
            if (SameGuid(gStoreGateRules[i].item->item, value))
            {
                *chapter = gStoreGateRules[i].chapter;
                *played = gStoreGateRules[i].played;
                return true;
            }
        if (const StoreItemRequirement* item = StoreItemFor(value))
        {
            *chapter = item->chapter;
            *played = item->played;
            return true;
        }
        return false;
    }

    void StoreAddItem_Hook(safetyhook::Context& ctx)
    {
        if (!gExtension || !gExtension->storeAddItem || !ctx.eax)
            return;
        InterlockedIncrement(&gStoreAddItemCalls);
        __try
        {
            const uintptr_t caller = *reinterpret_cast<const uintptr_t*>(ctx.esp);
            std::memcpy(&gStoreAddItemGuid, reinterpret_cast<const void*>(ctx.eax), sizeof(gStoreAddItemGuid));
            const int answer = gExtension->storeAddItem(gStoreAddItemGuid.value, caller);
            if (answer == ds2008::kStoreHide)
            {
                gStoreAddItemGuid = Guid128{};
                InterlockedIncrement(&gStoreAddItemHidden);
            }
            else if (answer == ds2008::kStoreReplaced)
                InterlockedIncrement(&gStoreAddItemReplaced);
            else
                return;
            ctx.eax = reinterpret_cast<uintptr_t>(&gStoreAddItemGuid);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gStoreAddItemFaults);
        }
    }

    // From the store gate, after the host's own rules. edi = the entry, a stack copy InsertEntry stores afterwards.
    void ExtensionStoreGate(safetyhook::Context& ctx)
    {
        if (!gExtension || !gExtension->storeGate)
            return;
        __try
        {
            unsigned char* entry = reinterpret_cast<unsigned char*>(ctx.edi);
            gExtension->storeGate(reinterpret_cast<const uint32_t*>(entry),
                                  reinterpret_cast<int32_t*>(entry + kStoreEntryChapter),
                                  reinterpret_cast<int32_t*>(entry + kStoreEntryPlayed), entry[kStoreEntryForced] != 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gStoreGateFaults);
        }
    }

    void InstallExtensionStore()
    {
        if (!ExtensionWantsStore())
            return;
        gStoreAddItemHook = VerifiedMid(reinterpret_cast<void*>(kPlayerStoreAddItem), &StoreAddItem_Hook);
        if (!gStoreGateHook)
            gStoreGateHook = VerifiedMid(reinterpret_cast<void*>(kStoreGate), &StoreGate_Hook);
        Log("extension store: AddItem hook %s at %08X, store gate %s",
            gStoreAddItemHook ? "active" : "NOT installed", static_cast<unsigned>(kPlayerStoreAddItem),
            gStoreGateHook ? "active" : "NOT installed");
    }

    void LogExtensionStoreTotals()
    {
        static LONG last[4] = {-1, -1, -1, -1};
        const LONG now[4] = {gStoreAddItemCalls, gStoreAddItemReplaced, gStoreAddItemHidden, gStoreAddItemFaults};
        if (!gStoreAddItemHook || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("extension store totals: %ld items offered, %ld replaced, %ld kept out, %ld faulted", now[0], now[1],
            now[2], now[3]);
    }
