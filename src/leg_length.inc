    // Leg lengths for model suits ([DeadSpace2008Randomizer] LegLengthCorrection;
    // analysis/DS1_LegLength_BoneMod_Research_2026-10-25.md).
    //
    // A suit built with own_legs (tools/ds1_model_import.py) binds its mesh to a
    // raised skeleton: pelvis and everything above higher, thigh and shin longer.
    // On every animation update of the player (0x006EFCA9, esi = Animated, eax =
    // AnimOutput) Isaac's animated bones are moved there with BoneMod pre records
    // (flags 0x09: a model-space translation children inherit; order 0, so the
    // ragdoll's own records still win when it takes over). Knees and ankles move
    // along the animated thigh and shin, and the pelvis rises by what keeps the
    // lower foot on the floor. Finalize then runs again so the weapon and other
    // bone queries see the result.
    //
    // ds1_randomizer_assets\dlc\legs.txt: "<suit archive> s_thigh s_shin
    // raise_at_rest ankle_offset" (mesh units; the palette is scaled by
    // S = [view+0xD4], 1.818 for the player). The worn suit is the archive loaded
    // by the suit change (0x005A2D83) or the world start (0x005A273D). It is
    // forgotten at every world teardown; otherwise dying back into another suit
    // kept the long legs and Isaac stayed a giant.
    uintptr_t PlayerObject();

    constexpr uintptr_t kLegUpdateHook = 0x006EFCA9;
    constexpr uintptr_t kBoneModRegister = 0x007A4D20;   // eax = view, ecx = &record, plain ret
    constexpr uintptr_t kPoseFinalize = 0x007A0F80;      // cdecl (view, out)
    constexpr uintptr_t kPoseFinalizeAlt = 0x0079F7D0;   // cdecl (view, out), when [out+0x3C] != 0
    constexpr uintptr_t kSuitLoadCaller = 0x005A2D83;    // the suit change at a store
    constexpr uintptr_t kSuitSpawnCaller = 0x005A273D;   // the saved suit when a world starts (load, death)
    constexpr uint32_t kPlayerSkeletonHash = 0x383225A1;
    constexpr int kLegBones[5] = {3, 22, 23, 38, 39};     // m_pelvis, l/r_knee, l/r_ankle
    constexpr int kHipBones[2] = {6, 7};                  // l/r_hip
    constexpr int kKneeBones[2] = {22, 23};
    constexpr int kAnkleBones[2] = {38, 39};

    struct alignas(16) BoneModBuffer { float q[4]; float t[4]; };
    struct BoneModRecord
    {
        void* buffer;
        int16_t bone;
        uint16_t flags;
        float* weight;
        int16_t source;
        int16_t order;
        float vec[4];
    };
    static_assert(sizeof(BoneModRecord) == 32, "BoneMod record layout");

    struct LegConfig { char archive[64]; float sThigh, sShin, raise, ankle; };
    bool gLegLength = true;
    LegConfig gLegConfigs[32]{};
    unsigned gLegConfigCount = 0;
    char gWornSuit[64]{};
    volatile LONG gWornSuitChanged = 0;
    safetyhook::MidHook gLegHook;
    BoneModBuffer gLegBuffers[5]{};
    uintptr_t gLegView = 0;
    float gLegHeight = 0.0f;
    bool gLegActive = false;
    volatile LONG gLegUpdates = 0, gLegRegistrations = 0, gLegSkips = 0, gLegFinalizes = 0, gLegLimits = 0;
    float gLegMinH = 1e9f, gLegMaxH = -1e9f;

    // From the stream load hook: the suit archive a suit change asks for.
    void NoteSuitLoad(const char* path, uintptr_t caller)
    {
        if (!path || (caller != kSuitLoadCaller && caller != kSuitSpawnCaller))
            return;
        const char* base = std::strrchr(path, '\\');
        base = base ? base + 1 : path;
        if (_stricmp(base, gWornSuit) == 0)
            return;
        std::snprintf(gWornSuit, sizeof(gWornSuit), "%s", base);
        InterlockedExchange(&gWornSuitChanged, 1);
        Log("leg length: suit archive %s requested by %s", gWornSuit,
            caller == kSuitLoadCaller ? "the suit change" : "the world start");
    }

    // The heartbeat saw the world go: whatever the next world loads decides again.
    void ForgetWornSuit()
    {
        if (gWornSuit[0])
            Log("leg length: world teardown, suit %s forgotten until the next world loads one", gWornSuit);
        gWornSuit[0] = 0;
    }

    const LegConfig* WornLegConfig()
    {
        for (unsigned i = 0; i < gLegConfigCount; ++i)
            if (_stricmp(gLegConfigs[i].archive, gWornSuit) == 0)
                return &gLegConfigs[i];
        return nullptr;
    }

    void ZeroLegBuffers()
    {
        for (BoneModBuffer& b : gLegBuffers)
        {
            b.q[0] = b.q[1] = b.q[2] = 0.0f;
            b.q[3] = 1.0f;
            b.t[0] = b.t[1] = b.t[2] = b.t[3] = 0.0f;
        }
    }

    void RegisterBoneMod(uintptr_t view, BoneModRecord* record)
    {
        __asm
        {
            mov eax, view
            mov ecx, record
            mov edx, kBoneModRegister
            call edx
        }
    }

    // The records live in the view and die with it, so they are looked up on every update and registered when missing.
    bool EnsureLegRecords(uintptr_t view)
    {
        const uintptr_t list = *reinterpret_cast<const uintptr_t*>(view + 0x70);
        auto present = [&](const void* buffer) {
            const int count = *reinterpret_cast<const int16_t*>(view + 0x78);
            for (int i = 0; list && i < count; ++i)
                if (*reinterpret_cast<void* const*>(list + 32 * i) == buffer)
                    return true;
            return false;
        };
        bool all = true;
        for (unsigned k = 0; k < 5; ++k)
        {
            if (present(&gLegBuffers[k]))
                continue;
            BoneModRecord record{&gLegBuffers[k], static_cast<int16_t>(kLegBones[k]), 0x0009, nullptr, 0, 0,
                                 {0.0f, 0.0f, 0.0f, 1.0f}};
            RegisterBoneMod(view, &record);
            const bool ok = present(&gLegBuffers[k]);
            all = all && ok;
            if (InterlockedIncrement(&gLegRegistrations) <= 40)
                Log("leg length: record for bone %d on view %08X %s (pre list %d entries)", kLegBones[k],
                    static_cast<unsigned>(view), ok ? "registered" : "NOT taken (list full?)",
                    *reinterpret_cast<const int16_t*>(view + 0x78));
        }
        return all;
    }

    struct Vec3 { float x, y, z; };
    Vec3 PaletteRow3(uintptr_t palette, const uint8_t* remap, int bone)
    {
        const float* m = reinterpret_cast<const float*>(palette + 0x40 * remap[bone]);
        return {m[12], m[13], m[14]};
    }

    void LegUpdate_Hook(safetyhook::Context& ctx)
    {
        static uintptr_t player = 0;
        static DWORD playerTick = 0;
        const DWORD tick = GetTickCount();
        if (tick - playerTick > 200)
        {
            playerTick = tick;
            player = PlayerObject();
        }
        if (!player || ctx.esi != player)
            return;
        __try
        {
            const uintptr_t animated = ctx.esi, out = ctx.eax;
            const uintptr_t view = *reinterpret_cast<const uintptr_t*>(animated + 0x268);
            if (!view)
                return;
            const LegConfig* legs = WornLegConfig();
            if (!legs)
            {
                if (gLegActive)
                {
                    ZeroLegBuffers();
                    gLegActive = false;
                    Log("leg length: off (suit %s has no leg table)", gWornSuit[0] ? gWornSuit : "unknown");
                }
                return;
            }
            const uintptr_t descTable = *reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(view + 0x34) + 0x1C);
            const uintptr_t desc = descTable + 0x18 * *reinterpret_cast<const uint16_t*>(view + 0x3A);
            if (*reinterpret_cast<const uint32_t*>(desc) != kPlayerSkeletonHash)
            {
                InterlockedIncrement(&gLegSkips);
                return;
            }
            if (view != gLegView)
            {
                gLegView = view;
                ZeroLegBuffers();
                gLegHeight = 0.0f;
                Log("leg length: player view %08X, scale %.3f, suit %s (thigh x%.3f, shin x%.3f, raise %.4f, ankle %+.4f)",
                    static_cast<unsigned>(view), *reinterpret_cast<const float*>(view + 0xD4), legs->archive,
                    legs->sThigh, legs->sShin, legs->raise, legs->ankle);
            }
            if (!EnsureLegRecords(view))
                return;
            if (!out || !*reinterpret_cast<const uintptr_t*>(out + 0x38) ||
                (*reinterpret_cast<const uint32_t*>(view + 4) & 0x1000))
            {
                InterlockedIncrement(&gLegSkips);
                return;
            }
            const float S = *reinterpret_cast<const float*>(view + 0xD4);
            const uint8_t* remap = *reinterpret_cast<const uint8_t* const*>(view + 0x84);
            const uintptr_t palette = *reinterpret_cast<const uintptr_t*>(view + 0x88);
            if (!remap || !palette || !(S > 0.5f && S < 4.0f))
                return;
            for (int b : {3, 6, 7, 22, 23, 38, 39})
                if (remap[b] == 0xFF)
                {
                    InterlockedIncrement(&gLegSkips);
                    return;
                }
            // The palette holds this frame's animation plus the current leg buffers; subtract them down the chain.
            const float* dp = gLegBuffers[0].t;
            float need[2], ankleY[2];
            Vec3 tK[2], tA[2];
            for (int side = 0; side < 2; ++side)
            {
                const float* curK = gLegBuffers[1 + side].t;
                const float* curA = gLegBuffers[3 + side].t;
                const Vec3 hip = PaletteRow3(palette, remap, kHipBones[side]);
                const Vec3 knee = PaletteRow3(palette, remap, kKneeBones[side]);
                const Vec3 ankle = PaletteRow3(palette, remap, kAnkleBones[side]);
                const Vec3 thigh{knee.x - hip.x - curK[0], knee.y - hip.y - curK[1], knee.z - hip.z - curK[2]};
                const Vec3 shin{ankle.x - knee.x - curA[0], ankle.y - knee.y - curA[1], ankle.z - knee.z - curA[2]};
                tK[side] = {(legs->sThigh - 1) * thigh.x, (legs->sThigh - 1) * thigh.y, (legs->sThigh - 1) * thigh.z};
                tA[side] = {(legs->sShin - 1) * shin.x, (legs->sShin - 1) * shin.y, (legs->sShin - 1) * shin.z};
                need[side] = legs->ankle * S - (tK[side].y + tA[side].y);
                ankleY[side] = ankle.y - dp[1] - curK[1] - curA[1];
            }
            // the lower (support) foot decides how high the pelvis goes
            const float band = 0.05f * S, lo = (std::min)(ankleY[0], ankleY[1]);
            float wsum = 0.0f, h = 0.0f;
            for (int side = 0; side < 2; ++side)
            {
                const float w = (std::max)(0.0f, (std::min)(1.0f, 1.0f - (ankleY[side] - lo) / band));
                wsum += w;
                h += w * need[side];
            }
            h = wsum > 0.0f ? h / wsum : need[0];
            const float dt = *reinterpret_cast<const float*>(ctx.esp);
            if (gLegHeight == 0.0f || std::fabs(h - gLegHeight) > 0.2f * S || !(dt > 0.0f && dt < 0.2f))
                gLegHeight = h;
            else
                gLegHeight += (h - gLegHeight) * (std::min)(1.0f, dt * 12.0f);
            const float hMax = 2.0f * S * (std::max)(legs->raise, 0.01f);
            bool bad = !(gLegHeight >= 0.0f && gLegHeight <= hMax);
            for (int side = 0; side < 2 && !bad; ++side)
                bad = std::fabs(tK[side].x) + std::fabs(tK[side].y) + std::fabs(tK[side].z) > 0.3f * S ||
                      std::fabs(tA[side].x) + std::fabs(tA[side].y) + std::fabs(tA[side].z) > 0.3f * S ||
                      tK[side].y != tK[side].y || tA[side].y != tA[side].y;
            if (bad)
            {
                InterlockedIncrement(&gLegLimits);
                ZeroLegBuffers();
                gLegHeight = 0.0f;
            }
            else
            {
                gLegBuffers[0].t[1] = gLegHeight;
                for (int side = 0; side < 2; ++side)
                {
                    float* k = gLegBuffers[1 + side].t;
                    float* a = gLegBuffers[3 + side].t;
                    k[0] = tK[side].x; k[1] = tK[side].y; k[2] = tK[side].z;
                    a[0] = tA[side].x; a[1] = tA[side].y; a[2] = tA[side].z;
                }
                gLegMinH = (std::min)(gLegMinH, gLegHeight / S);
                gLegMaxH = (std::max)(gLegMaxH, gLegHeight / S);
            }
            gLegActive = true;
            InterlockedIncrement(&gLegUpdates);
            // again, so the weapon and every bone query see the corrected pose this frame
            const auto finalize = reinterpret_cast<void(__cdecl*)(uintptr_t, uintptr_t)>(
                *reinterpret_cast<const uint32_t*>(out + 0x3C) ? kPoseFinalizeAlt : kPoseFinalize);
            finalize(view, out);
            InterlockedIncrement(&gLegFinalizes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gLegSkips);
            ZeroLegBuffers();
        }
    }

    void LoadLegConfigs()
    {
        char path[MAX_PATH]{};
        std::snprintf(path, sizeof(path), "%sds1_randomizer_assets\\dlc\\legs.txt", gGameRoot);
        FILE* file = nullptr;
        if (fopen_s(&file, path, "r") != 0 || !file)
        {
            Log("leg length: no table at %s", path);
            return;
        }
        char line[256];
        while (gLegConfigCount < 32 && std::fgets(line, sizeof(line), file))
        {
            LegConfig c{};
            if (line[0] == '#' || std::sscanf(line, "%63s %f %f %f %f", c.archive, &c.sThigh,
                                              &c.sShin, &c.raise, &c.ankle) != 5)
                continue;
            gLegConfigs[gLegConfigCount++] = c;
            Log("leg length: %s thigh x%.3f shin x%.3f raise %.4f ankle %+.4f", c.archive, c.sThigh, c.sShin,
                c.raise, c.ankle);
        }
        std::fclose(file);
    }

    void InstallLegLength()
    {
        gLegLength = GetPrivateProfileIntA("DeadSpace2008Randomizer", "LegLengthCorrection", 1, gIniPath) != 0;
        if (!gLegLength)
        {
            Log("leg length off (LegLengthCorrection=0): own_legs suits stand in the floor");
            return;
        }
        LoadLegConfigs();
        ZeroLegBuffers();
        if (!gLegConfigCount)
            return;
        gLegHook = VerifiedMid(reinterpret_cast<void*>(kLegUpdateHook), &LegUpdate_Hook);
        Log("leg length %s at %08X: %u suit(s) with own leg lengths", gLegHook ? "active" : "NOT installed",
            static_cast<unsigned>(kLegUpdateHook), gLegConfigCount);
    }

    void LogLegLengthTotals()
    {
        static LONG last[5] = {-1, -1, -1, -1, -1};
        static ULONGLONG lastTick = 0;
        const LONG now[5] = {gLegUpdates, gLegRegistrations, gLegSkips, gLegFinalizes, gLegLimits};
        // updates and re-finalizes move every frame: a line every 30 s, or at once when a registration, skip or
        // limit count changed
        const bool quiet = last[1] == now[1] && last[2] == now[2] && last[4] == now[4];
        if (!gLegHook || std::memcmp(last, now, sizeof(now)) == 0 || (quiet && GetTickCount64() - lastTick < 30000))
            return;
        lastTick = GetTickCount64();
        std::memcpy(last, now, sizeof(now));
        Log("leg length totals: %ld updates, %ld registrations, %ld skipped, %ld re-finalized, %ld over the limits; "
            "pelvis raise %.4f..%.4f (mesh units), suit %s", now[0], now[1], now[2], now[3], now[4],
            gLegMinH < 1e8f ? gLegMinH : 0.0f, gLegMaxH > -1e8f ? gLegMaxH : 0.0f, gWornSuit[0] ? gWornSuit : "unknown");
    }
