    // Sound bank slots ([DeadSpace2008Randomizer] SoundBankSlots, default 96; 35 keeps retail).
    //
    // DS1 holds 35 loaded sound banks: the bank manager (vtable 0x00C802CC,
    // [0x00F64AFC], 0x25C bytes, built by the audio init 0x006A5640) keeps 35 slots
    // of 16 bytes from +0x14, then six fields at +0x244..+0x25B. A bank requested
    // while the table is full is dropped silently, and the randomizer's enemy pool
    // fills it (analysis/DS1_Audio_Review_2026-10-24.md): level sirens, ambience and
    // foreign enemies went silent. The fields are moved behind a longer slot array:
    // the two allocations, the clear in the constructor, the four places that know
    // 35 and the 30 field accesses of the manager (its methods and the two
    // audio-init users), found by every size constant and field offset in the
    // functions that reach [0x00F64AFC].
    // Runs from DllMain, before the game's init builds the manager; every site is
    // checked first and one mismatch keeps the retail table.
    constexpr unsigned kBankRetailSlots = 35;
    constexpr unsigned kBankMaxSlots = 120;            // imm8 compares
    constexpr unsigned kBankSlotStart = 0x14;
    constexpr unsigned kBankSlotSize = 0x10;
    constexpr unsigned kBankRetailTail = 0x244;
    constexpr unsigned kBankTailSize = 0x18;           // +0x244..+0x25B
    constexpr uintptr_t kBankManagerGlobal = 0x00F64AFC;

    struct BankSite
    {
        uintptr_t at;
        uint8_t length;
        uint8_t field;   // position of the 32-bit offset in the instruction
        uint8_t bytes[10];
    };

    // Taken from the retail code; the field offset is 0x244..0x25B.
    constexpr BankSite kBankTailSites[] = {
        {0x006A58CA, 6, 2, {0x8B, 0x86, 0x54, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [esi + 0x254]
        {0x006AF8D2, 6, 2, {0x89, 0x86, 0x44, 0x02, 0x00, 0x00}},   // mov dword ptr [esi + 0x244], eax
        {0x006AF8D8, 6, 2, {0x89, 0x86, 0x48, 0x02, 0x00, 0x00}},   // mov dword ptr [esi + 0x248], eax
        {0x006AF8E4, 6, 2, {0x89, 0x86, 0x4C, 0x02, 0x00, 0x00}},   // mov dword ptr [esi + 0x24c], eax
        {0x006AF8EA, 6, 2, {0x89, 0x86, 0x50, 0x02, 0x00, 0x00}},   // mov dword ptr [esi + 0x250], eax
        {0x006AF8F0, 6, 2, {0x89, 0x86, 0x54, 0x02, 0x00, 0x00}},   // mov dword ptr [esi + 0x254], eax
        {0x006AF8FC, 6, 2, {0x88, 0x86, 0x58, 0x02, 0x00, 0x00}},   // mov byte ptr [esi + 0x258], al
        {0x006AF937, 6, 2, {0x8B, 0x86, 0x4C, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [esi + 0x24c]
        {0x006AF983, 6, 2, {0x8B, 0x86, 0x4C, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [esi + 0x24c]
        {0x006AF9B8, 6, 2, {0x88, 0x8F, 0x58, 0x02, 0x00, 0x00}},   // mov byte ptr [edi + 0x258], cl
        {0x006AFA69, 6, 2, {0x8B, 0x86, 0x54, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [esi + 0x254]
        {0x006AFABB, 6, 2, {0x8B, 0x86, 0x4C, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [esi + 0x24c]
        {0x006AFAC1, 10, 2, {0xC7, 0x86, 0x50, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},   // mov dword ptr [esi + 0x250], 0
        {0x006AFAD5, 10, 2, {0xC7, 0x86, 0x4C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},   // mov dword ptr [esi + 0x24c], 0
        {0x006AFADF, 10, 2, {0xC7, 0x86, 0x54, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},   // mov dword ptr [esi + 0x254], 0
        {0x006AFB15, 7, 2, {0x83, 0x83, 0x44, 0x02, 0x00, 0x00, 0x01}},   // add dword ptr [ebx + 0x244], 1
        {0x006AFB23, 6, 2, {0x8B, 0xBB, 0x44, 0x02, 0x00, 0x00}},   // mov edi, dword ptr [ebx + 0x244]
        {0x006AFEFB, 6, 2, {0x8B, 0xB7, 0x50, 0x02, 0x00, 0x00}},   // mov esi, dword ptr [edi + 0x250]
        {0x006AFF50, 6, 2, {0x8B, 0x8F, 0x4C, 0x02, 0x00, 0x00}},   // mov ecx, dword ptr [edi + 0x24c]
        {0x006AFFB5, 6, 2, {0x8B, 0x87, 0x4C, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [edi + 0x24c]
        {0x006AFFBB, 6, 2, {0x89, 0x9F, 0x50, 0x02, 0x00, 0x00}},   // mov dword ptr [edi + 0x250], ebx
        {0x006AFFCA, 6, 2, {0x89, 0x9F, 0x4C, 0x02, 0x00, 0x00}},   // mov dword ptr [edi + 0x24c], ebx
        {0x006AFFD0, 6, 2, {0x89, 0x9F, 0x54, 0x02, 0x00, 0x00}},   // mov dword ptr [edi + 0x254], ebx
        {0x006B011D, 7, 2, {0x80, 0xB9, 0x58, 0x02, 0x00, 0x00, 0x00}},   // cmp byte ptr [ecx + 0x258], 0
        {0x006B03E0, 7, 2, {0x83, 0x80, 0x44, 0x02, 0x00, 0x00, 0x01}},   // add dword ptr [eax + 0x244], 1
        {0x006B03E7, 6, 2, {0x8B, 0x80, 0x44, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [eax + 0x244]
        {0x006A5E42, 6, 2, {0x8B, 0x86, 0x4C, 0x02, 0x00, 0x00}},   // mov eax, dword ptr [esi + 0x24c]
        {0x006A58D0, 6, 2, {0x81, 0xC6, 0x4C, 0x02, 0x00, 0x00}},   // add esi, 0x24c
        {0x006A5E48, 6, 2, {0x81, 0xC6, 0x4C, 0x02, 0x00, 0x00}},   // add esi, 0x24c
        {0x006AFA6F, 6, 2, {0x81, 0xC6, 0x4C, 0x02, 0x00, 0x00}},   // add esi, 0x24c
    };

    unsigned gSoundBankSlots = kBankRetailSlots;

    bool BankCodeMatches(uintptr_t at, const uint8_t* bytes, size_t length)
    {
        __try
        {
            return std::memcmp(reinterpret_cast<const void*>(at), bytes, length) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool BankWriteCode(uintptr_t at, const void* data, size_t length)
    {
        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(at), length, PAGE_EXECUTE_READWRITE, &old))
            return false;
        std::memcpy(reinterpret_cast<void*>(at), data, length);
        VirtualProtect(reinterpret_cast<void*>(at), length, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), length);
        return true;
    }

    void InstallSoundBankSlots()
    {
        static bool done = false;
        if (done)
            return;
        done = true;
        unsigned wanted = GetPrivateProfileIntA("DeadSpace2008Randomizer", "SoundBankSlots", 96, gIniPath);
        if (wanted <= kBankRetailSlots)
        {
            Log("sound bank slots: %u requested, retail table of %u kept", wanted, kBankRetailSlots);
            return;
        }
        if (wanted > kBankMaxSlots)
            wanted = kBankMaxSlots;
        uintptr_t existing = 0;
        __try
        {
            existing = *reinterpret_cast<volatile uintptr_t*>(kBankManagerGlobal);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            existing = 1;
        }
        if (existing)
        {
            Log("sound bank slots: NOT extended, the bank manager exists already (%08X); table of %u kept",
                static_cast<unsigned>(existing), kBankRetailSlots);
            return;
        }

        struct Constant
        {
            uintptr_t at;
            uint8_t length;
            uint8_t bytes[6];
            const char* what;
        };
        static constexpr Constant kConstants[] = {
            {0x006A572A, 5, {0x68, 0x5C, 0x02, 0x00, 0x00}, "allocation size"},
            {0x006A580C, 5, {0x68, 0x5C, 0x02, 0x00, 0x00}, "second allocation size"},
            {0x006AF8F6, 5, {0x68, 0x40, 0x02, 0x00, 0x00}, "constructor clear size"},
            {0x006AFF16, 6, {0x66, 0xC7, 0x40, 0x08, 0x23, 0x00}, "list capacity"},
            {0x006AFF2D, 3, {0x8D, 0x4A, 0x23}, "slot init loop"},
            {0x006B0071, 5, {0x66, 0x83, 0x7F, 0x12, 0x23}, "room-left test"},
            {0x006B013C, 3, {0x83, 0xFE, 0x23}, "free-slot search"},
        };
        unsigned mismatches = 0;
        for (const Constant& c : kConstants)
            if (!BankCodeMatches(c.at, c.bytes, c.length))
            {
                ++mismatches;
                Log("sound bank slots: %s at %08X does not match", c.what, static_cast<unsigned>(c.at));
            }
        for (const BankSite& site : kBankTailSites)
            if (!BankCodeMatches(site.at, site.bytes, site.length))
            {
                ++mismatches;
                Log("sound bank slots: field access at %08X does not match", static_cast<unsigned>(site.at));
            }
        if (mismatches)
        {
            Log("sound bank slots: NOT extended, %u of %u site(s) differ; table of %u kept", mismatches,
                static_cast<unsigned>(std::size(kConstants) + std::size(kBankTailSites)), kBankRetailSlots);
            return;
        }

        const uint32_t tail = kBankSlotStart + wanted * kBankSlotSize;
        const uint32_t size = tail + kBankTailSize;
        const uint32_t clear = tail - 4;   // the constructor clears from +4 up to the fields
        const uint8_t count8 = static_cast<uint8_t>(wanted);
        const uint16_t count16 = static_cast<uint16_t>(wanted);
        unsigned written = 0;
        written += BankWriteCode(0x006A572A + 1, &size, 4);
        written += BankWriteCode(0x006A580C + 1, &size, 4);
        written += BankWriteCode(0x006AF8F6 + 1, &clear, 4);
        written += BankWriteCode(0x006AFF16 + 4, &count16, 2);
        written += BankWriteCode(0x006AFF2D + 2, &count8, 1);
        written += BankWriteCode(0x006B0071 + 4, &count8, 1);
        written += BankWriteCode(0x006B013C + 2, &count8, 1);
        for (const BankSite& site : kBankTailSites)
        {
            uint32_t field = 0;
            std::memcpy(&field, site.bytes + site.field, 4);
            const uint32_t moved = field - kBankRetailTail + tail;
            written += BankWriteCode(site.at + site.field, &moved, 4);
        }
        const unsigned expected = static_cast<unsigned>(std::size(kConstants) + std::size(kBankTailSites));
        if (written != expected)
        {
            Log("sound bank slots: only %u of %u write(s) took; the audio may misbehave", written, expected);
            return;
        }
        gSoundBankSlots = wanted;
        Log("sound bank slots: table extended from %u to %u banks; manager %X -> %X bytes, fields %X -> %X, "
            "%u site(s) patched", kBankRetailSlots, wanted, kBankRetailTail + kBankTailSize, size,
            kBankRetailTail, tail, expected);
    }
