    // Death message transplant ([DeadSpace2008Randomizer] DeathMessageTransplant).
    //
    // A randomized enemy is built from a donor's record, so it dies with the
    // donor's death message and the level that spawned it never hears its own.
    // 234 enemy targets send one, nearly all kill counters
    // (mm_supply_depot_npc_killed, mm_quarantine_npc_dead, var_hangar_kill_count,
    // ...), so a quarantine room whose enemy was replaced could never finish
    // (trainer/build_ds1_death_messages.py, analysis/DeadSpace1_DeathMessages.tsv).
    //
    // While the spawn handler runs a replacement, the first HealthComponent the
    // game decodes (0x0045CA70, thiscall(record), ret 4; the dismemberable decoder
    // 0x004507A0 calls it too) gets the source target's message, or the empty slot
    // of a component without one when the source sends none. The slot at +0x130 is
    // released through 0x006D8430 (esi = slot) and set through 0x006D8380 (eax =
    // hash, slot pushed, caller pops), the same pair the decoder and the teardown
    // use. A replacement the spawn hook rejects afterwards loses the message again.
#include "death_message_manifest_generated.inc"
    constexpr uintptr_t kHealthComponentDecode = 0x0045CA70;
    constexpr uintptr_t kMessageSlotAcquire = 0x006D8380;
    constexpr uintptr_t kMessageSlotRelease = 0x006D8430;
    constexpr uint32_t kDeathMessageSlot = 0x130;

    bool gDeathMessageTransplant = true;
    safetyhook::InlineHook gHealthDecodeHook;
    thread_local bool gTransplantArmed = false;
    thread_local Guid128 gTransplantSource{};
    thread_local void* gTransplantComponent = nullptr;   // what the armed call changed
    volatile LONG gTransplantsGiven = 0, gTransplantsMuted = 0, gTransplantFaults = 0, gTransplantLines = 0;
    volatile LONG gTransplantOddSlots = 0, gTransplantsRevoked = 0;

    // Every retail spawn call the spawn hook makes sets the state explicitly and
    // puts the caller's back, so a nested FuncSpawn never takes another's message.
    struct TransplantState
    {
        bool armed;
        Guid128 source;
        void* component;
    };

    // source = the authored target whose message the actor gets; an all-zero
    // GUID is in no manifest row, so the actor sends nothing (extra spawns).
    TransplantState ArmTransplant(bool on, const Guid128& source)
    {
        const TransplantState outer{gTransplantArmed, gTransplantSource, gTransplantComponent};
        gTransplantArmed = gDeathMessageTransplant && on;
        gTransplantSource = source;
        gTransplantComponent = nullptr;
        return outer;
    }

    // Returns the component this call gave a message to, or null.
    void* RestoreTransplant(const TransplantState& outer)
    {
        void* mine = gTransplantComponent;
        gTransplantArmed = outer.armed;
        gTransplantSource = outer.source;
        gTransplantComponent = outer.component;
        return mine;
    }

    const DeathMessageEntry* DeathMessageFor(const Guid128& target)
    {
        for (unsigned i = 0; i < kDeathMessageCount; ++i)
            if (SameGuid(kDeathMessages[i].target, target))
                return &kDeathMessages[i];
        return nullptr;
    }

    __declspec(noinline) void MessageSlotRelease(void* slot)
    {
        const uintptr_t function = kMessageSlotRelease;
        __asm
        {
            mov esi, slot
            mov eax, function
            call eax
        }
    }

    __declspec(noinline) void MessageSlotAcquire(uint32_t hash, void* slot)
    {
        const uintptr_t function = kMessageSlotAcquire;
        __asm
        {
            push slot
            mov eax, hash
            mov ecx, function
            call ecx
            add esp, 4
        }
    }

    // The slot is a handle and a use count (+6): the constructor leaves {0, 0},
    // which is what a component without an authored message keeps; the decoder
    // acquires once, and the release takes it back to {0, 0}. The acquire
    // ignores a new name while the count is not 0. hash 0 = none.
    enum class SlotResult { Changed, Unchanged, Odd, Fault };

    SlotResult SetDeathMessage(void* component, uint32_t hash)
    {
        unsigned char* slot = static_cast<unsigned char*>(component) + kDeathMessageSlot;
        __try
        {
            const uint16_t uses = *reinterpret_cast<const uint16_t*>(slot + 6);
            if (uses > 1)
                return SlotResult::Odd;
            if (!uses && !hash)
                return SlotResult::Unchanged;
            if (uses)
                MessageSlotRelease(slot);
            if (*reinterpret_cast<const uint16_t*>(slot + 6) != 0)
                return SlotResult::Odd;
            if (hash)
                MessageSlotAcquire(hash, slot);
            return SlotResult::Changed;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return SlotResult::Fault;
        }
    }

    void TransplantDeathMessage(void* component)
    {
        const DeathMessageEntry* entry = DeathMessageFor(gTransplantSource);
        const SlotResult result = SetDeathMessage(component, entry ? entry->hash : 0u);
        if (result == SlotResult::Fault || result == SlotResult::Odd)
        {
            InterlockedIncrement(result == SlotResult::Fault ? &gTransplantFaults : &gTransplantOddSlots);
            if (InterlockedIncrement(&gTransplantLines) <= 40)
                Log("death message transplant: component %08X %s, left as the donor made it",
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(component)),
                    result == SlotResult::Fault ? "faulted" : "has a message slot in use more than once");
            return;
        }
        gTransplantComponent = component;
        InterlockedIncrement(entry ? &gTransplantsGiven : &gTransplantsMuted);
        if (InterlockedIncrement(&gTransplantLines) <= 40)
            Log("death message transplant: component %08X of the replacement for %08X-%08X-%08X-%08X now sends %s",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(component)), gTransplantSource.value[0],
                gTransplantSource.value[1], gTransplantSource.value[2], gTransplantSource.value[3],
                entry ? entry->name : "nothing (the source sends no death message, or it is an extra spawn)");
    }

    // A replacement the spawn hook rejected still stands in the room next to the
    // authored enemy replayed after it; only one of them may count.
    void RevokeTransplant(void* component, const char* why)
    {
        if (!component)
            return;
        const SlotResult result = SetDeathMessage(component, 0);
        InterlockedIncrement(&gTransplantsRevoked);
        Log("death message transplant: component %08X sends nothing again (%s)%s",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(component)), why,
            result == SlotResult::Changed || result == SlotResult::Unchanged ? "" : ", FAILED");
    }

    void __fastcall HealthComponentDecode_Hook(void* self, void*, void* record)
    {
        gHealthDecodeHook.thiscall<void>(self, record);
        if (!gTransplantArmed || !self)
            return;
        gTransplantArmed = false;   // the replacement's own component, not a child's
        TransplantDeathMessage(self);
    }

    void LoadDeathMessageSettings()
    {
        gDeathMessageTransplant = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "DeathMessageTransplant", 1, gIniPath) != 0;
    }

    void InstallDeathMessageTransplant()
    {
        if (!gDeathMessageTransplant)
        {
            Log("death message transplant off (DeathMessageTransplant=0): a replaced enemy dies with its donor's message");
            return;
        }
        gHealthDecodeHook = VerifiedInline(
            reinterpret_cast<void*>(kHealthComponentDecode),
            reinterpret_cast<void*>(&HealthComponentDecode_Hook));
        Log("death message transplant %s at %08X: %u authored enemy messages known",
            gHealthDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kHealthComponentDecode), kDeathMessageCount);
    }

    void LogDeathMessageTotals()
    {
        static LONG last[5] = {-1, -1, -1, -1, -1};
        const LONG now[5] = {InterlockedCompareExchange(&gTransplantsGiven, 0, 0),
                             InterlockedCompareExchange(&gTransplantsMuted, 0, 0),
                             InterlockedCompareExchange(&gTransplantsRevoked, 0, 0),
                             InterlockedCompareExchange(&gTransplantOddSlots, 0, 0),
                             InterlockedCompareExchange(&gTransplantFaults, 0, 0)};
        if (!gDeathMessageTransplant || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        Log("death message transplant: %ld given the source's message, %ld muted, %ld revoked, "
            "%ld odd slots, %ld faulted", now[0], now[1], now[2], now[3], now[4]);
    }
