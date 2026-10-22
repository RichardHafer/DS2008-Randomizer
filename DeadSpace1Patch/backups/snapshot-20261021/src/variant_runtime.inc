// Per-variant runtime tuning: animation playback rate and model scale.
//
// Health, damage, limb thresholds and the rest are authored, so they travel
// in the archive and need no code.  Two knobs do not: the playback rate,
// which no creature authors, and the scale, which every creature authors as
// 1 and which is easier to drive from the same place.
//
// Where they live, from the decrypted image:
//
//   Animated is the creature's primary base - the Exploder constructor at
//   0x004CAE70 overwrites exactly the vtable slots the Animated constructor
//   at 0x006EF050 writes (+0, +0x3C, +0x44, +0x4C, +0x50, +0x54) with `this`
//   unchanged - so an Animated field is a creature field at the same offset.
//
//   Animated + 0x268   the AnimView, 0x300 bytes, allocated at 0x006EF6A5
//   AnimView + 0x104   the playback rate (m_AnimSpeed in the DS3 PDB)
//   AnimView + 0x0D4   the uniform scale, multiplied into the 3x3 matrix
//                      at 0x0079F535
//   0x006F1790         Animated::SetAnimSpeed(float), this=eax, value=xmm0
//   0x006F1810         Animated::SetScale(float), writes 0xD4 and 1/x to 0xD8
//
// The rate cannot be set once at spawn: eleven inlined stores put it back to
// 1.0 whenever an animation starts.  So the stores are what we ride, and the
// site table is generated from the image rather than typed in.
//
// The creature is not in a register at those stores - every one of them loads
// the view over the object register - so the hook has to recognise the view.
// It keeps a small table of view pointers filled at spawn.  Those pointers are
// only ever COMPARED, never dereferenced, which is what makes a stale entry
// harmless: the worst case is that a recycled view animates at another
// variant's rate until the table is refreshed, not a read through a dead
// pointer.  An earlier attempt that dereferenced stored object pointers
// produced ten crash dumps, and that is the mistake this avoids.

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

    // One entry per live variant creature.  Sized well past the spawn cap:
    // the director keeps four or five enemies alive, and even a doubled
    // population plus corpses stays far below this.
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
        // Called when the world is torn down.  Every view in the table
        // belonged to the old level, so keeping them would only invite a
        // recycled pointer to match.
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

    // How long after a creature appears its rate is left at what the game
    // wants.
    //
    // Built on the guess that a fast emergence animation overshot and put a
    // super exploder under the floor.  That guess was wrong: the run that
    // produced it had the rate at 100 and only the scale raised, so the rate
    // was never involved.  The mechanism is kept because a scripted arrival is
    // a plausible thing to want left alone, but it is off unless asked for.
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
            // Full: overwrite in turn rather than dropping the newest, so a
            // long level does not end up tuning only the first sixty-four
            // creatures it ever spawned.
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
            // Animated::SetScale at 0x006F1810 takes `this` in EAX and the
            // value in XMM0 - a convention no MSVC declaration can express,
            // and calling it as __fastcall would hand it ECX and crash.  All
            // it does is write the scale and its reciprocal as a pair, so the
            // two fields are written here instead.  Nothing resets them, so
            // once is enough.
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
        // Scale what the game was about to store rather than replacing it:
        // one of these sites carries the stasis slowdown, and a variant that
        // ignored it would shrug stasis off.
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
        // A rate of zero freezes the creature and a negative one runs the
        // animation backwards into undefined blend state; the band is
        // deliberately narrow, because the values that matter are all close
        // to one.
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
    // Direct and vent are not two values of one field: the vent form authors
    // three properties the direct form leaves out entirely, and the arrival
    // kind at +0x0C, the pose at +0x10 and a sixteen-byte vent reference at
    // +0x14 are exactly those three.  Removing properties from a record is the
    // one edit that is never allowed - it shifts every value behind them, and
    // an earlier attempt to do it in the archive hung the loading screen.
    //
    // The game only ever sees the decoded object, though, and an absent
    // property decodes to what the constructor wrote: 0x004560B0 sets +0x0C
    // and +0x10 to zero.  So writing zero into the finished component is
    // indistinguishable from the property never having been authored, and the
    // vent reference stops being read - the same reasoning the corpse
    // randomizer already relies on for the lying form.
    //
    // This is what lets the super exploder use a vent donor for a direct
    // spawn, which in turn lets enemy pool 2 stay out of the world: it ships
    // 327 entities and a chapter's worth of script objects for one Exploder.
    constexpr unsigned kSpawnArrivalKind = 0x0Cu;
    constexpr unsigned kSpawnArrivalPose = 0x10u;
    constexpr unsigned kArrivalKindVent = 1u;

    // Set when a sup_expl donor has just been bound into a directly placed
    // spawn, cleared by the first vent component that decodes afterwards.  It
    // is a bool, not a pointer: nothing here survives to be dereferenced.
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
        // Only ever convert a vent arrival.  Anything else is not the donor we
        // are waiting for, and the flag stays armed for the one that is.
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

    // Where a creature becomes identifiable.
    //
    // Class 0x71BC20FF decodes the model name and stores it at +0x668
    // (0x0047580C).  By 0x00475B8F that record is finished and EDI still
    // holds the object, so this is the first moment the name can be read -
    // and it fires for every creature, which is what makes it the right place
    // for a table that covers all of them rather than exploders only.
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
        gVariantRegisterHook = safetyhook::create_mid(
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
            // Scale is a one-shot call at spawn, so nothing has to be hooked
            // for it.  Eleven mid hooks on a hot path are not worth paying
            // for when no variant asks for a different rate.
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
                // A site that does not read as the expected store is a site
                // we would be hooking blind.  Skipping it costs one reset
                // path; hooking it could cost the process.
                Log("variant runtime: skipping %08X, the store does not match",
                    static_cast<unsigned>(site.address));
                continue;
            }
            gAnimSpeedHooks[i] = safetyhook::create_mid(
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

    // The INI drives it, so a variant can be retuned without a rebuild.  One
    // line per variant, keyed by the model name the archive ships under:
    //
    //     VariantSpeed_sup_expl=125     ; per cent, 100 = untouched
    //     VariantScale_sup_expl=110
    //
    // Only names that already appear in the character stream manifest are
    // worth listing; a name no entity authors simply never matches.
    // A variant belongs to a base creature: the class whose spawns it stands
    // in for.  Adding a variant means one line here plus its INI keys - the
    // substitution loop, the tuning and the streaming policy all read this.
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

    // Exact every-Nth rather than a coin flip, so a fifty per cent share
    // really does alternate inside one encounter instead of occasionally
    // producing five of the same in a row.
    bool ChooseVariantSpawn(VariantSubstitution& row)
    {
        const unsigned ordinal = row.ordinal++;
        if (row.percent >= 100)
            return true;
        if (row.percent <= 0)
            return false;
        // The same test the super exploder shipped with, kept verbatim so an
        // existing save sees the same alternation it saw before.
        const unsigned phase = (gStartingSuitSeed ^ 0x45585044u) % 100u;
        return ((ordinal * static_cast<unsigned>(row.percent) + phase) % 100u) <
               static_cast<unsigned>(row.percent);
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
            "DeadSpace1Patch", "VariantSizeDiagnostics", 0, gIniPath) != 0;
        gVariantMotionScaleFix = GetPrivateProfileIntA(
            "DeadSpace1Patch", "VariantMotionScaleFix", 0, gIniPath) != 0;
        Log("variant motion: experimental fix %s", gVariantMotionScaleFix ? "enabled" : "disabled");
        Log("variant size diagnostics: %s, up to 4 Super Exploders, 200 ms samples for 12 seconds",
            gVariantSizeDiagnostics ? "enabled" : "disabled");
        gVariantTuningCount = 0;
        gVariantSubstitutionCount = 0;
        const int delay = GetPrivateProfileIntA(
            "DeadSpace1Patch", "VariantSpeedDelayMs", 0, gIniPath);
        gVariantSpeedDelayMs =
            delay < 0 ? 0u : (delay > 10000 ? 10000u
                                            : static_cast<unsigned>(delay));
        for (unsigned i = 0; i < kKnownVariantCount; ++i)
        {
            const KnownVariant& known = kKnownVariants[i];
            char key[64];
            std::snprintf(key, sizeof(key), "VariantSpeed_%s", known.model);
            const int speed = GetPrivateProfileIntA(
                "DeadSpace1Patch", key, 100, gIniPath);
            std::snprintf(key, sizeof(key), "VariantScale_%s", known.model);
            const int scale = GetPrivateProfileIntA(
                "DeadSpace1Patch", key, 100, gIniPath);
            if (speed != 100 || scale != 100)
                AddVariantTuning(known.model,
                                 static_cast<float>(speed) / 100.0f,
                                 static_cast<float>(scale) / 100.0f);

            // The super exploder shipped before this table existed and keeps
            // its own two keys, so an existing INI carries over unchanged.
            std::snprintf(key, sizeof(key), "VariantShare_%s", known.model);
            int share = GetPrivateProfileIntA(
                "DeadSpace1Patch", key, -1, gIniPath);
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
