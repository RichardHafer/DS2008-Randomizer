    // =====================================================================
    // Corpses that get up.
    //
    // DSNPCSpawningComponent (class hash 0x0CFD7944, decoder at 0x004561C0,
    // dispatch table 0x0045654C) decides how a necromorph arrives.  Property
    // 0 at +0x0C is the arrival kind: 1 climbs out of a vent, 3 is already
    // lying in the room and gets up when the player comes close.  Property 1
    // at +0x10 picks the pose; the fourteen corpses the game authors use 2, 4
    // and 6.  Property 2 at +0x14 is a sixteen-byte reference the vent form
    // carries and the lying form does not.
    //
    // An earlier attempt rewrote this in the archives and hung the loading
    // screen: the lying form has one authored value fewer, so writing it over
    // a vent record shifted every value behind it and the decoder read past
    // the end of the record.  Doing it here instead sidesteps that entirely.
    // The record is decoded exactly as authored and only the finished object
    // changes, so nothing about lengths or the value stream is touched.  The
    // vent reference at +0x14 simply stops being read once the kind is 3.
    //
    // Safety comes from a positive list again.  Measured over all 794
    // archives: 999 spawning components, 424 of them vent, and every one of
    // the fourteen corpses the game ships is a Zombie - no Leaper, Lurker or
    // Exploder is ever authored lying down, so those very likely have no pose
    // for it and are left alone.  The component cannot see which creature it
    // belongs to, so identity comes from that +0x14 reference: 105 of them
    // belong exclusively to Zombie vent spawners, and 90 of those name a
    // single spawner.  A reference shared by several spawners converts them
    // together, which reads as a room with several bodies in it.
    // =====================================================================
    // Defined in variant_runtime.inc, which is included further down.  Both
    // features need the same decoder, and two inline hooks on one function
    // would fight, so there is one hook and it calls both.
    void ApplyVariantSpawnPlacement(void* component);

    constexpr uintptr_t kSpawningDecode = 0x004561C0;
    constexpr unsigned kSpawnKind = 0x0C;
    constexpr unsigned kSpawnPose = 0x10;
    constexpr unsigned kSpawnAnchor = 0x14;
    constexpr unsigned kSpawnKindVentArrival = 1;
    constexpr unsigned kSpawnKindLying = 3;
    // The three poses the shipped corpses use.
    constexpr unsigned kCorpsePoses[] = {2, 4, 6};
    constexpr unsigned kCorpsePoseCount =
        sizeof(kCorpsePoses) / sizeof(kCorpsePoses[0]);

    constexpr Guid128 kCorpseAnchors[] = {
        {{0x30C83C57u, 0x55DF1522u, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_c.str
        {{0x30C83CECu, 0x5C1DC3F3u, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_core.str
        {{0x30C842A6u, 0x51CA4FC4u, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_core.str
        {{0x30C842A8u, 0x7D0B16DEu, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_core.str
        {{0x30C842B8u, 0x43957F51u, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_d.str
        {{0x30C842B8u, 0xA196D852u, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_d.str
        {{0x30C842B8u, 0xAEF9723Cu, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_d.str
        {{0x30C87406u, 0x5F856765u, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_c.str
        {{0x30C8A1A1u, 0xCCA465FDu, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_d.str
        {{0x30C8A1A3u, 0x57F80DBDu, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_d.str
        {{0x30C8A1A3u, 0xB596CF3Du, 0x4E484F4Au, 0x324E4F53u}},  // 1 spawner(s), char_d.str
        {{0x33C80626u, 0x057BA60Cu, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), research_spoke_npcs.str
        {{0x33C80626u, 0x8A165672u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), research_spoke_npcs.str
        {{0x33C80627u, 0x21D49F63u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), research_spoke_npcs.str
        {{0x33C806C9u, 0x7D16A90Fu, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), research_spoke_npcs.str
        {{0x33C80ACDu, 0x2194CD4Au, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), critical_care_spoke_npcs.str
        {{0x33C80AD9u, 0x7CBFACAFu, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), critical_care_spoke_npcs.str
        {{0x33C80C47u, 0xFEC8BE9Cu, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emergency_spoke_npcs.str
        {{0x33C80C48u, 0x4AE711B6u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emergency_spoke_npcs.str
        {{0x33C8128Du, 0xEE8D6C71u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emregency_spoke_npcs.str
        {{0x33C8128Eu, 0x05E9138Du, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emregency_spoke_npcs.str
        {{0x33C8128Eu, 0x3673B0F9u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emregency_spoke_npcs.str
        {{0x33C814D5u, 0x122A18CFu, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), research_spoke_npcs.str
        {{0x33C81503u, 0x4B236D44u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emregency_spoke_npcs.str
        {{0x33C85C5Cu, 0x9F76106Du, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), emergency_spoke_npcs.str
        {{0x33C862C9u, 0xD92093E6u, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), critical_care_spoke_npcs.str
        {{0x33C86EACu, 0x7E28A47Au, 0x45544150u, 0x3233374Cu}},  // 1 spawner(s), research_spoke_npcs.str
        {{0x33C89A89u, 0x42A9FD6Bu, 0x45544150u, 0x3336304Cu}},  // 1 spawner(s), critical_care_spoke_npcs.str
        {{0x35C8D7FAu, 0x5E11F6D7u, 0x44455257u, 0x38343145u}},  // 1 spawner(s), mini_global_cargoArea.str
        {{0x35C8D7FBu, 0x2332495Fu, 0x44455257u, 0x38343145u}},  // 1 spawner(s), mini_global_cargoArea.str
        {{0x35C8D7FBu, 0x2484D867u, 0x44455257u, 0x38343145u}},  // 1 spawner(s), mini_global_cargoArea.str
        {{0x35C8D7FBu, 0x787F52E3u, 0x44455257u, 0x38343145u}},  // 1 spawner(s), mini_global_cargoArea.str
        {{0x35C8D7FBu, 0xFA09636Fu, 0x44455257u, 0x38343145u}},  // 1 spawner(s), mini_global_cargoArea.str
        {{0x36C87A48u, 0x591F9A63u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), barracks.str
        {{0x36C87A5Eu, 0x4D341548u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), barracks.str
        {{0x36C87A69u, 0xCB9C1D99u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), barracks.str
        {{0x36C89209u, 0xB2849DFCu, 0x44455257u, 0x34303245u}},  // 1 spawner(s), barracks.str
        {{0x36C8A56Au, 0x9F77F9F7u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), top_floor.str
        {{0x36C8A56Au, 0x9F77F9F8u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), top_floor.str
        {{0x36C8A56Au, 0xB08A3060u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), bottom_floor.str
        {{0x36C8A56Au, 0xB08A3061u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), bottom_floor.str
        {{0x36C8A56Au, 0xB08A3063u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), bottom_floor.str
        {{0x36C8A56Au, 0xBB0B8CF4u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), bottom_floor.str
        {{0x36C8A56Au, 0xD2DE4C41u, 0x44455257u, 0x34303245u}},  // 1 spawner(s), entrance.str
        {{0x38C82242u, 0xFE9B37D5u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_mining_sub_deck_control.str
        {{0x38C82243u, 0x19D6B610u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_mining_sub_deck_control.str
        {{0x38C82243u, 0x1A46C524u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_mining_sub_deck_control.str
        {{0x38C8287Cu, 0xB5F74BDBu, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_elevator.str
        {{0x38C8287Fu, 0x72592193u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C82881u, 0x34B1E927u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C8288Du, 0x8203EA1Bu, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C8288Du, 0x8203EA21u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C828B7u, 0xBFEBAECDu, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_maint_sub_deck_nicole.str
        {{0x38C828B7u, 0xC102A522u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_maint_sub_deck_nicole.str
        {{0x38C828B7u, 0xC233D5A2u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_maint_sub_deck_nicole.str
        {{0x38C82ACFu, 0x9DC1702Bu, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C832CFu, 0x1A939A64u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C832CFu, 0x4E1E0668u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C835D4u, 0x442A0630u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_mining_sub_deck_control.str
        {{0x38C838F1u, 0xE0A8D881u, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C838F1u, 0xEAD931EEu, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_global.str
        {{0x38C878FCu, 0xE0A1D20Au, 0x54544950u, 0x32303253u}},  // 1 spawner(s), npc_stream_maint_sub_deck_nicole.str
        {{0x38C8ECF3u, 0x3A62C306u, 0x43495053u, 0x36524548u}},  // 1 spawner(s), mini_global_lounge.str
        {{0x45C85936u, 0x4E614EB9u, 0x41455753u, 0x474E4952u}},  // 1 spawner(s), mini_global_west_1.str
        {{0x45C8593Eu, 0x40B31C7Bu, 0x41455753u, 0x474E4952u}},  // 1 spawner(s), mini_global_west_1.str
        {{0x45C8595Au, 0x10805C83u, 0x41455753u, 0x474E4952u}},  // 1 spawner(s), mini_global_central_1.str
        {{0x45C8DC70u, 0xA6CB45F4u, 0x43495053u, 0x54524548u}},  // 1 spawner(s), mini_global_lounge.str
        {{0x45C8DC70u, 0xBD6A75E6u, 0x43495053u, 0x54524548u}},  // 1 spawner(s), mini_global_lounge.str
        {{0x45C8DC71u, 0x4A5A5818u, 0x43495053u, 0x54524548u}},  // 1 spawner(s), mini_global_lounge.str
        {{0x45C8DC71u, 0x6F26431Eu, 0x43495053u, 0x54524548u}},  // 1 spawner(s), mini_global_lounge.str
        {{0x49C86F54u, 0x752B2385u, 0x4B544E45u, 0x4B53574Fu}},  // 8 spawner(s), global_tram_reapir.str
        {{0x49C86F55u, 0xF336B3AEu, 0x4B544E45u, 0x4B53574Fu}},  // 8 spawner(s), global_tram_reapir.str
        {{0x49C8734Au, 0x06FA5152u, 0x4B544E45u, 0x4B53574Fu}},  // 2 spawner(s), global_halla.str
        {{0x49C87A5Bu, 0x27058433u, 0x4B544E45u, 0x4B53574Fu}},  // 2 spawner(s), global_halla.str
        {{0x49C87B17u, 0xCF865A55u, 0x4B544E45u, 0x4B53574Fu}},  // 1 spawner(s), mini_global_lounge.str
        {{0x49C87B17u, 0xDBC598E9u, 0x4B544E45u, 0x4B53574Fu}},  // 1 spawner(s), mini_global_lounge.str
        {{0x49C87B17u, 0xE8C131E1u, 0x4B544E45u, 0x4B53574Fu}},  // 1 spawner(s), mini_global_lounge.str
        {{0x49C87F2Cu, 0x33B2778Cu, 0x4B544E45u, 0x4B53574Fu}},  // 2 spawner(s), global_maintenance_bay.str
        {{0x49C88A46u, 0x0F557FA0u, 0x4B544E45u, 0x4B53574Fu}},  // 8 spawner(s), global_tram_reapir.str
        {{0x49C89E9Au, 0x66272737u, 0x4B544E45u, 0x4B53574Fu}},  // 2 spawner(s), global_workshop.str
        {{0x4BC7D85Fu, 0x530C5F4Au, 0x544E4543u, 0x53574F4Bu}},  // 1 spawner(s), global_south_hunting_ground.str
        {{0x4BC835E7u, 0x607D4741u, 0x544E4543u, 0x53574F4Bu}},  // 2 spawner(s), global_maintenance_bay.str
        {{0x4BC835E8u, 0xB7C8C033u, 0x544E4543u, 0x53574F4Bu}},  // 2 spawner(s), global_maintenance_bay.str
        {{0x4BC835E9u, 0x2C11B1CFu, 0x544E4543u, 0x53574F4Bu}},  // 2 spawner(s), global_maintenance_bay.str
        {{0x4BC8392Eu, 0x842CA4E4u, 0x544E4543u, 0x53574F4Bu}},  // 2 spawner(s), global_tram_tunnel.str
        {{0x4BC8C4F9u, 0x35A460D6u, 0x544E4543u, 0x53574F4Bu}},  // 1 spawner(s), global_decon.str
        {{0x4BC8D1B2u, 0xF3E2C22Cu, 0x544E4543u, 0x53574F4Bu}},  // 2 spawner(s), global_halla.str
        {{0x4BC8E6EDu, 0x11631204u, 0x544E4543u, 0x53574F4Bu}},  // 1 spawner(s), global_arena.str
        {{0x58C7F401u, 0x423AF7F5u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_arena.str
        {{0x58C7F401u, 0xB94BD9F8u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_arena.str
        {{0x58C7F402u, 0x72CBAA48u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_arena.str
        {{0x58C7F409u, 0xBADBBF26u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_centrifuge.str
        {{0x58C7F40Au, 0xCD349A23u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_decon.str
        {{0x58C7F413u, 0x940E1E7Fu, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_north_hunting_ground.str
        {{0x58C7F414u, 0x98237CD1u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), global_north_hunting_ground.str
        {{0x58C7F4BAu, 0x2D319236u, 0x3030554Cu, 0x58583732u}},  // 2 spawner(s), global_hangar.str
        {{0x58C7F4BCu, 0x3B992CBFu, 0x3030554Cu, 0x58583732u}},  // 4 spawner(s), global_halla.str
        {{0x58C7F4BEu, 0x62396985u, 0x3030554Cu, 0x58583732u}},  // 2 spawner(s), global_maintenance_bay.str
        {{0x58C7F4C2u, 0x2C5F2F31u, 0x3030554Cu, 0x58583732u}},  // 1 spawner(s), emergency_spoke_npcs.str
        {{0x5AC83448u, 0xB1AD2781u, 0x52444F52u, 0x45554749u}},  // 1 spawner(s), Shipsystems_NPC_entities.str
        {{0x5AC83448u, 0xB1AD2784u, 0x52444F52u, 0x45554749u}},  // 1 spawner(s), Shipsystems_NPC_entities.str
        {{0x5AC837A5u, 0x4216B8A1u, 0x52444F52u, 0x45554749u}},  // 1 spawner(s), Atrium_NPC_Entities.str
        {{0x5AC837A5u, 0x8543BE46u, 0x52444F52u, 0x45554749u}},  // 1 spawner(s), Atrium_NPC_Entities.str
        {{0x5AC86051u, 0x943EC6F9u, 0x52444F52u, 0x45554749u}},  // 1 spawner(s), Shipsystems_NPC_entities.str
        {{0x5AC8D251u, 0x0C311264u, 0x52444F52u, 0x45554749u}},  // 1 spawner(s), ads_liftroom_combat.str
    };

    constexpr unsigned kCorpseAnchorCount =
        sizeof(kCorpseAnchors) / sizeof(kCorpseAnchors[0]);

    safetyhook::InlineHook gSpawningDecodeHook;
    bool gCorpseRandomizer = false;
    int gCorpsePercent = 20;
    unsigned gCorpsesConverted = 0;
    unsigned gCorpseAnchorsSeen = 0;

    uint32_t MixCorpseKey(const Guid128& anchor, uint32_t salt)
    {
        uint32_t mixed = gStartingSuitSeed ^ salt;
        for (unsigned i = 0; i < 4; ++i)
            mixed = (mixed ^ anchor.value[i]) * 0x01000193u;
        mixed ^= mixed >> 16;
        mixed *= 0x7FEB352Du;
        mixed ^= mixed >> 15;
        return mixed;
    }

    bool IsCorpseAnchor(const Guid128& anchor)
    {
        for (unsigned i = 0; i < kCorpseAnchorCount; ++i)
            if (SameGuid(anchor, kCorpseAnchors[i]))
                return true;
        return false;
    }

    void ApplySpawnKind(void* self)
    {
        if (!gCorpseRandomizer || !self || gCorpsePercent <= 0)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(self);

        uint32_t kind = 0;
        Guid128 anchor{};
        __try
        {
            std::memcpy(&kind, reinterpret_cast<const void*>(object + kSpawnKind),
                        sizeof(kind));
            std::memcpy(&anchor,
                        reinterpret_cast<const void*>(object + kSpawnAnchor),
                        sizeof(anchor));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        if (kind != kSpawnKindVentArrival)
            return;
        if (!IsCorpseAnchor(anchor))
            return;

        ++gCorpseAnchorsSeen;
        // Keyed on the anchor, so a spawner is a corpse for the whole run and
        // reloading a checkpoint never turns it back into a vent.
        const uint32_t roll = MixCorpseKey(anchor, 0x434F5250u);
        if (static_cast<int>(roll % 100u) >= gCorpsePercent)
            return;

        const uint32_t newKind = kSpawnKindLying;
        const uint32_t pose =
            kCorpsePoses[MixCorpseKey(anchor, 0x504F5345u) % kCorpsePoseCount];
        SIZE_T written = 0;
        if (!WriteProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<void*>(object + kSpawnKind),
                                &newKind, sizeof(newKind), &written))
            return;
        WriteProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<void*>(object + kSpawnPose),
                           &pose, sizeof(pose), &written);
        ++gCorpsesConverted;
        if (gCorpsesConverted <= 12)
            Log("corpse: spawner %08X now lies in wait, pose %u (%u of %u "
                "known anchors seen)", static_cast<unsigned>(object), pose,
                gCorpsesConverted, gCorpseAnchorsSeen);
    }

    void __fastcall SpawningDecode_Hook(void* self, void*, void* attributes)
    {
        gSpawningDecodeHook.thiscall<void>(self, attributes);
        ApplySpawnKind(self);
        ApplyVariantSpawnPlacement(self);
    }

    void LoadCorpseSettings()
    {
        gCorpseRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "PlayingDeadCorpses", 0, gIniPath) != 0;
        gCorpsePercent = GetPrivateProfileIntA(
            "DeadSpace1Patch", "PlayingDeadPercent", 20, gIniPath);
        if (gCorpsePercent < 0)
            gCorpsePercent = 0;
        if (gCorpsePercent > 100)
            gCorpsePercent = 100;
    }

    void InstallCorpseHooks()
    {
        // The super exploder needs this decoder too: its only donors emerge
        // from a vent, and a direct spawn is made by writing the arrival kind
        // back to the constructor default on the finished object.
        if (!gCorpseRandomizer && !gSuperExploderWanted)
            return;
        gSpawningDecodeHook = safetyhook::create_inline(
            reinterpret_cast<void*>(kSpawningDecode),
            reinterpret_cast<void*>(&SpawningDecode_Hook));
        Log("spawning decoder %s at %08X (%u corpse anchors, %d%% of them)",
            gSpawningDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kSpawningDecode), kCorpseAnchorCount,
            gCorpsePercent);
    }
