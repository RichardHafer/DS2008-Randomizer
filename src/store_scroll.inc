    // Store fast scrolling ([DeadSpace2008Randomizer] StoreFastScroll;
    // analysis/DS1_StoreScroll_Research_2026-10-25.md).
    //
    // The kiosk list moves one row per 0.25 s, about 25 s through 100 items.
    // The store screen is the player state machine playerStoreStateTable; its
    // per-frame update (dispatcher event 0x3A, 0x0059C504, ebx = the object)
    // takes the keys. A jump writes row, top and the previous fields the way
    // the row step does, then runs the step's own refresh events: 0x17 (old
    // row), 0x16 (all rows, prices, scrollbar), 0x19 (highlight, preview).
    constexpr uintptr_t kStoreUpdateAction = 0x0059C504;
    constexpr uintptr_t kStoreAction = 0x0059BC20;     // thiscall, 5 stack args, ret 14h
    constexpr uintptr_t kStoreRowCount = 0x0059E150;   // thiscall, ret
    constexpr uintptr_t kStoreItemAt = 0x0059FD40;     // thiscall (row, col), ret 8
    constexpr uint32_t kStoreStateVtable = 0x00C74BD0;
    constexpr uint32_t kStoreStateTableHash = 0xE6F9309D;   // sdbm("playerStoreStateTable")
    constexpr int kStoreBrowsing = 6;
    constexpr int kStoreRows = 10;
    constexpr unsigned kStoreStateIndex = 0x0C, kStorePlayer = 0x50, kStoreRowHighlight = 0x74,
        kStorePreview = 0x1E4, kStoreRow = 0x238, kStoreColumn = 0x23C, kStorePrevRow = 0x240,
        kStorePrevColumn = 0x244, kStoreFlags = 0x248, kStoreKiosk = 0x24C, kStoreTab = 0x254,
        kStoreTop = 0x25C, kStorePrevTop = 0x260;

    using StoreActionFn = char(__thiscall*)(void* sm, int a1, float dt, int a3, int event, int a5);
    using StoreRowCountFn = int(__thiscall*)(void* sm);
    using StoreItemAtFn = void*(__thiscall*)(void* sm, int row, int column);

    bool gStoreFastScroll = true;
    int gStoreScrollPage = kStoreRows;
    int gStoreScrollFirstKey = 0, gStoreScrollLastKey = 0;   // optional extra keys (Home 0x24 is the vacuum recorder's)
    bool gStoreScrollPad = true;
    DWORD gStoreScrollRateMs = 120;
    safetyhook::MidHook gStoreScrollHook;

    enum StoreScrollReject { kRejectObject, kRejectState, kRejectKiosk, kRejectEmpty, kRejectRate, kRejectFault, kRejectCount };
    const char* const kStoreScrollRejectNames[kRejectCount] = {"vtable/hash", "state", "no kiosk", "empty tab",
                                                               "rate limit", "fault"};
    volatile LONG gStoreScrollJumps = 0, gStoreScrollUpdates = 0;
    volatile LONG gStoreScrollRejects[kRejectCount]{};

    // Kept per store visit (the object lives only while a kiosk is in use).
    void* gStoreScrollObject = nullptr;
    DWORD gStoreScrollSessionStart = 0;
    LONG gStoreScrollSessionUpdates = 0;
    bool gStoreScrollRateLogged = false;
    int gStoreScrollStates[12]{};
    unsigned gStoreScrollStateCount = 0;
    int gStoreScrollLastState = -1;
    unsigned gStoreScrollSessionJumps = 0;

    // XInput through the module the game already loaded (the DeadSpaceFixes proxy or the system one)
    struct PadState { DWORD packet; WORD buttons; BYTE leftTrigger, rightTrigger; SHORT lx, ly, rx, ry; };
    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, PadState*);
    XInputGetStateFn gXInputGetState = nullptr;

    enum StoreJump { kJumpNone, kJumpPageUp, kJumpPageDown, kJumpFirst, kJumpLast };
    const char* const kStoreJumpNames[] = {"none", "page up", "page down", "first", "last"};

    StoreJump StoreScrollInput()
    {
        if (!GameIsForeground())
            return kJumpNone;
        auto key = [](int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; };
        const bool ctrl = key(VK_CONTROL);
        bool pageUp = key(VK_PRIOR), pageDown = key(VK_NEXT);
        bool first = key(gStoreScrollFirstKey) || (ctrl && pageUp);
        bool last = key(gStoreScrollLastKey) || (ctrl && pageDown);
        if (ctrl)
            pageUp = pageDown = false;
        if (gStoreScrollPad && gXInputGetState)
        {
            PadState pad{};
            if (gXInputGetState(0, &pad) == ERROR_SUCCESS)
            {
                // the triggers page; the store reads neither of them
                const bool lt = pad.leftTrigger > 128, rt = pad.rightTrigger > 128;
                pageUp = pageUp || (lt && !rt);
                pageDown = pageDown || (rt && !lt);
            }
        }
        // first press jumps, holding repeats after 400 ms every 150 ms
        static StoreJump held = kJumpNone;
        static DWORD heldSince = 0, lastRepeat = 0;
        const StoreJump now = first ? kJumpFirst : last ? kJumpLast : pageUp ? kJumpPageUp : pageDown ? kJumpPageDown
                                                                                                     : kJumpNone;
        const DWORD tick = GetTickCount();
        if (now != held)
        {
            held = now;
            heldSince = lastRepeat = tick;
            return now;
        }
        if (now == kJumpPageUp || now == kJumpPageDown)
            if (tick - heldSince >= 400 && tick - lastRepeat >= 150)
            {
                lastRepeat = tick;
                return now;
            }
        return kJumpNone;
    }

    void StoreScrollSession(unsigned char* sm, int state)
    {
        const DWORD tick = GetTickCount();
        if (sm != gStoreScrollObject)
        {
            gStoreScrollObject = sm;
            gStoreScrollSessionStart = tick;
            gStoreScrollSessionUpdates = 0;
            gStoreScrollRateLogged = false;
            gStoreScrollStateCount = 0;
            gStoreScrollLastState = -1;
            gStoreScrollSessionJumps = 0;
            Log("store scroll: store object %08X, kiosk %08X, tab %d", reinterpret_cast<unsigned>(sm),
                *reinterpret_cast<uint32_t*>(sm + kStoreKiosk), *reinterpret_cast<int*>(sm + kStoreTab));
        }
        ++gStoreScrollSessionUpdates;
        if (state != gStoreScrollLastState)
        {
            gStoreScrollLastState = state;
            if (gStoreScrollStateCount < sizeof(gStoreScrollStates) / sizeof(gStoreScrollStates[0]))
            {
                gStoreScrollStates[gStoreScrollStateCount++] = state;
                Log("store scroll: state %d seen in the update action", state);
            }
        }
        if (!gStoreScrollRateLogged && tick - gStoreScrollSessionStart >= 3000)
        {
            gStoreScrollRateLogged = true;
            Log("store scroll: %ld update action call(s) in the first %lu ms", gStoreScrollSessionUpdates,
                static_cast<unsigned long>(tick - gStoreScrollSessionStart));
        }
    }

    void StoreScrollJump(unsigned char* sm, StoreJump jump)
    {
        static DWORD lastJump = 0;
        const DWORD tick = GetTickCount();
        if (tick - lastJump < gStoreScrollRateMs)
        {
            InterlockedIncrement(&gStoreScrollRejects[kRejectRate]);
            return;
        }
        const int count = reinterpret_cast<StoreRowCountFn>(kStoreRowCount)(sm);
        const int tab = *reinterpret_cast<int*>(sm + kStoreTab);
        if (count <= 0)
        {
            InterlockedIncrement(&gStoreScrollRejects[kRejectEmpty]);
            return;
        }
        int& row = *reinterpret_cast<int*>(sm + kStoreRow);
        int& top = *reinterpret_cast<int*>(sm + kStoreTop);
        const int oldRow = row, oldTop = top, current = top + row;
        int target = jump == kJumpFirst ? 0 : jump == kJumpLast ? count - 1
                   : jump == kJumpPageUp ? current - gStoreScrollPage : current + gStoreScrollPage;
        target = target < 0 ? 0 : target > count - 1 ? count - 1 : target;
        const int maxTop = count > kStoreRows ? count - kStoreRows : 0;
        int newTop = target - row;
        newTop = newTop < 0 ? 0 : newTop > maxTop ? maxTop : newTop;
        const int newRow = target - newTop;
        if (newTop == oldTop && newRow == oldRow)
            return;
        lastJump = tick;
        // what the row step (0x0059F4D0 / 0x0059F520) saves before it moves
        *reinterpret_cast<int*>(sm + kStorePrevRow) = oldRow;
        *reinterpret_cast<int*>(sm + kStorePrevColumn) = *reinterpret_cast<int*>(sm + kStoreColumn);
        *reinterpret_cast<int*>(sm + kStorePrevTop) = oldTop;
        top = newTop;
        row = newRow;
        const auto action = reinterpret_cast<StoreActionFn>(kStoreAction);
        action(sm, 0, 0.0f, 0, 0x17, 0);
        action(sm, 0, 0.0f, 0, 0x16, 0);
        action(sm, 0, 0.0f, 0, 0x19, 0);
        InterlockedIncrement(&gStoreScrollJumps);
        if (++gStoreScrollSessionJumps <= 24)
        {
            void* item = reinterpret_cast<StoreItemAtFn>(kStoreItemAt)(sm, row, 0);
            Log("store scroll: %s on tab %d of %d: index %d -> %d (top %d -> %d, row %d -> %d), item %08X; "
                "after: top %d row %d, highlight %08X, preview %08X, flags %08X",
                kStoreJumpNames[jump], tab, count, oldTop + oldRow, target, oldTop, newTop, oldRow, newRow,
                reinterpret_cast<unsigned>(item), top, row,
                *reinterpret_cast<uint32_t*>(sm + kStoreRowHighlight + row * 0x10),
                *reinterpret_cast<uint32_t*>(sm + kStorePreview), *reinterpret_cast<uint32_t*>(sm + kStoreFlags));
        }
        else if (gStoreScrollSessionJumps == 25)
            Log("store scroll: further jumps this session only in the totals");
    }

    void StoreScroll_Hook(safetyhook::Context& ctx)
    {
        __try
        {
            InterlockedIncrement(&gStoreScrollUpdates);
            unsigned char* sm = reinterpret_cast<unsigned char*>(ctx.ebx);
            if (!sm || *reinterpret_cast<uint32_t*>(sm) != kStoreStateVtable ||
                *reinterpret_cast<uint32_t*>(sm + 8) != kStoreStateTableHash)
            {
                InterlockedIncrement(&gStoreScrollRejects[kRejectObject]);
                return;
            }
            const int state = *reinterpret_cast<int*>(sm + kStoreStateIndex);
            StoreScrollSession(sm, state);
            const StoreJump jump = StoreScrollInput();
            if (jump == kJumpNone)
                return;
            if (state != kStoreBrowsing)
            {
                InterlockedIncrement(&gStoreScrollRejects[kRejectState]);
                return;
            }
            if (!*reinterpret_cast<uint32_t*>(sm + kStoreKiosk) || !*reinterpret_cast<uint32_t*>(sm + kStorePlayer))
            {
                InterlockedIncrement(&gStoreScrollRejects[kRejectKiosk]);
                return;
            }
            StoreScrollJump(sm, jump);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&gStoreScrollRejects[kRejectFault]);
        }
    }

    void InstallStoreScroll()
    {
        gStoreFastScroll = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StoreFastScroll", 1, gIniPath) != 0;
        gStoreScrollPage = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StoreScrollPage", kStoreRows, gIniPath);
        if (gStoreScrollPage < 1)
            gStoreScrollPage = 1;
        gStoreScrollFirstKey = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StoreScrollFirstKey", 0, gIniPath);
        gStoreScrollLastKey = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StoreScrollLastKey", 0, gIniPath);
        gStoreScrollPad = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StoreScrollController", 1, gIniPath) != 0;
        if (!gStoreFastScroll)
        {
            Log("store scroll off (StoreFastScroll=0)");
            return;
        }
        if (gStoreScrollPad)
        {
            HMODULE xinput = GetModuleHandleA("xinput1_3.dll");
            if (!xinput)
                xinput = LoadLibraryA("xinput1_3.dll");
            if (xinput)
                gXInputGetState = reinterpret_cast<XInputGetStateFn>(GetProcAddress(xinput, "XInputGetState"));
        }
        gStoreScrollHook = VerifiedMid(reinterpret_cast<void*>(kStoreUpdateAction), &StoreScroll_Hook);
        Log("store scroll %s at %08X: page %d rows (PageUp/PageDown, Ctrl for first/last, extra keys %02X/%02X), "
            "controller triggers %s",
            gStoreScrollHook ? "active" : "NOT installed", static_cast<unsigned>(kStoreUpdateAction), gStoreScrollPage,
            gStoreScrollFirstKey, gStoreScrollLastKey,
            !gStoreScrollPad ? "off" : gXInputGetState ? "on" : "unavailable (no XInputGetState)");
    }

    void LogStoreScrollTotals()
    {
        static LONG last[kRejectCount + 1]{-1};
        LONG now[kRejectCount + 1]{};
        now[0] = InterlockedCompareExchange(&gStoreScrollJumps, 0, 0);
        for (unsigned i = 0; i < kRejectCount; ++i)
            now[i + 1] = InterlockedCompareExchange(&gStoreScrollRejects[i], 0, 0);
        if (!gStoreScrollHook || std::memcmp(last, now, sizeof(now)) == 0)
            return;
        std::memcpy(last, now, sizeof(now));
        char text[256]{};
        int at = std::snprintf(text, sizeof(text), "store scroll totals: %ld jump(s), %ld update call(s); rejected:",
                               now[0], InterlockedCompareExchange(&gStoreScrollUpdates, 0, 0));
        for (unsigned i = 0; i < kRejectCount && at > 0 && at < static_cast<int>(sizeof(text)); ++i)
            at += std::snprintf(text + at, sizeof(text) - at, " %s %ld", kStoreScrollRejectNames[i], now[i + 1]);
        Log("%s", text);
    }
