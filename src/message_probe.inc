    // Message probe (MessageProbe=1, off by default): logs when a watched message is sent, so a test of the
    // design keywords shows which link of a trigger -> message -> listener chain broke.
    //
    // SendMsg 0x006D8920 (analysis/DeadSpace1_Watchdog_2026-10-21.md): esi = &CMsg {id, data, flag}, the id
    // object holds the name hash (sdbm of the lower-case name) at +0x10 and its listener list at +0. The game
    // reads [esi] and [id] itself right there, so the probe reads nothing the game does not.
    // The names come from a text file next to the INI (MessageProbeFile, default
    // DeadSpace2008Randomizer_MessageProbe.txt), one per line, # starts a comment.

    constexpr uintptr_t kSendMsg = 0x006D8920;
    constexpr unsigned kMessageProbeNames = 512;
    constexpr unsigned kMessageProbeLines = 600;
    constexpr unsigned kMessageProbeEach = 4;          // sends logged per name before only counting

    struct MessageProbeName
    {
        uint32_t hash;
        char name[64];
        volatile LONG sends;
    };
    MessageProbeName gMessageProbeNames[kMessageProbeNames]{};
    unsigned gMessageProbeCount = 0;
    volatile LONG gMessageProbeLinesUsed = 0;
    volatile LONG gMessageProbeHits = 0;
    safetyhook::MidHook gSendMsgHook;

    uint32_t MessageProbeHash(const char* text)
    {
        uint32_t value = 0;
        for (; *text; ++text)
        {
            unsigned char c = static_cast<unsigned char>(*text);
            if (c >= 'A' && c <= 'Z')
                c = static_cast<unsigned char>(c - 'A' + 'a');
            value = value * 65599u + c;
        }
        return value;
    }

    const MessageProbeName* MessageProbeFind(uint32_t hash)
    {
        unsigned lo = 0, hi = gMessageProbeCount;
        while (lo < hi)
        {
            const unsigned mid = (lo + hi) / 2;
            if (gMessageProbeNames[mid].hash < hash)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo < gMessageProbeCount && gMessageProbeNames[lo].hash == hash ? &gMessageProbeNames[lo] : nullptr;
    }

    void SendMsg_Hook(safetyhook::Context& ctx)
    {
        if (!ctx.esi)
            return;
        const uintptr_t id = *reinterpret_cast<const uintptr_t*>(ctx.esi);
        if (!id)
            return;
        const uint32_t hash = *reinterpret_cast<const uint32_t*>(id + 0x10);
        if (gSceneGateRunCount)
            SceneGateHeard(hash);
        const MessageProbeName* hit = MessageProbeFind(hash);
        if (!hit)
            return;
        const LONG n = InterlockedIncrement(&const_cast<MessageProbeName*>(hit)->sends);
        InterlockedIncrement(&gMessageProbeHits);
        if (n > static_cast<LONG>(kMessageProbeEach) && n % 100 != 0)
            return;
        if (InterlockedIncrement(&gMessageProbeLinesUsed) > static_cast<LONG>(kMessageProbeLines))
            return;
        const bool listeners = *reinterpret_cast<const uintptr_t*>(id) != 0;
        float me[12]{};
        const bool placed = PlayerTransform(me);
        Log("message probe: %s (%08X) sent #%ld, %s at %.2f %.2f %.2f", hit->name, hash, n,
            listeners ? "has listeners" : "NO listener", placed ? me[9] : 0.0f, placed ? me[10] : 0.0f,
            placed ? me[11] : 0.0f);
    }

    // The scene gate fallback (scene_gates.inc) listens on the same hook, without any probe names.
    void InstallSceneGateListener()
    {
        if (gSendMsgHook || !gSceneGateFallback || !gEnemyRandomizer || !gRunningTickHook)
            return;
        gSendMsgHook = VerifiedMid(reinterpret_cast<void*>(kSendMsg), &SendMsg_Hook);
        gSceneGateListening = gSendMsgHook ? true : false;
        Log("scene gate: SendMsg %s at %08X", gSendMsgHook ? "hooked" : "NOT hooked, the fallback stays off",
            static_cast<unsigned>(kSendMsg));
    }

    void InstallMessageProbe()
    {
        if (!GetPrivateProfileIntA("DeadSpace2008Randomizer", "MessageProbe", 0, gIniPath))
        {
            InstallSceneGateListener();
            return;
        }
        char file[MAX_PATH]{};
        GetPrivateProfileStringA("DeadSpace2008Randomizer", "MessageProbeFile",
                                 "DeadSpace2008Randomizer_MessageProbe.txt", file, sizeof(file), gIniPath);
        char path[MAX_PATH]{};
        if (std::strchr(file, ':') || file[0] == '\\')
            std::snprintf(path, sizeof(path), "%s", file);
        else
        {
            const char* slash = std::strrchr(gIniPath, '\\');
            const int dir = slash ? static_cast<int>(slash - gIniPath + 1) : 0;
            std::snprintf(path, sizeof(path), "%.*s%s", dir, gIniPath, file);
        }
        FILE* fh = nullptr;
        if (fopen_s(&fh, path, "r") != 0 || !fh)
        {
            Log("message probe: on, but the name file %s cannot be read; not hooked", path);
            InstallSceneGateListener();
            return;
        }
        char line[256];
        unsigned skipped = 0;
        while (std::fgets(line, sizeof(line), fh))
        {
            char* text = line;
            while (*text == ' ' || *text == '\t')
                ++text;
            char* end = text + std::strlen(text);
            while (end > text && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
                *--end = 0;
            if (!*text || *text == '#')
                continue;
            if (gMessageProbeCount >= kMessageProbeNames || std::strlen(text) >= sizeof(gMessageProbeNames[0].name))
            {
                ++skipped;
                continue;
            }
            MessageProbeName& slot = gMessageProbeNames[gMessageProbeCount++];
            slot.hash = MessageProbeHash(text);
            std::snprintf(slot.name, sizeof(slot.name), "%s", text);
        }
        std::fclose(fh);
        // sorted by hash for the lookup in the hot path
        for (unsigned i = 1; i < gMessageProbeCount; ++i)
            for (unsigned j = i; j > 0 && gMessageProbeNames[j - 1].hash > gMessageProbeNames[j].hash; --j)
            {
                MessageProbeName t = gMessageProbeNames[j];
                gMessageProbeNames[j] = gMessageProbeNames[j - 1];
                gMessageProbeNames[j - 1] = t;
            }
        unsigned clashes = 0;
        for (unsigned i = 1; i < gMessageProbeCount; ++i)
            if (gMessageProbeNames[i].hash == gMessageProbeNames[i - 1].hash)
                ++clashes;
        if (!gMessageProbeCount)
        {
            Log("message probe: on, but %s names no message; not hooked", path);
            InstallSceneGateListener();
            return;
        }
        gSendMsgHook = VerifiedMid(reinterpret_cast<void*>(kSendMsg), &SendMsg_Hook);
        gSceneGateListening = gSendMsgHook && gSceneGateFallback && gEnemyRandomizer;
        Log("message probe: %u name(s) from %s (%u skipped, %u hash clash(es)), SendMsg %s at %08X",
            gMessageProbeCount, path, skipped, clashes, gSendMsgHook ? "hooked" : "NOT hooked",
            static_cast<unsigned>(kSendMsg));
    }
