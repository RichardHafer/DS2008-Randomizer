    // =====================================================================
    // Main menu version label ([DeadSpace1Patch] MenuVersionLabel).
    //
    // The game formats "v%d.%d.%d.%d" from its file version into a 100 byte
    // buffer once at startup (0x0042457B, buffer 0x00EB5928). DeadSpaceFixes
    // (xinput1_3.dll) writes "DeadSpaceFixes Installed!" into the same buffer
    // once from its own thread, so whichever comes last wins; with this patch
    // installing its hooks the game's text came last (dump 2026-10-22 00:11).
    // The label is rebuilt right after the game's format, or at install when
    // that already happened: version, then each mod that is loaded.
    // =====================================================================
    constexpr uintptr_t kVersionLabelFormatted = 0x00424583;   // mov byte [0x00EB598B],0 after the format
    constexpr uintptr_t kVersionLabel = 0x00EB5928;
    constexpr unsigned kVersionLabelSize = 0x64;
    constexpr char kFixesMarker[] = "DeadSpaceFixes Installed!";
    constexpr char kRandomizerVersion[] = "1.0";
    bool gMenuVersionLabel = true;
    safetyhook::MidHook gVersionLabelHook;

    bool DeadSpaceFixesLoaded()
    {
        const HMODULE fixes = GetModuleHandleA("xinput1_3.dll");
        if (!fixes)
            return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(fixes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(fixes) + dos->e_lfanew);
        const BYTE* image = reinterpret_cast<const BYTE*>(fixes);
        const size_t size = nt->OptionalHeader.SizeOfImage;
        const size_t length = sizeof(kFixesMarker) - 1;
        for (size_t i = 0; i + length <= size; ++i)
            if (image[i] == 'D' && std::memcmp(image + i, kFixesMarker, length) == 0)
                return true;
        return false;
    }

    void ComposeVersionLabel(const char* when)
    {
        char current[kVersionLabelSize]{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(kVersionLabel), current,
                               sizeof(current) - 1, nullptr) ||
            std::strstr(current, "DS2008 Randomizer"))
            return;
        char version[32]{};
        if (current[0] == 'v')
            for (unsigned i = 0; i + 1 < sizeof(version) && current[i] && current[i] != ' '; ++i)
                version[i] = current[i];
        const bool fixes = DeadSpaceFixesLoaded();
        char label[kVersionLabelSize]{};
        std::snprintf(label, sizeof(label), "%s%s%sDS2008 Randomizer v%s", version, version[0] ? " + " : "",
                      fixes ? "DeadSpaceFixes + " : "", kRandomizerVersion);
        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(kVersionLabel), kVersionLabelSize, PAGE_READWRITE, &old))
            return;
        std::memcpy(reinterpret_cast<void*>(kVersionLabel), label, sizeof(label));
        VirtualProtect(reinterpret_cast<void*>(kVersionLabel), kVersionLabelSize, old, &old);
        Log("menu label (%s): \"%s\" -> \"%s\"", when, current, label);
    }

    void VersionLabelFormatted_Hook(safetyhook::Context&)
    {
        ComposeVersionLabel("after the game's format");
    }

    void InstallMenuLabel()
    {
        gMenuVersionLabel = GetPrivateProfileIntA("DeadSpace1Patch", "MenuVersionLabel", 1, gIniPath) != 0;
        if (!gMenuVersionLabel)
            return;
        gVersionLabelHook =
            VerifiedMid(reinterpret_cast<void*>(kVersionLabelFormatted), VersionLabelFormatted_Hook);
        Log("menu label hook %s at %08X; DeadSpaceFixes %s", gVersionLabelHook ? "active" : "NOT installed",
            static_cast<unsigned>(kVersionLabelFormatted), DeadSpaceFixesLoaded() ? "loaded" : "not loaded");
        // The game may have formatted it before this thread got here.
        char first = 0;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(kVersionLabel), &first, 1, nullptr) &&
            first)
            ComposeVersionLabel("already formatted at install");
    }
