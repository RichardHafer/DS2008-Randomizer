// Per-variant runtime tuning: animation playback rate and model scale.
//
// Health, damage, limb thresholds and the rest are authored and travel in the
// archive. The playback rate is authored by no creature, and the scale is
// authored as 1 everywhere and easier to drive from here.
//
// From the decrypted image:
//
//   Animated is the creature's primary base: the Exploder constructor at
//   0x004CAE70 overwrites exactly the vtable slots the Animated constructor at
//   0x006EF050 writes (+0, +0x3C, +0x44, +0x4C, +0x50, +0x54) with `this`
//   unchanged, so an Animated field is a creature field at the same offset.
//
//   Animated + 0x268   the AnimView, 0x300 bytes, allocated at 0x006EF6A5
//   AnimView + 0x104   the playback rate (m_AnimSpeed in the DS3 PDB)
//   AnimView + 0x0D4   the uniform scale, multiplied into the 3x3 matrix
//                      at 0x0079F535
//   0x006F1790         Animated::SetAnimSpeed(float), this=eax, value=xmm0
//   0x006F1810         Animated::SetScale(float), writes 0xD4 and 1/x to 0xD8
//
// The rate cannot be set once at spawn: eleven inlined stores reset it to 1.0
// whenever an animation starts. So those stores are hooked, from a site table
// generated from the image.
//
// The creature is not in a register at those stores (each loads the view over
// the object register), so the hook recognises the view from a small table of
// view pointers filled at spawn. Those pointers are only compared, never
// dereferenced, so a stale entry is harmless: at worst a recycled view animates
// at another variant's rate until the table is refreshed. An earlier version
// that dereferenced stored object pointers produced ten crash dumps.

#include "anim_speed_sites.inc"
#include "variant_motion_scale.inc"

    // A variant is recognised by the model name the entity authors, which is
    // the same discriminator the spawn substitution already uses.
    struct EnemyVariantTuning
    {
        char model[32];
        float animSpeed;
        float scale;
    };

    constexpr unsigned kMaxVariantTunings = 16;
    EnemyVariantTuning gVariantTunings[kMaxVariantTunings];
    unsigned gVariantTuningCount = 0;

    // One entry per live variant creature. The director keeps four or five
    // enemies alive, so even a doubled population plus corpses stays far below.
    constexpr unsigned kMaxTunedViews = 64;

    struct TunedView
    {
        uintptr_t view;
        float animSpeed;
        unsigned readyAt;   // GetTickCount() before which the rate is left alone
    };

    TunedView gTunedViews[kMaxTunedViews];
    unsigned gTunedViewCount = 0;
    unsigned gTunedViewCursor = 0;

    bool gVariantRuntimeInstalled = false;
    unsigned gVariantRateOverrides = 0;
    safetyhook::MidHook gAnimSpeedHooks[kAnimSpeedSiteCount];

    void ResetVariantRuntime()
    {
        ResetVariantSizeDiagnostics();
        gVariantMotionReports[0] = gVariantMotionReports[1] = 0;
        gVariantMotionFaults = 0;
        // Every view in the table belonged to the old level; keeping them would
        // only let a recycled pointer match.
        gTunedViewCount = 0;
        gTunedViewCursor = 0;
        gVariantRateOverrides = 0;
        for (unsigned i = 0; i < kMaxTunedViews; ++i)
        {
            gTunedViews[i].view = 0;
            gTunedViews[i].animSpeed = 1.0f;
            gTunedViews[i].readyAt = 0;
        }
    }

    const EnemyVariantTuning* VariantTuningFor(const char* model)
    {
        if (!model || !*model)
            return nullptr;
        for (unsigned i = 0; i < gVariantTuningCount; ++i)
        {
            if (std::strcmp(gVariantTunings[i].model, model) == 0)
                return &gVariantTunings[i];
        }
        return nullptr;
    }

    // How long after a creature appears its rate is left alone. Added on the
    // theory that a fast emergence put a super exploder under the floor, which
    // turned out wrong (that run had the rate at 100). Kept for scripted arrivals,
    // off unless set.
    unsigned gVariantSpeedDelayMs = 0;

    float TunedRateFor(uintptr_t view)
    {
        for (unsigned i = 0; i < gTunedViewCount; ++i)
        {
            if (gTunedViews[i].view != view)
                continue;
            // Signed difference, so the comparison survives the tick counter
            // wrapping around after forty-nine days.
            const int remaining =
                static_cast<int>(gTunedViews[i].readyAt - GetTickCount());
            if (remaining > 0)
                return 0.0f;
            return gTunedViews[i].animSpeed;
        }
        return 0.0f;
    }

    void RememberTunedView(uintptr_t view, float animSpeed)
    {
        for (unsigned i = 0; i < gTunedViewCount; ++i)
        {
            if (gTunedViews[i].view == view)
            {
                gTunedViews[i].animSpeed = animSpeed;
                return;
            }
        }
        unsigned slot;
        if (gTunedViewCount < kMaxTunedViews)
        {
            slot = gTunedViewCount++;
        }
        else
        {
            // Full: overwrite in turn rather than dropping the newest, so a long
            // level does not end up tuning only its first 64 creatures.
            slot = gTunedViewCursor;
            gTunedViewCursor = (gTunedViewCursor + 1) % kMaxTunedViews;
        }
        gTunedViews[slot].view = view;
        gTunedViews[slot].animSpeed = animSpeed;
        gTunedViews[slot].readyAt = GetTickCount() + gVariantSpeedDelayMs;
    }

    // Reading a pointer field out of a freshly decoded object is safe here:
    // the caller has the object live in a register at the moment it fires.
    uintptr_t AnimViewOf(uintptr_t object)
    {
        if (!object)
            return 0;
        uintptr_t view = 0;
        std::memcpy(&view,
                    reinterpret_cast<const void*>(object + kAnimatedViewPointer),
                    sizeof(view));
        return view;
    }

    // Called once per creature, from the point where the authored model name
    // has just been decided.  This is where the variant is still identifiable.
    void RegisterVariantCreature(uintptr_t object, const char* model)
    {
        if (!gVariantRuntimeInstalled || !object)
            return;
        const EnemyVariantTuning* tuning = VariantTuningFor(model);
        if (!tuning)
            return;

        const uintptr_t view = AnimViewOf(object);
        if (!view)
            return;

        // Entity +578 initializes a countdown; it is unrelated to model size.
        if (tuning->scale > 0.0f && tuning->scale != 1.0f)
        {
            // Animated::SetScale at 0x006F1810 takes `this` in EAX and the value
            // in XMM0, which no MSVC declaration can express (as __fastcall it
            // would get ECX and crash). It only writes the scale and its
            // reciprocal, so both fields are written here. Nothing resets them.
            const float scale = tuning->scale;
            const float inverse = 1.0f / scale;
            const float previousScale = *reinterpret_cast<const float*>(view + kAnimViewScale);
            std::memcpy(reinterpret_cast<void*>(view + kAnimViewScale),
                        &scale, sizeof(scale));
            std::memcpy(reinterpret_cast<void*>(view + kAnimViewScale + 4),
                        &inverse, sizeof(inverse));
            static unsigned scaleReports = 0;
            if (gVariantSizeDiagnostics && scaleReports++ < 16)
                Log("variant size: applied model=%s owner=%08X view=%08X scale=%.3f->%.3f inverse=%.3f",
                    model, static_cast<unsigned>(object), static_cast<unsigned>(view),
                    previousScale, scale, inverse);
        }

        if (tuning->animSpeed > 0.0f && tuning->animSpeed != 1.0f)
            RememberTunedView(view, tuning->animSpeed);
    }

    safetyhook::Xmm* XmmSlot(SafetyHookContext& ctx, unsigned index)
    {
        switch (index)
        {
        case 0: return &ctx.xmm0;
        case 1: return &ctx.xmm1;
        case 2: return &ctx.xmm2;
        case 3: return &ctx.xmm3;
        case 4: return &ctx.xmm4;
        case 5: return &ctx.xmm5;
        case 6: return &ctx.xmm6;
        case 7: return &ctx.xmm7;
        default: return nullptr;
        }
    }

    // The table stores x86 register numbers; Context32 lists its general
    // registers in a different order, so the mapping lives here, once.
    uintptr_t GeneralRegister(const SafetyHookContext& ctx, unsigned index)
    {
        switch (index)
        {
        case 0: return ctx.eax;
        case 1: return ctx.ecx;
        case 2: return ctx.edx;
        case 3: return ctx.ebx;
        case 4: return ctx.esp;
        case 5: return ctx.ebp;
        case 6: return ctx.esi;
        case 7: return ctx.edi;
        default: return 0;
        }
    }

    void AnimSpeedStore(SafetyHookContext& ctx, unsigned site)
    {
        if (!gTunedViewCount || site >= kAnimSpeedSiteCount)
            return;
        const AnimSpeedSite& entry = kAnimSpeedSites[site];
        const uintptr_t view = GeneralRegister(ctx, entry.view);
        const float rate = TunedRateFor(view);
        if (rate <= 0.0f)
            return;
        safetyhook::Xmm* slot = XmmSlot(ctx, entry.value);
        if (!slot)
            return;
        // Scale what the game was about to store rather than replacing it: one of
        // these sites carries the stasis slowdown, which a variant must not ignore.
        slot->f32[0] *= rate;
        ++gVariantRateOverrides;
    }

#define DS1_ANIM_SPEED_HANDLER(index)                       \
    void AnimSpeedStore##index(safetyhook::Context& ctx)    \
    {                                                       \
        AnimSpeedStore(ctx, index);                         \
    }
    DS1_ANIM_SPEED_HANDLER_LIST
#undef DS1_ANIM_SPEED_HANDLER

    using AnimSpeedHandler = void (*)(safetyhook::Context&);

#define DS1_ANIM_SPEED_HANDLER(index) &AnimSpeedStore##index,
    AnimSpeedHandler gAnimSpeedHandlers[] = {
        DS1_ANIM_SPEED_HANDLER_LIST
    };
#undef DS1_ANIM_SPEED_HANDLER

    static_assert(sizeof(gAnimSpeedHandlers) / sizeof(gAnimSpeedHandlers[0]) ==
                      kAnimSpeedSiteCount,
                  "handler list and site table disagree - regenerate "
                  "anim_speed_sites.inc");

    void LoadVariantTunings();

    void AddVariantTuning(const char* model, float animSpeed, float scale)
    {
        if (!model || !*model || gVariantTuningCount >= kMaxVariantTunings)
            return;
        // Zero freezes the creature and a negative rate runs the animation
        // backwards into undefined blend state. The useful values are all close
        // to one, so the band is narrow.
        if (animSpeed < 0.25f) animSpeed = 0.25f;
        if (animSpeed > 4.0f)  animSpeed = 4.0f;
        if (scale < 0.25f) scale = 0.25f;
        if (scale > 4.0f)  scale = 4.0f;

        EnemyVariantTuning& entry = gVariantTunings[gVariantTuningCount++];
        std::strncpy(entry.model, model, sizeof(entry.model) - 1);
        entry.model[sizeof(entry.model) - 1] = '\0';
        entry.animSpeed = animSpeed;
        entry.scale = scale;
        Log("variant tuning: %s runs at %.2fx, scale %.2fx",
            entry.model, entry.animSpeed, entry.scale);
    }

    // Making a vent donor arrive like a directly placed enemy.
    //
    // The vent form authors three properties the direct form leaves out: the
    // arrival kind at +0x0C, the pose at +0x10 and a 16-byte vent reference at
    // +0x14. Removing properties from a record shifts every value behind them, and
    // doing that in the archive hung the loading screen.
    //
    // An absent property decodes to what the constructor wrote (0x004560B0 sets
    // +0x0C and +0x10 to zero), so writing zero into the finished component looks
    // exactly like a record without them, and the vent reference is no longer
    // read. The corpse randomizer relies on the same thing for the lying form.
    //
    // This lets the super exploder use a vent donor for a direct spawn, which keeps
    // enemy pool 2 (327 entities and a chapter of script objects for one Exploder)
    // out of the world.
    constexpr unsigned kSpawnArrivalKind = 0x0Cu;
    constexpr unsigned kSpawnArrivalPose = 0x10u;
    constexpr unsigned kArrivalKindVent = 1u;

    // Set when a sup_expl donor has just been bound into a directly placed spawn,
    // cleared by the next vent component that decodes. A bool, not a pointer, so
    // nothing stale can be dereferenced.
    bool gPendingDirectPlacement = false;
    unsigned gDirectPlacementsMade = 0;

    void ApplyVariantSpawnPlacement(void* component)
    {
        if (!gPendingDirectPlacement || !component)
            return;
        const uintptr_t object = reinterpret_cast<uintptr_t>(component);
        uint32_t kind = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(object + kSpawnArrivalKind),
                               &kind, sizeof(kind), nullptr))
            return;
        // Only convert a vent arrival. Anything else is not the expected donor,
        // and the flag stays armed for the one that is.
        if (kind != kArrivalKindVent)
            return;
        gPendingDirectPlacement = false;
        const uint32_t placed = 0;
        std::memcpy(reinterpret_cast<void*>(object + kSpawnArrivalKind),
                    &placed, sizeof(placed));
        std::memcpy(reinterpret_cast<void*>(object + kSpawnArrivalPose),
                    &placed, sizeof(placed));
        ++gDirectPlacementsMade;
        Log("variant spawn: vent donor placed directly (#%u)",
            gDirectPlacementsMade);
    }

    // Where a creature becomes identifiable. Class 0x71BC20FF decodes the model
    // name and stores it at +0x668 (0x0047580C). At 0x00475B8F that record is
    // finished and EDI still holds the object, so the name can be read there, and
    // the site fires for every creature, not only exploders.
    constexpr uintptr_t kVariantRegisterSite = 0x00475B8Fu;
    // "mov edx,[edi] / mov eax,[edx+0x3c] / mov ecx,edi"
    constexpr unsigned char kVariantRegisterBytes[] = {
        0x8B, 0x17, 0x8B, 0x42, 0x3C, 0x8B, 0xCF};
    constexpr unsigned kVariantModelName = 0x668u;
    safetyhook::MidHook gVariantRegisterHook;

    void VariantRegister_Hook(safetyhook::Context& ctx)
    {
        if (!gVariantTuningCount)
            return;
        const uintptr_t object = ctx.edi;
        if (!object)
            return;
        const char* model = nullptr;
        std::memcpy(&model,
                    reinterpret_cast<const void*>(object + kVariantModelName),
                    sizeof(model));
        if (!model)
            return;
        RegisterVariantCreature(object, model);
    }

    void InstallVariantRuntime()
    {
        InstallVariantMotionScaleFix();
        if (gVariantRuntimeInstalled || !gVariantTuningCount)
            return;

        if (std::memcmp(reinterpret_cast<const void*>(kVariantRegisterSite),
                        kVariantRegisterBytes,
                        sizeof(kVariantRegisterBytes)) != 0)
        {
            Log("variant runtime: %08X does not read as the end of the model "
                "record - nothing installed",
                static_cast<unsigned>(kVariantRegisterSite));
            return;
        }
        gVariantRegisterHook = VerifiedMid(
            reinterpret_cast<void*>(kVariantRegisterSite), VariantRegister_Hook);
        if (!gVariantRegisterHook)
        {
            Log("variant runtime: could not hook the model record epilogue");
            return;
        }

        bool wantsRate = false;
        for (unsigned i = 0; i < gVariantTuningCount; ++i)
        {
            if (gVariantTunings[i].animSpeed != 1.0f)
                wantsRate = true;
        }
        if (!wantsRate)
        {
            // Scale is set once at spawn and needs no hook. Eleven mid hooks on a
            // hot path are not worth it when no variant changes the rate.
            gVariantRuntimeInstalled = true;
            ResetVariantRuntime();
            return;
        }

        unsigned installed = 0;
        for (unsigned i = 0; i < kAnimSpeedSiteCount; ++i)
        {
            const AnimSpeedSite& site = kAnimSpeedSites[i];
            if (std::memcmp(reinterpret_cast<const void*>(site.address),
                            site.bytes, site.length) != 0)
            {
                // A site that does not match the expected store would be hooked
                // blind. Skipping it costs one reset path; hooking it could crash.
                Log("variant runtime: skipping %08X, the store does not match",
                    static_cast<unsigned>(site.address));
                continue;
            }
            gAnimSpeedHooks[i] = VerifiedMid(
                reinterpret_cast<void*>(site.address), gAnimSpeedHandlers[i]);
            if (gAnimSpeedHooks[i])
                ++installed;
        }

        gVariantRuntimeInstalled = installed > 0;
        ResetVariantRuntime();
        Log("variant runtime: %u of %u playback-rate site(s) hooked, "
            "%u tuning(s) loaded",
            installed, kAnimSpeedSiteCount, gVariantTuningCount);
    }

    // Driven by the INI so a variant can be retuned without a rebuild. One line
    // per variant, keyed by the model name the archive ships under:
    //
    //     VariantSpeed_sup_expl=125     ; percent, 100 = untouched
    //     VariantScale_sup_expl=110
    //
    // Only names in the character stream manifest are useful; a name no entity
    // authors never matches.
    // A variant belongs to a base creature, the class whose spawns it stands in
    // for. Adding one means a line here plus its INI keys; the substitution loop,
    // the tuning and the streaming policy all read this table.
    struct KnownVariant
    {
        const char* model;      // the archive and the authored model name
        const char* base;       // the creature class it replaces
    };

    const KnownVariant kKnownVariants[] = {
        {"sup_expl", "Exploder"},
    };
    constexpr unsigned kKnownVariantCount =
        sizeof(kKnownVariants) / sizeof(kKnownVariants[0]);

    // How often each one takes over a spawn of its base creature, and the
    // running count that makes the share exact rather than merely average.
    struct VariantSubstitution
    {
        const KnownVariant* variant;
        int percent;
        unsigned ordinal;
    };

    VariantSubstitution gVariantSubstitutions[kKnownVariantCount];
    unsigned gVariantSubstitutionCount = 0;

    void ResetVariantSubstitutionSequence()
    {
        for (unsigned i = 0; i < gVariantSubstitutionCount; ++i)
            gVariantSubstitutions[i].ordinal = 0;
    }

    // Exact every-Nth rather than a coin flip, so a fifty percent share really
    // alternates inside one encounter.
    // chapter: the spawner's own (SourceChapter). Drawn per spawner from the seed
    // rather than by counting spawns, so the same Exploder becomes a Super
    // Exploder in every run of a seed, whatever was killed before.
    bool ChooseVariantSpawn(VariantSubstitution& row, unsigned chapter, const Guid128& source,
                            const Guid128& target)
    {
        ++row.ordinal;   // for the log only
        int percent = row.percent;
        // EnemyRandomizerDifficultyRamp thins a late variant out here as in CandidateFor.
        if (percent > 0 && gEnemyRandomizerDifficultyRamp && chapter && LateGameVariant(row.variant->model))
        {
            percent = percent * static_cast<int>(chapter) / 12;
            if (LateCapActive(chapter) && percent > static_cast<int>(gEnemyRandomizerLateCapPercent))
                percent = static_cast<int>(gEnemyRandomizerLateCapPercent);
        }
        if (percent >= 100)
            return true;
        if (percent <= 0)
            return false;
        return StableRandomizerHash(source, target, 0x56535542u) % 100u < static_cast<unsigned>(percent);
    }

    VariantSubstitution* VariantSubstitutionForBase(const char* base)
    {
        if (!base)
            return nullptr;
        for (unsigned i = 0; i < gVariantSubstitutionCount; ++i)
        {
            if (std::strcmp(gVariantSubstitutions[i].variant->base, base) == 0)
                return &gVariantSubstitutions[i];
        }
        return nullptr;
    }

    void LoadVariantTunings()
    {
        gVariantSizeDiagnostics = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "VariantSizeDiagnostics", 0, gIniPath) != 0;
        gVariantMotionScaleFix = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "VariantMotionScaleFix", 0, gIniPath) != 0;
        Log("variant motion: experimental fix %s", gVariantMotionScaleFix ? "enabled" : "disabled");
        Log("variant size diagnostics: %s, up to 4 Super Exploders, 200 ms samples for 12 seconds",
            gVariantSizeDiagnostics ? "enabled" : "disabled");
        gVariantTuningCount = 0;
        gVariantSubstitutionCount = 0;
        const int delay = GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "VariantSpeedDelayMs", 0, gIniPath);
        gVariantSpeedDelayMs =
            delay < 0 ? 0u : (delay > 10000 ? 10000u
                                            : static_cast<unsigned>(delay));
        for (unsigned i = 0; i < kKnownVariantCount; ++i)
        {
            const KnownVariant& known = kKnownVariants[i];
            char key[64];
            std::snprintf(key, sizeof(key), "VariantSpeed_%s", known.model);
            const int speed = GetPrivateProfileIntA(
                "DeadSpace2008Randomizer", key, 100, gIniPath);
            std::snprintf(key, sizeof(key), "VariantScale_%s", known.model);
            const int scale = GetPrivateProfileIntA(
                "DeadSpace2008Randomizer", key, 100, gIniPath);
            if (speed != 100 || scale != 100)
                AddVariantTuning(known.model,
                                 static_cast<float>(speed) / 100.0f,
                                 static_cast<float>(scale) / 100.0f);

            // The super exploder shipped before this table existed and keeps
            // its own two keys, so an existing INI carries over unchanged.
            std::snprintf(key, sizeof(key), "VariantShare_%s", known.model);
            int share = GetPrivateProfileIntA(
                "DeadSpace2008Randomizer", key, -1, gIniPath);
            if (share < 0 && std::strcmp(known.model, "sup_expl") == 0)
                share = gEnhancedExploders ? gEnhancedExploderPercent : 0;
            if (share <= 0)
                continue;
            if (share > 100)
                share = 100;
            VariantSubstitution& row =
                gVariantSubstitutions[gVariantSubstitutionCount++];
            row.variant = &known;
            row.percent = share;
            row.ordinal = 0;
            Log("variant spawns: %s stands in for %d%% of %s spawns",
                known.model, share, known.base);
        }
    }
