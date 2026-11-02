    // Spawning component decoder.
    //
    // DSNPCSpawningComponent (class hash 0x0CFD7944, decoder 0x004561C0, dispatch
    // table 0x0045654C) decides how a necromorph arrives: property 0 at +0x0C is the
    // arrival kind (1 climbs out of a vent), property 2 at +0x14 the vent reference.
    // Several features change the decoded object, and two inline hooks on one
    // function would fight, so this one hook calls them all.

    // Defined in variant_runtime.inc.
    void ApplyVariantSpawnPlacement(void* component);
    // Defined in scene_gates.inc: a replacement's vent arrival takes the source's VentMarker.
    void ApplySourceVentAnchor(void* component);
    bool SourceVentAnchorWanted();

    constexpr uintptr_t kSpawningDecode = 0x004561C0;

    safetyhook::InlineHook gSpawningDecodeHook;

    void __fastcall SpawningDecode_Hook(void* self, void*, void* attributes)
    {
        gSpawningDecodeHook.thiscall<void>(self, attributes);
        ApplyVariantSpawnPlacement(self);
        ApplySourceVentAnchor(self);
    }

    void InstallSpawningDecoderHook()
    {
        // The super exploder's only donors emerge from a vent; a direct spawn is made
        // by writing the arrival kind back to the constructor default on the finished
        // object. A randomized directly placed spawner may get a vent donor the same way.
        if (!gSuperExploderWanted && !gDirectPlacementWanted && !SourceVentAnchorWanted())
            return;
        gSpawningDecodeHook = VerifiedInline(
            reinterpret_cast<void*>(kSpawningDecode),
            reinterpret_cast<void*>(&SpawningDecode_Hook));
        Log("spawning decoder %s at %08X",
            gSpawningDecodeHook ? "active" : "NOT installed",
            static_cast<unsigned>(kSpawningDecode));
    }
