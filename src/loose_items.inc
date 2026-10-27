    // Loose items ([DeadSpace2008Randomizer] LooseItemRandomizer).
    //
    // 150 PowerUps and SpareAmmo lie in the levels on their own (health kits,
    // credits, treasure, air, ammo; loose_items.py). Within one chapter and class
    // the seed deals their contents out again, so a chapter keeps what it had, only
    // elsewhere. Power Nodes, unnamed items, the two chapter 1 batteries the first
    // weapon retargets and the Peng (its pickup sends the achievement message) stay.
    //
    // What a pickup is lives in its own fields, not in the item it grants:
    // PickupItem[2] (+0x418) 10 takes an inventory slot, 11 goes to the wallet;
    // PowerUp[0] (+0x630) marks credits and [8]/[9] their amount, [4] the healing;
    // Item[16] the category, Item[32] the stack, Item[36] the price. Changing only
    // the grant made a full inventory refuse credits and filed credits as an
    // inventory item with a broken icon. So every such field is written, with the
    // kind's value or the constructor's default, after the class decoder (PowerUp
    // 0x00514E50, SpareAmmo 0x00516110) has run; those decode Item, PickupItem and
    // the base classes first and their own fields last. The name string at +0x300
    // then points at a static text with a release function that keeps it.
    struct LooseItemKind
    {
        Guid128 item;
        uint32_t pickupClass;   // 0 PowerUp, 1 SpareAmmo
        const char* name;
        uint32_t model;         // Entity[1], one hash four times
        int32_t price;          // Item[36], what a kiosk charges (it pays half); -1 when the item has none
    };
    constexpr LooseItemKind kLooseItemKinds[] = {
#define LOOSE_ITEM_KINDS
#include "loose_items_generated.inc"
#undef LOOSE_ITEM_KINDS
    };
    constexpr unsigned kLooseItemKindCount = sizeof(kLooseItemKinds) / sizeof(kLooseItemKinds[0]);

    // size 1, 4 or 16 bytes from value; 0x80: value[0] is a mask in a u32, set when value[1] is 1.
    struct LooseItemField
    {
        uint16_t offset;
        uint8_t size;
        uint32_t value[4];
    };
    constexpr unsigned kLooseItemMaxFields = 32;
    struct LooseItemFields
    {
        LooseItemField field[kLooseItemMaxFields];
    };
    constexpr LooseItemFields kLooseItemFields[] = {
#define LOOSE_ITEM_FIELDS
#include "loose_items_generated.inc"
#undef LOOSE_ITEM_FIELDS
    };
    static_assert(sizeof(kLooseItemFields) / sizeof(kLooseItemFields[0]) == kLooseItemKindCount,
                  "one field set per loose item kind");

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
    constexpr uintptr_t kPowerUpDecode = 0x00514E50;
    constexpr uintptr_t kSpareAmmoDecode = 0x00516110;
    // eax = min (+0x644), push max (+0x648): stores both and rolls the amount into +0x64C.
    constexpr uintptr_t kPowerUpRollAmount = 0x00515750;
    constexpr unsigned kPickupSlotType = 0x418;
    constexpr unsigned kPowerUpIsCurrency = 0x630;
    constexpr unsigned kPowerUpAmountMin = 0x644;
    constexpr unsigned kPowerUpAmountMax = 0x648;
    constexpr unsigned kPowerUpAmount = 0x64C;

    bool gLooseItemRandomizer = false;
    unsigned gLooseAssignment[kLooseItemCount]{};
    volatile LONG gLooseItemsChanged = 0, gLooseItemsSeen = 0, gLooseItemsFailed = 0;
    safetyhook::InlineHook gPowerUpDecodeHook, gSpareAmmoDecodeHook;

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

    bool RollPowerUpAmount(uintptr_t object, uint32_t minimum, uint32_t maximum)
    {
        __try
        {
            __asm
            {
                push esi
                mov esi, object
                push maximum
                mov eax, minimum
                mov ecx, kPowerUpRollAmount
                call ecx
                pop esi
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    unsigned WriteLooseItemFields(uintptr_t object, const LooseItemFields& fields)
    {
        unsigned written = 0;
        for (const LooseItemField& field : fields.field)
        {
            if (!field.size)
                break;
            void* at = reinterpret_cast<void*>(object + field.offset);
            bool ok = false;
            if (field.size == 0x80)
            {
                uint32_t flags = ReadField<uint32_t>(reinterpret_cast<const void*>(object), field.offset);
                flags = field.value[1] ? flags | field.value[0] : flags & ~field.value[0];
                ok = WriteProcessMemory(GetCurrentProcess(), at, &flags, sizeof(flags), nullptr) != 0;
            }
            else if (field.size == 1)
            {
                const uint8_t value = static_cast<uint8_t>(field.value[0]);
                ok = WriteProcessMemory(GetCurrentProcess(), at, &value, sizeof(value), nullptr) != 0;
            }
            else
                ok = WriteProcessMemory(GetCurrentProcess(), at, field.value, field.size, nullptr) != 0;
            written += ok;
        }
        return written;
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
        const unsigned authoredKind = kLooseItems[index].kind;
        const unsigned giveKind = gLooseAssignment[index];
        const LooseItemKind& authored = kLooseItemKinds[authoredKind];
        const LooseItemKind& give = kLooseItemKinds[giveKind];
        if (giveKind == authoredKind || !SameGuid(ReadField<Guid128>(pickup, kPickupGrantedItem), authored.item))
            return;
        const int32_t oldPrice = ReadField<int32_t>(pickup, kItemPrice);
        const uint32_t oldSlot = ReadField<uint32_t>(pickup, kPickupSlotType);
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kPickupGrantedItem), &give.item,
                                sizeof(give.item), nullptr))
        {
            InterlockedIncrement(&gLooseItemsFailed);
            return;
        }
        const unsigned written = WriteLooseItemFields(object, kLooseItemFields[giveKind]);
        // Credits roll their amount from [8]/[9] the way the decoder does; anything else carries none.
        bool rolled = false;
        if (give.pickupClass == 0 && ReadField<uint8_t>(pickup, kPowerUpIsCurrency))
            rolled = RollPowerUpAmount(object, ReadField<uint32_t>(pickup, kPowerUpAmountMin),
                                       ReadField<uint32_t>(pickup, kPowerUpAmountMax));
        else if (give.pickupClass == 0)
        {
            const uint32_t none = 0;
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object + kPowerUpAmount), &none,
                               sizeof(none), nullptr);
        }
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
        // The PowerUp transform is the 4x4 at +0x7C (world_editor.inc), its position at +0xAC; +0x104 is
        // logged too, it followed the floor height in the logs. Both tell where a dropped item lay.
        const float* at = reinterpret_cast<const float*>(object + 0xAC);
        const float height = ReadField<float>(pickup, 0x104);
        if (InterlockedIncrement(&gLooseItemsChanged) <= 64 || !modelled)
            Log("loose item %08X (%08X-%08X, chapter %u, at %.2f %.2f %.2f, +0x104 %.2f, model %08X -> %08X): %s -> "
                "%s, %u fields, slot type %u -> %u, price %d -> %d, amount %d%s%s", static_cast<unsigned>(object),
                own.value[0], own.value[1], kLooseItems[index].chapter, ReadField<float>(at, 0),
                ReadField<float>(at, 4), ReadField<float>(at, 8), height, authored.model, give.model,
                authored.name, give.name, written, oldSlot, ReadField<uint32_t>(pickup, kPickupSlotType), oldPrice,
                ReadField<int32_t>(pickup, kItemPrice),
                give.pickupClass == 0 ? ReadField<int32_t>(pickup, kPowerUpAmount) : -1,
                rolled ? " (rolled)" : "", modelled ? "" : ", its model could NOT be swapped");
    }

    void __fastcall PowerUpDecode_Hook(void* self, void*, void* attributes)
    {
        gPowerUpDecodeHook.thiscall<void>(self, attributes);
        ApplyLooseItem(self);
    }

    void __fastcall SpareAmmoDecode_Hook(void* self, void*, void* attributes)
    {
        gSpareAmmoDecodeHook.thiscall<void>(self, attributes);
        ApplyLooseItem(self);
    }

    void LoadLooseItemSettings()
    {
        gLooseItemRandomizer = GetPrivateProfileIntA("DeadSpace2008Randomizer", "LooseItemRandomizer", 0, gIniPath) != 0;
        if (gLooseItemRandomizer)
            BuildLooseItems();
    }

    void InstallLooseItems()
    {
        if (!gLooseItemRandomizer)
            return;
        gPowerUpDecodeHook =
            VerifiedInline(reinterpret_cast<void*>(kPowerUpDecode), reinterpret_cast<void*>(&PowerUpDecode_Hook));
        gSpareAmmoDecodeHook =
            VerifiedInline(reinterpret_cast<void*>(kSpareAmmoDecode), reinterpret_cast<void*>(&SpareAmmoDecode_Hook));
        Log("loose items: PowerUp decode hook %s at %08X, SpareAmmo decode hook %s at %08X",
            gPowerUpDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kPowerUpDecode),
            gSpareAmmoDecodeHook ? "active" : "NOT installed", static_cast<unsigned>(kSpareAmmoDecode));
    }
