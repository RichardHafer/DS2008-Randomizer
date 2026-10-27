    // Crash capture ([DeadSpace2008Randomizer] CrashDumps, 1 by default).
    //
    // An unhandled exception writes DeadSpace2008Randomizer.crash.<pid>.txt (code,
    // address, registers), a .dmp and a copy of the log next to the log, or into
    // C:\DS1CrashDumps when that folder exists. The game's R6025 never raises an
    // exception: its _purecall (0x008F9024, referenced by 2239 vtable slots) calls
    // _amsg_exit(25), shows the box and exits. A mid hook there writes the same files
    // with the caller and the object (whose vtable is already the abstract base),
    // then lets the game continue.
    // 1 = small dump (stacks, data, the memory they point at), 2 = full memory.
    constexpr uintptr_t kGamePurecall = 0x008F9024;

    unsigned gCrashDumps = 1;
    // The filter installed before ours, and a later one that ours displaced.
    LPTOP_LEVEL_EXCEPTION_FILTER gOriginalCrashFilter = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER gLaterCrashFilter = nullptr;
    safetyhook::MidHook gPurecallHook;
    volatile LONG gCrashInside = 0, gCrashWritten = 0, gCrashFilterReclaimed = 0, gCrashChainedBack = 0;

    void CrashFolder(char* out, size_t size)
    {
        if (GetFileAttributesA("C:\\DS1CrashDumps") != INVALID_FILE_ATTRIBUTES)
        {
            strncpy_s(out, size, "C:\\DS1CrashDumps", _TRUNCATE);
            return;
        }
        strncpy_s(out, size, gLogPath, _TRUNCATE);
        if (char* slash = std::strrchr(out, '\\'))
            *slash = '\0';
    }

    void CrashFile(char* out, size_t size, const char* extension)
    {
        char folder[MAX_PATH]{};
        CrashFolder(folder, sizeof(folder));
        std::snprintf(out, size, "%s\\DeadSpace2008Randomizer.crash.%lu.%s", folder,
                      static_cast<unsigned long>(GetCurrentProcessId()), extension);
    }

    // Plain Win32 only: this runs on a crashed thread, possibly with the log lock held.
    void WriteCrashNote(EXCEPTION_POINTERS* info, const char* what)
    {
        char path[MAX_PATH]{};
        CrashFile(path, sizeof(path), "txt");
        const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        const EXCEPTION_RECORD* r = info->ExceptionRecord;
        const CONTEXT* c = info->ContextRecord;
        char text[1024];
        int n = std::snprintf(text, sizeof(text),
            "%s\r\ncode=%08lX address=%08X access=%lu target=%08X\r\n"
            "eip=%08lX esp=%08lX ebp=%08lX eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX"
            " thread=%lu tick=%lu\r\n",
            what, r->ExceptionCode,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(r->ExceptionAddress)),
            r->NumberParameters >= 2 ? static_cast<unsigned long>(r->ExceptionInformation[0]) : 0ul,
            r->NumberParameters >= 2 ? static_cast<unsigned>(r->ExceptionInformation[1]) : 0u,
            c->Eip, c->Esp, c->Ebp, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi,
            static_cast<unsigned long>(GetCurrentThreadId()), static_cast<unsigned long>(GetTickCount()));
        // The first stack words name the callers when the dump cannot be opened.
        uint32_t stack[16]{};
        if (n > 0 && n < 768 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(c->Esp),
                                                  stack, sizeof(stack), nullptr))
        {
            // 16 words need 150 bytes; the header leaves at least 256.
            n += std::snprintf(text + n, sizeof(text) - n, "stack");
            for (uint32_t word : stack)
                n += std::snprintf(text + n, sizeof(text) - n, " %08X", word);
            n += std::snprintf(text + n, sizeof(text) - n, "\r\n");
        }
        DWORD done = 0;
        if (n > 0)
            WriteFile(file, text, static_cast<DWORD>(n), &done, nullptr);
        CloseHandle(file);
    }

    // The log is closed between lines, so a plain copy is complete.
    void WriteCrashFiles(EXCEPTION_POINTERS* info, const char* what)
    {
        if (InterlockedExchange(&gCrashWritten, 1))
            return;
        WriteCrashNote(info, what);
        char path[MAX_PATH]{};
        CrashFile(path, sizeof(path), "log");
        CopyFileA(gLogPath, path, FALSE);

        HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
        using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                        PMINIDUMP_EXCEPTION_INFORMATION, void*, void*);
        const auto write = dbghelp
            ? reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
        if (!write)
            return;
        CrashFile(path, sizeof(path), "dmp");
        const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
            (gCrashDumps >= 2 ? MiniDumpWithFullMemory
                              : MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory) |
            MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        write(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, nullptr, nullptr);
        CloseHandle(file);
    }

    // Ours first, then a filter set after ours, then the one before ours. A later
    // filter usually chains to its predecessor, which is us again: that call
    // goes on to the original one.
    LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info)
    {
        if (InterlockedExchange(&gCrashInside, 1))
        {
            InterlockedExchange(&gCrashChainedBack, 1);
            return gOriginalCrashFilter ? gOriginalCrashFilter(info) : EXCEPTION_CONTINUE_SEARCH;
        }
        if (info && info->ExceptionRecord && info->ContextRecord)
            WriteCrashFiles(info, "unhandled exception");
        InterlockedExchange(&gCrashChainedBack, 0);
        LONG result = EXCEPTION_CONTINUE_SEARCH;
        if (gLaterCrashFilter)
            result = gLaterCrashFilter(info);
        if (!gLaterCrashFilter || (result == EXCEPTION_CONTINUE_SEARCH &&
                                   !InterlockedCompareExchange(&gCrashChainedBack, 0, 0)))
            result = gOriginalCrashFilter ? gOriginalCrashFilter(info) : EXCEPTION_CONTINUE_SEARCH;
        // Handled after all: the files describe no crash, so the next real one may overwrite them.
        if (result == EXCEPTION_CONTINUE_EXECUTION)
            InterlockedExchange(&gCrashWritten, 0);
        InterlockedExchange(&gCrashInside, 0);
        return result;
    }

    void GamePurecall_Hook(safetyhook::Context& ctx)
    {
        uint32_t caller = 0, vtable = 0;
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(ctx.esp),
                          &caller, sizeof(caller), nullptr);
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(ctx.ecx),
                          &vtable, sizeof(vtable), nullptr);
        Log("R6025 pure virtual call from %08X: this=%08X vtable=%08X (the object is being destroyed "
            "or was never finished); crash files follow", caller, static_cast<unsigned>(ctx.ecx), vtable);

        // Segment registers and flags from this thread, the rest from the hooked instruction.
        CONTEXT context{};
        RtlCaptureContext(&context);
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS;
        context.Eip = static_cast<DWORD>(kGamePurecall);
        context.Esp = static_cast<DWORD>(ctx.esp);
        context.Ebp = static_cast<DWORD>(ctx.ebp);
        context.Eax = static_cast<DWORD>(ctx.eax);
        context.Ebx = static_cast<DWORD>(ctx.ebx);
        context.Ecx = static_cast<DWORD>(ctx.ecx);
        context.Edx = static_cast<DWORD>(ctx.edx);
        context.Esi = static_cast<DWORD>(ctx.esi);
        context.Edi = static_cast<DWORD>(ctx.edi);
        context.EFlags = static_cast<DWORD>(ctx.eflags);
        EXCEPTION_RECORD record{};
        record.ExceptionCode = 0xE0006025;   // ours, so the dump names R6025
        record.ExceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(caller));
        EXCEPTION_POINTERS pointers{&record, &context};
        WriteCrashFiles(&pointers, "R6025 pure virtual function call (address = the caller)");
    }

    void LoadCrashSettings()
    {
        gCrashDumps = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace2008Randomizer", "CrashDumps", 1, gIniPath));
    }

    // After SecuROM has decrypted the code: its start-up runs its own filters.
    void InstallCrashCapture()
    {
        char folder[MAX_PATH]{};
        CrashFolder(folder, sizeof(folder));
        if (!gCrashDumps)
        {
            Log("crash capture off (CrashDumps=0)");
            return;
        }
        gOriginalCrashFilter = SetUnhandledExceptionFilter(&CrashFilter);
        gPurecallHook = VerifiedMid(reinterpret_cast<void*>(kGamePurecall), &GamePurecall_Hook);
        Log("crash capture on (CrashDumps=%u, %s dump) into %s; R6025 hook %s at %08X",
            gCrashDumps, gCrashDumps >= 2 ? "full" : "small", folder,
            gPurecallHook ? "active" : "NOT installed", static_cast<unsigned>(kGamePurecall));
    }

    // From the watch loop: a filter set later by somebody else goes behind ours.
    void KeepCrashFilter()
    {
        if (!gCrashDumps || InterlockedCompareExchange(&gCrashInside, 0, 0))
            return;
        const LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(&CrashFilter);
        if (current == &CrashFilter)
            return;
        gLaterCrashFilter = current;
        if (InterlockedIncrement(&gCrashFilterReclaimed) <= 4)
            Log("crash capture: another module set its own exception filter (%08X); it now runs after ours",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(current)));
    }
