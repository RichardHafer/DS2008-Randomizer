    // =====================================================================
    // Loose items ([DeadSpace1Patch] LooseItemRandomizer).
    //
    // 151 PowerUps and SpareAmmo lie in the levels on their own (health kits,
    // credits, treasure, air, ammo; loose_items.py). Within one chapter and one
    // class the seed deals their contents out anew, so a chapter keeps what it
    // had, only elsewhere, and a pickup only ever gives what its own class
    // can. Power Nodes, unnamed items and the two chapter 1 batteries the
    // first weapon retargets stay. The pickup decoder hook (0x00513260)
    // writes the grant (+0x344) and what the prompt shows: the text keys,
    // icons and the name, whose string at +0x300 then points at our own text
    // with a release function that keeps it.
    // =====================================================================
    struct LooseItemKind
    {
        Guid128 item;
        uint32_t pickupClass;   // 0 PowerUp, 1 SpareAmmo
        const char* name;
        uint32_t keys[4];
        uint32_t icons[3];
        uint32_t model;         // Entity[1], one hash four times
    };
    constexpr LooseItemKind kLooseItemKinds[] = {
#define LOOSE_ITEM_KINDS
#include "loose_items_generated.inc"
#undef LOOSE_ITEM_KINDS
    };
    struct LooseItem
    {
        Guid128 entity;
        uint32_t chapter;
        uint32_t kind;
    };
    constexpr LooseItem kLooseItems[] = {
#include "loose_items_generated.inc"
    };
    constexpr unsigned kLooseItemCount = sizeof(kLooseItems) / sizeof(kLooseItems[0]);
    constexpr uint32_t kLoosePowerUpVtable = 0x00C6FAA0;

    bool gLooseItemRandomizer = false;
    unsigned gLooseAssignment[kLooseItemCount]{};
    volatile LONG gLooseItemsChanged = 0, gLooseItemsSeen = 0;

    void __cdecl KeepLooseItemName(void*)
    {
    }

    // The model follows the item. Entity[1] is a key the Entity decoder (0x0072DF48) looks
    // up in the resource table [0x00F44350] (0x006E5030: esi = key, edi = table) and hands
    // to SetModel (vtable +0xE0, 0x006F1FA0, thiscall, ret 4), which releases the model it
    // had first (+0x110). Item models live in global_assets, which is always loaded.
    constexpr uintptr_t kResourceTable = 0x00F44350;
    constexpr uintptr_t kResourceLookup = 0x006E5030;
    constexpr unsigned kEntitySetModel = 0xE0;

    uintptr_t LookupModelResource(const Guid128* key)
    {
        const uintptr_t table = *reinterpret_cast<const uintptr_t*>(kResourceTable);
        if (!table)
            return 0;
        uintptr_t resource = 0;
        __asm
        {
            push esi
            push edi
            mov esi, key
            mov edi, table
            mov eax, kResourceLookup
            call eax
            mov resource, eax
            pop edi
            pop esi
        }
        return resource;
    }

    bool SetLooseItemModel(uintptr_t object, uint32_t model)
    {
        if (!model)
            return false;
        __try
        {
            const Guid128 key{{model, model, model, model}};
            const uintptr_t resource = LookupModelResource(&key);
            if (!resource)
                return false;
            using SetModelFn = void(__thiscall*)(void*, uintptr_t);
            const uintptr_t* vtable = *reinterpret_cast<const uintptr_t* const*>(object);
            reinterpret_cast<SetModelFn>(vtable[kEntitySetModel / 4])(reinterpret_cast<void*>(object), resource);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Per chapter and class, the kinds those items hold are dealt out again.
    void BuildLooseItems()
    {
        bool done[kLooseItemCount]{};
        for (unsigned first = 0; first < kLooseItemCount; ++first)
        {
            if (done[first])
                continue;
            const uint32_t chapter = kLooseItems[first].chapter;
            const uint32_t pickupClass = kLooseItemKinds[kLooseItems[first].kind].pickupClass;
            unsigned members[kLooseItemCount]{}, kinds[kLooseItemCount]{};
            unsigned count = 0;
            for (unsigned i = first; i < kLooseItemCount; ++i)
                if (!done[i] && kLooseItems[i].chapter == chapter &&
                    kLooseItemKinds[kLooseItems[i].kind].pickupClass == pickupClass)
                {
                    done[i] = true;
                    members[count] = i;
                    kinds[count++] = kLooseItems[i].kind;
                }
            uint32_t state = (gStartingSuitSeed ^ 0x4C4F4F53u) + chapter * 0x9E3779B9u + pickupClass * 0x85EBCA6Bu;
            if (!state)
                state = 1;
            for (unsigned i = count; i > 1; --i)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                const unsigned j = state % i;
                const unsigned swap = kinds[i - 1];
                kinds[i - 1] = kinds[j];
                kinds[j] = swap;
            }
            for (unsigned i = 0; i < count; ++i)
                gLooseAssignment[members[i]] = kinds[i];
        }
        unsigned moved = 0;
        for (unsigned i = 0; i < kLooseItemCount; ++i)
            moved += gLooseAssignment[i] != kLooseItems[i].kind;
        Log("loose items: %u in the levels, %u now hold something else (seed %08X)", kLooseItemCount, moved,
            gStartingSuitSeed);
    }

    void ApplyLooseItem(void* pickup)
    {
        if (!gLooseItemRandomizer || !pickup)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(pickup);
        const uint32_t vtable = ReadField<uint32_t>(pickup, 0);
        if (vtable != kLoosePowerUpVtable && vtable != kSpareAmmoPickupVtable)
            return;
        const Guid128 own = ReadField<Guid128>(pickup, 0x20);
        unsigned index = kLooseItemCount;
        for (unsigned i = 0; i < kLooseItemCount && index == kLooseItemCount; ++i)
            if (SameGuid(kLooseItems[i].entity, own))
                index = i;
        if (index == kLooseItemCount)
            return;
        InterlockedIncrement(&gLooseItemsSeen);
        const LooseItemKind& authored = kLooseItemKinds[kLooseItems[index].kind];
        const LooseItemKind& give = kLooseItemKinds[gLooseAssignment[index]];
        if (&give == &authored || !SameGuid(ReadField<Guid128>(pickup, kPickupGrantedItem), authored.item))
            return;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kPickupGrantedItem), &give.item,
                                sizeof(give.item), nullptr))
            return;
        WriteItemTexts(object, give.keys);
        const PickupLabel icons{{0, 0, 0, 0}, "", {give.icons[0], give.icons[1], give.icons[2]}};
        WritePickupNameAndIcons(object, icons);   // icons only: the empty name is left out
        struct
        {
            const char* data;
            uint32_t length;
            uint32_t capacity;
            void(__cdecl* release)(void*);
        } name{give.name, static_cast<uint32_t>(std::strlen(give.name)),
               static_cast<uint32_t>(std::strlen(give.name) + 1), &KeepLooseItemName};
        const uint32_t hash = PickupNameHash(give.name);
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kPickupName), &name, sizeof(name),
                           nullptr);
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kPickupNameHash), &hash,
                           sizeof(hash), nullptr);
        const bool modelled = give.model == authored.model || SetLooseItemModel(object, give.model);
        if (InterlockedIncrement(&gLooseItemsChanged) <= 24 || !modelled)
            Log("loose item %08X (%08X-%08X, chapter %u): %s -> %s%s", static_cast<unsigned>(object), own.value[0],
                own.value[1], kLooseItems[index].chapter, authored.name, give.name,
                modelled ? "" : " (its model could NOT be swapped)");
    }

    void LoadLooseItemSettings()
    {
        gLooseItemRandomizer = GetPrivateProfileIntA("DeadSpace1Patch", "LooseItemRandomizer", 0, gIniPath) != 0;
        if (gLooseItemRandomizer)
            BuildLooseItems();
    }
