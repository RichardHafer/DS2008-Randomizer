    bool gVariantMotionScaleFix = false;
    safetyhook::MidHook gVariantMotionScaleHooks[2];
    unsigned gVariantMotionReports[2]{};
    unsigned gVariantMotionFaults = 0;

    void PreserveVariantMotion(SafetyHookContext& ctx, uintptr_t view, unsigned site)
    {
        if (!gVariantMotionScaleFix || ctx.xmm0.f32[0] == 1.0f)
            return;
        __try
        {
            // Animated initialization stores owner +3C at view +8C.
            const uintptr_t link = *reinterpret_cast<const uintptr_t*>(view + 0x8C);
            if (link < 0x1003C)
                return;
            const uintptr_t owner = link - 0x3C;
            if (*reinterpret_cast<const uintptr_t*>(owner) != 0x00C69580 ||
                *reinterpret_cast<const uintptr_t*>(owner + 0x268) != view)
                return;
            const char* model = *reinterpret_cast<const char* const*>(owner + 0x668);
            if (!model || std::memcmp(model, "sup_expl", 9) != 0)
                return;
            const float scale = ctx.xmm0.f32[0];
            if (!(scale >= 0.25f && scale <= 4.0f))
                return;
            // Only the translation multiplier changes, not the live view.
            ctx.xmm0.f32[0] = 1.0f;
            if (gVariantSizeDiagnostics && gVariantMotionReports[site]++ < 4)
                Log("variant motion: site=%u owner=%08X view=%08X translation scale=%.3f->1.000",
                    site, static_cast<unsigned>(owner), static_cast<unsigned>(view), scale);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (gVariantMotionFaults++ < 4)
                Log("variant motion: owner validation failed site=%u view=%08X",
                    site, static_cast<unsigned>(view));
        }
    }

    void VariantMotionDelta_Hook(SafetyHookContext& ctx)
    {
        PreserveVariantMotion(ctx, ctx.esi, 0);
    }

    void VariantMotionSample_Hook(SafetyHookContext& ctx)
    {
        PreserveVariantMotion(ctx, ctx.edx, 1);
    }

    void InstallVariantMotionScaleFix()
    {
        if (!gVariantMotionScaleFix || gVariantMotionScaleHooks[0])
            return;
        constexpr uintptr_t loads[] = {0x007B8E60, 0x007B9386};
        constexpr unsigned char bytes[][15] = {
            {0xF3,0x0F,0x10,0x86,0xD4,0,0,0,0x0F,0x2E,0x05,0x98,0x83,0xC5,0},
            {0xF3,0x0F,0x10,0x82,0xD4,0,0,0,0x0F,0x2E,0x05,0x98,0x83,0xC5,0}
        };
        for (unsigned i = 0; i < 2; ++i)
        {
            if (std::memcmp(reinterpret_cast<const void*>(loads[i]), bytes[i], 15) != 0)
            {
                Log("variant motion: signature mismatch at %08X, fix not installed",
                    static_cast<unsigned>(loads[i]));
                return;
            }
        }
        gVariantMotionScaleHooks[0] = safetyhook::create_mid(
            reinterpret_cast<void*>(loads[0] + 8), VariantMotionDelta_Hook);
        gVariantMotionScaleHooks[1] = safetyhook::create_mid(
            reinterpret_cast<void*>(loads[1] + 8), VariantMotionSample_Hook);
        if (!gVariantMotionScaleHooks[0] || !gVariantMotionScaleHooks[1])
        {
            gVariantMotionScaleHooks[0].reset();
            gVariantMotionScaleHooks[1].reset();
            Log("variant motion: hook installation failed, both sites disabled");
            return;
        }
        Log("variant motion: experimental Super Exploder translation fix installed at 007B8E68 and 007B938E");
    }
